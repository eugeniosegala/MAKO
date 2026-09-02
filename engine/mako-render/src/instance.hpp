/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-backend/mako.hpp"
#include "mako-common/configuration/config.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/pointers.hpp"
#include "mako-common/vulkan/vulkan.hpp"
#include "gamescope_hdr_feedback.hpp"
#include "runtime_transition.hpp"
#include "spatial_scaling_policy.hpp"
#include "swapchain.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <vulkan/vulkan_core.h>

namespace mako::layer {

    struct ConfigurationUpdateResult {
        bool reloaded{false};
        size_t liveContextsUpdated{0};
        size_t swapchainRecreationDeferredContexts{0};
        size_t processRestartDeferredContexts{0};
        size_t recreationRequestedContexts{0};
        bool processProfileChangeDeferred{false};
        bool globalChangeDeferred{false};
        bool hdrFeedbackChanged{false};
        size_t hdrContextsDeferred{0};
        bool refreshRateChanged{false};
    };

    struct SwapchainCreateModification {
        bool privateOrderedTransport{false};
        bool spatialScalingActive{false};
        bool variableSurface{false};
        bool variableFeedbackSuppressed{false};
        bool retainVariableSurfaceProof{false};
        bool spatialScalingMemoryConstrained{false};
        SpatialScalingInactiveReason spatialScalingInactiveReason{
            SpatialScalingInactiveReason::None
        };
        VkExtent2D applicationExtent{};
        VkExtent2D presentationExtent{};
        std::optional<VkExtent2D> gamescopePresentationTarget;
    };

    /// Root context of the MAKO Renderer layer.
    class Root {
    public:
        /// create the mako root context
        /// @throws ls::error on failure
        Root();

        /// check if the layer is active
        /// @return true if active
        [[nodiscard]] bool active() const { return this->active_profile.has_value(); }

        /// Whether this matched MAKO process provisioned frame-generation
        /// interop at device creation. Provisioning is independent from the
        /// live Frame Generation switch so Off can become On in place.
        [[nodiscard]] bool frameGenerationInteropProvisioned() const {
            return this->frameGenerationInteropProvisionedAtStartup;
        }
        /// Whether this process started with the Scaling Engine lane. Native
        /// Resolution is still provisioned: a later live scaler selection
        /// needs the same compute-capable application queue as an initially
        /// active scaler.
        [[nodiscard]] bool scalingEngineProvisioned() const {
            return this->active_profile &&
                this->active_profile->scaling_enabled &&
                spatialScalingProcessSupported(
                    this->gamescopeEnvironmentDetected,
                    this->gamescopeDetected,
                    this->presentationEnvironment.hdrExposureDisabled
                );
        }

        /// ensure the layer is up-to-date
        /// @param forceConfigurationPoll bypass the present-path polling
        /// interval at an application-owned swapchain creation boundary
        /// @return classification of any configuration update
        ConfigurationUpdateResult update(bool forceConfigurationPoll = false);

        /// modify instance create info
        /// @param createInfo original create info
        /// @param finish function to call after modification
        void modifyInstanceCreateInfo(VkInstanceCreateInfo& createInfo,
            const std::function<void(void)>& finish) const;
        /// modify device create info
        /// @param createInfo original create info
        /// @param swapchainMaintenance1Extension optional supported spelling
        /// @param finish function to call after modification
        void modifyDeviceCreateInfo(VkDeviceCreateInfo& createInfo,
            const char* swapchainMaintenance1Extension,
            const std::function<void(void)>& finish) const;

        /// modify swapchain create info
        /// @param vk vulkan instance
        /// @param createInfo original create info
        /// @param finish function to call after modification
        [[nodiscard]] SwapchainCreateModification modifySwapchainCreateInfo(
            const vk::Vulkan& vk, VkSwapchainCreateInfoKHR& createInfo,
            const std::optional<SpatialScalingExtents>&
                previousVariableExtents,
            const std::optional<SpatialScalingExtents>&
                variableSurfaceRollbackExtents,
            const std::optional<FixedSurfaceScalingContract>&
                fixedSurfaceContract,
            bool spatialSurfaceScalingSupported,
            const std::function<void(
                const FixedSurfaceScalingContract&)>& publishSpatialCreate,
            const std::function<void(void)>& finish) const;
        /// Replace a fixed native surface extent with the configured source
        /// extent after the lower driver has populated the capabilities.
        [[nodiscard]] std::optional<SpatialScalingCapabilitySelection>
        modifySurfaceCapabilities(
            VkSurfaceCapabilitiesKHR& capabilities) const;
        /// create swapchain context
        /// @param vk vulkan instance
        /// @param swapchain swapchain handle
        /// @param info swapchain info
        /// @throws ls::error on failure
        void createSwapchainContext(const vk::Vulkan& vk, VkSwapchainKHR swapchain,
            const SwapchainInfo& info,
            bool swapchainMaintenance1Enabled);
        /// get swapchain context
        /// @param swapchain swapchain handle
        /// @return swapchain context
        /// @throws ls::error if not found
        [[nodiscard]] Swapchain& getSwapchainContext(VkSwapchainKHR swapchain) {
            const auto& it = this->swapchains.find(swapchain);
            if (it == this->swapchains.end())
                throw ls::error("swapchain context not found");

            return it->second;
        }
        /// Remove a context from live configuration updates while preserving
        /// its GPU resources for bounded presentation-fence retirement.
        [[nodiscard]] std::optional<Swapchain> takeSwapchainContext(
            VkSwapchainKHR swapchain);
        /// remove swapchain context
        /// @param swapchain swapchain handle
        void removeSwapchainContext(VkSwapchainKHR swapchain);
    private:
        void publishSurfaceScalingPolicy() noexcept;
        [[nodiscard]] SpatialScalingPolicySnapshot
        surfaceScalingPolicySnapshot() const noexcept;

        ls::WatchedConfig config;
        std::optional<ls::GameConf> active_profile;
        bool frameGenerationInteropProvisionedAtStartup{false};
        bool scalingEngineConfiguredAtStartup{false};

        ls::lazy<backend::Instance> backend;
        std::optional<ls::GlobalConf> backendGlobal;
        std::optional<ls::GameConf> backendProfile;
        std::unordered_map<VkSwapchainKHR, Swapchain> swapchains;
        PresentationEnvironmentPolicy presentationEnvironment;
        bool gamescopeEnvironmentDetected{false};
        GamescopeHdrFeedbackReader hdrFeedbackReader;
        StableBooleanFeedback hdrFeedback;
        std::optional<bool> gamescopeHdrActive;
        bool gamescopeDetected{false};
        std::optional<VkExtent2D> gamescopePresentationTarget;
        std::optional<uint32_t> gamescopeRefreshHz;
        std::optional<bool> lastHdrFeedbackSample;
        std::string lastHdrActivationSource;
        std::optional<uint32_t> lastGamescopeRefreshHz;
        std::string lastHdrFeedbackDiagnosticKey;
        std::optional<std::chrono::steady_clock::time_point> lastHdrFeedbackPoll;
        std::optional<std::chrono::steady_clock::time_point> lastConfigurationPoll;
        uint64_t runtimeStateRevision{1};
        // A seqlock publishes packed flags + IEEE-754 factor as one coherent
        // policy snapshot. The even sequence divided by two is the revision
        // consumed by fixed-surface capability/create contracts.
        std::mutex surfaceScalingPolicyPublishMutex;
        std::atomic<uint64_t> surfaceScalingPolicySequence{0};
        std::atomic<uint64_t> surfaceScalingPolicy{0};
    };

}
