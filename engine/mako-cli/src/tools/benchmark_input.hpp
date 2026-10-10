/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace mako::cli::benchmark {
    // The SDR traffic recipe supplies sRGB/BT.709 bytes. Encode the same
    // relative scene in PQ/BT.2020 with 203-nit reference white, outside timing.
    inline std::vector<uint8_t> hdr10ProfileInput(std::span<const uint8_t> rgba) {
        if (rgba.size() % 4 != 0)
            throw std::invalid_argument("HDR profile input requires RGBA pixels");
        std::array<double, 256> linear{};
        for (size_t i = 0; i < linear.size(); ++i) {
            const double s = static_cast<double>(i) / 255.0;
            linear[i] = s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
        }
        const auto pq = [](double value) {
            const double p = std::pow(std::clamp(value * 203.0 / 10000.0, 0.0, 1.0), 2610.0 / 16384.0);
            const double encoded = std::pow((3424.0 / 4096.0 + 2413.0 / 128.0 * p) /
                (1.0 + 2392.0 / 128.0 * p), 2523.0 / 32.0);
            return static_cast<uint32_t>(std::lround(encoded * 1023.0));
        };
        std::vector<uint8_t> result(rgba.size());
        for (size_t i = 0; i < rgba.size(); i += 4) {
            const double r = linear[rgba[i]], g = linear[rgba[i + 1]], b = linear[rgba[i + 2]];
            const uint32_t packed = pq(0.627404 * r + 0.329283 * g + 0.043313 * b) |
                (pq(0.069097 * r + 0.919540 * g + 0.011362 * b) << 10) |
                (pq(0.016391 * r + 0.088013 * g + 0.895595 * b) << 20) |
                (static_cast<uint32_t>(std::lround(static_cast<double>(rgba[i + 3]) * 3.0 / 255.0)) << 30);
            for (size_t byte = 0; byte < 4; ++byte)
                result[i + byte] = static_cast<uint8_t>(packed >> (byte * 8));
        }
        return result;
    }
}
