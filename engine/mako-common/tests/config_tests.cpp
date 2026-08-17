/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/configuration/config.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

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
        output.close();
    }

    constexpr std::string_view validConfiguration = R"(version = 2
[global]
allow_fp16 = true

[[profile]]
name = "test"
active_in = "game"
adaptive = true
base_fps_cap = 60
adaptive_auto_base_fps_cap = true
target_fps = 144
adaptive_max_multiplier = 4
)";
}

int main() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("mako-config-test-" + std::to_string(static_cast<long long>(::getpid())));
    std::filesystem::create_directories(directory);
    const auto path = directory / "conf.toml";
    writeText(path, validConfiguration);

    unsetenv("MAKO_ENV");
    setenv("MAKO_CONFIG", path.c_str(), 1);

    ls::WatchedConfig config;
    expect(!config.update(),
        "The configuration parsed by the constructor must not reload immediately");

    const auto initialTimestamp = std::filesystem::last_write_time(path);
    writeText(path, "version = [partial");
    std::filesystem::last_write_time(path, initialTimestamp + std::chrono::seconds(2));

    bool firstParseFailed = false;
    try {
        static_cast<void>(config.update());
    } catch (const std::exception&) {
        firstParseFailed = true;
    }
    expect(firstParseFailed, "A partial configuration must be rejected");

    expect(!config.update(),
        "A failed parse must be briefly backed off instead of retried every frame");

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    bool sameVersionRetried = false;
    try {
        static_cast<void>(config.update());
    } catch (const std::exception&) {
        sameVersionRetried = true;
    }
    expect(sameVersionRetried,
        "A failed parse must retain its timestamp so the same version is retried");

    writeText(path, validConfiguration);
    std::filesystem::last_write_time(path, initialTimestamp + std::chrono::seconds(4));
    expect(config.update(), "A subsequent complete configuration must be accepted");
    expect(config.get().profiles().size() == 1,
        "The accepted configuration must replace the previous profile set");
    expect(config.get().profiles().front().target_fps == 144,
        "The accepted configuration must expose its new policy");
    expect(config.get().profiles().front().base_fps_cap == 60,
        "The accepted configuration must expose its base FPS cap");
    expect(config.get().profiles().front().adaptive_auto_base_fps_cap,
        "The accepted configuration must expose Adaptive auto-cap");

    std::filesystem::remove_all(directory);
    std::cout << "configuration watcher tests passed\n";
    return 0;
}
