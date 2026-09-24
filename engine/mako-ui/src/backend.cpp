/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <QStringListModel>
#include <QStringList>
#include <QString>
#include <QFile>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSaveFile>
#include <QUrl>

#include "backend.hpp"
#include "utils.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/configuration/config.hpp"
#include "mako-common/configuration/launch.hpp"
#include "mako-common/configuration/vkbasalt.hpp"

#include <exception>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace mako;
using namespace mako::ui;

namespace {

std::filesystem::path findBundledVkBasaltShaderDirectory() {
    if (const char* explicitPath = std::getenv("MAKO_VKBASALT_SHADER_DIR");
            explicitPath && *explicitPath != '\0') {
        return explicitPath;
    }
    const auto installed = std::filesystem::path(
        QCoreApplication::applicationDirPath().toStdString()
    ) / ".." / "share" / "mako-render" / "vkbasalt-shaders";
    if (std::filesystem::is_directory(installed))
        return std::filesystem::weakly_canonical(installed);
#ifdef MAKO_UI_VKBASALT_SHADER_SOURCE_DIR
    return MAKO_UI_VKBASALT_SHADER_SOURCE_DIR;
#else
    return installed.lexically_normal();
#endif
}

QString shellQuote(const QString& value) {
    QString quoted = value;
    quoted.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
    return QStringLiteral("'") + quoted + QStringLiteral("'");
}

}

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
    const auto configDirectory = path.parent_path();
    this->m_vkbasalt_profile_settings_path =
        configDirectory / "profile-wrapper-settings.json";
    this->m_profile_metadata_path = configDirectory / "profile-metadata.json";
    this->m_vkbasalt_profile_config_directory = configDirectory / "vkbasalt";
    this->m_vkbasalt_global_config_path = ls::findVkBasaltConfigurationFile();
    this->m_vkbasalt_shader_directory = findBundledVkBasaltShaderDirectory();
    this->loadVkBasaltProfiles();

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

void Backend::loadVkBasaltProfiles() {
    this->m_wrapper_settings_root = QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("profiles"), QJsonObject{}},
    };
    QFile settingsFile(QString::fromStdString(
        this->m_vkbasalt_profile_settings_path.string()
    ));
    if (settingsFile.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(
            settingsFile.readAll(), &error
        );
        const auto root = document.object();
        if (error.error == QJsonParseError::NoError &&
                document.isObject() &&
                root.value(QStringLiteral("version")).toInt(-1) == 1 &&
                root.value(QStringLiteral("profiles")).isObject()) {
            this->m_wrapper_settings_root = root;
        } else {
            std::cerr << "MAKO Renderer: ignoring invalid Decky profile wrapper settings\n";
        }
    }

    this->m_profile_metadata_root = QJsonObject{
        {QStringLiteral("version"), 1},
        {QStringLiteral("profiles"), QJsonObject{}},
    };
    QFile metadataFile(QString::fromStdString(
        this->m_profile_metadata_path.string()
    ));
    if (metadataFile.open(QIODevice::ReadOnly)) {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(
            metadataFile.readAll(), &error
        );
        const auto root = document.object();
        if (error.error == QJsonParseError::NoError &&
                document.isObject() &&
                root.value(QStringLiteral("version")).toInt(-1) == 1 &&
                root.value(QStringLiteral("profiles")).isObject()) {
            this->m_profile_metadata_root = root;
            const auto profiles = root.value(QStringLiteral("profiles")).toObject();
            for (auto iterator = profiles.begin(); iterator != profiles.end(); ++iterator) {
                const auto appId = iterator.value().toObject()
                    .value(QStringLiteral("steam_app_id"));
                if (appId.isString() && !appId.toString().isEmpty()) {
                    this->m_profile_steam_ids.emplace(
                        iterator.key().toStdString(), appId.toString().toStdString()
                    );
                }
            }
        } else {
            std::cerr << "MAKO Renderer: ignoring invalid Decky profile metadata\n";
        }
    }

    const auto storedProfiles = this->m_wrapper_settings_root
        .value(QStringLiteral("profiles")).toObject();
    this->m_vkbasalt_profiles.clear();
    this->m_vkbasalt_profiles.reserve(this->m_profiles.size());
    for (const auto& profile : this->m_profiles) {
        ls::VkBasaltConf settings;
        const auto stored = storedProfiles
            .value(QString::fromStdString(profile.name)).toObject();
        settings.enabled = stored.value(QStringLiteral("external_vulkan_layer"))
            .toString() == QStringLiteral("vkbasalt");
        const auto sharpening = stored.value(
            QStringLiteral("vkbasalt_sharpening")
        ).toString();
        if (ls::isVkBasaltSharpening(sharpening.toStdString()))
            settings.sharpening = sharpening.toStdString();
        const auto antialiasing = stored.value(
            QStringLiteral("vkbasalt_antialiasing")
        ).toString();
        if (ls::isVkBasaltAntialiasing(antialiasing.toStdString()))
            settings.antialiasing = antialiasing.toStdString();
        const auto shader = stored.value(QStringLiteral("vkbasalt_shader"))
            .toString();
        if (ls::isVkBasaltShader(shader.toStdString()))
            settings.shader = shader.toStdString();
        if (stored.value(QStringLiteral("vkbasalt_sharpness")).isDouble()) {
            settings.sharpness = std::clamp(
                static_cast<float>(stored.value(
                    QStringLiteral("vkbasalt_sharpness")
                ).toDouble()),
                ls::vkBasaltStrengthMinimum,
                ls::vkBasaltStrengthMaximum
            );
        }
        if (stored.value(QStringLiteral("vkbasalt_dls_denoise")).isDouble()) {
            settings.dls_denoise = std::clamp(
                static_cast<float>(stored.value(
                    QStringLiteral("vkbasalt_dls_denoise")
                ).toDouble()),
                ls::vkBasaltStrengthMinimum,
                ls::vkBasaltStrengthMaximum
            );
        }
        this->m_vkbasalt_profiles.push_back(std::move(settings));
    }
}

void Backend::writeProfileMetadata() const {
    QSaveFile output(QString::fromStdString(
        this->m_profile_metadata_path.string()
    ));
    if (!output.open(QIODevice::WriteOnly))
        throw std::runtime_error("unable to open Decky profile metadata");
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
        QFileDevice::ReadGroup | QFileDevice::ReadOther);
    const auto content = QJsonDocument(this->m_profile_metadata_root)
        .toJson(QJsonDocument::Indented);
    if (output.write(content) != content.size() || !output.commit())
        throw std::runtime_error("unable to replace Decky profile metadata");
}

void Backend::writeVkBasaltProfiles() const {
    QJsonObject root = this->m_wrapper_settings_root;
    QJsonObject profiles = root.value(QStringLiteral("profiles")).toObject();
    for (size_t index = 0; index < this->m_profiles.size(); ++index) {
        const auto& profile = this->m_profiles.at(index);
        const auto& settings = this->m_vkbasalt_profiles.at(index);
        const auto key = QString::fromStdString(profile.name);
        QJsonObject stored = profiles.value(key).toObject();
        const auto existingLayer = stored.value(
            QStringLiteral("external_vulkan_layer")
        ).toString();
        if (settings.enabled) {
            stored.insert(QStringLiteral("external_vulkan_layer"),
                QStringLiteral("vkbasalt"));
        } else if (existingLayer == QStringLiteral("vkbasalt") ||
                !stored.contains(QStringLiteral("external_vulkan_layer"))) {
            stored.insert(QStringLiteral("external_vulkan_layer"), QString{});
        }
        stored.insert(QStringLiteral("vkbasalt_sharpening"),
            QString::fromStdString(settings.sharpening));
        stored.insert(QStringLiteral("vkbasalt_sharpness"), settings.sharpness);
        stored.insert(QStringLiteral("vkbasalt_dls_denoise"), settings.dls_denoise);
        stored.insert(QStringLiteral("vkbasalt_antialiasing"),
            QString::fromStdString(settings.antialiasing));
        stored.insert(QStringLiteral("vkbasalt_shader"),
            QString::fromStdString(settings.shader));
        if (!stored.contains(QStringLiteral("disable_hdr_exposure")))
            stored.insert(QStringLiteral("disable_hdr_exposure"), true);
        for (const auto& field : {
                "disable_mako", "disable_steamdeck_mode", "enable_zink",
                "force_alsa_audio", "gamescope_wsi_compatibility"}) {
            if (!stored.contains(QString::fromLatin1(field)))
                stored.insert(QString::fromLatin1(field), false);
        }
        profiles.insert(key, stored);
    }
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("profiles"), profiles);

    QSaveFile output(QString::fromStdString(
        this->m_vkbasalt_profile_settings_path.string()
    ));
    if (!output.open(QIODevice::WriteOnly))
        throw std::runtime_error("unable to open Decky profile wrapper settings");
    output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner |
        QFileDevice::ReadGroup | QFileDevice::ReadOther);
    const auto content = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (output.write(content) != content.size() || !output.commit())
        throw std::runtime_error("unable to replace Decky profile wrapper settings");
}

std::filesystem::path Backend::vkBasaltConfigPathForName(
        const std::string& profileName) const {
    if (profileName == "mako")
        return this->m_vkbasalt_global_config_path;
    if (const auto appId = this->m_profile_steam_ids.find(profileName);
            appId != this->m_profile_steam_ids.end() &&
            !appId->second.empty() &&
            std::all_of(appId->second.begin(), appId->second.end(), [](char c) {
                return c >= '0' && c <= '9';
            })) {
        return this->m_vkbasalt_profile_config_directory /
            ("steam-" + appId->second + ".conf");
    }
    const auto digest = QCryptographicHash::hash(
        QByteArray::fromStdString(profileName), QCryptographicHash::Sha256
    ).toHex().left(12).toStdString();
    return this->m_vkbasalt_profile_config_directory /
        ("profile-" + digest + ".conf");
}

std::filesystem::path Backend::vkBasaltConfigPath(
        const size_t profileIndex) const {
    return vkBasaltConfigPathForName(this->m_profiles.at(profileIndex).name);
}

QString Backend::getLaunchOption() const {
    if (!isValidProfileIndex())
        return QStringLiteral("~/.local/bin/mako-launch %command%");
    const auto index = static_cast<size_t>(this->m_profile_index);
    QStringList environment;
    if (this->m_vkbasalt_profiles.at(index).enabled) {
        environment.append(QStringLiteral("ENABLE_VKBASALT=1"));
        environment.append(QStringLiteral("VKBASALT_CONFIG_FILE=") +
            shellQuote(QString::fromStdString(vkBasaltConfigPath(index).string())));
    }
    environment.append(QStringLiteral("MAKO_PROFILE=") + shellQuote(
        QString::fromStdString(this->m_profiles.at(index).name)
    ));
    environment.append(QStringLiteral("~/.local/bin/mako-launch %command%"));
    return environment.join(' ');
}

bool Backend::openVkBasaltConfig() {
    if (!isValidProfileIndex())
        return false;

    if (this->m_vkbasalt_dirty)
        this->savePendingChanges();

    const auto index = static_cast<size_t>(this->m_profile_index);
    const auto path = this->vkBasaltConfigPath(index);
    if (!std::filesystem::is_regular_file(path)) {
        try {
            ls::writeVkBasaltConfiguration(
                path,
                this->m_vkbasalt_profiles.at(index),
                this->m_vkbasalt_shader_directory
            );
        } catch (const std::exception& error) {
            std::cerr << "MAKO Renderer: unable to prepare vkBasalt configuration for editing:\n- "
                << error.what() << "\n";
            return false;
        }
    }

    return QDesktopServices::openUrl(QUrl::fromLocalFile(
        QString::fromStdString(path.string())
    ));
}

void Backend::renameVkBasaltProfile(
        const std::string& oldName, const std::string& newName) {
    auto profiles = this->m_wrapper_settings_root
        .value(QStringLiteral("profiles")).toObject();
    const auto oldKey = QString::fromStdString(oldName);
    const auto newKey = QString::fromStdString(newName);
    if (profiles.contains(oldKey)) {
        profiles.insert(newKey, profiles.take(oldKey));
        this->m_wrapper_settings_root.insert(
            QStringLiteral("profiles"), profiles
        );
    }
    auto metadata = this->m_profile_metadata_root
        .value(QStringLiteral("profiles")).toObject();
    if (metadata.contains(oldKey)) {
        auto entry = metadata.take(oldKey).toObject();
        entry.insert(QStringLiteral("display_name"), newKey);
        metadata.insert(newKey, entry);
        this->m_profile_metadata_root.insert(
            QStringLiteral("profiles"), metadata
        );
        this->m_profile_metadata_dirty = true;
    }
    const auto oldPath = vkBasaltConfigPathForName(oldName);
    if (auto appId = this->m_profile_steam_ids.extract(oldName); !appId.empty()) {
        auto moved = std::move(appId);
        moved.key() = newName;
        this->m_profile_steam_ids.insert(std::move(moved));
    }
    const auto newPath = vkBasaltConfigPathForName(newName);
    if (oldPath != newPath && std::filesystem::is_regular_file(oldPath) &&
            !std::filesystem::exists(newPath)) {
        std::filesystem::create_directories(newPath.parent_path());
        std::filesystem::rename(oldPath, newPath);
    }
    this->m_vkbasalt_dirty = true;
}

void Backend::deleteVkBasaltProfile(const size_t profileIndex) {
    const auto name = this->m_profiles.at(profileIndex).name;
    auto profiles = this->m_wrapper_settings_root
        .value(QStringLiteral("profiles")).toObject();
    profiles.remove(QString::fromStdString(name));
    this->m_wrapper_settings_root.insert(QStringLiteral("profiles"), profiles);
    auto metadata = this->m_profile_metadata_root
        .value(QStringLiteral("profiles")).toObject();
    const auto metadataKey = QString::fromStdString(name);
    if (metadata.contains(metadataKey)) {
        metadata.remove(metadataKey);
        this->m_profile_metadata_root.insert(
            QStringLiteral("profiles"), metadata
        );
        this->m_profile_metadata_dirty = true;
    }
    if (name != "mako") {
        const auto path = vkBasaltConfigPath(profileIndex);
        bool usedByAnotherProfile = false;
        for (size_t index = 0; index < this->m_profiles.size(); ++index) {
            if (index != profileIndex && vkBasaltConfigPath(index) == path) {
                usedByAnotherProfile = true;
                break;
            }
        }
        if (!usedByAnotherProfile) {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    }
    this->m_profile_steam_ids.erase(name);
    this->m_vkbasalt_dirty = true;
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
    if (std::exchange(this->m_vkbasalt_dirty, false)) {
        try {
            for (size_t index = 0; index < this->m_profiles.size(); ++index) {
                if (!this->m_vkbasalt_profiles.at(index).enabled)
                    continue;
                ls::writeVkBasaltConfiguration(
                    this->vkBasaltConfigPath(index),
                    this->m_vkbasalt_profiles.at(index),
                    this->m_vkbasalt_shader_directory
                );
            }
            this->writeVkBasaltProfiles();
        } catch (const std::exception& error) {
            std::cerr << "MAKO Renderer: unable to write vkBasalt configuration:\n- "
                << error.what() << "\n";
        }
    }
    if (std::exchange(this->m_profile_metadata_dirty, false)) {
        try {
            this->writeProfileMetadata();
        } catch (const std::exception& error) {
            std::cerr << "MAKO Renderer: unable to write Decky profile metadata:\n- "
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
