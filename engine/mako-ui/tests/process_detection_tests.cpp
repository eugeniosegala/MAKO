/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "backend.hpp"
#include "utils.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

// Process discovery and profile persistence must not require a Vulkan host.
QStringList mako::ui::getAvailableGPUs() { return {QStringLiteral("Default")}; }

namespace {

void require(const bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void writeFixture(const QString& path, const QByteArray& contents) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), "cannot open process fixture");
    require(file.write(contents) == contents.size(), "cannot write process fixture");
}

void processFixture(const QString& root, const int pid, const QString& executable,
        const QByteArray& maps = {}, const QByteArray& comm = "GameThread\n") {
    const QString directory = root + "/" + QString::number(pid);
    require(QDir().mkpath(directory), "cannot create process fixture");
    std::filesystem::create_symlink(executable.toStdString(), (directory + "/exe").toStdString());
    writeFixture(directory + "/maps", maps);
    writeFixture(directory + "/comm", comm);
}

void refreshGames(mako::ui::Backend& backend, const bool all = false) {
    QEventLoop events;
    QObject::connect(&backend, &mako::ui::Backend::runningGamesChanged, &events, [&] {
        if (!backend.isScanningGames())
            events.quit();
    });
    QTimer::singleShot(5000, &events, &QEventLoop::quit);
    backend.refreshRunningGames(all);
    require(backend.isScanningGames(), "process scan must run off the UI thread");
    require(!backend.captureRunningGame(0, true), "capture accepted an in-flight scan");
    backend.refreshRunningGames(all); // An overlapping request must be inert.
    events.exec();
    require(!backend.isScanningGames(), "process scan did not complete");
}

void testRunningGameCapture() {
    QTemporaryDir directory;
    require(directory.isValid(), "temporary process fixture directory failed");
    const QString proc = directory.filePath("proc");
    const QByteArray graphics = "100-200 r-xp 0 00:00 0 /usr/lib/libvulkan.so.1\n";
    processFixture(proc, 101, "/games/Native Game", graphics, "TruncatedName\n");
    // No Steam environment is necessary. Preserve spaces/case from the same
    // mapped Windows executable that the Renderer actually matches.
    processFixture(proc, 102, "/opt/proton/bin/wine64-preloader",
        "100-200 r-xp 0 00:00 0 /games/Windows Game/Windows Game.EXE\n", "GameThread\n");
    processFixture(proc, 103, "/usr/bin/steam", graphics);
    processFixture(proc, 104, "/opt/wine/bin/wine64",
        "100-200 r-xp 0 00:00 0 /games/UBISOFTCONNECT.EXE\n");
    processFixture(proc, 105, "/opt/wine/bin/wine64",
        "100-200 r-xp 0 00:00 0 /windows/system32/explorer.exe\n");
    processFixture(proc, 106, "/games/UnknownGraphicsGame", {}, {});
    processFixture(proc, 107, "/usr/bin/python3", graphics);
    processFixture(proc, 108, "/usr/bin/wineserver", graphics);
    processFixture(proc, 109, "/games/UplayWebCore.exe/ActualGame", graphics, "upc.exe\n");
    // An unrelated command-line argument or thread name is never captured.
    writeFixture(proc + "/101/cmdline", "Native Game OtherGame.exe");
    std::filesystem::create_directory_symlink((proc + "/101").toStdString(),
        (proc + "/110").toStdString());
    require(QDir().mkpath(proc + "/111"), "cannot create exited-process fixture");
    auto games = mako::ui::detectRunningGames(proc.toStdString(), false);
    require(games.size() == 3, "graphics scan includes helpers or misses a game");
    require(games.at(0).name == "ActualGame" && games.at(1).name == "Native Game" &&
            games.at(2).name == "Windows Game.EXE", "process identities were not authoritative");
    require(mako::ui::detectRunningGames(proc.toStdString(), true).size() == 4,
        "all-applications fallback must work without maps or comm");
    require(mako::ui::detectRunningGames(directory.filePath("missing").toStdString(), false).empty(),
        "missing procfs must be an empty result");

    qputenv("MAKO_CONFIG", directory.filePath("conf.toml").toUtf8());
    qputenv("MAKO_LAUNCH_CONFIG", directory.filePath("launcher.conf").toUtf8());
    qputenv("MAKO_PROFILE", "must-not-leak-into-capture");
    qunsetenv("MAKO_ENV");
    const auto identity = ls::identifyProcess((proc + "/102").toStdString());
    require(!identity.override && !identity.fallback, "capture inherited the UI environment");
    {
        mako::ui::Backend backend(proc.toStdString());
        while (backend.isValidProfileIndex())
            backend.deleteProfile();
        refreshGames(backend);
        require(!backend.captureRunningGame(-1, true) && !backend.captureRunningGame(99, true),
            "invalid selection must not create a profile");
        require(!backend.captureRunningGame(1, false), "capture cannot append without a profile");
        require(backend.captureRunningGame(1, true), "native game profile was not created");
        require(backend.getMatchedProcesses() == "Native Game", "native identity was not saved");
        backend.targetFPSUpdated(144);
        backend.addActiveIn("ManualAlias");
        require(backend.captureRunningGame(1, true), "repeated capture failed");
        require(backend.calculateProfileListModel()->rowCount() == 1 &&
                backend.getTargetFPS() == 144 && backend.getMatchedProcesses() == "Native Game, ManualAlias",
            "repeated capture duplicated a profile or discarded settings/manual identities");
        require(backend.captureRunningGame(2, false), "Proton capture failed");
        require(backend.getMatchedProcesses().endsWith("Windows Game.EXE"),
            "Proton capture stored a loader or truncated process name");

        backend.createProfile("Native Game");
        require(backend.captureRunningGame(1, false) && backend.getProfileIndex() == 0,
            "an existing match must be opened instead of creating an ambiguous match");
        backend.createProfile("ActualGame");
        require(backend.captureRunningGame(0, true), "capture with a name collision failed");
        require(backend.calculateProfileListModel()->stringList().last() == "ActualGame (2)",
            "capture did not create a unique profile name");

        std::filesystem::remove((proc + "/101/exe").toStdString());
        std::filesystem::create_symlink("/games/DifferentGame", (proc + "/101/exe").toStdString());
        const int profileCount = backend.calculateProfileListModel()->rowCount();
        require(!backend.captureRunningGame(1, true) && backend.captureFailed(),
            "capture accepted a reused PID");
        require(QDir(proc + "/102").removeRecursively(), "cannot remove exited game fixture");
        require(!backend.captureRunningGame(2, true), "capture accepted an exited game");
        require(backend.calculateProfileListModel()->rowCount() == profileCount,
            "stale capture changed profiles");
        refreshGames(backend);
        require(!backend.captureFailed(), "refresh did not clear the stale selection error");
    }
    const ls::ConfigFile saved(directory.filePath("conf.toml").toStdString());
    require(saved.profiles().front().target_fps == 144, "capture did not persist existing settings");
    require(saved.profiles().front().active_in ==
            std::vector<std::string>{"Native Game", "ManualAlias", "Windows Game.EXE"},
        "captured executable matches did not survive serialization");
    const auto matched = ls::findProfile(saved, identity);
    require(matched && matched->second.name == "Native Game",
        "the Renderer cannot activate the profile produced by capture");
    {
        mako::ui::Backend backend(proc.toStdString());
        while (backend.isValidProfileIndex())
            backend.deleteProfile();
        backend.createProfile("Manual path match");
        // Match the full registered path, preserving Renderer suffix semantics.
        backend.addActiveIn("UplayWebCore.exe/ActualGame");
        const int selected = backend.getProfileIndex();
        refreshGames(backend);
        qputenv("MAKO_ENV", "1");
        require(backend.captureRunningGame(0, true) && backend.getProfileIndex() == selected,
            "manual path match was not reused independently of the UI environment");
        qunsetenv("MAKO_ENV");
        backend.refreshRunningGames(true);
        // Closing during an in-flight scan must join the worker safely.
    }
}

}

int main(int argc, char* argv[]) {
    const QCoreApplication application(argc, argv);
    try {
        testRunningGameCapture();
    } catch (const std::exception& error) {
        std::cerr << "MAKO Renderer: process detection test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
