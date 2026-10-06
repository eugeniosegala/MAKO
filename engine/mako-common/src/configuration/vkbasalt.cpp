/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/configuration/vkbasalt.hpp"
#include "atomic_write.hpp"
#include "mako-common/helpers/errors.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

constexpr std::array<std::string_view, 3> SHARPENING{
    "none", "cas", "dls"
};
constexpr std::array<std::string_view, 3> ANTIALIASING{
    "none", "fxaa", "smaa"
};
constexpr std::array<std::string_view, 20> SHADERS{
    "none", "hdr_look", "clarity", "levels_plus", "vibrance", "colourfulness", "curves", "deband",
    "technicolor2", "dpx", "bleach_bypass", "noir", "technicolor",
    "monochrome", "sepia", "film_grain", "vignette", "cartoon",
    "nostalgia", "chromatic_aberration"
};

const std::map<std::string_view, std::string_view> SHADER_EFFECTS{
    {"vibrance", "makoVibrance"},
    {"curves", "makoCurves"},
    {"deband", "makoDeband"},
    {"technicolor", "makoTechnicolor"},
    {"sepia", "makoSepia"},
    {"monochrome", "makoMonochrome"},
    {"vignette", "makoVignette"},
    {"hdr_look", "makoHDRLook"},
    {"colourfulness", "makoColourfulness"},
    {"technicolor2", "makoTechnicolor2"},
    {"dpx", "makoDPX"},
    {"bleach_bypass", "makoBleachBypass"},
    {"noir", "makoNoir"},
    {"film_grain", "makoFilmGrain"},
    {"cartoon", "makoCartoon"},
    {"nostalgia", "makoNostalgia"},
    {"chromatic_aberration", "makoChromaticAberration"},
    {"clarity", "makoClarity"},
    {"levels_plus", "makoLevelsPlus"},
};

const std::map<std::string_view, std::string_view> SHADER_FILES{
    {"makoVibrance", "Vibrance.fx"},
    {"makoCurves", "Curves.fx"},
    {"makoTechnicolor", "Technicolor.fx"},
    {"makoSepia", "Sepia.fx"},
    {"makoMonochrome", "Monochrome.fx"},
    {"makoVignette", "Vignette.fx"},
    {"makoHDRLook", "FakeHDR.fx"},
    {"makoColourfulness", "Colourfulness.fx"},
    {"makoTechnicolor2", "Technicolor2.fx"},
    {"makoDPX", "DPX.fx"},
    {"makoBleachBypass", "BleachBypass.fx"},
    {"makoNoir", "Noir.fx"},
    {"makoFilmGrain", "FilmGrain.fx"},
    {"makoCartoon", "Cartoon.fx"},
    {"makoNostalgia", "Nostalgia.fx"},
    {"makoChromaticAberration", "ChromaticAberration.fx"},
    {"makoClarity", "Clarity.fx"},
    {"makoLevelsPlus", "LevelsPlus.fx"},
};

template<typename Values>
bool contains(const Values& values, const std::string_view value) noexcept {
    return std::find(values.begin(), values.end(), value) != values.end();
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string trim(const std::string_view value) {
    constexpr std::string_view whitespace{" \t\r\n"};
    const auto first = value.find_first_not_of(whitespace);
    if (first == std::string_view::npos)
        return {};
    const auto last = value.find_last_not_of(whitespace);
    return std::string(value.substr(first, last - first + 1));
}

size_t commentPosition(const std::string_view value) {
    bool quoted = false;
    bool escaped = false;
    for (size_t index = 0; index < value.size(); ++index) {
        const char c = value[index];
        if (c == '"' && !escaped) quoted = !quoted;
        if (c == '#' && !quoted && index > 0 &&
                std::isspace(static_cast<unsigned char>(value[index - 1]))) {
            while (index > 0 && std::isspace(static_cast<unsigned char>(value[index - 1]))) --index;
            return index;
        }
        escaped = c == '\\' && !escaped;
    }
    return std::string_view::npos;
}

std::string configValue(std::string value) {
    if (const auto position = commentPosition(value); position != std::string_view::npos)
        value.resize(position);
    value = trim(value);
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
        const auto inner = value.substr(1, value.size() - 2);
        value.clear();
        for (size_t index = 0; index < inner.size(); ++index) {
            if (inner[index] == '\\' && index + 1 < inner.size() &&
                    (inner[index + 1] == '\\' || inner[index + 1] == '"')) ++index;
            value += inner[index];
        }
    }
    return value;
}

std::vector<std::string> splitEffects(const std::string_view value) {
    std::vector<std::string> effects;
    size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find(':', start);
        auto effect = trim(value.substr(start,
            end == std::string_view::npos ? value.size() - start : end - start));
        if (!effect.empty())
            effects.push_back(std::move(effect));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return effects;
}

std::string joinEffects(const std::vector<std::string>& effects) {
    std::ostringstream output;
    for (size_t index = 0; index < effects.size(); ++index) {
        if (index != 0)
            output << ':';
        output << effects.at(index);
    }
    return output.str();
}

bool customShaderId(const std::string_view value) {
    constexpr std::string_view prefix = "custom/";
    if (!value.starts_with(prefix)) return false;
    const auto name = value.substr(prefix.size());
    return !name.empty() && name.size() <= 128 &&
        ((name.front() >= 'A' && name.front() <= 'Z') ||
         (name.front() >= 'a' && name.front() <= 'z')) &&
        std::all_of(name.begin(), name.end(), [](const char c) {
            return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') || c == '_';
        });
}

std::vector<std::string> selectedEffects(const ls::VkBasaltConf& settings) {
    std::vector<std::string> effects;
    if (settings.antialiasing != "none")
        effects.push_back(settings.antialiasing);
    if (settings.shader != "none") {
        for (const auto& shader : splitEffects(settings.shader)) {
            if (customShaderId(shader)) {
                if (settings.manage_custom_shaders)
                    effects.push_back(shader.substr(7));
            } else {
                effects.emplace_back(SHADER_EFFECTS.at(shader));
            }
        }
    }
    if (settings.sharpening != "none")
        effects.push_back(settings.sharpening);
    return effects;
}

std::string mergeEffects(const std::string_view existing,
        const std::vector<std::string>& selected,
        const std::vector<ls::VkBasaltCustomShader>& custom,
        const ls::VkBasaltConf& settings, const std::string_view previousSelection) {
    std::unordered_set<std::string> controlled{
        "cas", "dls", "fxaa", "smaa"
    };
    for (const auto& [unused, effect] : SHADER_EFFECTS) {
        static_cast<void>(unused);
        controlled.insert(lowercase(std::string(effect)));
    }
    std::unordered_set<std::string> controlledCustom;
    if (settings.manage_custom_shaders) {
        for (const auto& shader : custom)
            controlledCustom.insert(shader.name);
        for (const auto& shader : splitEffects(std::string(previousSelection) + ":" + settings.shader))
            if (customShaderId(shader))
                controlledCustom.insert(shader.substr(7));
    }

    std::vector<std::string> merged;
    bool inserted = false;
    for (auto effect : splitEffects(existing)) {
        if (controlled.contains(lowercase(effect)) || controlledCustom.contains(effect)) {
            if (!inserted) {
                merged.insert(merged.end(), selected.begin(), selected.end());
                inserted = true;
            }
            continue;
        }
        merged.push_back(std::move(effect));
    }
    if (!inserted)
        merged.insert(merged.end(), selected.begin(), selected.end());
    return joinEffects(merged);
}

std::string quotedPath(const std::filesystem::path& path) {
    std::string value = path.string();
    size_t position = 0;
    while ((position = value.find_first_of("\\\"", position)) != std::string::npos) {
        value.insert(position, 1, '\\');
        position += 2;
    }
    return '"' + value + '"';
}

}

bool ls::isVkBasaltSharpening(const std::string_view value) noexcept {
    return contains(SHARPENING, value);
}

bool ls::isVkBasaltAntialiasing(const std::string_view value) noexcept {
    return contains(ANTIALIASING, value);
}

bool ls::isVkBasaltShader(const std::string_view value) {
    if (value == "none")
        return true;
    if (value.empty())
        return false;
    std::unordered_set<std::string_view> seen;
    size_t start = 0;
    while (start < value.size()) {
        const auto end = value.find(':', start);
        const auto shader = value.substr(start,
            end == std::string_view::npos ? value.size() - start : end - start);
        const auto found = std::find(SHADERS.begin(), SHADERS.end(), shader);
        if (shader == "none" || (found == SHADERS.end() && !customShaderId(shader)))
            return false;
        if (!seen.insert(shader).second)
            return false;
        if (end == std::string_view::npos)
            return true;
        start = end + 1;
    }
    return false;
}

std::vector<ls::VkBasaltCustomShader> ls::customVkBasaltShaders(const std::string_view content) {
    static const std::regex assignment(R"(^\s*([A-Za-z][A-Za-z0-9_]*)\s*=\s*(.*)$)");
    std::map<std::string, std::string> entries;
    std::istringstream input{std::string(content)};
    for (std::string line; std::getline(input, line);) {
        std::smatch match;
        if (!std::regex_match(line, match, assignment)) continue;
        const auto name = match[1].str();
        entries.erase(name);
        auto value = configValue(match[2].str());
        constexpr std::array<std::string_view, 9> reservedKeys{
            "none", "effects", "reshadeincludepath", "reshadetexturepath",
            "enableonlaunch", "togglekey", "cassharpness", "dlssharpness", "dlsdenoise"
        };
        bool reserved = contains(reservedKeys, lowercase(name)) || contains(SHARPENING, lowercase(name)) ||
            contains(ANTIALIASING, lowercase(name));
        for (const auto& [unused, effect] : SHADER_EFFECTS) {
            static_cast<void>(unused);
            reserved |= lowercase(name) == lowercase(std::string(effect));
        }
        if (!reserved && customShaderId("custom/" + name) &&
                lowercase(value).ends_with(".fx"))
            entries[name] = value;
    }
    std::vector<VkBasaltCustomShader> result;
    result.reserve(entries.size());
    for (const auto& [name, path] : entries) result.push_back({name, path});
    return result;
}

std::string ls::vkBasaltShaderSelection(const std::string_view content, const VkBasaltConf& settings) {
    if (settings.manage_custom_shaders) return settings.shader;
    const auto custom = customVkBasaltShaders(content);
    std::string effects;
    std::istringstream input{std::string(content)};
    static const std::regex assignment(R"(^\s*effects\s*=\s*(.*)$)");
    for (std::string line; std::getline(input, line);) {
        std::smatch match;
        if (std::regex_match(line, match, assignment)) effects = configValue(match[1].str());
    }
    std::vector<std::string> selected;
    bool hasCustom = false;
    for (const auto& effect : splitEffects(effects)) {
        for (const auto& [id, managed] : SHADER_EFFECTS)
            if (lowercase(effect) == lowercase(std::string(managed)))
                selected.emplace_back(id);
        for (const auto& shader : custom) {
            if (effect == shader.name) {
                selected.push_back("custom/" + shader.name);
                hasCustom = true;
            }
        }
    }
    std::vector<std::string> unique;
    for (const auto& id : selected)
        if (std::find(unique.begin(), unique.end(), id) == unique.end()) unique.push_back(id);
    return hasCustom ? joinEffects(unique) : settings.shader;
}

std::string ls::readVkBasaltConfiguration(const std::filesystem::path& path) {
    if (!std::filesystem::is_regular_file(path)) return {};
    std::ifstream input(path);
    if (!input) throw ls::error("unable to read vkBasalt configuration");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

ls::VkBasaltCustomShader ls::addVkBasaltCustomShader(
        const std::filesystem::path& configPath, const std::filesystem::path& shaderPath) {
    const auto path = std::filesystem::absolute(shaderPath);
    const auto pathText = path.string();
    if (!std::filesystem::is_regular_file(path) || lowercase(path.extension().string()) != ".fx" ||
            std::any_of(pathText.begin(), pathText.end(), [](const unsigned char c) {
                return c < 32 || c == 127 || c == '"';
            }))
        throw ls::error("choose a readable local .fx shader file");
    std::ifstream shaderInput(path);
    if (!shaderInput) throw ls::error("choose a readable local .fx shader file");
    auto existing = readVkBasaltConfiguration(configPath);
    const auto shaders = customVkBasaltShaders(existing);
    for (const auto& shader : shaders)
        if (shader.path == path.string()) return shader;
    std::string stem;
    for (const auto c : path.stem().string())
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
            stem += c;
    if (stem.empty() || !customShaderId("custom/" + stem)) stem = "CustomShader";
    stem = "Custom" + stem.substr(0, 100);
    auto name = stem;
    // Do not replace any existing assignment, even one outside the FX catalog.
    static const std::regex assignment(R"(^\s*([A-Za-z][A-Za-z0-9_]*)\s*=)");
    std::unordered_set<std::string> names;
    std::istringstream input(existing);
    for (std::string line; std::getline(input, line);) {
        std::smatch match;
        if (std::regex_search(line, match, assignment)) names.insert(lowercase(match[1].str()));
    }
    for (size_t suffix = 2; names.contains(lowercase(name)); ++suffix)
        name = stem + std::to_string(suffix);
    if (!existing.empty() && !existing.ends_with('\n')) existing += '\n';
    existing += name + " = " + quotedPath(path) + '\n';
    detail::writeConfigurationAtomically(configPath, existing);
    return {name, path.string()};
}

std::filesystem::path ls::findVkBasaltConfigurationFile() {
    const char* xdgPath = std::getenv("XDG_CONFIG_HOME");
    if (xdgPath && *xdgPath != '\0')
        return std::filesystem::path(xdgPath) / "vkBasalt" / "vkBasalt.conf";

    const char* homePath = std::getenv("HOME");
    if (homePath && *homePath != '\0')
        return std::filesystem::path(homePath) /
            ".config" / "vkBasalt" / "vkBasalt.conf";

    return "/etc/vkBasalt.conf";
}

std::string ls::mergeVkBasaltConfiguration(
        const std::string_view existing,
        const VkBasaltConf& settings,
        const std::filesystem::path& shaderDirectory, const std::string_view previousSelection) {
    if (!isVkBasaltSharpening(settings.sharpening) ||
            !isVkBasaltAntialiasing(settings.antialiasing) ||
            !isVkBasaltShader(settings.shader)) {
        throw ls::error("invalid managed vkBasalt setting");
    }

    const auto selected = selectedEffects(settings);
    const auto custom = customVkBasaltShaders(existing);
    std::map<std::string, std::string> desired;
    for (const auto& [effect, filename] : SHADER_FILES)
        desired.emplace(effect, quotedPath(shaderDirectory / filename));

    std::ostringstream strength;
    strength.setf(std::ios::fixed);
    strength.precision(2);
    strength << settings.sharpness;
    if (settings.sharpening == "cas") {
        desired["casSharpness"] = strength.str();
    } else if (settings.sharpening == "dls") {
        desired["dlsSharpness"] = strength.str();
        std::ostringstream denoise;
        denoise.setf(std::ios::fixed);
        denoise.precision(2);
        denoise << settings.dls_denoise;
        desired["dlsDenoise"] = denoise.str();
    }

    std::vector<std::string> lines;
    if (trim(existing).empty()) {
        lines = {
            "# MAKO Decky merges its visible controls; other settings are preserved.",
            "effects = ",
            "enableOnLaunch = True",
            "toggleKey = Home",
        };
    } else {
        std::istringstream input{std::string(existing)};
        std::string line;
        while (std::getline(input, line))
            lines.push_back(std::move(line));
    }

    const std::regex assignment(
        R"(^(\s*([A-Za-z][A-Za-z0-9_]*)\s*=\s*)(.*)$)"
    );
    std::unordered_set<std::string> seen;
    std::vector<std::string> merged;
    for (auto& line : lines) {
        if (line == "# Generated by MAKO Decky. Changes will be replaced.") {
            merged.emplace_back(
                "# MAKO Decky merges its visible controls; other settings are preserved."
            );
            continue;
        }
        std::smatch match;
        if (!std::regex_match(line, match, assignment)) {
            merged.push_back(std::move(line));
            continue;
        }
        const std::string prefix = match[1].str();
        const std::string key = match[2].str();
        std::string value = match[3].str();
        std::string suffix;
        if (const auto position = commentPosition(value); position != std::string_view::npos) {
            suffix = value.substr(position);
            value.resize(position);
        }
        value = trim(value);
        if (key == "effects") {
            merged.push_back(prefix + mergeEffects(configValue(value), selected,
                custom, settings, previousSelection) + suffix);
            seen.insert(key);
        } else if (const auto desiredValue = desired.find(key);
                desiredValue != desired.end()) {
            merged.push_back(prefix + desiredValue->second + suffix);
            seen.insert(key);
        } else {
            merged.push_back(std::move(line));
        }
    }

    if (!seen.contains("effects"))
        merged.push_back("effects = " + joinEffects(selected));
    for (const auto& [key, value] : desired) {
        if (!seen.contains(key))
            merged.push_back(key + " = " + value);
    }

    std::ostringstream output;
    for (const auto& line : merged)
        output << line << '\n';
    return output.str();
}

void ls::writeVkBasaltConfiguration(
        const std::filesystem::path& path,
        const VkBasaltConf& settings,
        const std::filesystem::path& shaderDirectory, const std::string_view previousSelection) {
    try {
        const auto existing = readVkBasaltConfiguration(path);
        detail::writeConfigurationAtomically(
            path,
            mergeVkBasaltConfiguration(existing, settings, shaderDirectory, previousSelection)
        );
    } catch (const std::exception& error) {
        throw ls::error("unable to write vkBasalt configuration", error);
    }
}
