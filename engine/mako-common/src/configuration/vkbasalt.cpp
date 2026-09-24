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
constexpr std::array<std::string_view, 18> SHADERS{
    "none", "hdr_look", "vibrance", "colourfulness", "curves", "deband",
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

std::vector<std::string> selectedEffects(const ls::VkBasaltConf& settings) {
    std::vector<std::string> effects;
    if (settings.antialiasing != "none")
        effects.push_back(settings.antialiasing);
    if (settings.shader != "none")
        effects.emplace_back(SHADER_EFFECTS.at(settings.shader));
    if (settings.sharpening != "none")
        effects.push_back(settings.sharpening);
    return effects;
}

std::string mergeEffects(const std::string_view existing,
        const std::vector<std::string>& selected) {
    std::unordered_set<std::string> controlled{
        "cas", "dls", "fxaa", "smaa"
    };
    for (const auto& [unused, effect] : SHADER_EFFECTS) {
        static_cast<void>(unused);
        controlled.insert(lowercase(std::string(effect)));
    }

    std::vector<std::string> merged;
    bool inserted = false;
    for (auto effect : splitEffects(existing)) {
        if (controlled.contains(lowercase(effect))) {
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

bool ls::isVkBasaltShader(const std::string_view value) noexcept {
    return contains(SHADERS, value);
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
        const std::filesystem::path& shaderDirectory) {
    if (!isVkBasaltSharpening(settings.sharpening) ||
            !isVkBasaltAntialiasing(settings.antialiasing) ||
            !isVkBasaltShader(settings.shader)) {
        throw ls::error("invalid managed vkBasalt setting");
    }

    const auto selected = selectedEffects(settings);
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
        R"(^(\s*([A-Za-z][A-Za-z0-9]*)\s*=\s*)(.*)$)"
    );
    const std::regex trailingComment(R"((\s+#.*)$)");
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
        std::smatch comment;
        if (std::regex_search(value, comment, trailingComment)) {
            suffix = comment[1].str();
            value.erase(static_cast<size_t>(comment.position(1)));
        }
        value = trim(value);
        if (key == "effects") {
            merged.push_back(prefix + mergeEffects(value, selected) + suffix);
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
        const std::filesystem::path& shaderDirectory) {
    try {
        std::string existing;
        if (std::filesystem::is_regular_file(path)) {
            std::ifstream input(path);
            existing.assign(
                std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>()
            );
        }
        detail::writeConfigurationAtomically(
            path,
            mergeVkBasaltConfiguration(existing, settings, shaderDirectory)
        );
    } catch (const std::exception& error) {
        throw ls::error("unable to write vkBasalt configuration", error);
    }
}
