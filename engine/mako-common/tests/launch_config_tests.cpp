/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/configuration/launch.hpp"

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

    setenv("MAKO_LAUNCH_CONFIG", canonicalPath.c_str(), 1);
    expect(ls::findLaunchConfigurationFile() == canonicalPath,
        "MAKO_LAUNCH_CONFIG must override launcher configuration discovery");
    unsetenv("MAKO_LAUNCH_CONFIG");

    std::filesystem::remove_all(directory);
    std::cout << "standalone launcher configuration tests passed\n";
    return 0;
}
