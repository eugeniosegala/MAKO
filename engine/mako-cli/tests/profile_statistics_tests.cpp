/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "tools/profile_statistics.hpp"
#include "mako-common/vulkan/timestamp_query_pool.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

int main() {
    try {
        const auto require = [](bool value) { if (!value) throw std::runtime_error("profile timing contract failed"); };
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
