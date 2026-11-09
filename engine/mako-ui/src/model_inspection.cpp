/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "backend.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>

#include <chrono>
#include <sys/stat.h>

using namespace mako::ui;

namespace {
qint64 now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Detect replacements even when size and modification time have been restored.
QString fileIdentity(const QString& path) {
    struct stat status{};
    if (path.isEmpty() || ::stat(QFile::encodeName(path).constData(), &status) != 0)
        return path + QStringLiteral(":missing");
    return QFileInfo(path).canonicalFilePath() + ':' +
        QString::number(status.st_dev) + ':' + QString::number(status.st_ino) + ':' +
        QString::number(status.st_size) + ':' + QString::number(status.st_mtim.tv_sec) + ':' +
        QString::number(status.st_mtim.tv_nsec) + ':' + QString::number(status.st_ctim.tv_sec) + ':' +
        QString::number(status.st_ctim.tv_nsec);
}

QString inspectorCommand() {
    const auto sibling = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("mako-cli"));
    const QFileInfo siblingInfo(sibling);
    if (siblingInfo.isFile() && siblingInfo.isExecutable()) return sibling;
    const auto onPath = QStandardPaths::findExecutable(QStringLiteral("mako-cli"));
    if (!onPath.isEmpty()) return onPath;
    return QDir::home().filePath(QStringLiteral(".local/share/mako-render/bin/mako-cli"));
}
}

bool Backend::isLsfgUnavailable() const {
    return m_lossless_scaling_missing || m_lsfg_inspection.state == ModelInspectionState::Unavailable;
}

bool Backend::isLs1Unavailable() const {
    return m_lossless_scaling_missing || m_ls1_inspection.state == ModelInspectionState::Unavailable;
}

bool Backend::isModelInspectionUnavailable() const {
    if (!isValidProfileIndex() || m_lossless_scaling_missing) return false;
    return (getFrameGenerationModel() == QStringLiteral("lossless-scaling") &&
            m_lsfg_inspection.state == ModelInspectionState::Unknown) ||
        (ls::licensedScalingModelRequested(editingProfile().scaling_method) &&
            m_ls1_inspection.state == ModelInspectionState::Unknown);
}

void Backend::initializeModelInspection() {
    m_model_inspection_timer.setSingleShot(true);
    m_model_inspection_timer.setInterval(200);
    connect(&m_model_inspection_timer, &QTimer::timeout, this, &Backend::inspectSelectedModels);
    connect(this, &Backend::refreshUI, this, [this] {
        if (isValidProfileIndex()) updateModelInspectionKeys();
        m_model_inspection_timer.start();
    });
    m_model_inspection_timer.start();
}

void Backend::updateModelInspectionKeys() {
    m_model_cli = inspectorCommand();
    const auto identity = fileIdentity(m_model_dll_path) + '\n' + fileIdentity(m_model_cli);
    const auto update = [&](ModelInspection& inspection, const QStringList& arguments) {
        const auto key = identity + '\n' + arguments.join('\n');
        if (inspection.key != key || (inspection.checkedAt && now() - inspection.checkedAt >= 300000)) {
            if (!inspection.key.isEmpty()) m_lossless_scaling_fallback = false;
            inspection = {key, arguments};
        }
    };
    QStringList fg{QStringLiteral("--lsfg")};
    if (!getAllowFP16()) fg.append(QStringLiteral("--no-fp16"));
    update(m_lsfg_inspection, fg);
    // Retain the checked LS1 variant after auto-selecting MAKO so failed choices
    // stay disabled until their DLL, sharpness, or inspector changes.
    auto method = getScalingMethod();
    if (method != QStringLiteral("ls1") && method != QStringLiteral("ls1-performance"))
        method = m_ls1_inspection.arguments.value(1, QStringLiteral("ls1"));
    update(m_ls1_inspection, {QStringLiteral("--ls1"), method, QStringLiteral("--sharpness"),
        QString::number(getScalingSharpness(), 'g', 9), QStringLiteral("--no-fp16")});
}

void Backend::inspectSelectedModels() {
    if (!isValidProfileIndex()) {
        if (m_model_inspection_process) m_model_inspection_process->kill();
        return;
    }
    // File presence and inspection caches share the same refresh owner. No idle
    // polling: settings changes and window activation schedule bounded work.
    refreshLosslessScaling();
    m_model_inspection_timer.stop();
    if (m_lossless_scaling_missing) {
        if (m_model_inspection_process) m_model_inspection_process->kill();
        return;
    }
    if (m_model_inspection_process) {
        const auto& running = m_model_inspecting_fg ? m_lsfg_inspection : m_ls1_inspection;
        const bool stillSelected = m_model_inspecting_fg
            ? getFrameGenerationModel() == QStringLiteral("lossless-scaling")
            : ls::licensedScalingModelRequested(editingProfile().scaling_method);
        if (running.key != m_model_inspection_request_key || !stillSelected)
            m_model_inspection_process->kill();
        return;
    }
    const auto needsCheck = [](const ModelInspection& inspection) {
        return inspection.state == ModelInspectionState::Unchecked ||
            (inspection.state == ModelInspectionState::Unknown && now() - inspection.checkedAt >= 30000);
    };
    const bool fg = getFrameGenerationModel() == QStringLiteral("lossless-scaling") && needsCheck(m_lsfg_inspection);
    if (!fg && !(ls::licensedScalingModelRequested(editingProfile().scaling_method) && needsCheck(m_ls1_inspection))) return;
    auto& inspection = fg ? m_lsfg_inspection : m_ls1_inspection;
    inspection.state = ModelInspectionState::Checking;
    const auto key = inspection.key;
    auto* process = new QProcess(this);
    m_model_inspection_process = process;
    m_model_inspection_request_key = key;
    m_model_inspecting_fg = fg;
    process->setStandardErrorFile(QProcess::nullDevice());
    const auto output = std::make_shared<QByteArray>();
    const auto rejected = std::make_shared<bool>(false);
    const auto finish = [this, process, fg, key](ModelInspectionState state) {
        if (m_model_inspection_process != process) return;
        m_model_inspection_process = nullptr;
        process->disconnect(this);
        process->deleteLater();
        updateModelInspectionKeys();
        auto& result = fg ? m_lsfg_inspection : m_ls1_inspection;
        // A late response cannot rewrite a different DLL/precision/LS1 variant.
        if (result.key == key) {
            const bool selected = isValidProfileIndex() && (fg
                ? getFrameGenerationModel() == QStringLiteral("lossless-scaling")
                : ls::licensedScalingModelRequested(editingProfile().scaling_method));
            result.state = state == ModelInspectionState::Unknown && !selected
                ? ModelInspectionState::Unchecked : state;
            result.checkedAt = result.state == ModelInspectionState::Unchecked ? 0 : now();
            if (selectAvailableModels()) {
                m_config_dirty = true;
                m_save_timer.start();
            }
        }
        emit refreshUI();
    };
    connect(process, &QProcess::readyReadStandardOutput, this, [process, output, rejected] {
        output->append(process->readAllStandardOutput());
        if (output->size() > 65536) { *rejected = true; output->clear(); process->kill(); }
    });
    connect(process, &QProcess::errorOccurred, this, [process, rejected, finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(ModelInspectionState::Unknown);
        else { *rejected = true; process->kill(); }
    });
    connect(process, &QProcess::finished, this, [process, output, rejected, finish](int code, QProcess::ExitStatus status) {
        output->append(process->readAllStandardOutput());
        const auto document = QJsonDocument::fromJson(*output);
        const auto result = document.object();
        const auto compatible = result.value(QStringLiteral("compatible"));
        const auto schema = result.value(QStringLiteral("schema_version"));
        const bool valid = !*rejected && output->size() <= 65536 && status == QProcess::NormalExit &&
            document.isObject() && schema.isDouble() && schema.toDouble() == 1 && compatible.isBool() &&
            code == (compatible.toBool() ? 0 : 1);
        finish(!valid ? ModelInspectionState::Unknown : compatible.toBool()
            ? ModelInspectionState::Available : ModelInspectionState::Unavailable);
    });
    auto* timeout = new QTimer(process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, process, [process, rejected] { *rejected = true; process->kill(); });
    auto arguments = inspection.arguments;
    arguments.prepend(m_model_dll_path);
    arguments.prepend(QStringLiteral("--dll"));
    arguments.prepend(QStringLiteral("inspect-dll"));
    process->start(m_model_cli, arguments);
    timeout->start(15000);
}
