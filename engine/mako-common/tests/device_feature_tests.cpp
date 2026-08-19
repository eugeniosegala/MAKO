/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/vulkan/device_features.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
    void expect(const bool condition, const std::string_view message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main() {
    expect(!vk::selectOptionalDeviceFeatures(false, false).robustImageAccess2,
        "robust image access must remain disabled when unsupported");
    expect(!vk::selectOptionalDeviceFeatures(false, true).robustImageAccess2,
        "a feature bit cannot be used without the extension");
    expect(!vk::selectOptionalDeviceFeatures(true, false).robustImageAccess2,
        "the extension alone cannot enable an unsupported feature bit");
    expect(vk::selectOptionalDeviceFeatures(true, true).robustImageAccess2,
        "robust image access must be selected when fully supported");

    std::cout << "optional device feature tests passed\n";
    return 0;
}
