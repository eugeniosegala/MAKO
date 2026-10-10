/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "tools/profile_statistics.hpp"
#include "tools/benchmark_input.hpp"
#include "mako-common/vulkan/timestamp_query_pool.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

int main() {
    try {
        const auto require = [](bool value) { if (!value) throw std::runtime_error("profile timing contract failed"); };
        const auto packed = [](const std::vector<uint8_t>& bytes) {
            return static_cast<uint32_t>(bytes.at(0)) | (static_cast<uint32_t>(bytes.at(1)) << 8) |
                (static_cast<uint32_t>(bytes.at(2)) << 16) | (static_cast<uint32_t>(bytes.at(3)) << 24);
        };
        require(packed(mako::cli::benchmark::hdr10ProfileInput(std::vector<uint8_t>{0, 0, 0, 255})) == 0xc0000000U);
        const auto white = packed(mako::cli::benchmark::hdr10ProfileInput(std::vector<uint8_t>{255, 255, 255, 255}));
        require((white & 1023) == 594 && ((white >> 10) & 1023) == 594 && ((white >> 20) & 1023) == 594);
        const auto red = packed(mako::cli::benchmark::hdr10ProfileInput(std::vector<uint8_t>{255, 0, 0, 0}));
        require((red >> 30) == 0 && (red & 1023) > ((red >> 10) & 1023) && ((red >> 10) & 1023) > ((red >> 20) & 1023));
        uint32_t previous = 0;
        for (unsigned int i = 0; i < 256; ++i) {
            const auto v = static_cast<uint8_t>(i);
            const auto pixel = packed(mako::cli::benchmark::hdr10ProfileInput(std::vector<uint8_t>{v, v, v, 255}));
            require((pixel & 1023) >= previous);
            previous = pixel & 1023;
        }
        for (const double nits : {203.0, 1000.0, 10000.0}) {
            for (unsigned int i = 0; i < 256; ++i) {
                const auto v = static_cast<uint8_t>(i);
                const auto result = mako::cli::benchmark::hdr10QualityPreview(
                    mako::cli::benchmark::hdr10ProfileInput(std::vector<uint8_t>{v, v, v, 255}, nits), nits);
                for (size_t c = 0; c < 3; ++c)
                    require(std::abs(static_cast<int>(result[c]) - static_cast<int>(i)) <= 1);
                require(result[3] == 255);
            }
        }
        bool badInput = false;
        try { static_cast<void>(mako::cli::benchmark::hdr10ProfileInput(std::vector<uint8_t>{1, 2, 3})); }
        catch (const std::invalid_argument&) { badInput = true; }
        require(badInput);
        // Counter rollover must use the queue's valid width, including 64 bits.
        require(std::abs(vk::timestampElapsedMicroseconds(250, 4, 8, 100.0F) - 1.0) < 1e-10);
        require(std::abs(vk::timestampElapsedMicroseconds(UINT64_MAX - 4, 5, 64, 100.0F) - 1.0) < 1e-10);
        const auto invalid = [&](uint32_t bits, float period) {
            bool rejected = false;
            try { static_cast<void>(vk::timestampElapsedMicroseconds(0, 1, bits, period)); }
            catch (const ls::error&) { rejected = true; }
            require(rejected);
        };
        invalid(0, 1); invalid(65, 1); invalid(64, 0);
        invalid(64, std::numeric_limits<float>::infinity());
        const auto stats = mako::cli::profileStatistics({4, 1, 3, 2});
        require(stats.minimum == 1 && stats.median == 2.5 && stats.percentile95 == 4 && stats.maximum == 4);
        require(mako::cli::profileStatistics({0, 0}).coefficientOfVariationPercent == 0);
        for (const auto& values : {std::vector<double>{}, std::vector<double>{-1}, std::vector<double>{std::numeric_limits<double>::quiet_NaN()}}) {
            bool rejected = false;
            try { static_cast<void>(mako::cli::profileStatistics(values)); }
            catch (const ls::error&) { rejected = true; }
            require(rejected);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
