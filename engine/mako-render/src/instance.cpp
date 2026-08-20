/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "instance.hpp"
#include "device_selection.hpp"
#include "mako-common/helpers/paths.hpp"
#include "swapchain.hpp"
#include "mako-common/configuration/detection.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/vulkan/vulkan.hpp"
#include "pnext_chain.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <stdlib.h>
#include <vulkan/vulkan_core.h>

using namespace mako;
using namespace mako::layer;

#ifndef MAKO_BUILD_VERSION
#define MAKO_BUILD_VERSION "unknown"
#endif

namespace {
    constexpr char makoBuildIdentity[] =
        "MAKO Renderer: render layer active; identity="
        "VK_LAYER_MAKO_render; build="
        MAKO_BUILD_VERSION;

    std::string toHexId(const uint32_t id) {
        constexpr char digits[] = "0123456789ABCDEF";
        std::string value = "0x0000";
        value.at(2) = digits[(id >> 12U) & 0xFU];
        value.at(3) = digits[(id >> 8U) & 0xFU];
        value.at(4) = digits[(id >> 4U) & 0xFU];
        value.at(5) = digits[id & 0xFU];
        return value;
    }

    PhysicalDeviceIdentity identifyApplicationDevice(const vk::Vulkan& vk) {
        const auto& funcs = vk.fi();
        const auto physicalDevice = vk.physdev();

        uint32_t extensionCount{};
        funcs.EnumerateDeviceExtensionProperties(
            physicalDevice, nullptr, &extensionCount, VK_NULL_HANDLE
        );
        std::vector<VkExtensionProperties> extensions(extensionCount);
        funcs.EnumerateDeviceExtensionProperties(
            physicalDevice, nullptr, &extensionCount, extensions.data()
        );
        const bool hasPciInfo = std::ranges::find_if(
            extensions,
            [](const VkExtensionProperties& extension) {
                return std::string(extension.extensionName)
                    == VK_EXT_PCI_BUS_INFO_EXTENSION_NAME;
            }
        ) != extensions.end();

        VkPhysicalDevicePCIBusInfoPropertiesEXT pciInfo{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT
        };
        VkPhysicalDeviceProperties2 properties{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = hasPciInfo ? &pciInfo : nullptr
        };
        funcs.GetPhysicalDeviceProperties2(physicalDevice, &properties);

        std::array<char, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE> deviceName{};
        std::copy_n(
            properties.properties.deviceName,
            deviceName.size() - 1,
            deviceName.begin()
        );

        return {
            .name = deviceName.data(),
            .vendorId = toHexId(properties.properties.vendorID),
            .deviceId = toHexId(properties.properties.deviceID),
            .pci = hasPciInfo
                ? std::optional<std::string>{
                    std::to_string(pciInfo.pciBus) + ":" +
                    std::to_string(pciInfo.pciDevice) + "." +
                    std::to_string(pciInfo.pciFunction)
                }
                : std::nullopt,
        };
    }

    class ScopedEnvironmentOverride {
    public:
        ScopedEnvironmentOverride(std::string name, const char* value) :
                name(std::move(name)) {
            if (const char* current = std::getenv(this->name.c_str()))
                this->previousValue = current;

            if (setenv(this->name.c_str(), value, 1) != 0)
                throw ls::error("unable to set environment override: " + this->name);
        }

        ~ScopedEnvironmentOverride() {
            if (this->previousValue)
                static_cast<void>(setenv(
                    this->name.c_str(), this->previousValue->c_str(), 1
                ));
            else
                static_cast<void>(unsetenv(this->name.c_str()));
        }

        ScopedEnvironmentOverride(const ScopedEnvironmentOverride&) = delete;
        ScopedEnvironmentOverride& operator=(const ScopedEnvironmentOverride&) = delete;
        ScopedEnvironmentOverride(ScopedEnvironmentOverride&&) = delete;
        ScopedEnvironmentOverride& operator=(ScopedEnvironmentOverride&&) = delete;

    private:
        std::string name;
        std::optional<std::string> previousValue;
    };

    bool presentDiagnosticsEnabled() {
        const char* value = std::getenv("MAKO_PRESENT_DIAGNOSTICS");
        return value && std::string(value) != "0";
    }

    std::string hdrFeedbackDiagnosticKey(
            const GamescopeHdrFeedbackSample& sample) {
        return sample.status + '\n' + sample.display + '\n' +
            sample.activationSource + '\n' +
            sample.resolverStatus + '\n' + sample.resolverCandidates + '\n' +
            (sample.active
                ? (*sample.active ? "active" : "inactive") : "unknown") + '\n' +
            std::to_string(sample.gamescopePid.value_or(UINT32_MAX)) + '\n' +
            std::to_string(sample.xwaylandServerId.value_or(UINT32_MAX)) + '\n' +
            std::to_string(sample.refreshHz.value_or(0)) + '\n' +
            (sample.outputHdrEnabled
                ? (*sample.outputHdrEnabled ? "output-hdr" : "output-sdr")
                : "output-unknown") + '\n' +
            (sample.appHdrMetadataPresent ? "metadata" : "no-metadata");
    }

    void logHdrFeedbackDiagnostic(
            const char* prefix,
            const GamescopeHdrFeedbackSample& sample) {
        std::cerr << prefix << sample.status
                  << "; display="
                  << (sample.display.empty() ? "(unset)" : sample.display)
                  << "; resolver="
                  << (sample.resolverStatus.empty()
                        ? "(unset)" : sample.resolverStatus)
                  << "; gamescope_pid="
                  << sample.gamescopePid.value_or(UINT32_MAX)
                  << "; server_id="
                  << sample.xwaylandServerId.value_or(UINT32_MAX)
                  << "; refresh_hz="
                  << sample.refreshHz.value_or(0)
                  << "; active=";
        if (sample.active)
            std::cerr << (*sample.active ? 1 : 0);
        else
            std::cerr << "unknown";
        std::cerr
                  << "; activation_source="
                  << (sample.activationSource.empty()
                        ? "unavailable" : sample.activationSource)
                  << "; output_hdr=";
        if (sample.outputHdrEnabled)
            std::cerr << (*sample.outputHdrEnabled ? 1 : 0);
        else
            std::cerr << "unknown";
        std::cerr
                  << "; app_hdr_metadata="
                  << (sample.appHdrMetadataPresent ? 1 : 0)
                  << "; candidates="
                  << (sample.resolverCandidates.empty()
                        ? "(none)" : sample.resolverCandidates)
                  << '\n';
    }

    /// helper function to add required extensions
    std::vector<const char*> add_extensions(const char* const* existingExtensions, size_t count,
            const std::vector<const char*>& requiredExtensions) {
        std::vector<const char*> extensions(count);
        std::copy_n(existingExtensions, count, extensions.data());

        for (const auto& requiredExtension : requiredExtensions) {
            auto it = std::ranges::find_if(extensions,
                [requiredExtension](const char* extension) {
                    return std::string(extension) == std::string(requiredExtension);
                });
            if (it == extensions.end())
                extensions.push_back(requiredExtension);
        }

        return extensions;
    }
}

Root::Root() :
    presentationEnvironment(resolvePresentationEnvironmentPolicy(
        std::getenv("MAKO_DISABLE_HDR_EXPOSURE"),
        std::getenv("DXVK_HDR"),
        std::getenv("DISABLE_GAMESCOPE_WSI")
    )),
    hdrFeedbackReader(this->presentationEnvironment) {
    std::cerr << makoBuildIdentity << '\n';

    std::cerr << "MAKO Renderer: presentation policy: gamescope_wsi="
              << (this->presentationEnvironment.gamescopeWsiDisabled
                    ? "isolated" : "allowed")
              << "; hdr_exposure="
              << (this->presentationEnvironment.hdrExposureDisabled
                    ? "disabled" : "allowed")
              << '\n';

    const auto initialHdrFeedback =
        this->hdrFeedbackReader.diagnosticSample();
    this->lastHdrFeedbackSample = initialHdrFeedback.active;
    this->lastHdrActivationSource = initialHdrFeedback.activationSource;
    this->gamescopeDetected = initialHdrFeedback.gamescopeDetected;
    this->lastGamescopeRefreshHz = initialHdrFeedback.refreshHz;
    this->gamescopeRefreshHz = initialHdrFeedback.refreshHz;
    this->lastHdrFeedbackDiagnosticKey = hdrFeedbackDiagnosticKey(
        initialHdrFeedback
    );
    // Gamescope's per-application root properties can still describe the
    // previous held commit while a new process creates its first swapchain, so
    // those values retain the settling window. Output HDR capability is logged
    // separately and never promoted to application HDR intent.
    this->gamescopeHdrActive = initialGamescopeHdrActivation(
        initialHdrFeedback
    );
    this->hdrFeedback.seed(this->gamescopeHdrActive);
    this->lastHdrFeedbackPoll = std::chrono::steady_clock::now();
    if (this->gamescopeHdrActive) {
        std::cerr << "MAKO Renderer: Gamescope application HDR feedback initialized: active="
                  << *this->gamescopeHdrActive
                  << "; display=" << initialHdrFeedback.display
                  << "; refresh_hz="
                  << initialHdrFeedback.refreshHz.value_or(0)
                  << "; activation_source="
                  << (initialHdrFeedback.activationSource.empty()
                        ? "unavailable" : initialHdrFeedback.activationSource)
                  << '\n';
    } else {
        std::cerr << "MAKO Renderer: Gamescope application HDR feedback provisional; "
                     "normalized 10-bit swapchains use real-frame passthrough until confirmed; "
                  << "reason=" << initialHdrFeedback.status
                  << " display="
                  << (initialHdrFeedback.display.empty()
                        ? "(unset)" : initialHdrFeedback.display)
                  << "; resolver=" << initialHdrFeedback.resolverStatus
                  << "; activation_source="
                  << (initialHdrFeedback.activationSource.empty()
                        ? "unavailable" : initialHdrFeedback.activationSource)
                  << "; output_hdr=";
        if (initialHdrFeedback.outputHdrEnabled)
            std::cerr << (*initialHdrFeedback.outputHdrEnabled ? 1 : 0);
        else
            std::cerr << "unknown";
        std::cerr << "; app_hdr_metadata="
                  << (initialHdrFeedback.appHdrMetadataPresent ? 1 : 0)
                  << "; gamescope_pid="
                  << initialHdrFeedback.gamescopePid.value_or(UINT32_MAX)
                  << "; server_id="
                  << initialHdrFeedback.xwaylandServerId.value_or(UINT32_MAX)
                  << "; candidates="
                  << (initialHdrFeedback.resolverCandidates.empty()
                        ? "(none)" : initialHdrFeedback.resolverCandidates)
                  << '\n';
    }

    // find active profile
    const auto& profile = findProfile(this->config.get(), ls::identify());
    if (!profile.has_value())
        return;

    this->active_profile = profile->second;

    std::cerr << "MAKO Renderer: using profile with name '" << this->active_profile->name << "' ";
    switch (profile->first) {
        case ls::IdentType::OVERRIDE:
            std::cerr << "(identified via override)\n";
            break;
        case ls::IdentType::EXECUTABLE:
            std::cerr << "(identified via executable)\n";
            break;
        case ls::IdentType::WINE_EXECUTABLE:
            std::cerr << "(identified via wine executable)\n";
            break;
        case ls::IdentType::PROCESS_NAME:
            std::cerr << "(identified via process name)\n";
            break;
        case ls::IdentType::FALLBACK:
            std::cerr << "(identified via fallback)\n";
            break;
    }
}

ConfigurationUpdateResult Root::update() {
    ConfigurationUpdateResult result;
    const auto now = std::chrono::steady_clock::now();
    constexpr auto hdrFeedbackPollInterval = std::chrono::milliseconds(250);
    if (!this->lastHdrFeedbackPoll ||
            now - *this->lastHdrFeedbackPoll >= hdrFeedbackPollInterval) {
        const auto hdrFeedbackSample =
            this->hdrFeedbackReader.diagnosticSample();
        this->lastHdrFeedbackSample = hdrFeedbackSample.active;
        this->lastHdrActivationSource = hdrFeedbackSample.activationSource;
        this->gamescopeDetected = hdrFeedbackSample.gamescopeDetected;
        if (hdrFeedbackSample.refreshHz != this->lastGamescopeRefreshHz) {
            this->lastGamescopeRefreshHz = hdrFeedbackSample.refreshHz;
            this->gamescopeRefreshHz = hdrFeedbackSample.refreshHz;
            result.refreshRateChanged = true;
            for (auto& [swapchain, context] : this->swapchains) {
                static_cast<void>(swapchain);
                context.updateGamescopeRefreshRate(this->gamescopeRefreshHz);
            }
        }
        const auto diagnosticKey = hdrFeedbackDiagnosticKey(hdrFeedbackSample);
        if (diagnosticKey != this->lastHdrFeedbackDiagnosticKey) {
            this->lastHdrFeedbackDiagnosticKey = diagnosticKey;
            logHdrFeedbackDiagnostic(
                "MAKO Renderer: Gamescope application HDR feedback status: ",
                hdrFeedbackSample
            );
        }
        this->lastHdrFeedbackPoll = now;
    }

    if (const auto changed = this->hdrFeedback.observe(
            this->lastHdrFeedbackSample, now)) {
        this->gamescopeHdrActive = changed;
        this->runtimeStateRevision++;
        result.hdrFeedbackChanged = true;
        for (auto& [swapchain, context] : this->swapchains) {
            static_cast<void>(swapchain);
            if (context.updateGamescopeHdrState(
                    *changed, this->runtimeStateRevision))
                result.hdrContextsDeferred++;
        }
        std::cerr << "MAKO Renderer: Gamescope application HDR feedback stabilized: active="
                  << *changed
                  << "; activation_source="
                  << (this->lastHdrActivationSource.empty()
                        ? "unavailable" : this->lastHdrActivationSource)
                  << "; contexts_pending_private_transition="
                  << result.hdrContextsDeferred << '\n';
    }

    // Configuration hot reload does not need a filesystem metadata query for
    // every presented frame. Keep the UI responsive while bounding the check
    // to four times per second, matching the feedback sampling cadence.
    constexpr auto configurationPollInterval = std::chrono::milliseconds(250);
    if (this->lastConfigurationPoll &&
            now - *this->lastConfigurationPoll < configurationPollInterval)
        return result;
    this->lastConfigurationPoll = now;

    const auto previousGlobal = this->config.get().global();
    if (!this->config.update())
        return result;

    result.reloaded = true;
    this->runtimeStateRevision++;
    const auto& currentGlobal = this->config.get().global();
    result.globalChangeDeferred =
        previousGlobal.dll != currentGlobal.dll ||
        previousGlobal.allow_fp16 != currentGlobal.allow_fp16;

    const auto previousProfileName = this->active_profile
        ? std::optional<std::string>{this->active_profile->name}
        : std::nullopt;
    const auto& profile = findProfile(this->config.get(), ls::identify());
    if (profile.has_value())
        this->active_profile = profile->second;
    else
        this->active_profile = std::nullopt;

    const auto currentProfileName = this->active_profile
        ? std::optional<std::string>{this->active_profile->name}
        : std::nullopt;
    if (previousProfileName != currentProfileName) {
        std::cerr << "MAKO Renderer: live profile selection changed: previous='"
                  << previousProfileName.value_or("(none)")
                  << "' current='"
                  << currentProfileName.value_or("(none)") << "'";
        if (profile.has_value()) {
            switch (profile->first) {
                case ls::IdentType::OVERRIDE:
                    std::cerr << " source=override";
                    break;
                case ls::IdentType::EXECUTABLE:
                    std::cerr << " source=executable";
                    break;
                case ls::IdentType::WINE_EXECUTABLE:
                    std::cerr << " source=wine-executable";
                    break;
                case ls::IdentType::PROCESS_NAME:
                    std::cerr << " source=process-name";
                    break;
                case ls::IdentType::FALLBACK:
                    std::cerr << " source=fallback";
                    break;
            }
        }
        std::cerr << '\n';
    }

    if (this->active_profile) {
        for (auto& [swapchain, context] : this->swapchains) {
            static_cast<void>(swapchain);
            switch (context.updateProfile(
                    *this->active_profile, this->runtimeStateRevision)) {
                case ProfileUpdateAction::NoRuntimeChange:
                    break;
                case ProfileUpdateAction::ApplyLive:
                    result.liveContextsUpdated++;
                    break;
                case ProfileUpdateAction::DeferUntilSwapchainRecreation:
                    result.deferredContexts++;
                    break;
            }
        }
    } else {
        // Losing the active profile must stop generation immediately, but it
        // does not require destroying any in-flight Vulkan resources.
        for (auto& [swapchain, context] : this->swapchains) {
            static_cast<void>(swapchain);
            context.disableFrameGeneration();
            result.liveContextsUpdated++;
        }
    }

    return result;
}

void Root::modifyInstanceCreateInfo(VkInstanceCreateInfo& createInfo,
        const std::function<void(void)>& finish) const {
    if (!this->active_profile.has_value()) {
        finish();
        return;
    }

    auto extensions = add_extensions(
        createInfo.ppEnabledExtensionNames,
        createInfo.enabledExtensionCount,
        {
            "VK_KHR_get_physical_device_properties2",
            "VK_KHR_external_memory_capabilities",
            "VK_KHR_external_semaphore_capabilities"
        }
    );
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    finish();
}

void Root::modifyDeviceCreateInfo(VkDeviceCreateInfo& createInfo,
        const std::function<void(void)>& finish) const {
    if (!this->active_profile.has_value()) {
        finish();
        return;
    }

    auto extensions = add_extensions(
        createInfo.ppEnabledExtensionNames,
        createInfo.enabledExtensionCount,
        {
            "VK_KHR_external_memory",
            "VK_KHR_external_memory_fd",
            "VK_KHR_external_semaphore",
            "VK_KHR_external_semaphore_fd",
            "VK_KHR_timeline_semaphore"
        }
    );
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    bool isFeatureEnabled = false;
    auto* featureInfo = reinterpret_cast<VkBaseInStructure*>(const_cast<void*>(createInfo.pNext));
    while (featureInfo) {
        if (featureInfo->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES) {
            auto* features = reinterpret_cast<VkPhysicalDeviceVulkan12Features*>(featureInfo);
            features->timelineSemaphore = VK_TRUE;
            isFeatureEnabled = true;
        } else if (featureInfo->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES) {
            auto* features = reinterpret_cast<VkPhysicalDeviceTimelineSemaphoreFeatures*>(featureInfo);
            features->timelineSemaphore = VK_TRUE;
            isFeatureEnabled = true;
        }

        featureInfo = const_cast<VkBaseInStructure*>(featureInfo->pNext);
    }

    VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
        .pNext = const_cast<void*>(createInfo.pNext),
        .timelineSemaphore = VK_TRUE
    };
    if (!isFeatureEnabled)
        createInfo.pNext = &timelineFeatures;

    finish();
}

bool Root::modifySwapchainCreateInfo(const vk::Vulkan& vk,
        VkSwapchainCreateInfoKHR& createInfo,
        const std::function<void(void)>& finish) const {
    if (!this->active_profile.has_value()) {
        finish();
        return false;
    }

    VkSurfaceCapabilitiesKHR caps{}; // NOLINT (enum value 0)
    auto res = vk.fi().GetPhysicalDeviceSurfaceCapabilitiesKHR(
        vk.physdev(), createInfo.surface, &caps);
    if (res != VK_SUCCESS)
        throw ls::vulkan_error(res, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR() failed");

    const bool privateOrderedTransport = context_ModifySwapchainCreateInfo(
        *this->active_profile,
        caps.maxImageCount,
        createInfo,
        this->gamescopeHdrActive.value_or(false),
        this->gamescopeDetected,
        this->presentationEnvironment
    );

    // Gamescope forwards both a MAILBOX base mode and a MAILBOX-only
    // maintenance1 compatibility list. The private SDR transport changes the
    // base mode to FIFO, so that list must not reach the lower driver: leaving
    // it attached makes the create description inconsistent and has surfaced
    // as a Wine vkCreateSwapchainKHR assertion. Remove only the outer node from
    // our lower-facing chain; ScopedPNextRemoval never edits its immutable mode
    // array and restores the caller's chain after finish(). The Gamescope HDR
    // transport preserves the complete compositor contract.
    ScopedPNextRemoval presentModes(
        createInfo.pNext,
        VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT,
        privateOrderedTransport
    );

    finish();
    return privateOrderedTransport;
}

void Root::createSwapchainContext(const vk::Vulkan& vk,
        VkSwapchainKHR swapchain, const SwapchainInfo& info) {
    if (!this->active_profile.has_value())
        throw ls::error("attempted to create swapchain context while layer is inactive");
    const auto& profile = *this->active_profile;

    if (!this->backend.has_value()) { // emplace backend late, due to loader bug
        const auto& global = this->config.get().global();

        try {
            // The backend owns a separate Vulkan instance. Prevent MAKO
            // Renderer from entering that internal instance while preserving
            // caller-provided values for the rest of the game process.
            const ScopedEnvironmentOverride disableMako(
                "DISABLE_MAKO", "1"
            );
            std::string dll{};
            if (global.dll.has_value())
                dll = *global.dll;
            else
                dll = ls::findShaderDll();

            const auto applicationDevice = identifyApplicationDevice(vk);
            if (profile.gpu) {
                std::cerr << "MAKO Renderer: backend GPU selection: configured="
                          << *profile.gpu << '\n';
            } else {
                std::cerr << "MAKO Renderer: backend GPU selection: following game device="
                          << applicationDevice.name << " ("
                          << applicationDevice.vendorId << ":"
                          << applicationDevice.deviceId << ")\n";
            }

            this->backend.emplace(
                [gpu = profile.gpu, applicationDevice](
                    const std::string& deviceName,
                    std::pair<const std::string&, const std::string&> ids,
                    const std::optional<std::string>& pci
                ) {
                    return matchesBackendDevice(
                        {
                            .name = deviceName,
                            .vendorId = ids.first,
                            .deviceId = ids.second,
                            .pci = pci,
                        },
                        gpu,
                        applicationDevice
                    );
                },
                dll, global.allow_fp16
            );
        } catch (const std::exception& e) {
            throw ls::error("failed to create backend instance", e);
        }
    }

    const bool inserted = this->swapchains.emplace(swapchain,
        Swapchain(vk, this->backend.mut(), profile, info,
            this->gamescopeHdrActive,
            this->gamescopeDetected,
            this->presentationEnvironment.hdrExposureDisabled,
            this->gamescopeRefreshHz,
            this->runtimeStateRevision)).second;
    const auto insertedContext = this->swapchains.find(swapchain);
    const uint64_t diagnosticsContextId =
        insertedContext != this->swapchains.end()
        ? insertedContext->second.diagnosticsId()
        : 0;

    if (presentDiagnosticsEnabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: operation=swapchain-context-create"
                  << " context=" << diagnosticsContextId
                  << " swapchain=" << swapchain
                  << " active_contexts=" << this->swapchains.size()
                  << " inserted=" << inserted
                  << " layer_forced_recreation=disabled"
                  << '\n';
    }
}

void Root::removeSwapchainContext(VkSwapchainKHR swapchain) {
    const auto context = this->swapchains.find(swapchain);
    const uint64_t diagnosticsContextId = context != this->swapchains.end()
        ? context->second.diagnosticsId()
        : 0;
    const size_t removed = this->swapchains.erase(swapchain);
    if (presentDiagnosticsEnabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: operation=swapchain-context-destroy"
                  << " context=" << diagnosticsContextId
                  << " swapchain=" << swapchain
                  << " active_contexts=" << this->swapchains.size()
                  << " removed=" << removed << '\n';
    }
}
