/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "adaptive_scheduler.hpp"
#include "bridge_present_timing.hpp"

#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include <vulkan/vulkan_core.h>

namespace mako::layer::present_diagnostics {

    using Clock = std::chrono::steady_clock;

    [[nodiscard]] uint64_t allocateContextId();
    /// Return the process-start diagnostic policy shared by all layer paths.
    [[nodiscard]] bool enabled();
    /// Return the process-start slow-operation threshold in milliseconds.
    [[nodiscard]] double thresholdMilliseconds();
    [[nodiscard]] Clock::time_point start();

    struct SteamOverlayLibraries {
        bool hookLoaded{};
        bool vulkanLoaded{};
    };
    /// Inspect loaded ELF names once at diagnostic process identification.
    /// This neither loads libraries nor reads /proc or the environment.
    [[nodiscard]] SteamOverlayLibraries steamOverlayLibraries();

    /// Fixed-size private-present CPU windows, including healthy sub-threshold
    /// work. Index order is total, fence, schedule, copy, acquire, generated
    /// submit, generated present, original present, unattributed.
    class PresentPhaseTiming {
    public:
        struct Window {
            size_t calls{};
            std::array<BridgePresentTiming::Samples, 9> phases;
        };
        std::optional<Window> observe(uint64_t context, Clock::time_point finished,
                const std::array<double, 9>& milliseconds) {
            if (!started || context != owner) {
                owner = context;
                started = finished;
                window = {};
            }
            ++window.calls;
            for (size_t index = 0; index < milliseconds.size(); ++index)
                window.phases[index].add(milliseconds[index]);
            if (finished - *started < std::chrono::seconds(1))
                return {};
            const auto completed = window;
            window = {};
            started = finished;
            return completed;
        }
    private:
        uint64_t owner{};
        std::optional<Clock::time_point> started;
        Window window;
    };
    void recordPresentPhaseTiming(uint64_t context, size_t frame, size_t sequence,
        Clock::time_point finished, const std::array<double, 9>& milliseconds) noexcept;

    void logBridgeTiming(uint64_t bridgeId, VkSwapchainKHR swapchain,
        const BridgePresentTiming::Window& window,
        size_t outstanding, uint64_t refreshCycleNs);

    enum class PresentWaitApi { Khr, Khr2 };

    enum class ApplicationFrameApi { AcquireKhr, AcquireKhr2, Present };

    /// Bounded CPU observations at the application's layer entrypoints. These
    /// include intentional pacing, unlike the private-work present timer.
    /// A stream owns one caller/swapchain pair; switching drops its old window.
    class ApplicationFrameTiming {
    public:
        struct Window {
            size_t calls{};
            size_t successful{};
            size_t timeouts{};
            size_t notReady{};
            size_t errors{};
            size_t polls{};
            BridgePresentTiming::Samples entryInterval;
            BridgePresentTiming::Samples duration;
            BridgePresentTiming::Samples configurationUpdate;
        };

        [[nodiscard]] std::optional<Window> observe(uintptr_t caller,
                VkSwapchainKHR swapchain, Clock::time_point started,
                Clock::time_point finished, std::optional<VkResult> result,
                std::optional<uint64_t> timeout,
                std::optional<Clock::duration> configurationUpdate = {}) {
            if (!windowStarted || caller != lastCaller || swapchain != lastSwapchain) {
                windowStarted = started;
                previousEntry.reset();
                lastCaller = caller;
                lastSwapchain = swapchain;
                window = {};
            }
            if (previousEntry)
                window.entryInterval.add(milliseconds(started - *previousEntry));
            previousEntry = started;
            ++window.calls;
            if (result) {
                window.successful += *result == VK_SUCCESS || *result == VK_SUBOPTIMAL_KHR;
                window.timeouts += *result == VK_TIMEOUT;
                window.notReady += *result == VK_NOT_READY;
                window.errors += *result < 0;
            }
            window.polls += timeout && *timeout == 0;
            window.duration.add(milliseconds(finished - started));
            if (configurationUpdate)
                window.configurationUpdate.add(milliseconds(*configurationUpdate));
            if (finished - *windowStarted < std::chrono::seconds(1))
                return std::nullopt;
            windowStarted = finished;
            const auto completed = window;
            window = {};
            return completed;
        }
    private:
        static double milliseconds(Clock::duration value) {
            return std::chrono::duration<double, std::milli>(value).count();
        }
        std::optional<Clock::time_point> windowStarted;
        std::optional<Clock::time_point> previousEntry;
        uintptr_t lastCaller{};
        VkSwapchainKHR lastSwapchain{};
        Window window;
    };

    void recordApplicationFrameTiming(ApplicationFrameApi api, uintptr_t caller,
        VkSwapchainKHR swapchain, Clock::time_point started, Clock::time_point finished,
        std::optional<VkResult> result = {}, std::optional<uint64_t> timeout = {},
        std::optional<Clock::duration> configurationUpdate = {});

    template<typename Acquire>
    VkResult observeApplicationAcquire(VkDevice device, VkSwapchainKHR swapchain,
            ApplicationFrameApi api, uint64_t timeout, Acquire&& acquire) {
        if (!enabled())
            return acquire();
        const auto started = Clock::now();
        const auto result = acquire();
        const auto finished = Clock::now();
        try {
            recordApplicationFrameTiming(api, reinterpret_cast<uintptr_t>(device),
                swapchain, started, finished, result, timeout);
        } catch (...) {
            // Observing a completed acquire cannot change its returned result.
        }
        return result;
    }

    class ApplicationPresentScope {
    public:
        ApplicationPresentScope(VkQueue queue, VkSwapchainKHR swapchain);
        ~ApplicationPresentScope();
        ApplicationPresentScope(const ApplicationPresentScope&) = delete;
        ApplicationPresentScope& operator=(const ApplicationPresentScope&) = delete;
        void configurationUpdateStarted();
        void configurationUpdateFinished();
    private:
        bool active;
        uintptr_t caller;
        VkSwapchainKHR swapchain;
        Clock::time_point started{};
        Clock::time_point updateStarted{};
        std::optional<Clock::duration> updateDuration;
    };

    struct ApplicationPresentMode {
        int64_t mode{-1};
        bool dynamic{};
        bool operator==(const ApplicationPresentMode&) const = default;
    };

    [[nodiscard]] inline ApplicationPresentMode applicationPresentMode(
            VkPresentModeKHR createdMode, const void* chain) {
        for (auto* node = static_cast<const VkBaseInStructure*>(chain);
                node; node = node->pNext) {
            if (node->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT) {
                const auto* modes = reinterpret_cast<const VkSwapchainPresentModeInfoEXT*>(node);
                // A multi-swapchain call has no per-context index here. Leave
                // it explicitly unknown rather than attributing index zero.
                return {modes->swapchainCount == 1 && modes->pPresentModes
                    ? static_cast<int64_t>(*modes->pPresentModes) : -1, true};
            }
        }
        return {static_cast<int64_t>(createdMode), false};
    }

    /// One bounded stream per calling thread. Switching device, swapchain or
    /// API drops an incomplete window instead of joining unrelated lifetimes.
    /// These observations never authorize completion or change a timeout.
    class ApplicationPresentWait {
    public:
        struct Window {
            size_t calls{};
            size_t successful{};
            size_t timeouts{};
            size_t errors{};
            size_t polls{};
            uint64_t firstPresentId{};
            uint64_t lastPresentId{};
            BridgePresentTiming::Samples duration;
        };

        [[nodiscard]] std::optional<Window> observe(VkDevice device,
                VkSwapchainKHR swapchain, PresentWaitApi api,
                uint64_t presentId, uint64_t timeout, VkResult result,
                Clock::time_point started, Clock::time_point finished) {
            if (!windowStarted || device != lastDevice ||
                    swapchain != lastSwapchain || api != lastApi) {
                windowStarted = started;
                lastDevice = device;
                lastSwapchain = swapchain;
                lastApi = api;
                window = {};
            }
            if (!window.calls)
                window.firstPresentId = presentId;
            ++window.calls;
            window.successful += result == VK_SUCCESS;
            window.timeouts += result == VK_TIMEOUT;
            window.errors += result < 0;
            window.polls += timeout == 0;
            window.lastPresentId = presentId;
            window.duration.add(std::chrono::duration<double, std::milli>(
                finished - started).count());
            if (finished - *windowStarted < std::chrono::seconds(1))
                return std::nullopt;
            windowStarted = finished;
            const auto completed = window;
            window = {};
            return completed;
        }
    private:
        std::optional<Clock::time_point> windowStarted;
        VkDevice lastDevice{};
        VkSwapchainKHR lastSwapchain{};
        PresentWaitApi lastApi{};
        Window window;
    };

    void recordApplicationPresentWait(VkDevice device, VkSwapchainKHR swapchain,
        PresentWaitApi api, uint64_t presentId, uint64_t timeout, VkResult result,
        Clock::time_point started, Clock::time_point finished);

    template<typename Wait>
    VkResult observeApplicationPresentWait(VkDevice device, VkSwapchainKHR swapchain,
            PresentWaitApi api, uint64_t presentId, uint64_t timeout, Wait&& wait) {
        if (!enabled())
            return wait();
        const auto started = Clock::now();
        const auto result = wait();
        const auto finished = Clock::now();
        recordApplicationPresentWait(device, swapchain, api, presentId, timeout,
            result, started, finished);
        return result;
    }

    class ContextScope {
    public:
        explicit ContextScope(uint64_t contextId);
        ~ContextScope();

        ContextScope(const ContextScope&) = delete;
        ContextScope& operator=(const ContextScope&) = delete;
    private:
        uint64_t previousContextId;
    };

    void logSlowOperation(std::string_view operation,
        size_t frameIndex, size_t sequenceIndex,
        Clock::time_point started,
        std::optional<VkResult> result = std::nullopt,
        std::optional<size_t> passIndex = std::nullopt,
        std::optional<uint32_t> imageIndex = std::nullopt);

    void logPresentFallback(size_t frameIndex, size_t sequenceIndex,
        size_t passIndex, size_t skippedFrames, uint64_t timelineValue,
        std::string_view acquireMode, std::string_view backendWork);

    void logHistoryWarmup(size_t frameIndex, size_t sequenceIndex,
        size_t remainingFrames, bool recovery,
        std::optional<uint32_t> acquiredImage);

    [[nodiscard]] AdaptiveSchedulerDiagnostics& adaptiveScheduler();

}
