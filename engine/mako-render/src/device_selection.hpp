/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <optional>
#include <string>

namespace mako::layer {

    struct PhysicalDeviceIdentity {
        std::string name;
        std::string vendorId;
        std::string deviceId;
        std::optional<std::string> pci;
    };

    inline bool matchesBackendDevice(
            const PhysicalDeviceIdentity& candidate,
            const std::optional<std::string>& configuredGpu,
            const PhysicalDeviceIdentity& applicationDevice) {
        if (configuredGpu) {
            return candidate.name == *configuredGpu
                || candidate.vendorId + ":" + candidate.deviceId == *configuredGpu
                || (candidate.pci && *candidate.pci == *configuredGpu);
        }

        if (candidate.vendorId != applicationDevice.vendorId
                || candidate.deviceId != applicationDevice.deviceId)
            return false;

        if (candidate.pci && applicationDevice.pci)
            return candidate.pci == applicationDevice.pci;

        return candidate.name == applicationDevice.name;
    }

}
