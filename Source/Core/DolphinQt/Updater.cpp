// Copyright 2018 Dolphin Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinQt/Updater.h"

#include <algorithm>
#include <utility>

#include <QDesktopServices>
#include <QCoreApplication>
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

    const QString commit = QString::fromStdString(info.new_hash.substr(0, std::min<size_t>(8, info.new_hash.size())));
    auto* label = new QLabel(
        tr("<h2>A new version of Fin is available!</h2>"
           "Fin %1 is ready to download directly from Fin.<br>"
           "Update commit: <code>%2</code><br>"
           "You are running %3.<br><br>"
           "<h4>Release Notes:</h4>")
            .arg(QString::fromStdString(info.new_shortrev))
            .arg(commit)
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
    auto* install_btn = buttons->addButton(tr("Download and Install"), QDialogButtonBox::AcceptRole);
    auto* open_release_btn = buttons->addButton(tr("Open GitHub Release"), QDialogButtonBox::ActionRole);

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
    connect(open_release_btn, &QPushButton::clicked, m_parent, [this, release_url = info.release_url] {
      if (!QDesktopServices::openUrl(QUrl(QString::fromStdString(release_url))))
      {
        QMessageBox::warning(m_parent, tr("Unable to Open Release"),
                              tr("Fin could not open the GitHub release page."));
      }
    });

    if (dialog->exec() == QDialog::Accepted)
    {
      if (AutoUpdateChecker::TriggerUpdate(info, AutoUpdateChecker::RestartMode::RESTART_AFTER_UPDATE))
      {
        install_btn->setEnabled(false);
        m_parent->close();
        QCoreApplication::quit();
      }
    }

    return 0;
  });
}
