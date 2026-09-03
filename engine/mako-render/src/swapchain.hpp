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
#include "generated_frame_delivery.hpp"
#include "profile_update.hpp"
#include "presentation_policy.hpp"
#include "runtime_status.hpp"
#include "runtime_transition.hpp"
#include "spatial_scaler.hpp"
#include "spatial_scaling_policy.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
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
        VkSurfaceKHR surface;
        VkFormat format;
        VkColorSpaceKHR colorSpace;
        // Preserve the application's create signature independently from
        // MAKO's lower-facing usage and present-mode changes. The startup
        // compatibility policy must not leak across different WSI contracts.
        VkImageUsageFlags applicationImageUsage{0};
        VkPresentModeKHR applicationPresentMode{VK_PRESENT_MODE_FIFO_KHR};
        // Preserve the application's requested minimum separately from the
        // bounded minimum forwarded by MAKO and the actual returned count in
        // `images`. These values make startup compatibility reports
        // actionable without changing presentation behavior.
        uint32_t requestedMinImageCount{0};
        uint32_t provisionedMinImageCount{0};
        // Preserve whether the application supplied the Vulkan replacement
        // lineage explicitly. A short-lived swapchain in an oldSwapchain
        // chain is ordinary application-owned recreation, not evidence that
        // MAKO's image-count reservation prevented startup.
        bool applicationOldSwapchainProvided{false};
        // Extent the application requested after MAKO virtualized fixed WSI
        // capabilities. The actual swapchain images retain `extent`.
        VkExtent2D applicationExtent;
        VkExtent2D extent;
        // Proven Gamescope output geometry used to cap ordinary scaling and
        // to expose the live non-supersampling slider ceiling.
        std::optional<VkExtent2D> gamescopePresentationTarget;
        VkPresentModeKHR presentMode;
        // Persist the exact create-time transport decision. HDR feedback can
        // change later, but Vulkan present mode/pNext compatibility cannot be
        // reinterpreted without replacing the game-owned swapchain.
        bool privateOrderedTransport{false};
        bool spatialScalingActive{false};
        // Variable WSI surfaces retain the source extent and select a larger
        // lower presentation envelope. Factor edits which remain above an
        // already active ceiling can therefore be applied without replacing
        // the game-owned swapchain.
        bool variableSurface{false};
        // True when scaling remains active at a lower effective factor because
        // the requested presentation extent exceeded the create-time memory
        // admission envelope.
        bool spatialScalingMemoryConstrained{false};
        // Exact create-time reason for retaining native geometry. Keep this
        // independent from activation support: memory and output-headroom
        // guards are actionable on an otherwise supported surface.
        SpatialScalingInactiveReason spatialScalingInactiveReason{
            SpatialScalingInactiveReason::None
        };
        // False only when the lower split role proved that Gamescope WSI did
        // not own this surface. No live factor can make that surface safe, so
        // scaler choices remain dormant until a natural surface boundary.
        bool spatialScalingActivationSupported{true};
        // The upper WSI either supplied a live oldSwapchain handle or MAKO
        // completed retirement of the exact retained lower handle before a
        // null-old destroy-before-create replacement. This is a known
        // owner-driven replacement rather than a cold process start, so
        // Adaptive may use its bounded recovery guard.
        bool replacement{false};
    };

    /// modify the swapchain create info based on the profile pre-swapchain creation
    /// @param profile active game profile
    /// @param maxImages maximum number of images supported by the surface
    /// @param createInfo swapchain create info to modify
    [[nodiscard]] bool context_ModifySwapchainCreateInfo(
        const ls::GameConf& profile, uint32_t maxImages,
        VkSwapchainCreateInfoKHR& createInfo, bool gamescopeHdrActive,
        bool gamescopeDetected,
        const PresentationEnvironmentPolicy& presentationEnvironment,
        bool frameGenerationInteropEnabled,
        bool spatialScalingActive);

    /// swapchain context for a layer instance
    class Swapchain {
    public:
        /// create a new swapchain context
        /// @param vk vulkan instance
        /// @param backend optional MAKO frame-generation backend; scaling is
        /// independent and remains available when this is null
        /// @param profile active game profile
        /// @param info swapchain info
        Swapchain(const vk::Vulkan& vk, backend::Instance* backend,
            ls::GameConf profile, SwapchainInfo info,
            std::optional<std::filesystem::path> scalingShaderDll,
            std::optional<bool> gamescopeHdrActive,
            bool gamescopeDetected, bool hdrExposureDisabled,
            std::optional<uint32_t> gamescopeRefreshHz,
            uint64_t runtimeStateRevision,
            bool swapchainMaintenance1Enabled);

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
        [[nodiscard]] bool spatialScalingActive() const {
            return this->spatialScaler.has_value();
        }
        [[nodiscard]] VkExtent2D frameGenerationResourceExtent() const {
            return frameGenerationExtent(
                this->spatialFramePipelinePlacement,
                this->info.applicationExtent, this->info.extent
            );
        }
        [[nodiscard]] SpatialFramePipelinePlacement
        framePipelinePlacement() const {
            return this->spatialFramePipelinePlacement;
        }

        /// Apply configuration that is safe for an already-created context.
        /// Spatial model and sharpness changes rebuild MAKO's private scaler
        /// at the next presentation boundary. The lower managed spatial role
        /// may request one game-owned recreation for an extent change after a
        /// retirement-fenced present; unsupported resource-shape changes wait
        /// for a natural recreation. Process-wide backend changes wait for
        /// restart. The decision reports every applied and pending boundary.
        [[nodiscard]] ProfileUpdateDecision updateProfile(
            const ls::GameConf& profile, uint64_t runtimeStateRevision,
            const ls::GameConf* requestedProfile = nullptr,
            bool processRestartPending = false);

        /// After the lower WSI has consumed this present's wait semaphores,
        /// convert one successful result into VK_ERROR_OUT_OF_DATE_KHR for a
        /// pending live profile-resource change. The application remains the
        /// owner of destruction and recreation.
        [[nodiscard]] bool requestLiveProfileResourceRecreationAfterPresent(
            VkResult lowerPresentResult);

        /// Wait for every layer-owned maintenance1 present fence associated
        /// with this swapchain. A zero timeout is a nonblocking retirement
        /// poll; finite waits are used only at application destruction.
        [[nodiscard]] bool waitForPresentRetirement(
            const vk::Vulkan& vk, uint64_t timeoutNs);
        [[nodiscard]] bool presentRetirementEnabled() const {
            return !this->presentRetirementFences.empty();
        }
        [[nodiscard]] size_t completedApplicationPresentCount() const {
            return this->frameState.realFrameIndex;
        }
        [[nodiscard]] std::chrono::steady_clock::time_point
        compatibilityObservationStartedAt() const {
            return this->compatibilityObservationStarted;
        }

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
            // and headroom-tight ordered SDR may contain fewer, deliberately
            // re-spaced timestamps.
            GeneratedFramePlan scheduledGeneratedFrames;
            size_t admittedGeneratedFrameCount{0};
            bool historyWarmupActive{false};
            bool generatedImagesPreacquired{false};
            // Recovery probes are transport-owned synthetic delivery. Keep
            // them out of Adaptive ramp and Smooth Cadence qualification even
            // when an isolated guard clears before delivery is reported.
            bool orderedAcquireRecoveryProbe{false};
            bool boundedOrderedAcquireProbe{false};
            std::optional<uint64_t> configuredAcquireTimeout;
            // Opt-in diagnostic phase accounting stays inline with the frame
            // plan so a slow total can expose multiple sub-threshold waits
            // without allocating or emitting one record per phase.
            std::chrono::steady_clock::duration renderFenceWaitDuration{};
            std::chrono::steady_clock::duration preacquireDuration{};
            std::chrono::steady_clock::duration scheduleFramesDuration{};
            std::chrono::steady_clock::duration submitSourceCopyDuration{};
            std::array<uint32_t, GeneratedFramePlan::capacity>
                preacquiredGeneratedImages{};
        };

        struct FrameState {
            size_t sequenceIndex{1};
            // The backend timeline is reset only when MAKO commits a new
            // private context. Diagnostics and binary-semaphore ring indices
            // remain monotonic across that transition.
            size_t backendTimelineIndex{1};
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
            OrderedAcquireRecovery orderedAcquireRecovery;
            LowerPresentStallRecovery lowerPresentStallRecovery;
            PipelineBusyRecovery pipelineBusyRecovery;
            FixedCadenceCollapseRecovery fixedCadenceCollapseRecovery;
            ReplacementBackendStabilization replacementBackendStabilization;
            bool backendPending{false};
            size_t historyWarmupRemaining{0};
        };

        std::chrono::steady_clock::time_point compatibilityObservationStarted{};

        struct DiagnosticsState {
            std::optional<std::chrono::steady_clock::time_point>
                fixedWindowStarted;
            size_t fixedRealFrames{0};
            size_t fixedGeneratedFrames{0};
            size_t fixedSkippedFrames{0};
            uint64_t contextId{0};
            bool orderedAcquireClassificationSplitLogged{false};
            bool orderedGeneratedAdmissionPolicyLogged{false};
        };

        struct ColorTransitionState {
            std::optional<bool> pendingGamescopeHdrActive;
            uint64_t pendingHdrStateRevision{0};
            std::optional<std::chrono::steady_clock::time_point> retryAt;
        };

        struct SpatialResourceRequest {
            ls::ScalingMethod method{ls::ScalingMethod::Native};
            float sharpness{0.5F};

            friend bool operator==(
                    const SpatialResourceRequest& left,
                    const SpatialResourceRequest& right) {
                return left.method == right.method &&
                    left.sharpness == right.sharpness;
            }
        };

        struct FrameGenerationResourceRequest {
            ls::GameConf profile;

            friend bool operator==(
                    const FrameGenerationResourceRequest& left,
                    const FrameGenerationResourceRequest& right) {
                return ls::effectiveFlowScale(left.profile) ==
                        ls::effectiveFlowScale(right.profile) &&
                    ls::effectivePerformanceMode(left.profile) ==
                        ls::effectivePerformanceMode(right.profile) &&
                    generatedFrameCapacityForProfile(left.profile) ==
                        generatedFrameCapacityForProfile(right.profile);
            }
        };

        struct FrameGenerationResources {
            // Context must be destroyed before the exported Vulkan resources
            // it imports. Members are destroyed in reverse declaration order.
            std::vector<vk::Image> sourceImages;
            std::vector<vk::Image> destinationImages;
            ls::lazy<vk::TimelineSemaphore> syncSemaphore;
            ls::owned_ptr<ls::R<backend::Context>> context;
        };

        struct RuntimeStatusState {
            ls::GameConf requestedProfile;
            uint64_t stateRevision{0};
            bool swapchainRecreationPending{false};
            bool processRestartPending{false};
            std::optional<std::string> error;
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

        struct SpatialScalingPass {
            vk::CommandBuffer commandBuffer;
            vk::Semaphore readySemaphore;
            vk::Fence completionFence;
            bool completionInFlight{false};
        };
        std::optional<SpatialScaler> spatialScaler;
        std::vector<SpatialScalingPass> spatialScalingPasses;
        SpatialFramePipelinePlacement spatialFramePipelinePlacement{
            SpatialFramePipelinePlacement::PreFrameGeneration
        };

        struct PresentRetirementFence {
            vk::Fence fence;
            bool associated{false};
            bool used{false};
        };
        std::vector<PresentRetirementFence> presentRetirementFences;
        bool lastLowerPresentRetirementProtected{false};
        bool presentRetirementBusyLogged{false};
        bool externalPresentFenceLogged{false};

        backend::Instance* instance{nullptr};
        ls::owned_ptr<ls::R<backend::Context>> ctx;
        FrameState frameState;
        RecoveryState recoveryState;
        DiagnosticsState diagnosticsState;
        ColorTransitionState colorTransitionState;
        PrivateResourceTransition<SpatialResourceRequest> spatialTransition;
        std::optional<SpatialScaler> preparedSpatialScaler;
        PrivateResourceTransition<FrameGenerationResourceRequest>
            frameGenerationTransition;
        std::optional<FrameGenerationResources>
            preparedFrameGenerationResources;
        RuntimeStatusPublisher runtimeStatusPublisher;
        RuntimeStatusState runtimeStatusState;
        LiveProfileResourceRecreation liveProfileResourceRecreation;
        std::optional<AdaptiveScheduler> adaptiveScheduler;
        size_t configuredFixedGeneratedFrames{0};

        bool gamescopeDetected{false};
        // Immutable for this context; copied from SwapchainInfo rather than
        // inferred again from the current SDR/HDR colour pipeline.
        bool privateOrderedTransport{false};
        std::optional<uint32_t> gamescopeRefreshHz;
        FixedRefreshBudget fixedRefreshBudget;
        RealFramePacer realFramePacer;
        SmoothCadenceBaseCap smoothCadenceBaseCap;
        SmoothCadencePacerHandoff smoothCadencePacerHandoff;

        SwapchainColorPipeline colorPipeline;
        std::optional<std::filesystem::path> scalingShaderDll;
        ls::GameConf profile;
        SwapchainInfo info;

        [[nodiscard]] bool applyPendingColorPipeline(const vk::Vulkan& vk);
        [[nodiscard]] bool applyPendingFrameGenerationResources(
            const vk::Vulkan& vk);
        void applyPendingSpatialScaler(const vk::Vulkan& vk);
        void prepareSpatialScalingPass(const vk::Vulkan& vk,
            SpatialScalingPass& pass);
        [[nodiscard]] bool spatialScalingPassesReady(
            const vk::Vulkan& vk);
        void configureDirectSpatialFrameGenerationOutputs(
            const vk::Vulkan& vk, SpatialScaler& scaler,
            std::span<const vk::Image> outputs,
            std::string_view reason) noexcept;
        void clearDirectSpatialFrameGenerationOutputs() noexcept;
        void publishRuntimeStatus(std::string_view reason) noexcept;
        [[nodiscard]] static std::optional<uint64_t>
            generatedImageAcquireTimeoutNs();
        void ensureFrameGenerationExecutionResources(const vk::Vulkan& vk);
        [[nodiscard]] FrameGenerationResources buildFrameGenerationResources(
            const vk::Vulkan& vk, const SwapchainColorPipeline& pipeline,
            const ls::GameConf& resourceProfile);
        void commitFrameGenerationResources(
            FrameGenerationResources resources,
            const ls::GameConf& resourceProfile,
            std::string_view reason);
        void rebuildPrivateResources(const vk::Vulkan& vk,
            SwapchainColorPipeline pipeline,
            const ls::GameConf& resourceProfile);
        [[nodiscard]] bool resetGenerationScheduler(
            std::chrono::steady_clock::time_point now,
            std::string_view reason);
        void recordPresentCadence(
            std::chrono::steady_clock::time_point presentNow);
        void observeLowerPresentHealth(
            std::chrono::steady_clock::duration presentDuration,
            std::string_view source,
            size_t requestedGenerated = 0,
            size_t presentedGenerated = 0);
        [[nodiscard]] VkResult presentNativeFrame(
            const PresentInvocation& invocation);
        [[nodiscard]] VkResult presentSpatiallyScaledFrame(
            const PresentInvocation& invocation);
        [[nodiscard]] VkResult presentOriginalImage(
            const PresentInvocation& invocation, VkSemaphore waitSemaphore,
            const void* nextChain,
            std::chrono::steady_clock::duration* duration = nullptr);
        [[nodiscard]] VkResult queuePresentWithRetirementFence(
            const vk::Vulkan& vk, VkQueue queue,
            const VkPresentInfoKHR& presentInfo);
        [[nodiscard]] bool recoverBackendIfReady(const vk::Vulkan& vk);
        void ensureHistoryWarmup();
        [[nodiscard]] PresentationFramePlan prepareFramePlan(
            std::chrono::steady_clock::time_point presentNow,
            bool orderedAcquireRecoveryProbe);
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
            PresentationFramePlan& plan,
            bool trackNonblockingAdmission,
            uint64_t acquireTimeout);
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
