/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-common/configuration/config.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <thread>

namespace ls::detail {

    /// One outstanding system-supply read, independent of presentation.
    /// The watcher owns the polling cadence; no requests accumulate while a
    /// driver read is stalled. Unknown results retain the last confirmed source.
    /// Teardown joins the worker and can wait for an outstanding read to return.
    class PowerSourceMonitor {
    public:
        using Detector = std::function<PowerSource(const std::filesystem::path&)>;
        static constexpr auto startupWait = std::chrono::milliseconds(50);

        explicit PowerSourceMonitor(std::filesystem::path root,
            Detector detector = detectPowerSource);
        ~PowerSourceMonitor();

        PowerSourceMonitor(const PowerSourceMonitor&) = delete;
        PowerSourceMonitor& operator=(const PowerSourceMonitor&) = delete;

        [[nodiscard]] PowerSource sample() const noexcept;
        bool requestSample();
        bool waitForInitialSample();

    private:
        const std::filesystem::path root;
        const Detector detector;
        std::atomic<PowerSource> latest{PowerSource::Unknown};
        std::mutex mutex;
        std::condition_variable wake;
        bool requested{true};
        bool sampling{false};
        bool sampled{false};
        std::jthread worker;
    };

}
