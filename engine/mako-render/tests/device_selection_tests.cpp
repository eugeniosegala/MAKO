/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "device_selection.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>

using namespace mako::layer;

namespace {
    void expect(const bool condition, const std::string_view message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main() {
    const PhysicalDeviceIdentity integrated{
        .name = "Integrated GPU",
        .vendorId = "0x8086",
        .deviceId = "0x1234",
        .pci = "0:2.0",
    };
    const PhysicalDeviceIdentity gameDevice{
        .name = "Discrete GPU",
        .vendorId = "0x1002",
        .deviceId = "0x744C",
        .pci = "3:0.0",
    };

    expect(!matchesBackendDevice(integrated, std::nullopt, gameDevice),
        "automatic selection must not use the first unrelated GPU");
    expect(matchesBackendDevice(gameDevice, std::nullopt, gameDevice),
        "automatic selection must follow the game's Vulkan device");

    auto duplicateAtAnotherPciAddress = gameDevice;
    duplicateAtAnotherPciAddress.pci = "4:0.0";
    expect(!matchesBackendDevice(
            duplicateAtAnotherPciAddress, std::nullopt, gameDevice),
        "automatic selection must distinguish duplicate GPUs by PCI address");

    auto gameDeviceWithoutPci = gameDevice;
    gameDeviceWithoutPci.pci.reset();
    auto candidateWithoutPci = gameDeviceWithoutPci;
    expect(matchesBackendDevice(
            candidateWithoutPci, std::nullopt, gameDeviceWithoutPci),
        "automatic selection must work when PCI information is unavailable");

    expect(matchesBackendDevice(
            integrated, std::optional<std::string>{"0x8086:0x1234"}, gameDevice),
        "an explicit vendor/device selection must override automatic matching");
    expect(matchesBackendDevice(
            integrated, std::optional<std::string>{"0:2.0"}, gameDevice),
        "an explicit PCI selection must retain its existing behavior");
    expect(!matchesBackendDevice(
            gameDevice, std::optional<std::string>{"Integrated GPU"}, gameDevice),
        "an explicit GPU name must not silently fall back to the game device");

    std::cout << "device selection tests passed\n";
    return 0;
}
