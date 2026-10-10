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
    inline std::vector<uint8_t> hdr10ProfileInput(std::span<const uint8_t> rgba, double whiteNits = 203.0) {
        if (!std::isfinite(whiteNits) || whiteNits <= 0 || whiteNits > 10000)
            throw std::invalid_argument("HDR reference white must be in (0, 10000] nits");
        if (rgba.size() % 4 != 0)
            throw std::invalid_argument("HDR profile input requires RGBA pixels");
        std::array<double, 256> linear{};
        for (size_t i = 0; i < linear.size(); ++i) {
            const double s = static_cast<double>(i) / 255.0;
            linear[i] = s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
        }
        const auto pq = [whiteNits](double value) {
            const double p = std::pow(std::clamp(value * whiteNits / 10000.0, 0.0, 1.0), 2610.0 / 16384.0);
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
    // CPU-only readback for bounded HDR quality scenes. Undo the fixture's
    // reference-white mapping; this preview is not physical HDR scanout proof.
    inline std::vector<uint8_t> hdr10QualityPreview(std::span<const uint8_t> packed, double whiteNits) {
        if (packed.size() % 4 != 0 || !std::isfinite(whiteNits) || whiteNits <= 0 || whiteNits > 10000)
            throw std::invalid_argument("Invalid HDR quality readback");
        const auto linear = [whiteNits](uint32_t code) {
            const double p = std::pow(static_cast<double>(code) / 1023.0, 32.0 / 2523.0);
            return std::pow(std::max(p - 3424.0 / 4096.0, 0.0) /
                (2413.0 / 128.0 - 2392.0 / 128.0 * p), 16384.0 / 2610.0) * 10000.0 / whiteNits;
        };
        const auto srgb = [](double value) {
            value = std::clamp(value, 0.0, 1.0);
            return static_cast<uint8_t>(std::lround(255.0 * (value <= 0.0031308
                ? 12.92 * value : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055)));
        };
        std::vector<uint8_t> result(packed.size());
        for (size_t i = 0; i < packed.size(); i += 4) {
            uint32_t word = 0;
            for (size_t b = 0; b < 4; ++b) word |= static_cast<uint32_t>(packed[i+b]) << (8*b);
            const double r = linear(word & 1023), g = linear((word >> 10) & 1023), b = linear((word >> 20) & 1023);
            result[i] = srgb(1.660491*r - 0.587641*g - 0.072850*b);
            result[i+1] = srgb(-0.124550*r + 1.132900*g - 0.008349*b);
            result[i+2] = srgb(-0.018151*r - 0.100579*g + 1.118730*b);
            result[i+3] = static_cast<uint8_t>((word >> 30) * 85);
        }
        return result;
    }

}
