/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "device_selection.hpp"

#include "mako-backend/device_identity.hpp"

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

    // IDs with hexadecimal letters and a bus number above 9 must match between
    // the layer and MAKO FG, whose runtime reports the PCI address in hex.
    using mako::backend::deviceIdText;
    using mako::backend::pciAddressFromHexText;
    using mako::backend::pciAddressText;
    const PhysicalDeviceIdentity nvidiaGame{
        .name = "NVIDIA GeForce RTX 4070",
        .vendorId = deviceIdText(0x10DEU),
        .deviceId = deviceIdText(0x2786U),
        .pci = pciAddressText(45, 0, 0),
    };
    const PhysicalDeviceIdentity nvidiaFromMakoFg{
        .name = nvidiaGame.name,
        .vendorId = deviceIdText(0x10deU),
        .deviceId = deviceIdText(0x2786U),
        .pci = pciAddressFromHexText("2d:0.0"),
    };
    expect(nvidiaGame.vendorId == "0x10DE" && nvidiaGame.pci == "45:0.0",
        "device identities must use upper-case IDs and decimal PCI addresses");
    expect(matchesBackendDevice(nvidiaFromMakoFg, std::nullopt, nvidiaGame),
        "MAKO FG must follow a game GPU whose IDs contain letters on bus 45");
    expect(pciAddressFromHexText("c1:1f.7") == "193:31.7",
        "every PCI field must convert from hexadecimal");
    for (const std::string_view malformed : {"", "2d", "2d:0", "2d:0.0x", "x:0.0", "2d.0:0"})
        expect(!pciAddressFromHexText(malformed),
            "a malformed MAKO FG PCI address must not match any device");
    expect(deviceIdText(0x10005U) == "0x0005",
        "Khronos vendor IDs keep the same low four digits on both sides");

    std::cout << "device selection tests passed\n";
    return 0;
}
