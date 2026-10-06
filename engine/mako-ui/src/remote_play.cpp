/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "backend.hpp"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>

using namespace mako::ui;

void Backend::initializeRemotePlay() {
    if (m_remote_play_helper.isEmpty()) {
        const auto sibling = QDir(QCoreApplication::applicationDirPath()).filePath("mako-remote-play");
        m_remote_play_helper = QFileInfo(sibling).isExecutable() ? sibling
            : QStandardPaths::findExecutable("mako-remote-play");
    }
    if (m_remote_play_helper.isEmpty())
        m_remote_play_message = QStringLiteral("Install the current MAKO Renderer to manage Remote Play.");
    connect(&m_remote_play_process, &QProcess::stateChanged, this, [this] { emit refreshUI(); });
    connect(&m_remote_play_process, &QProcess::errorOccurred, this, [this] {
        m_remote_play_message = m_remote_play_process.errorString();
        emit refreshUI();
    });
    connect(&m_remote_play_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus exitStatus) {
        QJsonParseError error{};
        const auto document = QJsonDocument::fromJson(m_remote_play_process.readAllStandardOutput(), &error);
        const auto status = document.object();
        if (exitStatus != QProcess::NormalExit || error.error != QJsonParseError::NoError ||
                !status.value("success").isBool() || !status.value("installed").isBool() ||
                !status.value("managed").isBool() || !status.value("available").isBool() ||
                !status.value("running").isBool()) {
            m_remote_play_status = {};
            m_remote_play_message = QStringLiteral("Remote Play helper returned an invalid response.");
        } else {
            m_remote_play_status = status;
            m_remote_play_message = status.value("error").toString();
            if (m_remote_play_message.isEmpty())
                m_remote_play_message = status.value("message").toString();
        }
        emit refreshUI();
    });
}

void Backend::runRemotePlayAction(const QString& action) {
    if (remotePlayBusy() || m_remote_play_helper.isEmpty()) return;
    m_remote_play_message.clear();
    QStringList arguments{action};
    arguments.append({"--config", QString::fromStdString(m_config_path.string())});
    if (action == "install") {
        const auto launcher = QDir(QFileInfo(m_remote_play_helper).absolutePath()).filePath("mako-launch");
        arguments.append({"--launcher", launcher});
    }
    m_remote_play_process.start(m_remote_play_helper, arguments);
}

void Backend::refreshRemotePlay() { runRemotePlayAction("refresh"); }

bool Backend::editRemotePlayProfile() {
    if (remotePlayBusy() || !savePendingChanges()) return false;
    if (remotePlayManaged() && m_remote_play_status.value("configuration_path").toString() !=
            QString::fromStdString(m_config_path.string())) {
        m_remote_play_message = QStringLiteral("Open the configuration used by the Remote Play override before editing its profile.");
        emit refreshUI();
        return false;
    }
    const QString name = QStringLiteral("Remote-Play");
    const auto index = m_profile_list_model->stringList().indexOf(name);
    if (index >= 0) {
        profileSelected(index);
        return true;
    }
    createProfile(name);
    auto& profile = m_profiles.back();
    profile.active_in = {"streaming_client", "streaming_client.mako-original"};
    profile.multiplier = 2;
    profile.adaptive = false;
    profile.base_fps_cap = 30;
    profile.target_fps = 60;
    m_active_in_list_models.back()->setStringList({"streaming_client", "streaming_client.mako-original"});
    const bool saved = savePendingChanges();
    emit refreshUI();
    return saved;
}

void Backend::setRemotePlayOverride(const bool enabled) {
    if (remotePlayBusy() || m_remote_play_helper.isEmpty()) return;
    if (enabled && !editRemotePlayProfile()) return;
    if (!savePendingChanges()) return;
    runRemotePlayAction(enabled ? "install" : "remove");
}
