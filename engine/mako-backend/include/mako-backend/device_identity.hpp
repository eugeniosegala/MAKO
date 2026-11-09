/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace mako::backend {

    /// VkPhysicalDeviceIDProperties's driverUUID and deviceUUID. Vulkan shares
    /// memory and semaphores only between devices that agree on both.
    struct DriverIdentity {
        std::array<uint8_t, 16> driver{};
        std::array<uint8_t, 16> device{};
    };

    /// A vendor or device ID as DevicePicker receives it: `0x` and the low four
    /// hexadecimal digits in upper case, such as `0x10DE`. The layer and every
    /// backend must format both sides of a comparison with this.
    inline std::string deviceIdText(const uint32_t id) {
        constexpr char digits[] = "0123456789ABCDEF";
        std::string value = "0x0000";
        value.at(2) = digits[(id >> 12U) & 0xFU];
        value.at(3) = digits[(id >> 8U) & 0xFU];
        value.at(4) = digits[(id >> 4U) & 0xFU];
        value.at(5) = digits[id & 0xFU];
        return value;
    }

    /// A PCI address as DevicePicker receives it: decimal `bus:device.function`
    /// without padding, such as `16:0.0`.
    inline std::string pciAddressText(const uint32_t bus, const uint32_t device,
            const uint32_t function) {
        return std::to_string(bus) + ":" + std::to_string(device) + "." +
            std::to_string(function);
    }

    /// Convert a hexadecimal `bus:device.function` address, as MAKO FG's
    /// runtime reports it, to pciAddressText(). Empty or malformed text has
    /// no address.
    inline std::optional<std::string> pciAddressFromHexText(const std::string_view text) {
        uint32_t parts[3]{};
        const char* cursor = text.data();
        const char* const end = text.data() + text.size();
        for (size_t index = 0; index < 3; ++index) {
            if (index > 0) {
                if (cursor == end || *cursor != (index == 1 ? ':' : '.'))
                    return std::nullopt;
                ++cursor;
            }
            const auto [next, result] = std::from_chars(cursor, end, parts[index], 16);
            if (result != std::errc{} || next == cursor)
                return std::nullopt;
            cursor = next;
        }
        if (cursor != end)
            return std::nullopt;
        return pciAddressText(parts[0], parts[1], parts[2]);
    }

}
