/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/configuration/launch.hpp"
#include "mako-common/configuration/vkbasalt.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#include <unistd.h>

namespace {

    void expect(const bool condition, const std::string_view message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }

    void writeText(const std::filesystem::path& path, const std::string_view text) {
        std::ofstream output(path, std::ios::trunc);
        output << text;
    }

    std::string readText(const std::filesystem::path& path) {
        std::ifstream input(path);
        return {
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };
    }

    bool rejects(const std::filesystem::path& path, const std::string_view content) {
        writeText(path, content);
        try {
            static_cast<void>(ls::LaunchConfigFile(path));
        } catch (const std::exception&) {
            return true;
        }
        return false;
    }

}

int main() {
    const ls::LaunchConfigFile defaults;
    expect(!defaults.settings().enable_zink &&
            !defaults.settings().force_alsa_audio,
        "standalone launcher settings must fail closed by default");

    const auto directory = std::filesystem::temp_directory_path() /
        ("mako-launch-config-test-" +
         std::to_string(static_cast<long long>(::getpid())));
    std::filesystem::create_directories(directory);

    ls::LaunchConfigFile configured;
    configured.settings().enable_zink = true;
    configured.settings().force_alsa_audio = true;
    const auto canonicalPath = directory / "launcher.conf";
    configured.write(canonicalPath);

    expect(readText(canonicalPath) ==
            "version=1\n"
            "enable_zink=1\n"
            "force_alsa_audio=1\n",
        "launcher configuration writer must retain its canonical shell-safe format");

    const ls::LaunchConfigFile restored(canonicalPath);
    expect(restored.settings().enable_zink &&
            restored.settings().force_alsa_audio,
        "launcher configuration must round-trip every supported setting");

    expect(rejects(directory / "missing-version.conf", "enable_zink=1\n"),
        "launcher configuration without a version must be rejected");
    expect(rejects(directory / "future-version.conf", "version=2\n"),
        "future launcher configuration versions must be rejected");
    expect(rejects(directory / "invalid-boolean.conf",
            "version=1\nenable_zink=true\n"),
        "launcher booleans outside 0/1 must be rejected");
    expect(rejects(directory / "unknown-setting.conf",
            "version=1\nunknown_setting=1\n"),
        "unknown launcher settings must remain inert");
    expect(rejects(directory / "duplicate-setting.conf",
            "version=1\nenable_zink=1\nenable_zink=0\n"),
        "duplicate launcher settings must be rejected");
    ls::VkBasaltConf shaderSettings;
    shaderSettings.enabled = true;
    shaderSettings.sharpening = "dls";
    shaderSettings.sharpness = 0.75F;
    shaderSettings.dls_denoise = 0.4F;
    shaderSettings.antialiasing = "fxaa";
    shaderSettings.shader = "hdr_look:clarity:vibrance:levels_plus";
    const auto shaderDirectory = directory / "shaders";
    const std::string merged = ls::mergeVkBasaltConfiguration(
        "# custom\neffects = makoDeband:customEffect:cas # order\n"
        "customOption = keep\ndlsSharpness = 0.10\n",
        shaderSettings,
        shaderDirectory
    );
    expect(merged.find("effects = fxaa:makoHDRLook:makoClarity:makoVibrance:makoLevelsPlus:dls:customEffect # order\n") !=
            std::string::npos,
        "managed shader graph did not replace only controlled effects");
    expect(merged.find("customOption = keep\n") != std::string::npos,
        "managed shader merge discarded an advanced option");
    expect(merged.find("dlsSharpness = 0.75\n") != std::string::npos &&
            merged.find("dlsDenoise = 0.40\n") != std::string::npos,
        "managed DLS strengths were not written");
    expect(merged.find(
            "makoVibrance = \"" + (shaderDirectory / "Vibrance.fx").string() +
            "\"\n") != std::string::npos,
        "managed shader source path was not written");
    expect(ls::isVkBasaltShader("clarity:levels_plus") &&
            !ls::isVkBasaltShader("clarity:clarity") &&
            !ls::isVkBasaltShader("none:vibrance"),
        "ordered shader selection validation is inconsistent");

    const std::string customContent =
        "# preserve\neffects = \"Tone_A:makoVibrance:ToneB:cas\" # selected\n"
        "Tone_A = \"/tmp/a # colour.fx\" # source\n"
        "ToneB = /tmp/b.FX\n"
        "makoVibrance = /tmp/bundled.fx\n"
        "reshadeIncludePath = /tmp/includes.fx\n"
        "reshadeTexturePath = /tmp/textures.fx\n"
        "ignored = /tmp/not.fx\nignored = disabled\n"
        "# disabled = /tmp/disabled.fx\n"
        "customOption = keep\n";
    expect(ls::customVkBasaltShaders("Backslash = \"/tmp/a\\n.fx\"\n").at(0).path == "/tmp/a\\n.fx",
        "custom quoted paths must retain literal unknown backslash escapes");
    const auto custom = ls::customVkBasaltShaders(customContent);
    expect(custom.size() == 2 && custom.at(0).name == "ToneB" &&
            custom.at(1).name == "Tone_A" && custom.at(1).path == "/tmp/a # colour.fx",
        "custom catalog must honor quotes, comments, duplicate assignments and reserved aliases");
    ls::VkBasaltConf customSettings;
    customSettings.shader = "vibrance";
    expect(ls::vkBasaltShaderSelection(customContent, customSettings) ==
            "custom/Tone_A:vibrance:custom/ToneB",
        "legacy activation and mixed effect order must be visible before adoption");
    expect(ls::mergeVkBasaltConfiguration(customContent, customSettings, shaderDirectory)
            .find("effects = Tone_A:makoVibrance:cas:ToneB") != std::string::npos,
        "unrelated writes must preserve legacy custom effects");
    customSettings.manage_custom_shaders = true;
    customSettings.shader = "custom/ToneB:vibrance:custom/Tone_A";
    const auto managedCustom = ls::mergeVkBasaltConfiguration(customContent, customSettings, shaderDirectory);
    expect(managedCustom.find("effects = ToneB:makoVibrance:Tone_A:cas # selected") != std::string::npos,
        "custom effects must participate in ordered selection");
    customSettings.shader = "none";
    const auto clearedCustom = ls::mergeVkBasaltConfiguration(managedCustom, customSettings, shaderDirectory);
    expect(clearedCustom.find("effects = cas # selected") != std::string::npos &&
            clearedCustom.find("Tone_A = \"/tmp/a # colour.fx\" # source") != std::string::npos &&
            clearedCustom.find("customOption = keep") != std::string::npos,
        "clearing effects must retain custom definitions and advanced options");
    expect(ls::mergeVkBasaltConfiguration("effects = Removed:opaque\n", customSettings,
                shaderDirectory, "custom/Removed").find("effects = cas:opaque\n") != std::string::npos,
        "clearing a missing custom definition must remove its formerly selected alias");
    expect(ls::isVkBasaltShader("vibrance:custom/Tone_A") &&
            !ls::isVkBasaltShader("custom/Tone_A:custom/Tone_A") &&
            !ls::isVkBasaltShader("custom/../unsafe"),
        "custom selection validation must preserve safe case-sensitive identities");
    const auto localShader = directory / "Tone # local.fx";
    writeText(localShader, "// synthetic shader fixture\n");
    const auto customPath = directory / "custom.conf";
    writeText(customPath, "CustomTonelocal = reserved\n# retain\n");
    const auto added = ls::addVkBasaltCustomShader(customPath, localShader);
    expect(added.name == "CustomTonelocal2" && added.path == localShader.string(),
        "adding a shader must avoid collisions and reference its original file");
    const auto registered = readText(customPath);
    static_cast<void>(ls::addVkBasaltCustomShader(customPath, localShader));
    expect(readText(customPath) == registered &&
            ls::customVkBasaltShaders(registered).at(0).path == localShader.string(),
        "shader registration must be idempotent and round-trip quoted paths");
    bool rejectedShader = false;
    try { static_cast<void>(ls::addVkBasaltCustomShader(customPath, directory / "missing.fx")); }
    catch (const std::exception&) { rejectedShader = true; }
    expect(rejectedShader && readText(customPath) == registered,
        "invalid shader imports must leave the profile file untouched");

    setenv("MAKO_LAUNCH_CONFIG", canonicalPath.c_str(), 1);
    const std::string removalFixture =
        "# preserved\r\neffects = \"Tone:makoVibrance:tone:cas:Missing\" # chain\r\n"
        "Tone = /tmp/first.fx\r\nTone = \"/tmp/tone # local.FX\" # last\r\n"
        "tone = /tmp/other.fx\r\nMissing = disabled\r\ncustomOption = keep";
    const auto removed = ls::removeVkBasaltCustomShaders(removalFixture, {"custom/Tone", "custom/Missing"});
    expect(ls::vkBasaltShaderSelection(removalFixture, ls::VkBasaltConf{}) == "custom/Tone:vibrance:custom/tone",
        "legacy custom selection must resolve case-sensitive FX aliases in CRLF files");
    expect(removed == "# preserved\r\neffects = makoVibrance:tone:cas # chain\r\n"
            "tone = /tmp/other.fx\r\nMissing = disabled\r\ncustomOption = keep",
        "shader deletion must preserve case, comments, line endings, bundled effects and non-FX options");
    expect(ls::customVkBasaltShaders(removed).size() == 1 &&
            ls::customVkBasaltShaders(removed).front().name == "tone",
        "deleted duplicate definitions resurfaced in the catalog");
    auto retainedSettings = ls::VkBasaltConf{};
    retainedSettings.shader = "vibrance:custom/tone";
    retainedSettings.manage_custom_shaders = true;
    expect(ls::mergeVkBasaltConfiguration(removed, retainedSettings, shaderDirectory)
            .find("effects = makoVibrance:tone:cas # chain\n") != std::string::npos,
        "normal settings writes must merge the retained CRLF effect chain correctly");
    expect(ls::removeVkBasaltCustomShaders(removalFixture, {}) == removalFixture,
        "empty deletion changed advanced shader content");
    for (const auto* id : {"vibrance", "custom/makoVibrance", "custom/cas", "custom/reshadeIncludePath", "custom/../unsafe"}) {
        bool rejected = false;
        try { static_cast<void>(ls::removeVkBasaltCustomShaders(removalFixture, {id})); }
        catch (const std::exception&) { rejected = true; }
        expect(rejected, "shader deletion accepted a bundled or invalid alias");
    }
    ls::writeVkBasaltConfiguration(customPath, removed);
    expect(readText(customPath) == removed, "atomic shader deletion write changed its content");
    expect(ls::findLaunchConfigurationFile() == canonicalPath,
        "MAKO_LAUNCH_CONFIG must override launcher configuration discovery");
    unsetenv("MAKO_LAUNCH_CONFIG");

    std::filesystem::remove_all(directory);
    std::cout << "standalone launcher configuration tests passed\n";
    return 0;
}
