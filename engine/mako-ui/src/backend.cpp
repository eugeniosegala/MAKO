/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <QStringListModel>
#include <QStringList>
#include <QString>

#include "backend.hpp"
#include "utils.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/configuration/config.hpp"
#include "mako-common/configuration/launch.hpp"

#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <thread>

using namespace mako;
using namespace mako::ui;

Backend::Backend() {
    // load configuration
    ls::ConfigFile config{};

    auto path = ls::findConfigurationFile();
    if (std::filesystem::exists(path)) {
        try {
            config = ls::ConfigFile(path);
        } catch (const std::exception&) {
            std::cerr << "the configuration file is invalid, it has been backed up to '.old'\n";
            std::filesystem::rename(path, path.string() + ".old");
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

    // spawn saving thread
    std::thread([this, path, launchPath]() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            const bool configDirty = this->m_config_dirty.exchange(false);
            const bool launchDirty = this->m_launch_dirty.exchange(false);
            if (!configDirty && !launchDirty)
                continue;

            if (configDirty) {
                ls::ConfigFile config{};
                config.global() = this->m_global;
                config.profiles() = this->m_profiles;

                try {
                    std::filesystem::create_directories(path.parent_path());
                    if (!std::filesystem::exists(path.parent_path()))
                        throw ls::error("unable to create configuration directory");
                    config.write(path);
                } catch (const std::exception& error) {
                    std::cerr << "unable to write configuration:\n- "
                        << error.what() << "\n";
                }
            }

            if (launchDirty) {
                ls::LaunchConfigFile launchConfig{};
                launchConfig.settings() = this->m_launch;
                try {
                    launchConfig.write(launchPath);
                } catch (const std::exception& error) {
                    std::cerr << "unable to write standalone launcher configuration:\n- "
                        << error.what() << "\n";
                }
            }
        }
    }).detach();
}
