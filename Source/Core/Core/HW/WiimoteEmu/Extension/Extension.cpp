// Copyright 2010 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Core/HW/WiimoteEmu/Extension/Extension.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Common/Assert.h"
#include "Common/CommonTypes.h"

#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteEmu/Extension/DesiredExtensionState.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"

#include "InputCommon/ControllerEmu/ControlGroup/Buttons.h"
#include "InputCommon/ControllerEmu/ControlGroup/ControlGroup.h"

namespace WiimoteEmu
{
Extension::Extension(const char* name) : Extension(name, name)
{
}

Extension::Extension(const char* config_name, const char* display_name)
    : m_config_name(config_name), m_display_name(display_name)
{
}

std::string Extension::GetName() const
{
  return m_config_name;
}

std::string Extension::GetDisplayName() const
{
  return m_display_name;
}

None::None() : Extension("None")
{
}

bool None::ReadDeviceDetectPin() const
{
  return false;
}

void None::BuildDesiredExtensionState(DesiredExtensionState* target_state)
{
  target_state->data.emplace<std::monostate>();
}

void None::Update(const DesiredExtensionState& target_state)
{
  // Nothing needed.
}

void None::Reset()
{
  // Nothing needed.
}

void None::DoState(PointerWrap& p)
{
  // Nothing needed.
}

int None::BusRead(u8 slave_addr, u8 addr, int count, u8* data_out)
{
  return 0;
}

int None::BusWrite(u8 slave_addr, u8 addr, int count, const u8* data_in)
{
  return 0;
}

namespace
{
constexpr std::array<std::array<u16, 3>, 4> BALANCE_BOARD_CALIBRATION{{
    {{0x07BC, 0x0E6E, 0x152E}},
    {{0x118B, 0x1879, 0x1F71}},
    {{0x06BA, 0x0D5D, 0x1407}},
    {{0x4652, 0x4D4C, 0x5451}},
}};

constexpr std::array<u8, 32> BALANCE_BOARD_CALIBRATION_BLOCK{{
    0x01, 0x69, 0x00, 0x00,
    0x07, 0xBC, 0x11, 0x8B, 0x06, 0xBA, 0x46, 0x52,
    0x0E, 0x6E, 0x18, 0x79, 0x0D, 0x5D, 0x4D, 0x4C,
    0x15, 0x2E, 0x1F, 0x71, 0x14, 0x07, 0x54, 0x51,
    0xA9, 0x06, 0xB4, 0xF0,
}};

u16 DecodePackedSensorWeight(const std::array<u8, 6>& packed, size_t sensor)
{
  const size_t pair = sensor / 2;
  const size_t offset = pair * 3;
  if ((sensor & 1) == 0)
    return static_cast<u16>(packed[offset] | ((packed[offset + 1] & 0x0F) << 8));

  return static_cast<u16>((packed[offset + 1] >> 4) | (packed[offset + 2] << 4));
}

void EncodePackedSensorWeight(std::array<u8, 6>* packed, size_t sensor, u16 value)
{
  const size_t pair = sensor / 2;
  const size_t offset = pair * 3;
  value = std::min<u16>(value, 0x0FFF);

  if ((sensor & 1) == 0)
  {
    (*packed)[offset] = static_cast<u8>(value & 0xFF);
    (*packed)[offset + 1] = static_cast<u8>((*packed)[offset + 1] & 0xF0);
    (*packed)[offset + 1] |= static_cast<u8>(value >> 8);
  }
  else
  {
    (*packed)[offset + 1] = static_cast<u8>((*packed)[offset + 1] & 0x0F);
    (*packed)[offset + 1] |= static_cast<u8>((value & 0x0F) << 4);
    (*packed)[offset + 2] = static_cast<u8>(value >> 4);
  }
}

u16 WeightToPackedSensor(u16 raw, size_t sensor)
{
  const auto& calibration = BALANCE_BOARD_CALIBRATION.at(sensor);
  const double raw_min = calibration[0];
  const double raw_max = calibration[2];
  const double normalized = std::clamp((static_cast<double>(raw) - raw_min) / (raw_max - raw_min), 0.0, 1.0);
  return static_cast<u16>(std::lround(normalized * 4095.0));
}

u16 PackedSensorToRaw(u16 packed_value, size_t sensor)
{
  const auto& calibration = BALANCE_BOARD_CALIBRATION.at(sensor);
  const double normalized = static_cast<double>(packed_value) / 4095.0;
  const double raw = calibration[0] + (calibration[2] - calibration[0]) * normalized;
  return static_cast<u16>(std::clamp<long>(std::lround(raw), 0L, 0xFFFFL));
}
}  // namespace

BalanceBoard::BalanceBoard() : Extension("BalanceBoard", _trans("Wii Balance Board"))
{
  using Translatability = ControllerEmu::Translatability;

  groups.emplace_back(m_button = new ControllerEmu::Buttons(_trans("Button")));
  m_button->AddInput(Translatability::DoNotTranslate, "A");

  constexpr std::array<const char*, 4> sensor_names{
      _trans("Top Right"), _trans("Bottom Right"), _trans("Top Left"), _trans("Bottom Left")};
  for (size_t i = 0; i < sensor_names.size(); ++i)
  {
    groups.emplace_back(m_sensor_groups[i] = new ControllerEmu::ControlGroup(sensor_names[i]));
    m_sensor_groups[i]->AddInput(Translatability::Translate, _trans("Weight"));
  }

  auto* const options = new ControllerEmu::ControlGroup(_trans("Options"));
  options->AddSetting(
      &m_sensor_smoothing,
      ControllerEmu::NumericSettingDetails(
          "Smoothing", "%", _trans("Smooths rapid virtual sensor changes. Set to 0% to preserve raw input."),
          _trans("Sensor Smoothing")),
      0.0, 0.0, 95.0);
  groups.emplace_back(options);
}

bool BalanceBoard::ReadDeviceDetectPin() const
{
  return true;
}

void BalanceBoard::BuildDesiredExtensionState(DesiredExtensionState* target_state)
{
  DesiredState& state = target_state->data.emplace<DesiredState>();
  for (size_t i = 0; i < m_sensor_groups.size(); ++i)
  {
    ControlState value = m_sensor_groups[i]->controls.front()->GetState();
    if (m_input_override_function)
    {
      if (auto override_value = m_input_override_function(
              m_sensor_groups[i]->name, m_sensor_groups[i]->controls.front()->name, value))
      {
        value = *override_value;
      }
    }

    double normalized = value;
    if (!std::isfinite(normalized))
      normalized = 0.0;

    normalized = std::clamp(normalized, 0.0, 1.0);
    const double smoothing = std::clamp(m_sensor_smoothing.GetValue(), 0.0, 95.0) / 100.0;

    if (smoothing == 0.0 || !m_have_smoothed_weight)
      m_smoothed_weight[i] = normalized;
    else
      m_smoothed_weight[i] =
          smoothing * m_smoothed_weight[i] + (1.0 - smoothing) * normalized;

    const double weight_kg =
        std::clamp(m_smoothed_weight[i], 0.0, 1.0) * MAX_SENSOR_WEIGHT_KG;
    const u16 raw = WeightToRaw(i, weight_kg);
    EncodePackedSensorWeight(&state.sensor_weight, i, WeightToPackedSensor(raw, i));
  }

  m_have_smoothed_weight = true;
}

u16 BalanceBoard::WeightToRaw(size_t sensor, double weight_kg)
{
  const auto& calibration = BALANCE_BOARD_CALIBRATION.at(sensor);
  const double weight = std::clamp(weight_kg, 0.0, MAX_SENSOR_WEIGHT_KG);
  double raw;
  if (weight <= 17.0)
  {
    const double t = weight / 17.0;
    raw = calibration[0] + (calibration[1] - calibration[0]) * t;
  }
  else
  {
    const double t = (weight - 17.0) / 17.0;
    raw = calibration[1] + (calibration[2] - calibration[1]) * t;
  }
  return static_cast<u16>(std::clamp<long>(std::lround(raw), 0L, 0xFFFFL));
}

void BalanceBoard::Update(const DesiredExtensionState& target_state)
{
  DesiredState desired_state{};
  if (std::holds_alternative<DesiredState>(target_state.data))
    desired_state = std::get<DesiredState>(target_state.data);

  for (size_t i = 0; i < 4; ++i)
  {
    const u16 packed = DecodePackedSensorWeight(desired_state.sensor_weight, i);
    const u16 raw = PackedSensorToRaw(packed, i);
    m_registers[i * 2] = static_cast<u8>(raw >> 8);
    m_registers[i * 2 + 1] = static_cast<u8>(raw & 0xFF);
  }
  m_registers[8] = 0x19;
  m_registers[9] = 0x00;
  m_registers[10] = m_battery_level;
  m_registers[0x60] = 0x19;
  m_registers[0x61] = 0x01;
}

bool BalanceBoard::GetButtonState()
{
  ControlState value = m_button->controls.front()->GetState();
  if (m_input_override_function)
  {
    if (auto override_value =
            m_input_override_function(m_button->name, m_button->controls.front()->name, value))
    {
      value = *override_value;
    }
  }
  return value > 0.5;
}

void BalanceBoard::Reset()
{
  m_smoothed_weight.fill(0.0);
  m_have_smoothed_weight = false;
  m_registers.fill(0);
  std::copy(BALANCE_BOARD_CALIBRATION_BLOCK.begin(), BALANCE_BOARD_CALIBRATION_BLOCK.end(),
            m_registers.begin() + 0x20);
  m_registers[0xFE] = 0x04;
  m_registers[0xFF] = 0x02;
}

void BalanceBoard::DoState(PointerWrap& p)
{
  p.Do(m_registers);
  p.Do(m_smoothed_weight);
  p.Do(m_have_smoothed_weight);
}

void BalanceBoard::LoadDefaults()
{
  m_button->SetControlExpression(0, "SPACE");
  m_sensor_groups[0]->SetControlExpression(0, "Q");
  m_sensor_groups[1]->SetControlExpression(0, "W");
  m_sensor_groups[2]->SetControlExpression(0, "A");
  m_sensor_groups[3]->SetControlExpression(0, "S");
}

ControllerEmu::ControlGroup* BalanceBoard::GetGroup(BalanceBoardGroup group)
{
  switch (group)
  {
  case BalanceBoardGroup::Button: return m_button;
  case BalanceBoardGroup::TopRight: return m_sensor_groups[0];
  case BalanceBoardGroup::BottomRight: return m_sensor_groups[1];
  case BalanceBoardGroup::TopLeft: return m_sensor_groups[2];
  case BalanceBoardGroup::BottomLeft: return m_sensor_groups[3];
  default:
    ASSERT(false);
    return nullptr;
  }
}

int BalanceBoard::BusRead(u8 slave_addr, u8 addr, int count, u8* data_out)
{
  if (slave_addr != I2C_ADDR || addr + count > static_cast<int>(m_registers.size()))
    return 0;
  std::copy_n(m_registers.begin() + addr, count, data_out);
  return count;
}

int BalanceBoard::BusWrite(u8 slave_addr, u8 addr, int count, const u8* data_in)
{
  if (slave_addr != I2C_ADDR || addr + count > static_cast<int>(m_registers.size()))
    return 0;
  std::copy_n(data_in, count, m_registers.begin() + addr);
  return count;
}

bool EncryptedExtension::ReadDeviceDetectPin() const
{
  return true;
}

int EncryptedExtension::BusRead(u8 slave_addr, u8 addr, int count, u8* data_out)
{
  if (I2C_ADDR != slave_addr)
    return 0;

  if (offsetof(Register, controller_data) == addr)
  {
    // This is where real hardware would update controller data
  }

  auto const result = RawRead(&m_reg, addr, count, data_out);

  // Encrypt data read from extension register.
  if (ENCRYPTION_ENABLED == m_reg.encryption)
  {
    if (m_is_key_dirty)
    {
      UpdateEncryptionKey();
      m_is_key_dirty = false;
    }

    ext_key.Encrypt(data_out, addr, count);
  }

  return result;
}

int EncryptedExtension::BusWrite(u8 slave_addr, u8 addr, int count, const u8* data_in)
{
  if (I2C_ADDR != slave_addr)
    return 0;

  auto const result = RawWrite(&m_reg, addr, count, data_in);

  constexpr u8 ENCRYPTION_KEY_DATA_BEGIN = offsetof(Register, encryption_key_data);
  constexpr u8 ENCRYPTION_KEY_DATA_END = ENCRYPTION_KEY_DATA_BEGIN + 0x10;

  if (addr + count > ENCRYPTION_KEY_DATA_BEGIN && addr < ENCRYPTION_KEY_DATA_END)
  {
    // FYI: Real extensions seem to require the key data written in specifically sized chunks.
    // We just run the key generation on all writes to the key area.
    m_is_key_dirty = true;
  }

  return result;
}

void EncryptedExtension::Reset()
{
  // Clear register state.
  m_reg = {};

  // Clear encryption key state.
  ext_key = {};
  m_is_key_dirty = true;
}

void EncryptedExtension::DoState(PointerWrap& p)
{
  p.Do(m_reg);

  if (p.IsReadMode())
  {
    // No need to sync the key when we can just regenerate it.
    m_is_key_dirty = true;
  }
}

void Extension1stParty::UpdateEncryptionKey()
{
  ext_key = KeyGen1stParty().GenerateFromExtensionKeyData(m_reg.encryption_key_data);
}

void Extension3rdParty::UpdateEncryptionKey()
{
  ext_key = KeyGen3rdParty().GenerateFromExtensionKeyData(m_reg.encryption_key_data);
}

}  // namespace WiimoteEmu
