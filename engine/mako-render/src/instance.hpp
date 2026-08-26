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

#include <chrono>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

#include <vulkan/vulkan_core.h>

namespace mako::layer {

    struct ConfigurationUpdateResult {
        bool reloaded{false};
        size_t liveContextsUpdated{0};
        size_t deferredContexts{0};
        size_t processRestartContexts{0};
        size_t recreationRequestedContexts{0};
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
        VkExtent2D applicationExtent{};
        VkExtent2D presentationExtent{};
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

        /// Whether this process selected frame generation at device creation.
        /// Standalone scaling deliberately avoids negotiating the external
        /// memory/semaphore feature set used only by the LSFG backend.
        [[nodiscard]] bool frameGenerationConfigured() const {
            return this->frameGenerationConfiguredAtStartup;
        }
        [[nodiscard]] bool spatialScalingConfigured() const {
            return this->active_profile &&
                this->active_profile->scaling_enabled &&
                std::isfinite(this->active_profile->scaling_factor) &&
                this->active_profile->scaling_factor > 1.0F &&
                this->active_profile->scaling_factor <=
                    ls::GameConfLimits::maximumScalingFactor &&
                spatialScalingProcessSupported(
                    this->gamescopeEnvironmentDetected,
                    this->gamescopeDetected,
                    this->presentationEnvironment.hdrExposureDisabled
                );
        }

        /// ensure the layer is up-to-date
        /// @return classification of any configuration update
        ConfigurationUpdateResult update();

        /// modify instance create info
        /// @param createInfo original create info
        /// @param finish function to call after modification
        void modifyInstanceCreateInfo(VkInstanceCreateInfo& createInfo,
            const std::function<void(void)>& finish) const;
        /// modify device create info
        /// @param createInfo original create info
        /// @param finish function to call after modification
        void modifyDeviceCreateInfo(VkDeviceCreateInfo& createInfo,
            const std::function<void(void)>& finish) const;

        /// modify swapchain create info
        /// @param vk vulkan instance
        /// @param createInfo original create info
        /// @param finish function to call after modification
        [[nodiscard]] SwapchainCreateModification modifySwapchainCreateInfo(
            const vk::Vulkan& vk, VkSwapchainCreateInfoKHR& createInfo,
            const std::optional<SpatialScalingExtents>&
                previousVariableExtents,
            const std::function<void(void)>& finish) const;
        /// Replace a fixed native surface extent with the configured source
        /// extent after the lower driver has populated the capabilities.
        [[nodiscard]] bool modifySurfaceCapabilities(
            VkSurfaceCapabilitiesKHR& capabilities) const;
        /// create swapchain context
        /// @param vk vulkan instance
        /// @param swapchain swapchain handle
        /// @param info swapchain info
        /// @throws ls::error on failure
        void createSwapchainContext(const vk::Vulkan& vk, VkSwapchainKHR swapchain,
            const SwapchainInfo& info);
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
        /// remove swapchain context
        /// @param swapchain swapchain handle
        void removeSwapchainContext(VkSwapchainKHR swapchain);
    private:
        void publishSurfaceScalingPolicy() noexcept;

        ls::WatchedConfig config;
        std::optional<ls::GameConf> active_profile;
        bool frameGenerationConfiguredAtStartup{false};

        ls::lazy<backend::Instance> backend;
        std::unordered_map<VkSwapchainKHR, Swapchain> swapchains;
        PresentationEnvironmentPolicy presentationEnvironment;
        bool gamescopeEnvironmentDetected{false};
        GamescopeHdrFeedbackReader hdrFeedbackReader;
        StableBooleanFeedback hdrFeedback;
        std::optional<bool> gamescopeHdrActive;
        bool gamescopeDetected{false};
        std::optional<uint32_t> gamescopeRefreshHz;
        std::optional<bool> lastHdrFeedbackSample;
        std::string lastHdrActivationSource;
        std::optional<uint32_t> lastGamescopeRefreshHz;
        std::string lastHdrFeedbackDiagnosticKey;
        std::optional<std::chrono::steady_clock::time_point> lastHdrFeedbackPoll;
        std::optional<std::chrono::steady_clock::time_point> lastConfigurationPoll;
        uint64_t runtimeStateRevision{1};
        // Packed flags + IEEE-754 factor form one coherent, lock-free policy
        // snapshot for instance-level surface capability queries.
        std::atomic<uint64_t> surfaceScalingPolicy{0};
    };

}
