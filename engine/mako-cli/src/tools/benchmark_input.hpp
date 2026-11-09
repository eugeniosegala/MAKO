/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
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
    inline uint16_t halfFloat(float value) {
        uint32_t bits;std::memcpy(&bits, &value, 4);
        const uint32_t sign = (bits >> 16) & 0x8000u, exponent = (bits >> 23) & 0xffu, mantissa = bits & 0x7fffffu;
        if (exponent == 0xffu) return static_cast<uint16_t>(sign | 0x7c00u | (mantissa ? 0x200u : 0u));
        const int e = static_cast<int>(exponent) - 127 + 15;
        if (e >= 31) return static_cast<uint16_t>(sign | 0x7c00u);
        if (e <= 0) {
            if (e < -10) return static_cast<uint16_t>(sign);
            const uint32_t m = (mantissa | 0x800000u) >> (1 - e);
            return static_cast<uint16_t>(sign | ((m + 0x1000u) >> 13));
        }
        return static_cast<uint16_t>(sign | ((static_cast<uint32_t>(e) << 10) + ((mantissa + 0x1000u) >> 13)));
    }
    inline float fromHalf(uint16_t h) {
        const uint32_t sign = (h & 0x8000u) << 16, exponent = (h >> 10) & 0x1fu, mantissa = h & 0x3ffu;
        float value;
        if (exponent == 0) value = std::ldexp(static_cast<float>(mantissa), -24);
        else if (exponent == 31) value = mantissa ? NAN : INFINITY;
        else value = std::ldexp(static_cast<float>(mantissa | 0x400u), static_cast<int>(exponent) - 25);
        return sign ? -value : value;
    }
    // The same relative scene as half-float PQ code values (BT.2020) or linear scRGB (BT.709,
    // 1.0 = 80 nits), with the given reference white.
    inline std::vector<uint8_t> hdr16Input(std::span<const uint8_t> rgba, double whiteNits, bool pq) {
        if (rgba.size() % 4 != 0 || !std::isfinite(whiteNits) || whiteNits <= 0 || whiteNits > 10000)
            throw std::invalid_argument("Invalid HDR half-float input");
        const auto linear = [](uint8_t v) {const double s = v / 255.0; return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);};
        const auto encode = [whiteNits](double value) {
            const double p = std::pow(std::clamp(value * whiteNits / 10000.0, 0.0, 1.0), 2610.0 / 16384.0);
            return std::pow((3424.0 / 4096.0 + 2413.0 / 128.0 * p) / (1.0 + 2392.0 / 128.0 * p), 2523.0 / 32.0);
        };
        std::vector<uint8_t> result(rgba.size() * 2);
        for (size_t i = 0, o = 0; i < rgba.size(); i += 4, o += 8) {
            const double r = linear(rgba[i]), g = linear(rgba[i + 1]), b = linear(rgba[i + 2]);
            std::array<double, 4> v{};
            if (pq) v = {encode(0.627404 * r + 0.329283 * g + 0.043313 * b), encode(0.069097 * r + 0.919540 * g + 0.011362 * b),
                         encode(0.016391 * r + 0.088013 * g + 0.895595 * b), rgba[i + 3] / 255.0};
            else v = {r * whiteNits / 80.0, g * whiteNits / 80.0, b * whiteNits / 80.0, rgba[i + 3] / 255.0};
            for (size_t c = 0; c < 4; ++c) {const uint16_t h = halfFloat(static_cast<float>(v[c])); result[o + 2 * c] = static_cast<uint8_t>(h); result[o + 2 * c + 1] = static_cast<uint8_t>(h >> 8);}
        }
        return result;
    }
    inline std::vector<uint8_t> hdr16Preview(std::span<const uint8_t> bytes, double whiteNits, bool pq) {
        if (bytes.size() % 8 != 0) throw std::invalid_argument("Invalid HDR half-float readback");
        const auto srgb = [](double value) {
            value = std::clamp(value, 0.0, 1.0);
            return static_cast<uint8_t>(std::lround(255.0 * (value <= 0.0031308 ? 12.92 * value : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055)));
        };
        const auto decode = [whiteNits](double e) {
            const double p = std::pow(std::clamp(e, 0.0, 1.0), 32.0 / 2523.0);
            return std::pow(std::max(p - 3424.0 / 4096.0, 0.0) / (2413.0 / 128.0 - 2392.0 / 128.0 * p), 16384.0 / 2610.0) * 10000.0 / whiteNits;
        };
        std::vector<uint8_t> result(bytes.size() / 2);
        for (size_t i = 0, o = 0; i < bytes.size(); i += 8, o += 4) {
            std::array<double, 4> v{};
            for (size_t c = 0; c < 4; ++c) v[c] = fromHalf(static_cast<uint16_t>(bytes[i + 2 * c] | (bytes[i + 2 * c + 1] << 8)));
            if (pq) {
                const double r = decode(v[0]), g = decode(v[1]), b = decode(v[2]);
                result[o] = srgb(1.660491 * r - 0.587641 * g - 0.072850 * b);
                result[o + 1] = srgb(-0.124550 * r + 1.132900 * g - 0.008349 * b);
                result[o + 2] = srgb(-0.018151 * r - 0.100579 * g + 1.118730 * b);
            } else for (size_t c = 0; c < 3; ++c) result[o + c] = srgb(v[c] * 80.0 / whiteNits);
            result[o + 3] = static_cast<uint8_t>(std::lround(std::clamp(v[3], 0.0, 1.0) * 255.0));
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
