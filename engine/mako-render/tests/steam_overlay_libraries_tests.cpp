/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "present_diagnostics.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <dlfcn.h>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
}

int main(int argc, char** argv) {
    expect(argc == 2, "synthetic library path is required");
    char directory[] = "/tmp/mako-overlay-libraries.XXXXXX";
    expect(mkdtemp(directory) != nullptr, "temporary directory must be created");
    const std::filesystem::path root{directory};
    using mako::layer::present_diagnostics::steamOverlayLibraries;
    auto loaded = steamOverlayLibraries();
    expect(!loaded.hookLoaded && !loaded.vulkanLoaded, "control must have no Steam libraries");

    for (const auto* name : {"gameoverlayrenderer.so.backup", "gameoverlayrenderer.so", "steamoverlayvulkanlayer.so"})
        std::filesystem::copy_file(argv[1], root / name);
    void* unrelated = dlopen((root / "gameoverlayrenderer.so.backup").c_str(), RTLD_NOW | RTLD_LOCAL);
    expect(unrelated != nullptr, "unrelated fixture must load");
    loaded = steamOverlayLibraries();
    expect(!loaded.hookLoaded && !loaded.vulkanLoaded, "names must match exact basenames");
    void* hooks = dlopen((root / "gameoverlayrenderer.so").c_str(), RTLD_NOW | RTLD_LOCAL);
    expect(hooks != nullptr, "hook fixture must load");
    loaded = steamOverlayLibraries();
    expect(loaded.hookLoaded && !loaded.vulkanLoaded, "orphaned hook must be identified");
    void* vulkan = dlopen((root / "steamoverlayvulkanlayer.so").c_str(), RTLD_NOW | RTLD_LOCAL);
    expect(vulkan != nullptr, "Vulkan fixture must load");
    loaded = steamOverlayLibraries();
    expect(loaded.hookLoaded && loaded.vulkanLoaded, "paired libraries must be identified");
    dlclose(hooks);
    loaded = steamOverlayLibraries();
    expect(!loaded.hookLoaded && loaded.vulkanLoaded, "inspection must not keep a library loaded");
    dlclose(vulkan);
    dlclose(unrelated);
    std::filesystem::remove_all(root);
    std::cout << "Steam overlay library diagnostics: PASS\n";
}
