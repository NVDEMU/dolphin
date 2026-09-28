// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "UICommon/AutoUpdate.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <string>

#include <fmt/format.h>
#include <picojson.h>

#include "Common/HttpRequest.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/ScopeGuard.h"
#include "Common/StringUtil.h"
#include "Common/Version.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <sys/stat.h>
#endif

#if defined(_WIN32) || defined(__APPLE__)
#define OS_SUPPORTS_UPDATER
#include "Common/CommonFuncs.h"
#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#endif

// Refer to docs/autoupdate_overview.md for a detailed overview of the autoupdate process

namespace
{
std::atomic_bool s_check_in_progress = false;
bool s_update_triggered = false;

#ifdef __APPLE__
const char UPDATER_CONTENT_PATH[] = "/Contents/MacOS/Dolphin Updater";
#endif

#ifdef OS_SUPPORTS_UPDATER

const char UPDATER_LOG_FILE[] = "Updater.log";

std::string UpdaterPath(bool relocated = false)
{
#ifdef __APPLE__
  if (relocated)
    return File::GetExeDirectory() + DIR_SEP + ".Dolphin Updater.2.app";
  else
    return File::GetBundleDirectory() + DIR_SEP + "Contents/Helpers/Dolphin Updater.app";
#else
  return File::GetExeDirectory() + DIR_SEP + "Updater.exe";
#endif
}

std::string MakeUpdaterCommandLine(const std::map<std::string, std::string>& flags)
{
#ifdef __APPLE__
  std::string cmdline = "\"" + UpdaterPath(true) + UPDATER_CONTENT_PATH + "\"";
#else
  std::string cmdline = UpdaterPath();
#endif

  cmdline += " ";

  for (const auto& pair : flags)
  {
    std::string value = "--" + pair.first + "=" + pair.second;
    value = ReplaceAll(value, "\"", "\\\"");  // Escape double quotes.
    value = "\"" + value + "\" ";
    cmdline += value;
  }
  return cmdline;
}

void CleanupFromPreviousUpdate()
{
#ifdef __APPLE__
  // Remove the relocated updater file.
  File::DeleteDirRecursively(UpdaterPath(true));

  // Remove the old (non-embedded) updater app bundle.
  // While the update process will delete the files within the old bundle after updating to a
  // version with an embedded updater, it won't delete the folder structure of the bundle, so
  // we should clean those leftovers up.
  File::DeleteDirRecursively(File::GetExeDirectory() + DIR_SEP + "Dolphin Updater.app");
#endif

  // Updater.log was moved from GetExeDirectory() to GetUserPath(D_LOGS_IDX) in 5.0-14529.
  File::Delete(File::GetExeDirectory() + DIR_SEP + "Updater.log",
               File::IfAbsentBehavior::NoConsoleWarning);
}
#endif

// This ignores i18n because most of the text in there (change descriptions) is only going to be
// written in english anyway.
std::string GenerateChangelog(const picojson::array& versions)
{
  std::string changelog;
  for (const auto& ver : versions)
  {
    if (!ver.is<picojson::object>())
      continue;
    picojson::object ver_obj = ver.get<picojson::object>();

    if (ver_obj["changelog_html"].is<picojson::null>())
    {
      if (!changelog.empty())
        changelog += "<div style=\"margin-top: 0.4em;\"></div>";  // Vertical spacing.

      // Try to link to the PR if we have this info. Otherwise just show shortrev.
      if (ver_obj["pr_url"].is<std::string>())
      {
        changelog += "<a href=\"" + ver_obj["pr_url"].get<std::string>() + "\">" +
                     ver_obj["shortrev"].get<std::string>() + "</a>";
      }
      else
      {
        changelog += ver_obj["shortrev"].get<std::string>();
      }
      const std::string escaped_description =
          Common::GetEscapedHtml(ver_obj["short_descr"].get<std::string>());
      changelog += " by <a href = \"" + ver_obj["author_url"].get<std::string>() + "\">" +
                   ver_obj["author"].get<std::string>() + "</a> &mdash; " + escaped_description;
    }
    else
    {
      if (!changelog.empty())
        changelog += "<hr>";
      changelog += "<b>Dolphin " + ver_obj["shortrev"].get<std::string>() + "</b>";
      changelog += "<p>" + ver_obj["changelog_html"].get<std::string>() + "</p>";
    }
  }
  return changelog;
}
}  // namespace

bool AutoUpdateChecker::SystemSupportsAutoUpdates()
{
#if defined(AUTOUPDATE)
  return true;
#else
  return false;
#endif
}

static std::string GetUpdateRepository()
{
  auto repository = std::getenv("DOLPHIN_UPDATE_REPOSITORY");
  if (repository && *repository)
    return repository;
  return "NVDEMU/dolphin";
}

static std::string NormalizeReleaseTag(std::string tag)
{
  if (tag.rfind("dolphin-", 0) == 0)
    tag.erase(0, 8);
  if (tag.rfind("v", 0) == 0)
    tag.erase(0, 1);
  if (tag.size() >= 6 && tag.compare(tag.size() - 6, 6, "-dirty") == 0)
    tag.erase(tag.size() - 6);
  return tag;
}

static std::string GetUpdateServerUrl()
{
  return fmt::format("https://api.github.com/repos/{}/releases/latest", GetUpdateRepository());
}

static bool IsGitCommitHash(const std::string& value)
{
  if (value.size() != 40)
    return false;

  return std::all_of(value.begin(), value.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
  });
}

static std::string GetExpectedPackageName(const std::string& tag)
{
#ifdef __APPLE__
  return fmt::format("Fin-{}-macOS-universal.zip", tag);
#elif defined(_WIN32)
#if defined(_M_ARM64) || defined(__aarch64__)
  return fmt::format("Fin-{}-Windows-arm64.zip", tag);
#else
  return fmt::format("Fin-{}-Windows-x64.zip", tag);
#endif
#else
  return {};
#endif
}

static u32 GetOwnProcessId()
{
#ifdef _WIN32
  return GetCurrentProcessId();
#else
  return getpid();
#endif
}

void AutoUpdateChecker::CheckForUpdate(std::string_view update_track,
                                       std::string_view hash_override, const CheckType check_type)
{
  bool expected_check_in_progress = false;
  if (!s_check_in_progress.compare_exchange_strong(expected_check_in_progress, true))
    return;

  Common::ScopeGuard guard([]() { s_check_in_progress.store(false); });

  // Don't bother checking if updates are not supported or not enabled.
  if (!SystemSupportsAutoUpdates() || update_track.empty())
    return;

  const bool is_manual_check = check_type == CheckType::Manual;

  Common::HttpRequest req{std::chrono::seconds{10}};
  const Common::HttpRequest::Headers headers = {
      {"Accept", "application/vnd.github+json"},
      {"X-GitHub-Api-Version", "2022-11-28"},
      {"User-Agent", "Fin-NVDEMU-Updater"},
  };

  const std::string url = GetUpdateServerUrl();
  auto resp = req.Get(url, headers);
  if (!resp)
  {
    if (is_manual_check)
      CriticalAlertFmtT("Unable to contact GitHub's release API.");
    INFO_LOG_FMT(COMMON, "GitHub release check failed with HTTP status {}.",
                 req.GetLastResponseCode());
    return;
  }

  const std::string contents(reinterpret_cast<char*>(resp->data()), resp->size());
  INFO_LOG_FMT(COMMON, "GitHub release JSON response: {}", contents);

  picojson::value json;
  const std::string err = picojson::parse(json, contents);
  if (!err.empty() || !json.is<picojson::object>())
  {
    if (is_manual_check)
      CriticalAlertFmtT("Invalid JSON received from GitHub's release API.");
    return;
  }

  const picojson::object& obj = json.get<picojson::object>();
  const auto tag_it = obj.find("tag_name");
  const auto url_it = obj.find("html_url");
  const auto target_it = obj.find("target_commitish");
  if (tag_it == obj.end() || !tag_it->second.is<std::string>() ||
      url_it == obj.end() || !url_it->second.is<std::string>())
  {
    if (is_manual_check)
      CriticalAlertFmtT("GitHub returned an unexpected release response.");
    return;
  }

  const std::string latest_tag = NormalizeReleaseTag(tag_it->second.get<std::string>());

  // Releases are only useful to this updater when they have a version identifier.
  if (latest_tag.empty())
    return;

  std::string target_commit;
  if (target_it != obj.end() && target_it->second.is<std::string>())
    target_commit = target_it->second.get<std::string>();

  if (IsGitCommitHash(target_commit) && target_commit == Common::GetScmRevGitStr())
  {
    if (is_manual_check)
      SuccessAlertFmtT("You are running the latest release of this Dolphin fork.");
    INFO_LOG_FMT(COMMON, "GitHub release status: we are up to date.");
    return;
  }

  NewVersionInformation nvi;
  nvi.new_shortrev = latest_tag;
  nvi.new_hash = target_commit;
  nvi.release_url = url_it->second.get<std::string>();

  const std::string expected_package_name = GetExpectedPackageName(tag_it->second.get<std::string>());
  const auto assets_it = obj.find("assets");
  if (expected_package_name.empty() || assets_it == obj.end() ||
      !assets_it->second.is<picojson::array>())
  {
    if (is_manual_check)
      CriticalAlertFmtT("This Fin build does not have an installable update package.");
    return;
  }

  for (const auto& asset : assets_it->second.get<picojson::array>())
  {
    if (!asset.is<picojson::object>())
      continue;

    const auto& asset_obj = asset.get<picojson::object>();
    const auto name_it = asset_obj.find("name");
    const auto download_it = asset_obj.find("browser_download_url");
    if (name_it == asset_obj.end() || !name_it->second.is<std::string>() ||
        download_it == asset_obj.end() || !download_it->second.is<std::string>())
    {
      continue;
    }

    if (name_it->second.get<std::string>() == expected_package_name)
    {
      nvi.package_name = expected_package_name;
      nvi.package_url = download_it->second.get<std::string>();
      break;
    }
  }

  if (nvi.package_url.empty())
  {
    if (is_manual_check)
      CriticalAlertFmtT("GitHub has no installable Fin package for this platform.");
    return;
  }
  const auto body_it = obj.find("body");
  const std::string release_body =
      body_it != obj.end() && body_it->second.is<std::string>() ? body_it->second.get<std::string>() : "";

  nvi.changelog_html =
      "<p><b>Dolphin " + Common::GetEscapedHtml(latest_tag) + "</b> is available from GitHub.</p>";
  if (!release_body.empty())
    nvi.changelog_html += "<p>" + Common::GetEscapedHtml(release_body) + "</p>";

  OnUpdateAvailable(nvi);
}

void AutoUpdateChecker::TriggerUpdate(const AutoUpdateChecker::NewVersionInformation& info,
                                      const AutoUpdateChecker::RestartMode restart_mode)
{
  // Check to make sure we don't already have an update triggered
  if (s_update_triggered)
  {
    WARN_LOG_FMT(COMMON, "Auto-update: received a redundant trigger request, ignoring");
    return;
  }

#ifdef OS_SUPPORTS_UPDATER
  std::map<std::string, std::string> updater_flags;
  updater_flags["this-manifest-url"] = info.this_manifest_url;
  updater_flags["next-manifest-url"] = info.next_manifest_url;
  updater_flags["content-store-url"] = info.content_store_url;
  updater_flags["package-url"] = info.package_url;
  updater_flags["package-name"] = info.package_name;
  updater_flags["package-commit"] = info.new_hash;
  updater_flags["parent-pid"] = std::to_string(GetOwnProcessId());
#ifdef __APPLE__
  updater_flags["install-base-path"] =
      std::filesystem::path(File::GetBundleDirectory()).parent_path().string();
#else
  updater_flags["install-base-path"] = File::GetExeDirectory();
#endif
  updater_flags["log-file"] = File::GetUserPath(D_LOGS_IDX) + UPDATER_LOG_FILE;

  if (restart_mode == RestartMode::RESTART_AFTER_UPDATE)
    updater_flags["binary-to-restart"] = File::GetExePath();

#ifdef __APPLE__
  // Copy the updater so it can update itself if needed.
  const std::string reloc_updater_path = UpdaterPath(true);
  if (!File::Copy(UpdaterPath(), reloc_updater_path))
  {
    CriticalAlertFmtT("Unable to create updater copy.");
    return;
  }
  if (chmod((reloc_updater_path + UPDATER_CONTENT_PATH).c_str(), 0700) != 0)
  {
    CriticalAlertFmtT("Unable to set permissions on updater copy.");
    return;
  }
#endif

  // Run the updater!
  std::string command_line = MakeUpdaterCommandLine(updater_flags);
  INFO_LOG_FMT(COMMON, "Updater command line: {}", command_line);

#ifdef _WIN32
  STARTUPINFO sinfo{.cb = sizeof(sinfo)};
  sinfo.dwFlags = STARTF_FORCEOFFFEEDBACK;  // No hourglass cursor after starting the process.
  PROCESS_INFORMATION pinfo;
  if (CreateProcessW(UTF8ToWString(UpdaterPath()).c_str(), UTF8ToWString(command_line).data(),
                     nullptr, nullptr, FALSE, 0, nullptr, nullptr, &sinfo, &pinfo))
  {
    CloseHandle(pinfo.hThread);
    CloseHandle(pinfo.hProcess);
    s_update_triggered = true;
  }
  else
  {
    const std::string error = Common::GetLastErrorString();
    CriticalAlertFmtT("Could not start updater process: {0}", error);
  }
#else
  if (popen(command_line.c_str(), "r") == nullptr)
  {
    const std::string error = Common::LastStrerrorString();
    CriticalAlertFmtT("Could not start updater process: {0}", error);
  }
  else
  {
    s_update_triggered = true;
  }
#endif

#endif
}
