/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <charconv>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace mako::cli::quality {
    // A semicolon separates source frames. Each frame contains "history" or
    // one to four strictly ordered, comma-separated interpolation timestamps.
    inline std::vector<std::vector<float>> parseSequence(std::string_view text) {
        std::vector<std::vector<float>> frames;
        do {
            const auto end = text.find(';');
            auto frame = text.substr(0, end);
            std::vector<float> timestamps;
            if (frame != "history") {
                do {
                    const auto comma = frame.find(',');
                    const auto value = frame.substr(0, comma);
                    float timestamp{};
                    const auto parsed = std::from_chars(value.data(),
                        value.data() + value.size(), timestamp);
                    if (value.empty() || parsed.ec != std::errc{} ||
                            parsed.ptr != value.data() + value.size() ||
                            !std::isfinite(timestamp) || timestamp <= 0.0F ||
                            timestamp >= 1.0F || timestamps.size() == 4 ||
                            (!timestamps.empty() && timestamp <= timestamps.back()))
                        throw std::invalid_argument("invalid temporal interpolation sequence");
                    timestamps.push_back(timestamp);
                    if (comma == std::string_view::npos) break;
                    frame.remove_prefix(comma + 1);
                } while (true);
            }
            frames.push_back(std::move(timestamps));
            if (frames.size() > 240)
                throw std::invalid_argument("temporal sequence exceeds 240 source frames");
            if (end == std::string_view::npos) break;
            text.remove_prefix(end + 1);
        } while (true);
        return frames;
    }

    inline float sequenceSceneTime(const size_t sourceFrame) {
        const auto phase = sourceFrame % 24;
        return static_cast<float>(phase <= 12 ? phase : 24 - phase) / 12.0F;
    }
}
