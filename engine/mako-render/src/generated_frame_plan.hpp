/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <stdexcept>

namespace mako::layer {

    /// Normalized interpolation timestamps for one real-frame interval.
    ///
    /// MAKO supports at most 4x output, so a plan contains no more than three
    /// generated frames. Values stay inline because plans cross the scheduling,
    /// admission, and backend-submission boundaries on every generated frame.
    class GeneratedFramePlan {
    public:
        static constexpr size_t capacity = 3;
        using const_iterator = std::span<const float>::iterator;

        /// Construct an evenly-spaced plan containing generatedFrameCount
        /// timestamps. Zero produces an empty plan.
        [[nodiscard]] static GeneratedFramePlan evenlySpaced(
                const size_t generatedFrameCount) {
            if (generatedFrameCount > capacity) {
                throw std::invalid_argument(
                    "generated-frame plan exceeds inline capacity"
                );
            }

            GeneratedFramePlan plan;
            plan.count = generatedFrameCount;
            for (size_t i = 0; i < generatedFrameCount; ++i) {
                plan.values[i] = static_cast<float>(i + 1) /
                    static_cast<float>(generatedFrameCount + 1);
            }
            return plan;
        }

        /// Construct a validated explicit plan. This factory is the extension
        /// seam for a future target clock; presentation must not reconstruct a
        /// fully admitted plan from its count and discard these timestamps.
        [[nodiscard]] static GeneratedFramePlan fromTimestamps(
                const std::span<const float> timestamps) {
            if (timestamps.size() > capacity) {
                throw std::invalid_argument(
                    "generated-frame plan exceeds inline capacity"
                );
            }

            GeneratedFramePlan plan;
            float previous = 0.0F;
            for (const float timestamp : timestamps) {
                if (!std::isfinite(timestamp) || timestamp <= previous ||
                        timestamp >= 1.0F) {
                    throw std::invalid_argument(
                        "generated-frame timestamps must be strictly increasing between 0 and 1"
                    );
                }
                plan.values[plan.count++] = timestamp;
                previous = timestamp;
            }
            return plan;
        }

        [[nodiscard]] size_t size() const { return this->count; }
        [[nodiscard]] bool empty() const { return this->count == 0; }
        [[nodiscard]] float front() const { return this->values.front(); }
        [[nodiscard]] float operator[](const size_t index) const {
            return this->values[index];
        }
        [[nodiscard]] const_iterator begin() const {
            return this->timestamps().begin();
        }
        [[nodiscard]] const_iterator end() const {
            return this->timestamps().end();
        }
        [[nodiscard]] std::span<const float> timestamps() const {
            return std::span<const float, capacity>{this->values}.first(
                this->count
            );
        }

        bool operator==(const GeneratedFramePlan&) const = default;

    private:
        std::array<float, capacity> values{};
        size_t count{0};
    };

    /// Select the timestamps that can actually be scheduled after presentation
    /// admission. Full admission preserves the request exactly. Partial
    /// admission retains MAKO's established behavior by redistributing the
    /// admitted frames evenly across the real-frame interval rather than taking
    /// a prefix of the original timestamps.
    [[nodiscard]] inline GeneratedFramePlan scheduleAdmittedGeneratedFrames(
            const GeneratedFramePlan& requested,
            const size_t admittedGeneratedFrameCount) {
        if (admittedGeneratedFrameCount > requested.size()) {
            throw std::invalid_argument(
                "generated-frame admission exceeds the requested plan"
            );
        }
        if (admittedGeneratedFrameCount == requested.size())
            return requested;
        return GeneratedFramePlan::evenlySpaced(admittedGeneratedFrameCount);
    }

}
