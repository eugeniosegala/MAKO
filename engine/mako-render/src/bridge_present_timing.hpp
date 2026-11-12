/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <optional>
#include <utility>

namespace mako::layer::present_diagnostics {

    /// Observation only: no result from this accumulator feeds presentation policy.
    class BridgePresentTiming {
    public:
        struct Samples {
            size_t count{};
            double sum{};
            double squaredSum{};
            double maximum{};
            void add(double value) {
                ++count;
                sum += value;
                squaredSum += value * value;
                maximum = count == 1 ? value : std::max(maximum, value);
            }
            [[nodiscard]] double mean() const { return count ? sum / static_cast<double>(count) : 0; }
            [[nodiscard]] double deviation() const {
                return count ? std::sqrt(std::max(0.0,
                    squaredSum / static_cast<double>(count) - mean() * mean())) : 0;
            }
        };
        struct Window {
            size_t requests{};
            size_t feedbacks{};
            size_t unmatched{};
            size_t overwritten{};
            size_t discontinuities{};
            // These causes overlap; discontinuities still counts each
            // unusable interval only once. Reused predictions are not drops.
            size_t nonconsecutiveIds{};
            size_t repeatedTimestamps{};
            size_t backwardsTimestamps{};
            // Matched feedback with a decodable latch later than its prediction.
            size_t lateLatchFeedbacks{};
            size_t dropped{};
            size_t invalidStages{};
            size_t unavailableStages{};
            Samples queueReady;
            Samples compositorHold;
            Samples requestedInterval;
            Samples reportedInterval;
            Samples lateness;
            Samples submitLateness;
        };
        /// One matched or dropped output for opt-in per-frame diagnostics.
        /// Timestamps are CLOCK_MONOTONIC nanoseconds; zero means unavailable.
        struct Frame {
            uint64_t id{};
            uint64_t desired{};
            uint64_t plannedSubmit{};
            uint64_t submitted{};
            uint64_t shown{};
            uint64_t latch{};
            uint64_t queueEnd{};
            bool lateLatch{};
            bool dropped{};
        };

        explicit BridgePresentTiming(bool staged = false) : staged(staged) {}

        void request(uint64_t id, uint64_t desired, uint64_t wake, uint64_t submit) {
            if (!started)
                started = wake;
            auto& entry = pending[id % pending.size()];
            if (entry.valid)
                ++window.overwritten;
            entry = {id, desired, wake, submit, true};
            ++window.requests;
            if (previousDesired && desired > previousDesired)
                window.requestedInterval.add(milliseconds(desired - previousDesired));
            previousDesired = desired;
            window.submitLateness.add(wake > submit ? milliseconds(wake - submit) : 0);
        }

        std::optional<Frame> feedback(uint64_t id, uint64_t desired, uint64_t actual,
                bool lateLatch = false, uint64_t latch = 0, uint64_t queueEnd = 0) {
            auto& entry = pending[id % pending.size()];
            if (!entry.valid || entry.id != id || entry.desired != desired || !actual) {
                ++window.unmatched;
                return std::nullopt;
            }
            entry.valid = false;
            const Frame frame{id, desired, entry.plannedSubmit, entry.submittedAt,
                actual, latch, queueEnd, lateLatch, false};
            ++window.feedbacks;
            window.lateLatchFeedbacks += lateLatch;
            // Gamescope may report a target vblank slightly before the request.
            window.lateness.add(actual >= desired ? milliseconds(actual - desired)
                                                  : -milliseconds(desired - actual));
            if (previousActual) {
                const auto nextId = staged ? previousId + 1
                    : static_cast<uint32_t>(previousId + 1);
                window.nonconsecutiveIds += id != nextId;
                window.repeatedTimestamps += actual == previousActual;
                window.backwardsTimestamps += actual < previousActual;
                if (id == nextId && actual > previousActual)
                    window.reportedInterval.add(milliseconds(actual - previousActual));
                else
                    ++window.discontinuities;
            }
            previousActual = actual;
            previousId = id;
            return frame;
        }

        std::optional<Frame> feedbackTiming(uint64_t id, uint64_t queueEnd, uint64_t latch,
                uint64_t predicted, uint64_t receivedAt) {
            auto& entry = pending[id % pending.size()];
            if (!entry.valid || entry.id != id) {
                ++window.unmatched;
                return std::nullopt;
            }
            if (!queueEnd && !latch && !predicted) {
                entry.valid = false;
                ++window.dropped;
                return Frame{id, entry.desired, entry.plannedSubmit, entry.submittedAt,
                    0, 0, 0, false, true};
            }
            const bool invalid =
                (queueEnd && (queueEnd < entry.submittedAt || queueEnd > receivedAt)) ||
                (latch && (latch > receivedAt || (queueEnd && latch < queueEnd))) ||
                predicted > static_cast<uint64_t>(INT64_MAX);
            if (invalid)
                ++window.invalidStages;
            else {
                if (queueEnd)
                    window.queueReady.add(milliseconds(queueEnd - entry.submittedAt));
                if (queueEnd && latch)
                    window.compositorHold.add(milliseconds(latch - queueEnd));
            }
            if (!queueEnd || !latch || !predicted)
                ++window.unavailableStages;
            if (predicted && predicted <= static_cast<uint64_t>(INT64_MAX))
                return feedback(id, entry.desired, predicted, !invalid && latch > predicted,
                    latch, queueEnd);
            entry.valid = false;
            ++window.feedbacks;
            return Frame{id, entry.desired, entry.plannedSubmit, entry.submittedAt,
                0, latch, queueEnd, false, false};
        }

        [[nodiscard]] std::optional<Window> take(uint64_t now) {
            if (!started || now < started || now - started < 1000000000)
                return std::nullopt;
            started = now;
            return std::exchange(window, {});
        }

        [[nodiscard]] size_t outstanding() const {
            return static_cast<size_t>(std::count_if(pending.begin(), pending.end(),
                [](const auto& entry) { return entry.valid; }));
        }

    private:
        struct Pending {
            uint64_t id{};
            uint64_t desired{};
            uint64_t submittedAt{};
            uint64_t plannedSubmit{};
            bool valid{};
        };
        std::array<Pending, 128> pending{};
        uint64_t started{};
        uint64_t previousDesired{};
        uint64_t previousActual{};
        uint64_t previousId{};
        bool staged{};
        Window window;
        static double milliseconds(uint64_t ns) { return static_cast<double>(ns) / 1000000.0; }
    };
}
