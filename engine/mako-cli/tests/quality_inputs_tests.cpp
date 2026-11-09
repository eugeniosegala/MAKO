/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "tools/quality_inputs.hpp"
#include <cstdlib>
#include <iostream>

int main() {
    using namespace mako::cli::quality;
    auto pattern = (std::filesystem::temp_directory_path() / "mako-quality-input-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const auto created = mkdtemp(buffer.data());
    if (!created) return 1;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::filesystem::remove_all(path); }
    } cleanup{created};
    const auto require = [](bool value) { if (!value) throw std::runtime_error("quality input assertion failed"); };
    const auto rejects = [&](auto action) {
        bool rejected = false;
        try { action(); } catch (const std::exception&) { rejected = true; }
        require(rejected);
    };
    const std::string bytes{"\n\x20\xff\x01\x02\x03\x04\x05\x06\x07\x08\x09", 12};
    const auto write = [&](const char* name, const std::string& header, const std::string& payload) {
        std::ofstream stream(cleanup.path / name, std::ios::binary);
        stream << header << payload;
    };
    try {
        for (const auto* name : {"previous.ppm", "current.ppm", "reference.ppm"})
            write(name, "P6\n2 2\n255\n", bytes);
        const auto scene = readQualityPair(cleanup.path);
        require(scene.width == 2 && scene.height == 2 && scene.previous.size() == 16);
        require(scene.previous[0] == 10 && scene.previous[1] == 32 && scene.previous[2] == 255 && scene.previous[3] == 255);
        require(scene.previous == scene.current && scene.current == scene.reference);
        require(scene.focusMask == std::vector<uint8_t>(4, 1));
        for (const auto* header : {"P3\n2 2\n255\n", "P6\n2 2\n65535\n", "P6\n0 2\n255\n",
                "P6\n2 -2\n255\n", "P6\n999999999999999999999 2\n255\n",
                "P6\n16385 2\n255\n", "P6\n4001 4000\n255\n", "P6\n2 2 junk\n255\n"}) {
            write("invalid.ppm", header, bytes);
            rejects([&] { static_cast<void>(readQualityInput(cleanup.path / "invalid.ppm")); });
        }
        for (const auto& payload : {bytes.substr(0, 11), bytes + "x"}) {
            write("invalid.ppm", "P6\n2 2\n255\n", payload);
            rejects([&] { static_cast<void>(readQualityInput(cleanup.path / "invalid.ppm")); });
        }
        std::filesystem::create_symlink(cleanup.path / "previous.ppm", cleanup.path / "link.ppm");
        rejects([&] { static_cast<void>(readQualityInput(cleanup.path / "link.ppm")); });
        write("reference.ppm", "P6\n3 2\n255\n", bytes + bytes.substr(0, 6));
        rejects([&] { static_cast<void>(readQualityPair(cleanup.path)); });
        std::filesystem::remove(cleanup.path / "reference.ppm");
        rejects([&] { static_cast<void>(readQualityPair(cleanup.path)); });
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "Canonical saved-frame admission passed\n";
}
