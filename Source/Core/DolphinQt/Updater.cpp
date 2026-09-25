// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Updater.h"

#include <utility>

#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include "Common/Version.h"

#include "DolphinQt/QtUtils/RunOnObject.h"
#include "DolphinQt/Settings.h"

// Refer to docs/autoupdate_overview.md for a detailed overview of the autoupdate process

Updater::Updater(QWidget* parent, std::string update_track, std::string hash_override)
    : m_parent(parent), m_update_track(std::move(update_track)),
      m_hash_override(std::move(hash_override))
{
  connect(this, &QThread::finished, this, &QObject::deleteLater);
}

void Updater::run()
{
  AutoUpdateChecker::CheckForUpdate(m_update_track, m_hash_override,
                                    AutoUpdateChecker::CheckType::Automatic);
}

void Updater::CheckForUpdate()
{
  AutoUpdateChecker::CheckForUpdate(m_update_track, m_hash_override,
                                    AutoUpdateChecker::CheckType::Manual);
}

void Updater::OnUpdateAvailable(const NewVersionInformation& info)
{
  RunOnObject(m_parent, [&] {
    QDialog* dialog = new QDialog(m_parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setWindowTitle(tr("Update available"));

    auto* label = new QLabel(
        tr("<h2>A new version of this Dolphin fork is available!</h2>"
           "Dolphin %1 is available on GitHub.<br>"
           "You are running %2.<br><br>"
           "<h4>Release Notes:</h4>")
            .arg(QString::fromStdString(info.new_shortrev))
            .arg(QString::fromStdString(Common::GetScmDescStr())));
    label->setTextFormat(Qt::RichText);

    auto* changelog = new QTextBrowser;
    changelog->setHtml(QString::fromStdString(info.changelog_html));
    changelog->setOpenExternalLinks(true);
    changelog->setMinimumWidth(500);

    auto* buttons = new QDialogButtonBox;

    auto* disable_btn =
        buttons->addButton(tr("Disable Automatic Checks"), QDialogButtonBox::DestructiveRole);
    buttons->addButton(tr("Remind Me Later"), QDialogButtonBox::RejectRole);
    buttons->addButton(tr("Open GitHub Release"), QDialogButtonBox::AcceptRole);

    auto* layout = new QVBoxLayout;
    dialog->setLayout(layout);

    layout->addWidget(label);
    layout->addWidget(changelog);
    layout->addWidget(buttons);

    connect(disable_btn, &QPushButton::clicked, [dialog] {
      Settings::Instance().SetAutoUpdateTrack(QString{});
      dialog->reject();
    });

    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

    if (dialog->exec() == QDialog::Accepted)
    {
      const bool opened = QDesktopServices::openUrl(QUrl(QString::fromStdString(info.release_url)));
      if (!opened)
      {
        QMessageBox::warning(m_parent, tr("Unable to Open Release"),
                              tr("Dolphin could not open the GitHub release page."));
      }
    }

    return 0;
  });
}
