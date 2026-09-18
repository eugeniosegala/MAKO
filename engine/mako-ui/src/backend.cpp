/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <QStringListModel>
#include <QStringList>
#include <QString>
#include <QFile>

#include "backend.hpp"
#include "utils.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/configuration/config.hpp"
#include "mako-common/configuration/launch.hpp"

#include <exception>
#include <filesystem>
#include <iostream>

using namespace mako;
using namespace mako::ui;

Backend::Backend(std::filesystem::path procRoot) : m_proc_root(std::move(procRoot)) {
    // load configuration
    ls::ConfigFile config{};

    auto path = ls::findConfigurationFile();
    if (std::filesystem::exists(path)) {
        try {
            config = ls::ConfigFile(path);
        } catch (const std::exception&) {
            if (!QFile::rename(QString::fromStdString(path.string()),
                    QString::fromStdString(path.string() + ".old"))) {
                throw ls::error("the invalid configuration was preserved; unable to "
                    "create its .old backup without replacing an existing file. "
                    "Repair the configuration or move the existing backup before retrying");
            }
            std::cerr << "the configuration file is invalid, it has been backed up to '.old'\n";
        }
    }

    this->m_global = config.global();
    this->m_profiles = config.profiles();

    ls::LaunchConfigFile launchConfig{};
    const auto launchPath = ls::findLaunchConfigurationFile();
    if (std::filesystem::exists(launchPath)) {
        try {
            launchConfig = ls::LaunchConfigFile(launchPath);
        } catch (const std::exception& error) {
            std::cerr << "the standalone launcher configuration is invalid and was ignored:\n- "
                << error.what() << "\n";
        }
    }
    this->m_launch = launchConfig.settings();

    // create gpu list
    this->m_gpu_list = ui::getAvailableGPUs();

    // create profile list model
    QStringList profiles;
    for (const auto& profile : this->m_profiles)
        profiles.append(QString::fromStdString(profile.name));

    this->m_profile_list_model = new QStringListModel(profiles, this);

    // create active_in list models
    this->m_active_in_list_models.reserve(this->m_profiles.size());
    for (const auto& profile : this->m_profiles) {
        QStringList active_in;
        for (const auto& path : profile.active_in)
            active_in.append(QString::fromStdString(path));

        this->m_active_in_list_models.push_back(new QStringListModel(active_in, this));
    }

    // try to select first profile
    if (!this->m_profiles.empty())
        this->m_profile_index = 0;

    // Save on the QObject's thread so profile edits and serialization cannot race.
    // A single-shot timer has no idle wakeups; shutdown flushes its pending edit.
    this->m_config_path = path;
    this->m_launch_path = launchPath;
    this->m_save_timer.setSingleShot(true);
    this->m_save_timer.setInterval(500);
    connect(&this->m_save_timer, &QTimer::timeout, this, &Backend::savePendingChanges);
}

Backend::~Backend() {
    if (m_detection_thread)
        m_detection_thread->wait();
    this->savePendingChanges();
}

QVariantList Backend::getRunningGames() const {
    QVariantList games;
    for (const auto& game : m_running_games) {
        games.append(QVariantMap{
            {"name", game.name}, {"pid", game.pid}, {"executable", game.executable}
        });
    }
    return games;
}

void Backend::refreshRunningGames(const bool includeAllApplications) {
    if (m_scanning_games)
        return;
    if (m_detection_thread)
        m_detection_thread->wait();
    m_running_games.clear();
    m_capture_failed = false;
    m_scanning_games = true;
    emit runningGamesChanged();
    m_detection_thread.reset(QThread::create([this, includeAllApplications] {
        auto games = detectRunningGames(m_proc_root, includeAllApplications);
        QMetaObject::invokeMethod(this, [this, games = std::move(games)] {
            m_running_games = games;
            m_scanning_games = false;
            emit runningGamesChanged();
        }, Qt::QueuedConnection);
    }));
    m_detection_thread->start();
}

bool Backend::captureRunningGame(const int index, const bool create) {
    if (m_scanning_games || index < 0 || std::cmp_greater_equal(index, m_running_games.size()))
        return false;
    const auto& game = m_running_games.at(static_cast<size_t>(index));
    const auto identity = runningGameIdentity(game, m_proc_root);
    if (!identity) {
        m_capture_failed = true;
        emit runningGamesChanged();
        return false;
    }
    // Reuse the Renderer's matching precedence, including manually configured
    // path suffixes/process names, without applying the UI's launch environment.
    ls::ConfigFile config;
    config.profiles() = m_profiles;
    const auto match = ls::findProfile(config, *identity, false);
    if (match) {
        for (size_t profile = 0; profile < m_profiles.size(); ++profile) {
            const auto& existing = m_profiles.at(profile);
            if (existing.name == match->second.name && existing.active_in == match->second.active_in) {
                profileSelected(static_cast<int>(profile));
                return true;
            }
        }
    }
    if (create) {
        QString name = game.name;
        int suffix = 2;
        const auto names = m_profile_list_model->stringList();
        while (names.contains(name))
            name = game.name + QStringLiteral(" (%1)").arg(suffix++);
        createProfile(name);
    }
    if (!isValidProfileIndex())
        return false;
    addActiveIn(game.name);
    return true;
}

void Backend::savePendingChanges() {
    this->m_save_timer.stop();
    if (std::exchange(this->m_config_dirty, false)) {
        try {
            ls::ConfigFile config{};
            config.global() = this->m_global;
            config.profiles() = this->m_profiles;
            config.write(this->m_config_path);
        } catch (const std::exception& error) {
            std::cerr << "MAKO Renderer: unable to write configuration:\n- "
                << error.what() << "\n";
        }
    }
    if (std::exchange(this->m_launch_dirty, false)) {
        try {
            ls::LaunchConfigFile config{};
            config.settings() = this->m_launch;
            config.write(this->m_launch_path);
        } catch (const std::exception& error) {
            std::cerr << "MAKO Renderer: unable to write standalone launcher configuration:\n- "
                << error.what() << "\n";
        }
    }
}
