/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "power_source.hpp"
#include "mako-common/configuration/config.hpp"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <stop_token>
#include <utility>

using namespace ls;

detail::PowerSourceMonitor::PowerSourceMonitor(std::filesystem::path root,
        Detector detector) : root(std::move(root)), detector(std::move(detector)),
    worker([this](const std::stop_token stop) {
        std::unique_lock<std::mutex> lock(this->mutex);
        while (!stop.stop_requested()) {
            this->wake.wait(lock, [&] {
                return stop.stop_requested() || this->requested;
            });
            if (stop.stop_requested()) break;
            this->requested = false;
            this->sampling = true;
            lock.unlock();

            PowerSource source = PowerSource::Unknown;
            try {
                source = this->detector(this->root);
            } catch (...) {
                // A failed supply read cannot terminate the game or erase a
                // previously confirmed selection.
                source = PowerSource::Unknown;
            }
            if (source != PowerSource::Unknown)
                this->latest.store(source, std::memory_order_relaxed);

            lock.lock();
            this->sampling = false;
            this->sampled = true;
            this->wake.notify_all();
        }
    }) {}

detail::PowerSourceMonitor::~PowerSourceMonitor() {
    this->worker.request_stop();
    // Pair stop notification with the wait mutex so an idle worker cannot
    // miss it. Driver reads never hold this mutex.
    {
        const std::lock_guard<std::mutex> lock(this->mutex);
        this->wake.notify_all();
    }
    this->worker.join();
}

PowerSource detail::PowerSourceMonitor::sample() const noexcept {
    return this->latest.load(std::memory_order_relaxed);
}

bool detail::PowerSourceMonitor::requestSample() {
    const std::unique_lock<std::mutex> lock(this->mutex, std::try_to_lock);
    if (!lock.owns_lock() || this->requested || this->sampling)
        return false;
    this->requested = true;
    this->wake.notify_one();
    return true;
}

bool detail::PowerSourceMonitor::waitForInitialSample() {
    std::unique_lock<std::mutex> lock(this->mutex);
    return this->wake.wait_for(lock, startupWait, [this] {
        return this->sampled;
    });
}
