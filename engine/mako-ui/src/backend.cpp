/* SPDX-License-Identifier: GPL-3.0-or-later */

#include <QStringListModel>
#include <QStringList>
#include <QString>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QJsonArray>
#include <QSet>
#include <QProcessEnvironment>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include "backend.hpp"
#include "utils.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/paths.hpp"
#include "mako-common/configuration/config.hpp"
#include "mako-common/configuration/launch.hpp"
#include "mako-common/configuration/vkbasalt.hpp"

#include <cmath>
#include <exception>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace mako;
using namespace mako::ui;

namespace {

// Independently deployed Qt binding of the helper's schema; malformed identities
// must never connect a friendly label to another effect's persisted parameter.
QVariantList shaderMetadata(const QJsonObject& data) {
    const auto invalid = [] { throw std::runtime_error("Invalid or ambiguous shader parameter metadata"); };
    if (!data.value("schema").isDouble() || data.value("schema").toDouble() != 1 || !data.value("effects").isArray()) invalid();
    static const QRegularExpression keyPattern(QStringLiteral("^makoShader_(?:[0-9a-f]{2})+_(?:[0-9a-f]{2})+_(?:0|[1-9][0-9]*)$"));
    const auto finite = [](const QJsonValue& v) { return v.isDouble() && std::isfinite(v.toDouble()); };
    QSet<QString> ids, keys;
    QVariantList result;
    for (const auto& value : data.value("effects").toArray()) {
        if (!value.isObject()) invalid();
        auto effect = value.toObject();
        for (const auto* field : {"id", "name", "path"}) if (!effect.value(field).isString()) invalid();
        const auto id = effect.value("id").toString();
        if (id.isEmpty() || ids.contains(id) || !effect.value("parameters").isArray()) invalid();
        ids.insert(id);
        if (!effect.value("error").isNull() && !effect.value("error").isUndefined() && !effect.value("error").isString()) invalid();
        const auto prefix = QStringLiteral("makoShader_") + QString::fromLatin1(id.toUtf8().toHex()) + '_';
        QJsonArray parameters;
        for (const auto& item : effect.value("parameters").toArray()) {
            if (!item.isObject()) invalid();
            auto p = item.toObject();
            for (const auto* field : {"key", "label", "description", "category", "type"}) if (!p.value(field).isString()) invalid();
            const auto key = p.value("key").toString(), type = p.value("type").toString();
            if (!keyPattern.match(key).hasMatch() || !key.startsWith(prefix) || keys.contains(key) ||
                (type != "float" && type != "int" && type != "uint" && type != "bool")) invalid();
            keys.insert(key);
            for (const auto* field : {"default", "value", "min", "max", "step"}) {
                const auto v = p.value(field);
                if (std::string_view(field) != "default" && std::string_view(field) != "value" && (v.isNull() || v.isUndefined())) { p.insert(field, QJsonValue::Null); continue; }
                if (!finite(v)) invalid();
            }
            for (const auto* field : {"default", "value"}) {
                const auto number = p.value(field).toDouble();
                if ((type == "float" && !std::isfinite(static_cast<float>(number))) ||
                    (type != "float" && number != std::floor(number)) ||
                    (type == "bool" && number != 0 && number != 1) ||
                    (type == "int" && (number < INT32_MIN || number > INT32_MAX)) ||
                    (type == "uint" && (number < 0 || number > UINT32_MAX))) invalid();
            }
            if (!p.value("items").isArray()) invalid();
            for (const auto& label : p.value("items").toArray()) if (!label.isString()) invalid();
            if (!p.value("items").toArray().isEmpty() && type != "int" && type != "uint") invalid();
            if (p.contains("resettable") && !p.value("resettable").isBool()) invalid();
            const auto hint = p.value("presentation").toObject();
            if (type == "bool" && hint.value("widget").toString() == "button") continue;
            const auto count = hint.value("count"), component = hint.value("component");
            const QStringList widgets{QString(), QStringLiteral("input"), QStringLiteral("drag"), QStringLiteral("slider"), QStringLiteral("combo"), QStringLiteral("radio"), QStringLiteral("list"), QStringLiteral("color"), QStringLiteral("button")};
            if (!hint.value("widget").isString() || !widgets.contains(hint.value("widget").toString()) ||
                !hint.value("label").isString() || !keyPattern.match(hint.value("group").toString()).hasMatch() ||
                !finite(count) || count.toDouble() != std::floor(count.toDouble()) || count.toDouble() < 1 || count.toDouble() > 4 ||
                !finite(component) || component.toDouble() != std::floor(component.toDouble()) || component.toDouble() < 0 || component.toDouble() >= count.toDouble()) p.remove("presentation");
            parameters.append(p);
        }
        effect.insert("parameters", parameters);
        if (!effect.contains("error")) effect.insert("error", QJsonValue::Null);
        result.append(effect.toVariantMap());
    }
    return result;
}

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

const QRegularExpression& forbiddenProfileNameCharacters() {
    static const QRegularExpression expression(QStringLiteral("[\\t\\n\\r'\"\\\\/$|&;()<>{}\\[\\]`*?]"));
    return expression;
}

// Profile operations span the shared TOML, sidecars, and shader files. Preserve
// their exact bytes on failure; each replacement uses Qt's atomic file writer.
struct ProfileFileSnapshot {
    QString path;
    std::optional<QByteArray> content;
    QFileDevice::Permissions permissions{};

    explicit ProfileFileSnapshot(const std::filesystem::path& source)
        : path(QString::fromStdString(source.string())) {
        const QFileInfo info(path);
        if (info.isSymLink() || (info.exists() && !info.isFile()))
            throw std::runtime_error("profile state is not a regular file: " + source.string());
        if (!info.exists()) return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            throw std::runtime_error("unable to read profile state: " + source.string());
        content = file.readAll();
        if (file.error() != QFileDevice::NoError)
            throw std::runtime_error("unable to read complete profile state: " + source.string());
        permissions = file.permissions();
    }

    void restore() const {
        const QFileInfo info(path);
        if (info.isSymLink() || (info.exists() && !info.isFile()))
            throw std::runtime_error("unable to restore non-regular profile state");
        if (!content) {
            if (info.exists() && !QFile::remove(path))
                throw std::runtime_error("unable to remove incomplete profile state");
            return;
        }
        QFile current(path);
        if (current.open(QIODevice::ReadOnly) && current.readAll() == *content)
            return;
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly) || !output.setPermissions(permissions) ||
                output.write(*content) != content->size() || !output.commit())
            throw std::runtime_error("unable to restore profile state");
    }
};

}

static QString launcherPathForUiDirectory(const QString& directory) {
    const QString sibling = QDir(directory).absoluteFilePath(
        QStringLiteral("mako-launch")
    );
    const QFileInfo siblingInfo(sibling);
    if (siblingInfo.isFile() && siblingInfo.isExecutable())
        return sibling;

    const QString onPath = QStandardPaths::findExecutable(
        QStringLiteral("mako-launch")
    );
    if (!onPath.isEmpty())
        return onPath;

    return {};
}

QString ui::launcherCommandForUiDirectory(const QString& directory) {
    const auto path = launcherPathForUiDirectory(directory);
    return path.isEmpty() ? QStringLiteral("~/.local/bin/mako-launch") : shellQuote(path);
}

Backend::Backend(std::filesystem::path procRoot, QString remotePlayHelper, QString shaderHelper, QString launcherHelper)
        : m_proc_root(std::move(procRoot)), m_launcher_helper(std::move(launcherHelper)), m_remote_play_helper(std::move(remotePlayHelper)), m_shader_helper(std::move(shaderHelper)) {
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
    this->refreshLosslessScaling();

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
    this->m_vkbasalt_shader_source_directory =
        findBundledVkBasaltShaderDirectory();
    this->m_vkbasalt_shader_directory =
        this->m_vkbasalt_profile_config_directory / "shaders";
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

    // Use the same saved selection and fallback as the native stream launcher.
    if (!this->m_profiles.empty())
        this->m_profile_index = 0;
    auto selection = config.current_profile.value_or("");
    const auto hasSelection = [this](const std::string& name) {
        return std::any_of(m_profiles.begin(), m_profiles.end(), [&name](const auto& profile) {
            return profile.name == name;
        });
    };
    if (!hasSelection(selection)) {
        // This is ordinary published Decky configuration, independent of the override.
        QFile source(QString::fromStdString(path.string()));
        const QRegularExpression comment(QStringLiteral(R"re(^\s*#\s*decky-current-profile\s*=\s*"([^"]+)"\s*$)re"));
        if (source.open(QIODevice::ReadOnly | QIODevice::Text)) {
            while (!source.atEnd()) {
                const auto match = comment.match(QString::fromUtf8(source.readLine()));
                if (match.hasMatch() && hasSelection(match.captured(1).toStdString())) {
                    selection = match.captured(1).toStdString();
                    break;
                }
            }
        }
    }
    if (!hasSelection(selection)) selection = "mako";
    for (size_t index = 0; index < this->m_profiles.size(); ++index)
        if (selection == this->m_profiles[index].name)
            this->m_profile_index = static_cast<int>(index);

    // Save on the QObject's thread so profile edits and serialization cannot race.
    // A single-shot timer has no idle wakeups; shutdown flushes its pending edit.
    this->m_config_path = path;
    this->m_launch_path = launchPath;
    this->m_save_timer.setSingleShot(true);
    this->m_save_timer.setInterval(500);
    connect(&this->m_save_timer, &QTimer::timeout, this, [this] { savePendingChanges(); });
    initializeRemotePlay();
    initializeMangoHud();
    refreshLosslessScaling();
    initializeModelInspection();
}

bool Backend::selectAvailableModels() {
    if (!isValidProfileIndex()) return false;
    updateModelInspectionKeys();
    auto& profile = editingProfile();
    const bool lsfgSelected = profile.frame_generation_model == ls::FrameGenerationModel::LosslessScaling;
    const bool ls1Selected = ls::licensedScalingModelRequested(profile.scaling_method);
    // A missing file can return, as an unmounted SD card does, so the saved choice stays and the Renderer uses MAKO until then.
    if (m_lossless_scaling_missing) {
        if (lsfgSelected || ls1Selected) m_lossless_scaling_fallback = true;
        return false;
    }
    bool changed = false;
    if (lsfgSelected && isLsfgUnavailable()) {
        profile.frame_generation_model = ls::FrameGenerationModel::MakoFg;
        changed = true;
    }
    if (ls1Selected && isLs1Unavailable()) {
        profile.scaling_method = ls::ScalingMethod::Mako;
        changed = true;
    }
    if (changed) m_lossless_scaling_fallback = true;
    return changed;
}

void Backend::refreshLosslessScaling() {
    bool missing = false;
    m_model_dll_path.clear();
    try {
        const auto path = m_global.dll.has_value()
            ? std::filesystem::path(*m_global.dll) : ls::findShaderDll();
        m_model_dll_path = QString::fromStdString(path.string());
        std::error_code error;
        const bool found = std::filesystem::is_regular_file(path, error);
        missing = !found || bool(error);
    } catch (const ls::error&) {
        missing = true;
    } catch (const std::filesystem::filesystem_error&) {
        // Disable only the optional choices when their file cannot be checked.
        missing = true;
    }
    const bool changed = missing != m_lossless_scaling_missing;
    const bool previousFallback = m_lossless_scaling_fallback;
    m_lossless_scaling_missing = missing;
    if (changed) m_lossless_scaling_fallback = false;
    const bool selected = selectAvailableModels();
    if (selected) {
        m_config_dirty = true;
        m_save_timer.start();
    }
    if (changed || selected || previousFallback != m_lossless_scaling_fallback)
        emit refreshUI();
    m_model_inspection_timer.start();
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
        settings.manage_custom_shaders = stored.value(
            QStringLiteral("vkbasalt_manage_custom_shaders")).toBool(false);
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
        try {
            settings.shader = ls::vkBasaltShaderSelection(
                ls::readVkBasaltConfiguration(vkBasaltConfigPathForName(profile.name)), settings);
        } catch (const std::exception& error) {
            std::cerr << "MAKO Renderer: unable to read custom shader selection: " << error.what() << '\n';
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

void Backend::writeVkBasaltProfiles() {
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
        stored.insert(QStringLiteral("vkbasalt_manage_custom_shaders"), settings.manage_custom_shaders);
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
    this->m_wrapper_settings_root = root;
}

std::filesystem::path Backend::vkBasaltConfigPathForName(
        const std::string& profileName, const bool useSteamIdentity) const {
    if (profileName == "mako")
        return this->m_vkbasalt_global_config_path;
    if (const auto appId = this->m_profile_steam_ids.find(profileName);
            useSteamIdentity && appId != this->m_profile_steam_ids.end() &&
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

void Backend::writeSelectedVkBasaltConfig() const {
    std::string content = "effects = none\n";
    if (isValidProfileIndex() && this->m_vkbasalt_profiles.at(static_cast<size_t>(m_profile_index)).enabled) {
        const auto index = static_cast<size_t>(this->m_profile_index);
        QFile input(QString::fromStdString(vkBasaltConfigPath(index).string()));
        if (input.exists()) {
            if (!QFileInfo(input).isFile())
                throw std::runtime_error("selected shader configuration is not a regular file");
            if (!input.open(QIODevice::ReadOnly))
                throw std::runtime_error("unable to read selected shader configuration");
            content = input.readAll().toStdString();
        }
    }
    const auto cachePath = this->m_vkbasalt_profile_config_directory / "current-profile.conf";
    QFile cache(QString::fromStdString(cachePath.string()));
    if (cache.open(QIODevice::ReadOnly) && cache.readAll().toStdString() == content)
        return;
    ls::writeVkBasaltConfiguration(cachePath, content);
}

bool Backend::getEnableMangoHud() const {
    if (!isValidProfileIndex() || getEnableVkBasalt()) return false;
    const auto name = QString::fromStdString(m_profiles.at(static_cast<size_t>(m_profile_index)).name);
    return m_wrapper_settings_root.value(QStringLiteral("profiles")).toObject()
        .value(name).toObject().value(QStringLiteral("external_vulkan_layer")).toString()
        == QStringLiteral("mangohud");
}

void Backend::setExternalVulkanLayer(const QString& layer) {
    if (!isValidProfileIndex()) return;
    const auto index = static_cast<size_t>(m_profile_index);
    m_vkbasalt_profiles.at(index).enabled = layer == QStringLiteral("vkbasalt");
    auto profiles = m_wrapper_settings_root.value(QStringLiteral("profiles")).toObject();
    const auto name = QString::fromStdString(m_profiles.at(index).name);
    auto profile = profiles.value(name).toObject();
    profile.insert(QStringLiteral("external_vulkan_layer"), layer);
    profiles.insert(name, profile);
    m_wrapper_settings_root.insert(QStringLiteral("profiles"), profiles);
    m_vkbasalt_dirty = true;
    m_save_timer.start();
    emit refreshUI();
}

void Backend::enableMangoHudUpdated(bool enabled) {
    if (enabled && !m_mangohud_available) return;
    setExternalVulkanLayer(enabled ? QStringLiteral("mangohud") :
        (getEnableVkBasalt() ? QStringLiteral("vkbasalt") : QString{}));
}

bool Backend::getMangoHudCountRealFrames() const {
    if (!isValidProfileIndex()) return false;
    const auto name = QString::fromStdString(m_profiles.at(static_cast<size_t>(m_profile_index)).name);
    return m_wrapper_settings_root.value(QStringLiteral("profiles")).toObject()
        .value(name).toObject().value(QStringLiteral("mangohud_count_real_frames")).toBool(false);
}

void Backend::mangoHudCountRealFramesUpdated(bool enabled) {
    if (!isValidProfileIndex() || enabled == getMangoHudCountRealFrames()) return;
    auto profiles = m_wrapper_settings_root.value(QStringLiteral("profiles")).toObject();
    const auto name = QString::fromStdString(m_profiles.at(static_cast<size_t>(m_profile_index)).name);
    auto profile = profiles.value(name).toObject();
    profile.insert(QStringLiteral("mangohud_count_real_frames"), enabled);
    profiles.insert(name, profile);
    m_wrapper_settings_root.insert(QStringLiteral("profiles"), profiles);
    m_vkbasalt_dirty = true;
    m_save_timer.start();
    emit refreshUI();
}

void Backend::initializeMangoHud() {
    if (m_launcher_helper.isEmpty()) {
        m_launcher_helper = launcherPathForUiDirectory(QCoreApplication::applicationDirPath());
        if (m_launcher_helper.isEmpty())
            m_launcher_helper = QDir::home().filePath(QStringLiteral(".local/bin/mako-launch"));
    }
    m_mangohud_timeout.setSingleShot(true);
    m_mangohud_timeout.setInterval(5000);
    connect(&m_mangohud_timeout, &QTimer::timeout, this, [this] {
        if (m_mangohud_process.state() != QProcess::NotRunning)
            m_mangohud_process.kill();
    });
    connect(&m_mangohud_process, &QProcess::errorOccurred, this, [this] {
        m_mangohud_available = false;
        m_mangohud_timeout.stop();
        emit refreshUI();
    });
    connect(&m_mangohud_process, &QProcess::finished, this,
        [this](int code, QProcess::ExitStatus status) {
            m_mangohud_timeout.stop();
            const auto result = QJsonDocument::fromJson(m_mangohud_process.readAllStandardOutput());
            const auto available = result.object().value(QStringLiteral("available"));
            m_mangohud_available = code == 0 && status == QProcess::NormalExit &&
                available.isBool() && available.toBool();
            emit refreshUI();
        });
    refreshMangoHud();
}

void Backend::refreshMangoHud() {
    if (mangoHudChecking()) return;
    m_mangohud_available = false;
    m_mangohud_process.start(m_launcher_helper, {QStringLiteral("--check-mangohud")});
    m_mangohud_timeout.start();
    emit refreshUI();
}

QString Backend::getLaunchOption() const {
    const QString launcher = launcherCommandForUiDirectory(
        QCoreApplication::applicationDirPath()
    );
    if (!isValidProfileIndex())
        return launcher + QStringLiteral(" %command%");
    const auto index = static_cast<size_t>(this->m_profile_index);
    QStringList environment;
    if (this->m_vkbasalt_profiles.at(index).enabled) {
        environment.append(QStringLiteral("ENABLE_VKBASALT=1"));
        environment.append(QStringLiteral("VKBASALT_CONFIG_FILE=") +
            shellQuote(QString::fromStdString(vkBasaltConfigPath(index).string())));
    }
    if (getEnableMangoHud()) {
        environment.append(QStringLiteral("MANGOHUD=1"));
        if (getMangoHudCountRealFrames())
            environment.append(QStringLiteral("MAKO_MANGOHUD_REAL_FRAMES=1"));
    }
    environment.append(QStringLiteral("MAKO_PROFILE=") + shellQuote(
        QString::fromStdString(this->m_profiles.at(index).name)
    ));
    environment.append(launcher + QStringLiteral(" %command%"));
    return environment.join(' ');
}

QVariantList Backend::getCustomShaderEffects() const {
    QVariantList result;
    if (!isValidProfileIndex()) return result;
    try {
        const auto content = ls::readVkBasaltConfiguration(vkBasaltConfigPath(
            static_cast<size_t>(this->m_profile_index)));
        for (const auto& shader : ls::customVkBasaltShaders(content)) {
            result.append(QVariantMap{
                {QStringLiteral("id"), QString::fromStdString("custom/" + shader.name)},
                {QStringLiteral("name"), QString::fromStdString(shader.name)},
                {QStringLiteral("path"), QString::fromStdString(shader.path)},
            });
        }
    } catch (const std::exception& error) {
        std::cerr << "MAKO Renderer: unable to read custom shader catalog: " << error.what() << '\n';
    }
    return result;
}

void Backend::loadShaderParameters() {
    // Reopening (possibly for another profile) supersedes the previous discovery.
    if (auto* previous = m_shader_parameter_process) {
        previous->disconnect(this);
        if (previous->state() == QProcess::NotRunning) previous->deleteLater();
        else {
            connect(previous, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), previous, &QObject::deleteLater);
            previous->kill();
        }
        m_shader_parameter_process = nullptr;
    }
    m_shader_parameters_loading = false;
    m_shader_parameter_effects.clear();
    m_shader_parameter_error.clear();
    if (!isValidProfileIndex()) { emit shaderParametersChanged(); return; }
    if (!savePendingChanges()) {
        m_shader_parameter_error = QStringLiteral("Unable to save pending profile settings");
        emit shaderParametersChanged(); return;
    }
    const auto path = vkBasaltConfigPath(static_cast<size_t>(m_profile_index));
    m_shader_parameter_path = path;
    try { m_shader_parameter_content = ls::readVkBasaltConfiguration(path); }
    catch (const std::exception& error) { m_shader_parameter_error = QString::fromUtf8(error.what()); emit shaderParametersChanged(); return; }
    QString program = m_shader_helper.isEmpty() ? QCoreApplication::applicationDirPath() + QStringLiteral("/mako-shader-tool") : m_shader_helper;
    if (m_shader_helper.isEmpty() && !QFileInfo::exists(program)) program = QDir::homePath() + QStringLiteral("/.local/share/mako-render/bin/mako-shader-tool");
    if (!QFileInfo::exists(program)) {
        m_shader_parameter_error = QStringLiteral("Install the updated MAKO Renderer to customize shader parameters.");
        emit shaderParametersChanged(); return;
    }
    auto* process = new QProcess(this);
    m_shader_parameter_process = process;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("VKBASALT_LOG_LEVEL"), QStringLiteral("none"));
    environment.insert(QStringLiteral("VKBASALT_LOG_FILE"), QStringLiteral("stderr"));
    process->setProcessEnvironment(environment);
    m_shader_parameters_loading = true;
    emit shaderParametersChanged();
    auto* timeout = new QTimer(process);
    timeout->setSingleShot(true);
    timeout->setInterval(30000);
    connect(timeout, &QTimer::timeout, process, &QProcess::kill);
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        m_shader_parameter_process = nullptr;
        m_shader_parameters_loading = false;
        m_shader_parameter_error = process->errorString();
        emit shaderParametersChanged();
        process->deleteLater();
    });
    connect(process, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this, [this, process, timeout, path](int code, QProcess::ExitStatus status) {
        timeout->stop();
        m_shader_parameter_process = nullptr;
        m_shader_parameters_loading = false;
        const auto output = process->readAllStandardOutput();
        const auto document = QJsonDocument::fromJson(output);
        if (code != 0 || status != QProcess::NormalExit || output.size() > 4 * 1024 * 1024 || document.object().value(QStringLiteral("schema")).toInt() != 1 || !document.object().value(QStringLiteral("effects")).isArray())
            m_shader_parameter_error = QStringLiteral("Unable to read shader parameters: ") + QString::fromUtf8(process->readAllStandardError());
        else if (!isValidProfileIndex() || vkBasaltConfigPath(static_cast<size_t>(m_profile_index)) != path)
            m_shader_parameter_error = QStringLiteral("Profile changed; reopen the shader editor");
        else {
            try {
                if (ls::readVkBasaltConfiguration(path) != m_shader_parameter_content)
                    throw std::runtime_error("Shader settings changed; reopen the editor");
                m_shader_parameter_effects = shaderMetadata(document.object());
            } catch (const std::exception& error) { m_shader_parameter_error = QString::fromUtf8(error.what()); }
        }
        process->deleteLater();
        emit shaderParametersChanged();
    });
    process->start(program, {QString::fromStdString(path.string())});
    timeout->start();
}

bool Backend::setShaderParameters(const QVariantMap& changes) {
    try {
        if (!isValidProfileIndex() || m_shader_parameters_loading || changes.isEmpty() || changes.size() > 1024 ||
            vkBasaltConfigPath(static_cast<size_t>(m_profile_index)) != m_shader_parameter_path)
            throw std::runtime_error("Profile changed; reopen the shader editor");
        const auto original = ls::readVkBasaltConfiguration(m_shader_parameter_path);
        if (original != m_shader_parameter_content) throw std::runtime_error("Shader settings changed; reopen the editor");
        std::map<std::string, std::string> values;
        QVariantMap allowed;
        for (const auto& effect : m_shader_parameter_effects)
            for (const auto& value : effect.toMap().value(QStringLiteral("parameters")).toList()) {
                const auto parameter = value.toMap();
                allowed.insert(parameter.value(QStringLiteral("key")).toString(), parameter);
            }
        for (auto it = changes.cbegin(); it != changes.cend(); ++it) {
            if (!allowed.contains(it.key())) throw std::runtime_error("Unknown shader parameter");
            const auto p = allowed.value(it.key()).toMap();
            bool ok = false;
            const bool resetting = it.value().isNull();
            const auto inputType = it.value().userType();
            if (!resetting && inputType != QMetaType::Double && inputType != QMetaType::Float && inputType != QMetaType::Int && inputType != QMetaType::UInt && inputType != QMetaType::LongLong && inputType != QMetaType::ULongLong)
                throw std::runtime_error("Shader values must be numbers");
            const auto value = (resetting ? p.value(QStringLiteral("default")) : it.value()).toDouble(&ok);
            const auto type = p.value(QStringLiteral("type")).toString();
            const auto minimum = p.value(QStringLiteral("min")), maximum = p.value(QStringLiteral("max"));
            const auto items = p.value(QStringLiteral("items")).toStringList();
            if (resetting && p.contains("resettable") && !p.value("resettable").toBool()) throw std::runtime_error("This shader parameter does not allow reset");
            const auto comparison = type == "float" ? static_cast<double>(static_cast<float>(value)) : value;
            const auto bound = [&](const QVariant& v) { return type == "float" ? static_cast<double>(static_cast<float>(v.toDouble())) : v.toDouble(); };
            if (!ok || !std::isfinite(value) || !std::isfinite(comparison) ||
                (type != QStringLiteral("float") && value != std::floor(value)) ||
                (type == QStringLiteral("bool") && value != 0 && value != 1) ||
                (type == QStringLiteral("int") && (value < INT32_MIN || value > INT32_MAX)) ||
                (type == QStringLiteral("uint") && (value < 0 || value > UINT32_MAX)) ||
                (!resetting && ((!minimum.isNull() && comparison < bound(minimum)) || (!maximum.isNull() && comparison > bound(maximum)) ||
                (!items.empty() && (value < 0 || value >= items.size())))))
                throw std::runtime_error("Shader value is outside its declared range");
            const auto serialized = resetting ? QStringLiteral("default") : type == QStringLiteral("float")
                ? QString::number(value, 'g', 9)
                : QString::number(static_cast<qlonglong>(value));
            values.emplace(it.key().toStdString(), serialized.toStdString());
        }
        const auto content = ls::updateVkBasaltShaderParameters(original, values);
        ls::writeVkBasaltConfiguration(m_shader_parameter_path, content);
        try { writeSelectedVkBasaltConfig(); }
        catch (...) { ls::writeVkBasaltConfiguration(m_shader_parameter_path, original); writeSelectedVkBasaltConfig(); throw; }
        m_shader_parameter_content = content;
        m_shader_parameter_error.clear();
        emit shaderParametersChanged();
        return true;
    } catch (const std::exception& error) {
        m_shader_parameter_error = QString::fromUtf8(error.what());
        emit shaderParametersChanged();
        return false;
    }
}

bool Backend::addCustomShader(const QString& value) {
    if (!isValidProfileIndex()) return false;
    try {
        this->savePendingChanges();
        const QUrl url(value);
        const auto path = url.isLocalFile() ? url.toLocalFile() : value;
        static_cast<void>(ls::addVkBasaltCustomShader(vkBasaltConfigPath(
            static_cast<size_t>(this->m_profile_index)), path.toStdString()));
        this->writeSelectedVkBasaltConfig();
        this->m_shader_load_error.clear();
        emit this->refreshUI();
        return true;
    } catch (const std::exception& error) {
        this->m_shader_load_error = QString::fromUtf8(error.what());
        emit this->refreshUI();
        return false;
    }
}

void Backend::refreshCustomShaders() {
    if (!isValidProfileIndex()) return;
    try {
        if (!this->savePendingChanges())
            throw std::runtime_error("unable to save pending settings; shaders were not reloaded");
        const auto path = vkBasaltConfigPath(static_cast<size_t>(this->m_profile_index));
        const auto original = ls::readVkBasaltConfiguration(path);
        const auto content = ls::reloadVkBasaltConfiguration(original);
        auto& settings = this->m_vkbasalt_profiles.at(static_cast<size_t>(this->m_profile_index));
        const auto previous = settings;
        ls::writeVkBasaltConfiguration(path, content);
        try {
            this->writeSelectedVkBasaltConfig();
            settings.shader = ls::vkBasaltShaderSelection(content, settings);
            this->m_vkbasalt_dirty = true;
            if (!this->savePendingChanges())
                throw std::runtime_error("unable to persist reloaded shader settings");
        } catch (...) {
            settings = previous;
            try {
                ls::writeVkBasaltConfiguration(path, original);
                this->writeSelectedVkBasaltConfig();
            } catch (const std::exception& error) {
                std::cerr << "MAKO Renderer: unable to restore shader reload: " << error.what() << '\n';
            }
            throw;
        }
        this->m_shader_load_error.clear();
    } catch (const std::exception& error) {
        this->m_shader_load_error = QString::fromUtf8(error.what());
    }
    emit this->refreshUI();
}

bool Backend::deleteSelectedCustomShaders() {
    if (!isValidProfileIndex()) return false;
    try {
        if (!this->savePendingChanges())
            throw std::runtime_error("unable to save pending settings; custom shaders were not deleted");
        const auto index = static_cast<size_t>(this->m_profile_index);
        const auto path = this->vkBasaltConfigPath(index);
        const auto original = ls::readVkBasaltConfiguration(path);
        const auto previous = this->m_vkbasalt_profiles.at(index);
        auto updated = previous;
        const auto selected = QString::fromStdString(ls::vkBasaltShaderSelection(original, previous)).split(':');
        std::vector<std::string> deleted;
        QStringList remaining;
        for (const auto& id : selected) {
            if (id.startsWith(QStringLiteral("custom/"))) deleted.push_back(id.toStdString());
            else remaining.append(id);
        }
        if (!deleted.empty()) {
            updated.shader = remaining.isEmpty() ? "none" : remaining.join(':').toStdString();
            updated.manage_custom_shaders = true;
            const auto content = ls::mergeVkBasaltConfiguration(
                ls::removeVkBasaltCustomShaders(original, deleted), updated,
                this->m_vkbasalt_shader_directory, selected.join(':').toStdString());
            ls::writeVkBasaltConfiguration(path, content);
            this->m_vkbasalt_profiles.at(index) = updated;
            try {
                this->writeSelectedVkBasaltConfig();
                this->writeVkBasaltProfiles();
            } catch (const std::exception& error) {
                this->m_vkbasalt_profiles.at(index) = previous;
                try {
                    ls::writeVkBasaltConfiguration(path, original);
                    this->writeSelectedVkBasaltConfig();
                } catch (const std::exception& restoreError) {
                    throw std::runtime_error(std::string(error.what()) +
                        "; unable to restore shader configuration: " + restoreError.what());
                }
                throw;
            }
        }
        this->m_shader_load_error.clear();
        emit this->refreshUI();
        return true;
    } catch (const std::exception& error) {
        this->m_shader_load_error = QString::fromUtf8(error.what());
        emit this->refreshUI();
        return false;
    }
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
            this->writeVkBasaltShaderAssets();
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

void Backend::writeVkBasaltShaderAssets() const {
    const auto& sourceDirectory = this->m_vkbasalt_shader_source_directory;
    const auto& destinationDirectory = this->m_vkbasalt_shader_directory;
    if (!std::filesystem::is_directory(sourceDirectory))
        throw std::runtime_error(
            "the bundled vkBasalt shader directory is unavailable: " +
            sourceDirectory.string()
        );

    std::filesystem::create_directories(destinationDirectory);
    if (std::filesystem::equivalent(sourceDirectory, destinationDirectory))
        return;

    for (const auto& entry : std::filesystem::directory_iterator(sourceDirectory)) {
        if (!entry.is_regular_file())
            continue;

        QFile source(QString::fromStdString(entry.path().string()));
        if (!source.open(QIODevice::ReadOnly))
            throw std::runtime_error(
                "unable to read bundled vkBasalt shader asset: " +
                entry.path().string()
            );
        const QByteArray contents = source.readAll();
        const auto destinationPath = destinationDirectory / entry.path().filename();
        QFile existing(QString::fromStdString(destinationPath.string()));
        if (existing.open(QIODevice::ReadOnly) && existing.readAll() == contents)
            continue;

        QSaveFile destination(QString::fromStdString(destinationPath.string()));
        if (!destination.open(QIODevice::WriteOnly) ||
                destination.write(contents) != contents.size() ||
                !destination.commit()) {
            throw std::runtime_error(
                "unable to install vkBasalt shader asset: " +
                destinationPath.string()
            );
        }
    }
}

void Backend::renameVkBasaltProfile(
        const std::string& oldName, const std::string& newName) {
    auto profiles = this->m_wrapper_settings_root
        .value(QStringLiteral("profiles")).toObject();
    const auto oldKey = QString::fromStdString(oldName);
    const auto newKey = QString::fromStdString(newName);
    profiles.insert(newKey, profiles.take(oldKey).toObject());
    this->m_wrapper_settings_root.insert(QStringLiteral("profiles"), profiles);
    auto metadata = this->m_profile_metadata_root
        .value(QStringLiteral("profiles")).toObject();
    auto entry = metadata.take(oldKey).toObject();
    entry.insert(QStringLiteral("display_name"), newKey);
    if (!entry.contains(QStringLiteral("kind"))) entry.insert(QStringLiteral("kind"), QStringLiteral("process"));
    if (!entry.contains(QStringLiteral("steam_app_id"))) entry.insert(QStringLiteral("steam_app_id"), QJsonValue::Null);
    if (!entry.contains(QStringLiteral("captured_processes"))) entry.insert(QStringLiteral("captured_processes"), QJsonArray{});
    metadata.insert(newKey, entry);
    this->m_profile_metadata_root.insert(QStringLiteral("profiles"), metadata);
    this->m_profile_metadata_dirty = true;
    const auto oldPath = vkBasaltConfigPathForName(oldName);
    this->m_profile_steam_ids.erase(newName);
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
            if (error)
                throw std::system_error(error, "unable to delete profile shader configuration");
        }
    }
    this->m_profile_steam_ids.erase(name);
    this->m_vkbasalt_dirty = true;
}

bool Backend::validateProfileName(const QString& name, const bool renaming) {
    // Native profile identities may contain spaces; retain those existing names.
    // New identities otherwise obey the same reserved-name/character rules as Decky.
    if (name.isEmpty() || forbiddenProfileNameCharacters().match(name).hasMatch() ||
            name.compare(QStringLiteral("global"), Qt::CaseInsensitive) == 0 ||
            name.compare(QStringLiteral("profile"), Qt::CaseInsensitive) == 0) {
        m_profile_operation_error = QStringLiteral("invalid_name");
    } else {
        for (size_t index = 0; index < m_profiles.size(); ++index) {
            if (renaming && static_cast<int>(index) == m_profile_index) continue;
            if (m_profiles[index].name == name.toStdString()) {
                m_profile_operation_error = QStringLiteral("duplicate_name");
                emit refreshUI();
                return false;
            }
        }
        m_profile_operation_error.clear();
        return true;
    }
    emit refreshUI();
    return false;
}

void Backend::rebuildProfileModels() {
    QStringList names;
    for (auto* model : m_active_in_list_models) delete model;
    m_active_in_list_models.clear();
    for (const auto& profile : m_profiles) {
        names.append(QString::fromStdString(profile.name));
        QStringList matches;
        for (const auto& match : profile.active_in) matches.append(QString::fromStdString(match));
        m_active_in_list_models.push_back(new QStringListModel(matches, this));
    }
    m_profile_list_model->setStringList(names);
    m_active_in_index = -1;
}

bool Backend::applyProfileChange(const std::function<void()>& change,
        const std::vector<std::filesystem::path>& additionalPaths, const bool rebuildModels) {
    if (!savePendingChanges()) {
        m_profile_operation_error = QStringLiteral("save_failed");
        emit refreshUI();
        return false;
    }
    const auto profiles = m_profiles;
    const auto shaders = m_vkbasalt_profiles;
    const auto wrapper = m_wrapper_settings_root;
    const auto metadata = m_profile_metadata_root;
    const auto steamIds = m_profile_steam_ids;
    const auto selected = m_profile_index;
    const auto power = m_power_mode;
    const auto activeIn = m_active_in_index;
    const auto selectedName = isValidProfileIndex() ? m_profiles.at(static_cast<size_t>(selected)).name : std::string{};
    const bool fallback = m_lossless_scaling_fallback;
    std::vector<ProfileFileSnapshot> files;
    bool changed = false;
    try {
        std::vector<std::filesystem::path> paths{
            m_config_path, m_vkbasalt_profile_config_directory / "current-profile.conf"};
        if (rebuildModels) {
            paths.push_back(m_vkbasalt_profile_settings_path);
            paths.push_back(m_profile_metadata_path);
            for (size_t index = 0; index < m_profiles.size(); ++index)
                paths.push_back(vkBasaltConfigPath(index));
        }
        paths.insert(paths.end(), additionalPaths.begin(), additionalPaths.end());
        std::sort(paths.begin(), paths.end());
        paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
        for (const auto& path : paths) files.emplace_back(path);
        changed = true;
        change();
        if (!isValidProfileIndex() || selectedName != m_profiles.at(static_cast<size_t>(m_profile_index)).name || power != m_power_mode)
            m_lossless_scaling_fallback = false;
        selectAvailableModels();
        m_config_dirty = true;
        if (!savePendingChanges(false))
            throw std::runtime_error("unable to persist profile operation");
        if (rebuildModels) rebuildProfileModels();
        m_shader_load_error.clear();
        m_profile_operation_error.clear();
        emit refreshUI();
        return true;
    } catch (const std::exception& error) {
        m_profiles = profiles;
        m_vkbasalt_profiles = shaders;
        m_wrapper_settings_root = wrapper;
        m_profile_metadata_root = metadata;
        m_profile_steam_ids = steamIds;
        m_profile_index = selected;
        m_power_mode = power;
        m_active_in_index = activeIn;
        m_lossless_scaling_fallback = fallback;
        m_config_dirty = m_vkbasalt_dirty = m_profile_metadata_dirty = false;
        std::cerr << "MAKO Renderer: profile operation failed: " << error.what() << '\n';
        for (auto iterator = files.rbegin(); changed && iterator != files.rend(); ++iterator) {
            try { iterator->restore(); }
            catch (const std::exception& restoreError) {
                std::cerr << "MAKO Renderer: profile rollback failed for "
                    << iterator->path.toStdString() << ": " << restoreError.what() << '\n';
            }
        }
        m_profile_operation_error = QStringLiteral("save_failed");
        emit refreshUI();
        return false;
    }
}

void Backend::profileSelected(const int index) {
    if (index < 0 || std::cmp_greater_equal(index, m_profiles.size())) return;
    applyProfileChange([this, index] {
        m_profile_index = index;
        m_power_mode = 0;
        m_active_in_index = -1;
    }, {}, false);
}

bool Backend::createProfile(const QString& input) {
    const auto name = input.trimmed();
    if (!validateProfileName(name)) return false;
    const auto destination = vkBasaltConfigPathForName(name.toStdString(), false);
    return applyProfileChange([this, name, destination] {
        if (std::filesystem::exists(destination))
            throw std::runtime_error("new profile shader configuration already exists");
        ls::GameConf profile;
        ls::VkBasaltConf shaders;
        auto wrapperProfiles = m_wrapper_settings_root.value(QStringLiteral("profiles")).toObject();
        if (isValidProfileIndex()) {
            const auto source = static_cast<size_t>(m_profile_index);
            profile = m_profiles.at(source);
            shaders = m_vkbasalt_profiles.at(source);
            wrapperProfiles.insert(name, wrapperProfiles.value(QString::fromStdString(profile.name)).toObject());
            const auto content = ls::readVkBasaltConfiguration(vkBasaltConfigPath(source));
            if (!content.empty()) ls::writeVkBasaltConfiguration(destination, content);
        }
        profile.name = name.toStdString();
        for (auto& powerProfile : profile.power_profiles) powerProfile.name = profile.name;
        m_profiles.push_back(std::move(profile));
        m_vkbasalt_profiles.push_back(std::move(shaders));
        m_wrapper_settings_root.insert(QStringLiteral("profiles"), wrapperProfiles);
        auto metadata = m_profile_metadata_root.value(QStringLiteral("profiles")).toObject();
        metadata.insert(name, QJsonObject{{QStringLiteral("display_name"), name == QStringLiteral("mako") ? QStringLiteral("Default") : name},
            {QStringLiteral("kind"), name == QStringLiteral("mako") ? QStringLiteral("default") : QStringLiteral("process")},
            {QStringLiteral("steam_app_id"), QJsonValue::Null},
            {QStringLiteral("captured_processes"), QJsonArray{}}});
        m_profile_metadata_root.insert(QStringLiteral("profiles"), metadata);
        m_profile_steam_ids.erase(name.toStdString());
        m_profile_index = static_cast<int>(m_profiles.size() - 1);
        m_power_mode = 0;
        m_vkbasalt_dirty = m_profile_metadata_dirty = true;
    }, {destination});
}

bool Backend::renameProfile(const QString& input) {
    if (!canManageProfile()) {
        m_profile_operation_error = QStringLiteral("default_protected");
        emit refreshUI();
        return false;
    }
    const auto name = input.trimmed();
    const auto& current = m_profiles.at(static_cast<size_t>(m_profile_index)).name;
    if (current == name.toStdString()) { clearProfileOperationError(); return true; }
    if (!validateProfileName(name, true)) return false;
    const auto oldPath = vkBasaltConfigPathForName(current);
    const auto newPath = oldPath != vkBasaltConfigPathForName(current, false)
        ? oldPath : vkBasaltConfigPathForName(name.toStdString(), false);
    return applyProfileChange([this, name, oldPath, newPath] {
        if (oldPath != newPath && std::filesystem::exists(newPath))
            throw std::runtime_error("renamed profile shader configuration already exists");
        auto& profile = m_profiles.at(static_cast<size_t>(m_profile_index));
        renameVkBasaltProfile(profile.name, name.toStdString());
        profile.name = name.toStdString();
        for (auto& powerProfile : profile.power_profiles) powerProfile.name = profile.name;
    }, {newPath});
}

bool Backend::deleteProfile() {
    if (!canManageProfile()) {
        m_profile_operation_error = QStringLiteral("default_protected");
        emit refreshUI();
        return false;
    }
    return applyProfileChange([this] {
        deleteVkBasaltProfile(static_cast<size_t>(m_profile_index));
        m_profiles.erase(m_profiles.begin() + m_profile_index);
        m_vkbasalt_profiles.erase(m_vkbasalt_profiles.begin() + m_profile_index);
        m_profile_index = m_profiles.empty() ? -1 : 0;
        for (size_t index = 0; index < m_profiles.size(); ++index)
            if (m_profiles[index].name == "mako") m_profile_index = static_cast<int>(index);
        m_power_mode = 0;
    });
}

Backend::~Backend() {
    m_model_inspection_timer.stop();
    if (m_model_inspection_process) {
        m_model_inspection_process->disconnect(this);
        m_model_inspection_process->kill();
        m_model_inspection_process->waitForFinished(1000);
    }
    m_mangohud_timeout.stop();
    if (m_mangohud_process.state() != QProcess::NotRunning) {
        m_mangohud_process.disconnect(this);
        m_mangohud_process.kill();
        m_mangohud_process.waitForFinished(1000);
    }
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
                return m_profile_operation_error.isEmpty();
            }
        }
    }
    if (create) {
        QString name = game.name;
        int suffix = 2;
        const auto names = m_profile_list_model->stringList();
        name.replace(forbiddenProfileNameCharacters(), QStringLiteral("-"));
        if (name.isEmpty() || name == QStringLiteral("mako") ||
                name.compare(QStringLiteral("global"), Qt::CaseInsensitive) == 0 ||
                name.compare(QStringLiteral("profile"), Qt::CaseInsensitive) == 0)
            name = QStringLiteral("game-%1").arg(game.pid);
        const auto base = name;
        while (names.contains(name)) name = base + QStringLiteral("-%1").arg(suffix++);
        if (!createProfile(name)) return false;
    }
    if (!isValidProfileIndex())
        return false;
    addActiveIn(game.name);
    return true;
}

bool Backend::savePendingChanges(const bool writeShaderFiles) {
    this->m_save_timer.stop();
    const bool selectionChanged = this->m_config_dirty || this->m_vkbasalt_dirty;
    bool saved = true;
    if (std::exchange(this->m_config_dirty, false)) {
        try {
            ls::ConfigFile config{};
            config.global() = this->m_global;
            config.profiles() = this->m_profiles;
            if (isValidProfileIndex())
                config.current_profile = this->m_profiles.at(static_cast<size_t>(this->m_profile_index)).name;
            config.write(this->m_config_path);
        } catch (const std::exception& error) {
            saved = false;
            this->m_config_dirty = true;
            std::cerr << "MAKO Renderer: unable to write configuration:\n- "
                << error.what() << "\n";
        }
    }
    if (std::exchange(this->m_vkbasalt_dirty, false)) {
        try {
            if (writeShaderFiles) this->writeVkBasaltShaderAssets();
            for (size_t index = 0; writeShaderFiles && index < this->m_profiles.size(); ++index) {
                const auto& settings = this->m_vkbasalt_profiles.at(index);
                if (!settings.enabled && !(settings.manage_custom_shaders &&
                        std::filesystem::is_regular_file(this->vkBasaltConfigPath(index))))
                    continue;
                const auto previous = this->m_wrapper_settings_root.value(QStringLiteral("profiles")).toObject()
                    .value(QString::fromStdString(this->m_profiles.at(index).name)).toObject()
                    .value(QStringLiteral("vkbasalt_shader")).toString(QStringLiteral("none")).toStdString();
                ls::writeVkBasaltConfiguration(
                    this->vkBasaltConfigPath(index),
                    this->m_vkbasalt_profiles.at(index),
                    this->m_vkbasalt_shader_directory,
                    previous
                );
            }
            this->writeVkBasaltProfiles();
        } catch (const std::exception& error) {
            saved = false;
            this->m_vkbasalt_dirty = true;
            std::cerr << "MAKO Renderer: unable to write vkBasalt configuration:\n- "
                << error.what() << "\n";
        }
    }
    if (std::exchange(this->m_profile_metadata_dirty, false)) {
        try {
            this->writeProfileMetadata();
        } catch (const std::exception& error) {
            saved = false;
            this->m_profile_metadata_dirty = true;
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
            saved = false;
            this->m_launch_dirty = true;
            std::cerr << "MAKO Renderer: unable to write standalone launcher configuration:\n- "
                << error.what() << "\n";
        }
    }
    if (saved && selectionChanged) {
        try {
            this->writeSelectedVkBasaltConfig();
        } catch (const std::exception& error) {
            saved = false;
            this->m_vkbasalt_dirty = true;
            std::cerr << "MAKO Renderer: unable to write selected shader configuration:\n- "
                << error.what() << "\n";
        }
    }
    return saved;
}
