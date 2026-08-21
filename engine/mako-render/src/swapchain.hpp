/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-backend/mako.hpp"
#include "mako-common/configuration/config.hpp"
#include "mako-common/helpers/pointers.hpp"
#include "mako-common/vulkan/command_buffer.hpp"
#include "mako-common/vulkan/fence.hpp"
#include "mako-common/vulkan/image.hpp"
#include "mako-common/vulkan/semaphore.hpp"
#include "mako-common/vulkan/timeline_semaphore.hpp"
#include "mako-common/vulkan/vulkan.hpp"
#include "adaptive_scheduler.hpp"
#include "color_pipeline.hpp"
#include "profile_update.hpp"
#include "presentation_policy.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace mako::layer {

    /// swapchain info struct
    struct SwapchainInfo {
        std::vector<VkImage> images;
        VkFormat format;
        VkColorSpaceKHR colorSpace;
        VkExtent2D extent;
        VkPresentModeKHR presentMode;
        // Persist the exact create-time transport decision. HDR feedback can
        // change later, but Vulkan present mode/pNext compatibility cannot be
        // reinterpreted without replacing the game-owned swapchain.
        bool privateOrderedTransport{false};
    };

    /// modify the swapchain create info based on the profile pre-swapchain creation
    /// @param profile active game profile
    /// @param maxImages maximum number of images supported by the surface
    /// @param createInfo swapchain create info to modify
    [[nodiscard]] bool context_ModifySwapchainCreateInfo(
        const ls::GameConf& profile, uint32_t maxImages,
        VkSwapchainCreateInfoKHR& createInfo, bool gamescopeHdrActive,
        bool gamescopeDetected,
        const PresentationEnvironmentPolicy& presentationEnvironment);

    /// swapchain context for a layer instance
    class Swapchain {
    public:
        /// create a new swapchain context
        /// @param vk vulkan instance
        /// @param backend mako backend instance
        /// @param profile active game profile
        /// @param info swapchain info
        Swapchain(const vk::Vulkan& vk, backend::Instance& backend,
            ls::GameConf profile, SwapchainInfo info,
            std::optional<bool> gamescopeHdrActive,
            bool gamescopeDetected, bool hdrExposureDisabled,
            std::optional<uint32_t> gamescopeRefreshHz,
            uint64_t runtimeStateRevision);

        /// present a frame
        /// @param vk vulkan instance
        /// @param queue presentation queue
        /// @param next_chain next chain pointer for the present info (WARN: shared!)
        /// @param imageIdx swapchain image index to present to
        /// @param semaphores semaphores to wait on before presenting
        /// @throws ls::vulkan_error on vulkan errors
        VkResult present(const vk::Vulkan& vk,
            VkQueue queue, VkSwapchainKHR swapchain,
            void* next_chain, uint32_t imageIdx,
            std::span<const VkSemaphore> semaphores);
        /// stable identifier used to correlate this context's diagnostics
        [[nodiscard]] uint64_t diagnosticsId() const {
            return this->diagnosticsState.contextId;
        }

        /// Apply configuration that is safe for an already-created context.
        /// Resource-shape and backend-construction changes remain pending for
        /// a natural game-owned recreation; this layer never forces one for a
        /// Decky setting change.
        [[nodiscard]] ProfileUpdateAction updateProfile(
            const ls::GameConf& profile, uint64_t runtimeStateRevision);

        /// Record a confirmed Gamescope application-HDR state change. Existing
        /// contexts retain their safe encoding until the game naturally
        /// recreates them.
        [[nodiscard]] bool updateGamescopeHdrState(
            bool active, uint64_t runtimeStateRevision);

        /// Update the compositor scanout budget without rebuilding resources.
        void updateGamescopeRefreshRate(std::optional<uint32_t> refreshHz);

        /// Stop generation in place when the active profile disappears.
        void disableFrameGeneration();
    private:
        struct PresentInvocation {
            const vk::Vulkan& vk;
            VkQueue queue;
            VkSwapchainKHR swapchain;
            const void* nextChain;
            uint32_t imageIndex;
            std::span<const VkSemaphore> waitSemaphores;
            std::chrono::steady_clock::time_point started;
        };

        struct PresentationFramePlan {
            // Policy request before WSI admission. Fully admitted timestamps
            // remain authoritative through backend submission.
            GeneratedFramePlan requestedGeneratedFrames;
            // Executable plan after transport-specific admission. Gamescope HDR
            // may contain fewer, deliberately re-spaced timestamps.
            GeneratedFramePlan scheduledGeneratedFrames;
            size_t admittedGeneratedFrameCount{0};
            bool historyWarmupActive{false};
            std::optional<uint64_t> configuredAcquireTimeout;
            std::array<uint32_t, GeneratedFramePlan::capacity>
                preacquiredGeneratedImages{};
        };

        struct FrameState {
            size_t sequenceIndex{1};
            size_t realFrameIndex{0};
            size_t backendFrameIndex{0};
            bool renderFenceInFlight{false};
            std::optional<std::chrono::steady_clock::time_point>
                lastPresentStarted;
            std::optional<std::chrono::steady_clock::duration>
                recentRealInterval;
        };

        struct RecoveryState {
            GeneratedImageAdmission generatedImageAdmission;
            PipelineBusyRecovery pipelineBusyRecovery;
            bool backendPending{false};
            size_t historyWarmupRemaining{0};
        };

        struct DiagnosticsState {
            std::optional<std::chrono::steady_clock::time_point>
                fixedWindowStarted;
            size_t fixedRealFrames{0};
            size_t fixedGeneratedFrames{0};
            size_t fixedSkippedFrames{0};
            uint64_t contextId{0};
        };

        struct ColorTransitionState {
            std::optional<bool> pendingGamescopeHdrActive;
            uint64_t pendingHdrStateRevision{0};
            std::optional<std::chrono::steady_clock::time_point> retryAt;
        };

        std::vector<vk::Image> sourceImages;
        std::vector<vk::Image> destinationImages;
        ls::lazy<vk::TimelineSemaphore> syncSemaphore;

        ls::lazy<vk::CommandBuffer> renderCommandBuffer;
        ls::lazy<vk::Fence> renderFence;
        struct RenderPass {
            vk::CommandBuffer commandBuffer;
            vk::Semaphore acquireSemaphore;
        };
        std::vector<RenderPass> passes;
        std::vector<std::pair<vk::Semaphore, vk::Semaphore>> postCopySemaphores;

        ls::R<backend::Instance> instance;
        ls::owned_ptr<ls::R<backend::Context>> ctx;
        FrameState frameState;
        RecoveryState recoveryState;
        DiagnosticsState diagnosticsState;
        ColorTransitionState colorTransitionState;
        std::optional<AdaptiveScheduler> adaptiveScheduler;
        size_t configuredFixedGeneratedFrames{0};

        bool gamescopeDetected{false};
        // Immutable for this context; copied from SwapchainInfo rather than
        // inferred again from the current SDR/HDR colour pipeline.
        bool privateOrderedTransport{false};
        std::optional<uint32_t> gamescopeRefreshHz;
        FixedRefreshBudget fixedRefreshBudget;
        RealFramePacer realFramePacer;

        SwapchainColorPipeline colorPipeline;
        ls::GameConf profile;
        SwapchainInfo info;

        [[nodiscard]] bool applyPendingColorPipeline(const vk::Vulkan& vk);
        [[nodiscard]] static std::optional<uint64_t>
            generatedImageAcquireTimeoutNs();
        void rebuildPrivateResources(const vk::Vulkan& vk,
            SwapchainColorPipeline pipeline);
        void recordPresentCadence(
            std::chrono::steady_clock::time_point presentNow);
        [[nodiscard]] VkResult presentNativeFrame(
            const PresentInvocation& invocation);
        [[nodiscard]] VkResult presentOriginalImage(
            const PresentInvocation& invocation, VkSemaphore waitSemaphore,
            const void* nextChain);
        [[nodiscard]] bool recoverBackendIfReady(const vk::Vulkan& vk);
        void ensureHistoryWarmup();
        [[nodiscard]] PresentationFramePlan prepareFramePlan(
            std::chrono::steady_clock::time_point presentNow,
            bool gamescopeHdrTransport);
        void reportAdaptiveDelivery(const PresentationFramePlan& plan,
            size_t acceptedForPresentation);
        [[nodiscard]] bool generationPipelineReady(
            const vk::Vulkan& vk, bool gamescopeHdrTransport,
            const PresentationFramePlan& plan,
            std::chrono::steady_clock::time_point presentNow);
        [[nodiscard]] bool prepareRenderFence(const vk::Vulkan& vk);
        void handleRenderFenceBudgetMiss(
            const PresentationFramePlan& plan);
        void preacquireGeneratedImages(
            const PresentInvocation& invocation,
            PresentationFramePlan& plan);
        void submitSourceCopy(const PresentInvocation& invocation,
            VkImage swapchainImage, const vk::Image& sourceImage);
        [[nodiscard]] VkResult presentHistoryOnly(
            const PresentInvocation& invocation,
            const PresentationFramePlan& plan);
        [[nodiscard]] VkResult presentGeneratedFrames(
            const PresentInvocation& invocation,
            const PresentationFramePlan& plan,
            bool gamescopeHdrTransport);
        VkResult retireAcquiredImagesAndPresent(const vk::Vulkan& vk,
            VkQueue queue, VkSwapchainKHR swapchain, const void* nextChain,
            uint32_t originalImageIndex,
            std::span<const VkSemaphore> applicationWaitSemaphores,
            std::span<const uint32_t> acquiredImageIndices,
            VkImage originalImage);
    };

}
