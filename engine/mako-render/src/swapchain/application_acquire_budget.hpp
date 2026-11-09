#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace mako::layer {

    /// Zink presents from a worker thread and sizes its acquire window from
    /// the image count MAKO exposes, so it can hold the generated-frame images
    /// MAKO reserved. With MAKO's own generated acquires that exceeds the
    /// lower swapchain's acquire limit, and its acquire fails; Zink then
    /// destroys the swapchain. Holding the application to the window its own
    /// request implies keeps the lower swapchain within its limit, and lets
    /// MAKO's pacing reach the game thread as it does for Vulkan games.
    class ApplicationAcquireBudget {
    public:
        using Clock = std::chrono::steady_clock;

        ApplicationAcquireBudget(const uint32_t requestedImages,
                const uint32_t surfaceMinimumImages) :
            limit(windowFor(requestedImages, surfaceMinimumImages)) {}

        /// Images an application may hold when its swapchain has exactly the
        /// requested image count: requested minus the surface minimum, plus
        /// the one acquire that waits for an image to return.
        [[nodiscard]] static constexpr uint32_t windowFor(
                const uint32_t requestedImages,
                const uint32_t surfaceMinimumImages) {
            return requestedImages > surfaceMinimumImages
                ? requestedImages - surfaceMinimumImages + 1 : 1;
        }

        /// A wrong count must never deadlock a game, so an unanswered wait
        /// falls back to the unthrottled acquire and forgets the count.
        [[nodiscard]] static constexpr Clock::duration fallbackWait() {
            return std::chrono::milliseconds{250};
        }

        /// Waits while the application already holds its whole window.
        /// Returns false when the bounded wait expired without a present.
        [[nodiscard]] bool waitForSlot(const Clock::duration bound) {
            std::unique_lock lock(this->mutex);
            return this->released.wait_for(lock, bound, [this] {
                return this->closed || this->held < this->limit;
            });
        }

        void acquired() {
            const std::lock_guard lock(this->mutex);
            this->held++;
        }

        void presented() {
            {
                const std::lock_guard lock(this->mutex);
                if (this->held > 0)
                    this->held--;
            }
            this->released.notify_all();
        }

        void close() {
            {
                const std::lock_guard lock(this->mutex);
                this->closed = true;
            }
            this->released.notify_all();
        }

        /// An image released without a present would otherwise keep every
        /// later acquire waiting for the fallback, so an expired wait treats
        /// the count as stale and starts again from an empty window.
        void forgetHeldImages() {
            const std::lock_guard lock(this->mutex);
            this->held = 0;
        }

        /// True only for the first expired wait, so it is reported once.
        [[nodiscard]] bool firstFallback() {
            const std::lock_guard lock(this->mutex);
            const bool first = !this->fallbackReported;
            this->fallbackReported = true;
            return first;
        }

        [[nodiscard]] uint32_t window() const {
            return this->limit;
        }

        [[nodiscard]] uint32_t heldImages() {
            const std::lock_guard lock(this->mutex);
            return this->held;
        }

    private:
        std::mutex mutex;
        std::condition_variable released;
        uint32_t held{0};
        const uint32_t limit;
        bool closed{false};
        bool fallbackReported{false};
    };

    /// Only Zink, and only when MAKO added images the application can take.
    [[nodiscard]] inline bool applicationAcquireBudgetApplies(
            const std::string_view engineName,
            const uint32_t requestedImages, const uint32_t provisionedImages,
            const uint32_t surfaceMinimumImages) {
        return engineName == "mesa zink" && surfaceMinimumImages > 0 &&
            provisionedImages > requestedImages;
    }

}
