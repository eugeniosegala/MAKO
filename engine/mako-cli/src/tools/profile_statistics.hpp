/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "mako-common/helpers/errors.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>
namespace mako::cli {
    struct ProfileStatistics {
        double minimum{};
        double median{};
        double percentile95{};
        double maximum{};
        double coefficientOfVariationPercent{};
    };

    [[nodiscard]] inline ProfileStatistics profileStatistics(
            const std::vector<double>& samples) {
        if (samples.empty())
            throw ls::error("GPU profile returned no samples");
        if (std::ranges::any_of(samples, [](double value) { return !std::isfinite(value) || value < 0.0; }))
            throw ls::error("profile samples must be finite and nonnegative");
        std::vector<double> ordered = samples;
        std::ranges::sort(ordered);
        const size_t middle = ordered.size() / 2;
        const double median = ordered.size() % 2 == 0
            ? (ordered.at(middle - 1) + ordered.at(middle)) / 2.0
            : ordered.at(middle);
        const size_t percentile95Index = std::min(
            ordered.size() - 1,
            static_cast<size_t>(std::ceil(ordered.size() * 0.95)) - 1
        );
        const double mean = std::accumulate(
            ordered.begin(), ordered.end(), 0.0
        ) / static_cast<double>(ordered.size());
        const double squaredDeviation = std::accumulate(
            ordered.begin(), ordered.end(), 0.0,
            [mean](const double total, const double value) {
                const double difference = value - mean;
                return total + difference * difference;
            }
        );
        const double deviation = std::sqrt(
            squaredDeviation / static_cast<double>(ordered.size())
        );
        return {
            .minimum = ordered.front(),
            .median = median,
            .percentile95 = ordered.at(percentile95Index),
            .maximum = ordered.back(),
            .coefficientOfVariationPercent = mean > 0.0
                ? deviation / mean * 100.0 : 0.0,
        };
    }

}
