/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "process_detection.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QCoreApplication>

#include <algorithm>
#include <unistd.h>

namespace {

QString executablePath(const ls::Identification& identity) {
    return QString::fromStdString(identity.wine_executable.value_or(identity.executable));
}

QString executableName(const QString& path) {
    return path.mid(std::max(path.lastIndexOf('/'), path.lastIndexOf('\\')) + 1);
}

bool helperProcess(const QString& name) {
    static const QSet<QString> helpers{
        "bash", "bwrap", "conhost.exe", "explorer.exe", "flatpak",
        "gameoverlayui", "gamescope", "mako-ui", "mako-launch", "mako-run",
        "ntoskrnl.exe", "plugplay.exe", "proton", "pv-adverb", "pv-bwrap",
        "reaper", "rpcss.exe", "services.exe", "sh", "srt-bwrap", "steam",
        "steam.exe", "steamwebhelper", "svchost.exe", "tabtip.exe",
        "winedevice.exe", "wine", "wine64", "wineboot.exe",
        "winemenubuilder.exe", "wine-preloader", "wine64-preloader",
        "wineserver", "xalia.exe"
    };
    const QString lower = name.toLower();
    return lower.isEmpty() || helpers.contains(lower) ||
        lower.startsWith("pressure-vessel") || lower.startsWith("steam-runtime-") ||
        lower.startsWith("python");
}

bool usesGraphics(const QString& directory) {
    QFile maps(directory + "/maps");
    if (!maps.open(QIODevice::ReadOnly))
        return false;
    // Bound snapshot work even for very large processes. The all-applications
    // option remains available when graphics libraries cannot be inspected.
    const QByteArray contents = maps.read(4 * 1024 * 1024);
    return contents.contains("/libvulkan") || contents.contains("/libGL") ||
        contents.contains("/libEGL");
}

}

std::vector<mako::ui::RunningGame> mako::ui::detectRunningGames(
        const std::filesystem::path& procRoot, const bool includeAllApplications) {
    std::vector<RunningGame> games;
    const QDir directory(QString::fromStdString(procRoot.string()));
    for (const QFileInfo& entry : directory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool validPid = false;
        const int pid = entry.fileName().toInt(&validPid);
        if (!validPid || pid <= 0 || pid == QCoreApplication::applicationPid() ||
                entry.isSymLink() || entry.ownerId() != getuid())
            continue;
        const auto identity = ls::identifyProcess(entry.filePath().toStdString());
        const QString executable = executablePath(identity);
        const QString name = executableName(executable);
        if (identity.executable.empty() || helperProcess(name) ||
                ls::isExcludedLauncher(identity))
            continue;
        if (!includeAllApplications && !identity.wine_executable &&
                !usesGraphics(entry.filePath()))
            continue;
        games.push_back({pid, name, executable});
    }
    std::sort(games.begin(), games.end(), [](const RunningGame& left, const RunningGame& right) {
        const int order = QString::compare(left.name, right.name, Qt::CaseInsensitive);
        return order == 0 ? left.pid < right.pid : order < 0;
    });
    return games;
}

std::optional<ls::Identification> mako::ui::runningGameIdentity(
        const RunningGame& game, const std::filesystem::path& procRoot) {
    const auto directory = procRoot / std::to_string(game.pid);
    const QFileInfo entry(QString::fromStdString(directory.string()));
    if (!entry.isDir() || entry.isSymLink() || entry.ownerId() != getuid())
        return std::nullopt;
    const auto identity = ls::identifyProcess(directory);
    if (identity.executable.empty() || ls::isExcludedLauncher(identity) ||
            executablePath(identity) != game.executable)
        return std::nullopt;
    return identity;
}
