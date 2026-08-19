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
        bool gamescopeManaged, bool hdrExposureDisabled);

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
            bool gamescopeManaged, bool hdrExposureDisabled,
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
            return this->diagnosticsContextId;
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
        size_t idx{1};
        size_t fidx{0}; // real frame index
        size_t backendFrameIndex{0};
        bool renderFenceInFlight{false};
        GeneratedImageAdmission generatedImageAdmission;
        PipelineBusyRecovery pipelineBusyRecovery;
        bool backendRecoveryPending{false};
        std::optional<AdaptiveScheduler> adaptiveScheduler;
        std::vector<float> fixedFrameTimestamps;
        std::optional<std::chrono::steady_clock::time_point>
            fixedDiagnosticWindowStarted;
        size_t fixedDiagnosticRealFrames{0};
        size_t fixedDiagnosticGeneratedFrames{0};
        size_t fixedDiagnosticSkippedFrames{0};
        size_t configurationHistoryWarmupRemaining{0};
        uint64_t diagnosticsContextId{0};

        bool gamescopeManaged{false};
        // Immutable for this context; copied from SwapchainInfo rather than
        // inferred again from the current SDR/HDR colour pipeline.
        bool privateOrderedTransport{false};
        std::optional<uint32_t> gamescopeRefreshHz;
        std::optional<bool> pendingGamescopeHdrActive;
        uint64_t pendingHdrStateRevision{0};
        std::optional<std::chrono::steady_clock::time_point>
            colorTransitionRetryAt;
        FixedRefreshBudget fixedRefreshBudget;
        RealFramePacer realFramePacer;
        std::optional<std::chrono::steady_clock::time_point> lastPresentStarted;
        std::optional<std::chrono::steady_clock::duration> recentRealInterval;

        SwapchainColorPipeline colorPipeline;
        ls::GameConf profile;
        SwapchainInfo info;

        [[nodiscard]] bool applyPendingColorPipeline(const vk::Vulkan& vk);
        void rebuildPrivateResources(const vk::Vulkan& vk,
            SwapchainColorPipeline pipeline);
        VkResult retireAcquiredImagesAndPresent(const vk::Vulkan& vk,
            VkQueue queue, VkSwapchainKHR swapchain, const void* nextChain,
            uint32_t originalImageIndex,
            std::span<const VkSemaphore> applicationWaitSemaphores,
            std::span<const uint32_t> acquiredImageIndices,
            VkImage originalImage);
    };

}
