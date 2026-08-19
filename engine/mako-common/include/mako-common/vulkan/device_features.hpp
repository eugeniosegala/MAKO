/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

namespace vk {

    /// Optional Vulkan features MAKO may enable without changing correctness.
    struct OptionalDeviceFeatures {
        bool robustImageAccess2{};
    };

    /// Select optional features only when both the extension and feature bit exist.
    [[nodiscard]] constexpr OptionalDeviceFeatures selectOptionalDeviceFeatures(
            const bool robustness2Extension,
            const bool robustImageAccess2) {
        return {
            .robustImageAccess2 = robustness2Extension && robustImageAccess2,
        };
    }
}
