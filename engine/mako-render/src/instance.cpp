/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "instance.hpp"

#include "device_selection.hpp"
#include "layer_role.hpp"
#include "pnext_chain.hpp"
#include "present_diagnostics.hpp"
#include "spatial_scaling_policy.hpp"
#include "swapchain.hpp"

#include "mako-common/configuration/detection.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/paths.hpp"
#include "mako-common/vulkan/vulkan.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <stdlib.h>
#include <unistd.h>
#include <vulkan/vulkan_core.h>

using namespace mako;
using namespace mako::layer;

#ifndef MAKO_BUILD_VERSION
#define MAKO_BUILD_VERSION "unknown"
#endif
#ifndef MAKO_BUILD_FINGERPRINT
#define MAKO_BUILD_FINGERPRINT MAKO_BUILD_VERSION
#endif

namespace {
    constexpr uint64_t spatialScalingEnabledBit = uint64_t{1};
    constexpr uint64_t spatialScalingProcessSupportedBit = uint64_t{2};

    vk::DeviceMemoryTotals memoryDelta(
            const vk::DeviceMemoryTotals& after,
            const vk::DeviceMemoryTotals& before) {
        return {
            .bytes = after.bytes >= before.bytes
                ? after.bytes - before.bytes
                : 0,
            .allocations = after.allocations >= before.allocations
                ? after.allocations - before.allocations
                : 0,
        };
    }

#if defined(MAKO_LAYER_ROLE_SPATIAL_SCALING)
    constexpr char makoBuildIdentity[] =
        "MAKO Renderer: render layer active; identity="
        "VK_LAYER_MAKO_spatial_scaling; build="
        MAKO_BUILD_VERSION
        "; fingerprint="
        MAKO_BUILD_FINGERPRINT
        "; role=spatial-scaling";
#else
    constexpr char makoBuildIdentity[] =
        "MAKO Renderer: render layer active; identity="
        "VK_LAYER_MAKO_render; build="
        MAKO_BUILD_VERSION
        "; fingerprint="
        MAKO_BUILD_FINGERPRINT
        "; role=frame-generation";
#endif

    std::string diagnosticToken(const std::string_view raw) {
        if (raw.empty())
            return "(unset)";

        const auto separator = raw.find_last_of("/\\");
        std::string token(raw.substr(
            separator == std::string_view::npos ? 0 : separator + 1
        ));
        for (char& character : token) {
            if (character == ' ' || character == '\t' ||
                    character == '\r' || character == '\n' ||
                    character == '=') {
                character = '_';
            }
        }
        return token.empty() ? "(unset)" : token;
    }

    std::string_view identificationToken(const ls::IdentType type) {
        switch (type) {
            case ls::IdentType::OVERRIDE:
                return "override";
            case ls::IdentType::EXECUTABLE:
                return "executable";
            case ls::IdentType::WINE_EXECUTABLE:
                return "wine-executable";
            case ls::IdentType::PROCESS_NAME:
                return "process-name";
            case ls::IdentType::FALLBACK:
                return "fallback";
        }
        return "unknown";
    }

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
            .pNext = hasPciInfo && funcs.GetPhysicalDeviceProperties2
                ? &pciInfo : nullptr
        };
        if (funcs.GetPhysicalDeviceProperties2) {
            funcs.GetPhysicalDeviceProperties2(physicalDevice, &properties);
        } else {
            funcs.GetPhysicalDeviceProperties(
                physicalDevice, &properties.properties
            );
        }

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
            .pci = hasPciInfo && funcs.GetPhysicalDeviceProperties2
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

void Root::publishSurfaceScalingPolicy() noexcept {
    const bool enabled = this->active_profile &&
        ls::spatialScalingRequested(*this->active_profile);
    const float factor = this->active_profile
        ? this->active_profile->scaling_factor : 1.0F;
    const bool processSupported = spatialScalingProcessSupported(
        this->gamescopeEnvironmentDetected,
        this->gamescopeDetected,
        this->presentationEnvironment.hdrExposureDisabled
    );
    const uint64_t packedFactor = static_cast<uint64_t>(
        std::bit_cast<uint32_t>(factor)
    ) << 32U;
    const uint64_t packed = packedFactor |
        (enabled ? spatialScalingEnabledBit : uint64_t{0}) |
        (processSupported
            ? spatialScalingProcessSupportedBit : uint64_t{0});
    const uint64_t observedSequence =
        this->surfaceScalingPolicySequence.load(std::memory_order_acquire);
    if ((observedSequence & uint64_t{1}) == 0 &&
            this->surfaceScalingPolicy.load(std::memory_order_acquire) ==
                packed &&
            this->surfaceScalingPolicySequence.load(
                std::memory_order_acquire
            ) == observedSequence) {
        return;
    }

    const std::lock_guard lock(this->surfaceScalingPolicyPublishMutex);
    if (this->surfaceScalingPolicy.load(std::memory_order_relaxed) == packed)
        return;

    this->surfaceScalingPolicySequence.fetch_add(
        1, std::memory_order_acq_rel
    );
    this->surfaceScalingPolicy.store(packed, std::memory_order_release);
    this->surfaceScalingPolicySequence.fetch_add(
        1, std::memory_order_release
    );
}

SpatialScalingPolicySnapshot Root::surfaceScalingPolicySnapshot()
        const noexcept {
    while (true) {
        const uint64_t sequenceBefore =
            this->surfaceScalingPolicySequence.load(std::memory_order_acquire);
        if ((sequenceBefore & uint64_t{1}) != 0)
            continue;
        const uint64_t packed = this->surfaceScalingPolicy.load(
            std::memory_order_acquire
        );
        const uint64_t sequenceAfter =
            this->surfaceScalingPolicySequence.load(std::memory_order_acquire);
        if (sequenceBefore != sequenceAfter)
            continue;

        return SpatialScalingPolicySnapshot{
            .policy = {
                .enabled = (packed & spatialScalingEnabledBit) != 0,
                .factor = std::bit_cast<float>(
                    static_cast<uint32_t>(packed >> 32U)
                ),
            },
            .processSupported =
                (packed & spatialScalingProcessSupportedBit) != 0,
            .revision = sequenceAfter / 2,
        };
    }
}

Root::Root() :
    presentationEnvironment(resolvePresentationEnvironmentPolicy(
        std::getenv("MAKO_DISABLE_HDR_EXPOSURE"),
        std::getenv("DXVK_HDR"),
        std::getenv("DISABLE_GAMESCOPE_WSI")
    )),
    gamescopeEnvironmentDetected(gamescopeProcessEnvironmentHint()),
    hdrFeedbackReader(this->presentationEnvironment) {
    std::cerr << makoBuildIdentity << '\n';

    std::cerr << "MAKO Renderer: presentation policy: gamescope_wsi="
              << (this->presentationEnvironment.gamescopeWsiDisabled
                    ? "isolated" : "allowed")
              << "; hdr_exposure="
              << (this->presentationEnvironment.hdrExposureDisabled
                    ? "disabled" : "allowed")
              << "; gamescope_process_hint="
              << (this->gamescopeEnvironmentDetected ? "present" : "absent")
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
    const auto identification = ls::identify();
    const auto& profile = findProfile(this->config.get(), identification);
    if (present_diagnostics::enabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: "
                     "operation=process-identity"
                  << " pid=" << ::getpid()
                  << " executable="
                  << diagnosticToken(identification.executable)
                  << " wine_executable="
                  << diagnosticToken(
                         identification.wine_executable.value_or("")
                     )
                  << " process_name="
                  << diagnosticToken(identification.process_name)
                  << " profile="
                  << diagnosticToken(
                         profile ? profile->second.name : "(none)"
                     )
                  << " identification="
                  << (profile
                        ? identificationToken(profile->first) : "none")
                  << " build=" << MAKO_BUILD_VERSION
                  << " fingerprint=" << MAKO_BUILD_FINGERPRINT
                  << '\n';
    }
    if (!profile.has_value()) {
        this->publishSurfaceScalingPolicy();
        return;
    }

    this->active_profile = profileForLayer(profile->second);
    // Every matched MAKO process reserves the application-device interop used
    // by LSFG. The user's Frame Generation switch remains a live execution
    // policy: Off performs no generation work, while On can reuse the retained
    // backend and private resources without reconstructing the Vulkan device.
    this->frameGenerationInteropProvisionedAtStartup = frameGenerationLayer;
    this->scalingEngineConfiguredAtStartup =
        this->active_profile->scaling_enabled;

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
    if (this->active_profile->ultra_performance) {
        std::cerr << "MAKO Renderer: Ultra Performance active: flow_scale="
                  << ls::effectiveFlowScale(*this->active_profile)
                  << "; lighter_model="
                  << ls::effectivePerformanceMode(*this->active_profile)
                  << "; resources=active-policy"
                  << "; compatible_profile_reload=live\n";
    }
    this->publishSurfaceScalingPolicy();
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

    this->publishSurfaceScalingPolicy();

    // Configuration hot reload does not need a filesystem metadata query for
    // every presented frame. Keep the UI responsive while bounding the check
    // to four times per second, matching the feedback sampling cadence.
    constexpr auto configurationPollInterval = std::chrono::milliseconds(250);
    if (this->lastConfigurationPoll &&
            now - *this->lastConfigurationPoll < configurationPollInterval)
        return result;
    this->lastConfigurationPoll = now;

    if (!this->config.update())
        return result;

    result.reloaded = true;
    const auto& currentGlobal = this->config.get().global();
    result.globalChangeDeferred = backendGlobalChangePending(
        this->backendGlobal, currentGlobal
    );

    const auto previousProfileName = this->active_profile
        ? std::optional<std::string>{this->active_profile->name}
        : std::nullopt;
    const auto& profile = findProfile(this->config.get(), ls::identify());
    auto requestedProfile = profile
        ? std::optional<ls::GameConf>{profileForLayer(profile->second)}
        : std::nullopt;
    const bool activeUltraPerformance = this->active_profile &&
        this->active_profile->ultra_performance;
    const bool requestedUltraPerformance = requestedProfile &&
        requestedProfile->ultra_performance;

    auto runtimeProfile = requestedProfile;
    bool profileProcessRestartRequired = false;
    bool gpuSelectionPending = false;
    bool ultraPerformancePending =
        activeUltraPerformance != requestedUltraPerformance;
    bool scalingEnginePending = false;
    if (runtimeProfile && this->active_profile) {
        auto projection = projectProcessStaticProfileForLiveUpdate(
            *this->active_profile, *runtimeProfile,
            this->scalingEngineConfiguredAtStartup
        );
        *runtimeProfile = std::move(projection.runtimeProfile);
        gpuSelectionPending = projection.gpuSelectionPending;
        ultraPerformancePending = projection.ultraPerformancePending;
        scalingEnginePending = projection.scalingEnginePending;
        profileProcessRestartRequired = projection.restartRequired();
    } else if (runtimeProfile) {
        if (runtimeProfile->scaling_enabled !=
                this->scalingEngineConfiguredAtStartup) {
            runtimeProfile->scaling_enabled =
                this->scalingEngineConfiguredAtStartup;
            scalingEnginePending = true;
            profileProcessRestartRequired = true;
        }
    }
    if (ultraPerformancePending) {
        profileProcessRestartRequired = true;
        std::cerr << "MAKO Renderer: Ultra Performance toggle deferred; "
                     "restart the game to apply its process-static FP16 and "
                     "resource policy; compatible profile changes remain live\n";
    }
    if (scalingEnginePending) {
        std::cerr << "MAKO Renderer: Scaling Engine toggle deferred; restart "
                     "the game to rebuild the process Vulkan layer chain; "
                     "method and tuning changes remain live\n";
    }
    if (profileProcessRestartRequired) {
        result.processRestartDeferredContexts += this->swapchains.size();
        result.processProfileChangeDeferred = true;
    }

    this->runtimeStateRevision++;
    if (profileProcessRestartRequired && present_diagnostics::enabled()) {
        for (const auto& [swapchain, context] : this->swapchains) {
            static_cast<void>(swapchain);
            std::cerr << "MAKO Renderer: present diagnostics: "
                         "operation=runtime-transition-pending"
                      << " context=" << context.diagnosticsId()
                      << " state_revision=" << this->runtimeStateRevision
                      << " reason=process-static-profile"
                      << " gpu_selection_pending=" << gpuSelectionPending
                      << " ultra_performance_pending="
                      << ultraPerformancePending
                      << " scaling_engine_pending="
                      << scalingEnginePending
                      << " action=wait-for-process-restart\n";
        }
    }
    if (runtimeProfile)
        this->active_profile = std::move(*runtimeProfile);
    else
        this->active_profile = std::nullopt;
    this->publishSurfaceScalingPolicy();

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
            const auto update = context.updateProfile(
                *this->active_profile, this->runtimeStateRevision
            );
            if (update.action == ProfileUpdateAction::ApplyLive)
                result.liveContextsUpdated++;
            if (update.swapchainRecreationDeferred)
                result.swapchainRecreationDeferredContexts++;
            if (update.processRestartDeferred)
                result.processRestartDeferredContexts++;
            if (update.swapchainRecreationRequested)
                result.recreationRequestedContexts++;
        }
        result.processProfileChangeDeferred =
            result.processProfileChangeDeferred ||
            (requestedProfile && backendProfileChangePending(
                this->backendProfile, *requestedProfile
            ));
        if (result.processProfileChangeDeferred) {
            result.processRestartDeferredContexts = std::max(
                result.processRestartDeferredContexts,
                this->swapchains.size()
            );
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
    if (!this->frameGenerationInteropProvisioned()) {
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
        const char* const swapchainMaintenance1Extension,
        const std::function<void(void)>& finish) const {
    if (!this->frameGenerationInteropProvisioned()) {
        finish();
        return;
    }

    std::vector<const char*> requiredExtensions{
        "VK_KHR_external_memory",
        "VK_KHR_external_memory_fd",
        "VK_KHR_external_semaphore",
        "VK_KHR_external_semaphore_fd",
        "VK_KHR_timeline_semaphore"
    };
    if (swapchainMaintenance1Extension)
        requiredExtensions.push_back(swapchainMaintenance1Extension);
    auto extensions = add_extensions(
        createInfo.ppEnabledExtensionNames,
        createInfo.enabledExtensionCount,
        requiredExtensions
    );
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    bool timelineFeatureEnabled = false;
    bool swapchainMaintenance1FeatureEnabled = false;
    auto* featureInfo = reinterpret_cast<VkBaseInStructure*>(const_cast<void*>(createInfo.pNext));
    while (featureInfo) {
        if (featureInfo->sType ==
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES) {
            auto* features = reinterpret_cast<VkPhysicalDeviceVulkan12Features*>(featureInfo);
            features->timelineSemaphore = VK_TRUE;
            timelineFeatureEnabled = true;
        } else if (featureInfo->sType ==
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES) {
            auto* features = reinterpret_cast<VkPhysicalDeviceTimelineSemaphoreFeatures*>(featureInfo);
            features->timelineSemaphore = VK_TRUE;
            timelineFeatureEnabled = true;
        } else if (featureInfo->sType ==
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR) {
            auto* features = reinterpret_cast<
                VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR*>(
                    featureInfo
                );
            if (swapchainMaintenance1Extension)
                features->swapchainMaintenance1 = VK_TRUE;
            swapchainMaintenance1FeatureEnabled =
                features->swapchainMaintenance1 == VK_TRUE;
        }

        featureInfo = const_cast<VkBaseInStructure*>(featureInfo->pNext);
    }

    VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
        .pNext = const_cast<void*>(createInfo.pNext),
        .timelineSemaphore = VK_TRUE
    };
    if (!timelineFeatureEnabled)
        createInfo.pNext = &timelineFeatures;

    VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR maintenance1Features{
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR,
        .pNext = const_cast<void*>(createInfo.pNext),
        .swapchainMaintenance1 = VK_TRUE,
    };
    if (swapchainMaintenance1Extension &&
            !swapchainMaintenance1FeatureEnabled) {
        createInfo.pNext = &maintenance1Features;
    }

    finish();
}

SwapchainCreateModification Root::modifySwapchainCreateInfo(const vk::Vulkan& vk,
        VkSwapchainCreateInfoKHR& createInfo,
        const std::optional<SpatialScalingExtents>& previousVariableExtents,
        const std::optional<FixedSurfaceScalingContract>& fixedSurfaceContract,
        const std::function<void(void)>& finish) const {
    SwapchainCreateModification modification{
        .applicationExtent = createInfo.imageExtent,
        .presentationExtent = createInfo.imageExtent,
    };
    if (!this->active_profile.has_value()) {
        finish();
        return modification;
    }

    VkSurfaceCapabilitiesKHR caps{}; // NOLINT (enum value 0)
    auto res = vk.fi().GetPhysicalDeviceSurfaceCapabilitiesKHR(
        vk.physdev(), createInfo.surface, &caps);
    if (res != VK_SUCCESS)
        throw ls::vulkan_error(res, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR() failed");

    modification.variableSurface = !fixedSurfaceExtent(caps.currentExtent);

    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vk.fi().GetPhysicalDeviceMemoryProperties(
        vk.physdev(), &memoryProperties
    );
    const VkDeviceSize deviceLocalHeapBytes =
        largestDeviceLocalHeapBytes(memoryProperties);
    const uint64_t presentationPixelBudget =
        variablePresentationPixelBudget(memoryProperties);

    const auto policySnapshot = this->surfaceScalingPolicySnapshot();
    const auto scalingDecision = scalingDecisionForCreate(
        policySnapshot.policy,
        policySnapshot.processSupported,
        policySnapshot.revision,
        caps,
        createInfo.imageExtent,
        previousVariableExtents,
        fixedSurfaceContract,
        presentationPixelBudget
    );
    const auto& scalingExtents = scalingDecision.extents;
    modification.variableFeedbackSuppressed =
        scalingDecision.inactiveReason ==
            SpatialScalingInactiveReason::VariableSurfaceFeedback;
    const bool fixedVirtualSourceRequest =
        fixedSurfaceExtent(caps.currentExtent) &&
        !sameExtent(createInfo.imageExtent, caps.currentExtent);
    const auto colorPipeline = classifySwapchainColor(
        createInfo.imageFormat, createInfo.imageColorSpace,
        this->gamescopeHdrActive.value_or(false)
    );
    const bool sdrScalingEncoding =
        spatialScalingColorSupported(colorPipeline);
    VkBool32 queueFamilySupportsPresentation = VK_FALSE;
    const bool queueSurfaceSupportChecked =
        scalingExtents && vk.fi().GetPhysicalDeviceSurfaceSupportKHR;
    if (scalingExtents && vk.fi().GetPhysicalDeviceSurfaceSupportKHR) {
        const auto surfaceSupportResult =
            vk.fi().GetPhysicalDeviceSurfaceSupportKHR(
                vk.physdev(), vk.queueFamilyIndex(), createInfo.surface,
                &queueFamilySupportsPresentation
            );
        if (surfaceSupportResult != VK_SUCCESS) {
            throw ls::vulkan_error(
                surfaceSupportResult,
                "vkGetPhysicalDeviceSurfaceSupportKHR() failed"
            );
        }
    }
    const bool spatialShapeSupported =
        spatialScalingSwapchainShapeSupported(createInfo);
    const bool spatialQueueCommandsSupported = vk.queueFamilySupports(
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT
    );
    const bool spatialFormatSupported =
        sdrScalingEncoding &&
        (caps.supportedUsageFlags & (
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT
        )) == (
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT
        ) &&
        vk.supportsOptimalTilingFormatFeatures(
            createInfo.imageFormat,
            VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                VK_FORMAT_FEATURE_BLIT_DST_BIT
        ) &&
        vk.supportsOptimalTilingFormatFeatures(
            colorPipeline.exchangeFormat,
            VK_FORMAT_FEATURE_BLIT_SRC_BIT |
                VK_FORMAT_FEATURE_BLIT_DST_BIT |
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
                VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT
        );
    const bool spatialScalingSupported = spatialShapeSupported &&
        spatialFormatSupported && spatialQueueCommandsSupported &&
        queueFamilySupportsPresentation == VK_TRUE;
    const char* const queuePresentationSupport =
        !queueSurfaceSupportChecked ? "not-checked" :
        queueFamilySupportsPresentation == VK_TRUE
            ? "supported" : "unsupported";
    auto inactiveReason = scalingDecision.inactiveReason;
    if (scalingExtents && spatialScalingSupported) {
        modification.spatialScalingActive = true;
        modification.applicationExtent = scalingExtents->source;
        modification.presentationExtent = scalingExtents->presentation;
        createInfo.imageExtent = scalingExtents->presentation;
        inactiveReason = SpatialScalingInactiveReason::None;
    } else if (scalingExtents) {
        inactiveReason = !spatialShapeSupported
            ? SpatialScalingInactiveReason::SwapchainShapeUnsupported
            : (queueFamilySupportsPresentation != VK_TRUE
                ? SpatialScalingInactiveReason::QueuePresentationUnsupported
                : (!spatialQueueCommandsSupported
                    ? SpatialScalingInactiveReason::QueueCommandsUnsupported
                    : SpatialScalingInactiveReason::SwapchainFormatUnsupported));
    }
    if (this->active_profile->scaling_enabled) {
        const auto advertised = scalingDecision.fixedContract;
        std::cerr << "MAKO Renderer: spatial scaling swapchain policy: "
                  << "role=" << layerRoleName
                  << "; requested=" << modification.applicationExtent.width
                  << 'x' << modification.applicationExtent.height
                  << "; surface_current=" << caps.currentExtent.width
                  << 'x' << caps.currentExtent.height
                  << "; surface_extent_mode="
                  << (modification.variableSurface ? "variable" : "fixed")
                  << "; device_local_heap_mib="
                  << (deviceLocalHeapBytes / (1024 * 1024))
                  << "; variable_presentation_pixel_budget="
                  << presentationPixelBudget
                  << "; advertised_source="
                  << (advertised ? advertised->extents.source.width : 0)
                  << 'x'
                  << (advertised ? advertised->extents.source.height : 0)
                  << "; advertised_presentation="
                  << (advertised ? advertised->extents.presentation.width : 0)
                  << 'x'
                  << (advertised ? advertised->extents.presentation.height : 0)
                  << "; actual_source="
                  << (fixedVirtualSourceRequest && !scalingExtents
                        ? 0 : modification.applicationExtent.width)
                  << 'x'
                  << (fixedVirtualSourceRequest && !scalingExtents
                        ? 0 : modification.applicationExtent.height)
                  << "; actual_presentation="
                  << (fixedVirtualSourceRequest && !scalingExtents
                        ? 0 : modification.presentationExtent.width)
                  << 'x'
                  << (fixedVirtualSourceRequest && !scalingExtents
                        ? 0 : modification.presentationExtent.height)
                  << "; policy_revision=" << policySnapshot.revision
                  << "; contract_policy_revision="
                  << (advertised ? advertised->policyRevision : 0)
                  << "; query_generation="
                  << (advertised ? advertised->queryGeneration : 0)
                  << "; selected_source="
                  << (scalingExtents ? scalingExtents->source.width : 0)
                  << 'x'
                  << (scalingExtents ? scalingExtents->source.height : 0)
                  << "; selected_presentation="
                  << (scalingExtents ? scalingExtents->presentation.width : 0)
                  << 'x'
                  << (scalingExtents ? scalingExtents->presentation.height : 0)
                  << "; format=" << static_cast<int>(createInfo.imageFormat)
                  << "; format_supported=" << spatialFormatSupported
                  << "; shape_supported=" << spatialShapeSupported
                  << "; queue_presentation_support="
                  << queuePresentationSupport
                  << "; queue_commands_supported="
                  << spatialQueueCommandsSupported
                  << "; variable_feedback_suppressed="
                  << modification.variableFeedbackSuppressed
                  << "; previous_presentation_budget_reused="
                  << scalingDecision.reusedPreviousPresentationBudget
                  << "; inactive_reason="
                  << spatialScalingInactiveReasonName(inactiveReason)
                  << "; source_presentation_split="
                  << (modification.spatialScalingActive ? 1 : 0)
                  << "; active=" << modification.spatialScalingActive
                  << '\n';
        if (modification.variableFeedbackSuppressed &&
                previousVariableExtents) {
            std::cerr << "MAKO Renderer: spatial scaling variable-surface "
                         "feedback guard: surface=" << createInfo.surface
                      << "; previous_source="
                      << previousVariableExtents->source.width << 'x'
                      << previousVariableExtents->source.height
                      << "; previous_presentation="
                      << previousVariableExtents->presentation.width << 'x'
                      << previousVariableExtents->presentation.height
                      << "; requested="
                      << modification.applicationExtent.width << 'x'
                      << modification.applicationExtent.height
                      << "; action=native-feedback-guard\n";
        }
    }

    if (fixedVirtualSourceRequest && !scalingExtents) {
        throw ls::vulkan_error(
            VK_ERROR_INITIALIZATION_FAILED,
            "fixed-surface spatial scaling create request does not match the "
            "latest capability contract; inactive_reason=" +
                std::string(spatialScalingInactiveReasonName(inactiveReason))
        );
    }
    if (scalingExtents && !spatialScalingSupported &&
            fixedSurfaceExtent(caps.currentExtent)) {
        // A fixed surface was previously advertised at the virtual source
        // extent. Passing that low extent to the real WSI surface without a
        // scaler would either fail lower creation or expose an incomplete
        // top-left frame. Reject an unexpected format-capability mismatch
        // explicitly instead of silently violating the advertised contract.
        throw ls::vulkan_error(
            VK_ERROR_INITIALIZATION_FAILED,
            !spatialShapeSupported
                ? "fixed-surface spatial scaling requires an opaque, "
                  "single-layer, unprotected, non-shared-present swapchain"
                : (queueFamilySupportsPresentation != VK_TRUE
                    ? "MAKO's graphics/compute queue family cannot present to "
                      "the fixed surface"
                    : (!spatialQueueCommandsSupported
                        ? "MAKO's presentation queue family lacks graphics and "
                          "compute support"
                    : "the selected fixed-surface format cannot be spatially scaled")
                    )
        );
    }

    modification.privateOrderedTransport = context_ModifySwapchainCreateInfo(
        *this->active_profile,
        caps.maxImageCount,
        createInfo,
        this->gamescopeHdrActive.value_or(false),
        this->gamescopeDetected,
        this->presentationEnvironment,
        frameGenerationInteropForLayer(vk.frameGenerationInteropEnabled()),
        modification.spatialScalingActive
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
        modification.privateOrderedTransport
    );

    finish();
    modification.presentationExtent = createInfo.imageExtent;
    return modification;
}

std::optional<SpatialScalingCapabilitySelection>
Root::modifySurfaceCapabilities(
        VkSurfaceCapabilitiesKHR& capabilities) const {
    const auto snapshot = this->surfaceScalingPolicySnapshot();
    if (!snapshot.policy.enabled || !snapshot.processSupported)
        return std::nullopt;
    const auto extents = virtualizeSurfaceCapabilities(
        snapshot.policy, capabilities
    );
    if (!extents)
        return std::nullopt;
    return SpatialScalingCapabilitySelection{
        .extents = *extents,
        .factor = snapshot.policy.factor,
        .policyRevision = snapshot.revision,
    };
}

void Root::createSwapchainContext(const vk::Vulkan& vk,
        VkSwapchainKHR swapchain, const SwapchainInfo& info,
        const bool swapchainMaintenance1Enabled) {
    if (!this->active_profile.has_value())
        throw ls::error("attempted to create swapchain context while layer is inactive");
    const auto& profile = *this->active_profile;
    const auto& global = this->config.get().global();

    std::optional<std::filesystem::path> scalingShaderDll;
    if (info.spatialScalingActive) {
        try {
            scalingShaderDll = global.dll.has_value()
                ? std::filesystem::path(*global.dll)
                : std::filesystem::path(ls::findShaderDll());
        } catch (const std::exception& error) {
            // Native Resolution and MAKO do not require the licensed shader,
            // but retaining its path while the Scaling Engine is provisioned
            // is what makes a later private LS1 selection genuinely live.
            if (ls::licensedScalingModelRequested(profile.scaling_method)) {
                std::cerr << "MAKO Renderer: unable to locate Lossless.dll for LS1; "
                             "the MAKO fallback will be used: "
                          << error.what() << '\n';
            }
        }
    }

    std::optional<std::string> backendInitializationError;
    const bool frameGenerationRequested =
        profile.frame_generation_enabled;
    const bool frameGenerationAvailableOnDevice =
        frameGenerationInteropForLayer(vk.frameGenerationInteropEnabled());
    if (frameGenerationAvailableOnDevice &&
            !this->backend.has_value()) { // emplace backend late, due to loader bug
        try {
            // The backend owns a separate Vulkan instance. Prevent MAKO
            // Renderer, Gamescope WSI, or a selected post-process layer from
            // entering that internal instance while preserving caller values
            // for the rest of the game process. The split chain has two MAKO
            // identities, so disabling only the upper identity would let the
            // lower role intercept the backend device and break construction.
            const ScopedEnvironmentOverride disableMako(
                "DISABLE_MAKO", "1"
            );
            const ScopedEnvironmentOverride disableSpatialMako(
                "DISABLE_MAKO_SPATIAL_SCALING", "1"
            );
            const ScopedEnvironmentOverride disableGamescopeWsi(
                "DISABLE_GAMESCOPE_WSI", "1"
            );
            const ScopedEnvironmentOverride disableMangoHud(
                "MANGOHUD", "0"
            );
            const ScopedEnvironmentOverride disableVkBasalt(
                "ENABLE_VKBASALT", "0"
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
                dll, ls::effectiveAllowFp16(global, profile)
            );
            this->backendGlobal = global;
            this->backendProfile = profile;
        } catch (const std::exception& e) {
            backendInitializationError = e.what();
            if (frameGenerationRequested && !info.spatialScalingActive)
                throw ls::error("failed to create backend instance", e);
        }
    }

    backend::Instance* frameGenerationBackend =
        frameGenerationAvailableOnDevice && this->backend.has_value()
        ? &this->backend.mut() : nullptr;
    if (frameGenerationRequested && !vk.frameGenerationInteropEnabled()) {
        std::cerr << "MAKO Renderer: frame generation was enabled after the "
                     "application device was created without external interop; "
                     "a game restart is required; independent spatial scaling "
                     "remains available\n";
    } else if (frameGenerationRequested && !frameGenerationBackend &&
            info.spatialScalingActive) {
        std::cerr << "MAKO Renderer: frame-generation backend initialization "
                     "failed; independent spatial scaling retained:\n"
                  << "- "
                  << backendInitializationError.value_or("unknown failure")
                  << '\n';
    } else if (!frameGenerationAvailableOnDevice &&
            !frameGenerationRequested && info.spatialScalingActive) {
        std::cerr << "MAKO Renderer: standalone spatial scaling: "
                     "frame-generation backend and interop resources were not "
                     "created\n";
    }

    auto contextProfile = this->backendProfile
        ? profileForExistingBackend(profile, *this->backendProfile)
        : profile;
    if (!frameGenerationAvailableOnDevice)
        contextProfile.frame_generation_enabled = false;
    const auto memoryBefore = vk.deviceMemorySnapshot();
    const bool inserted = this->swapchains.emplace(swapchain,
        Swapchain(vk, frameGenerationBackend, std::move(contextProfile), info,
            std::move(scalingShaderDll),
            this->gamescopeHdrActive,
            this->gamescopeDetected,
            this->presentationEnvironment.hdrExposureDisabled,
            this->gamescopeRefreshHz,
            this->runtimeStateRevision,
            swapchainMaintenance1Enabled)).second;
    const auto memoryAfter = vk.deviceMemorySnapshot();
    const auto contextInternal = memoryDelta(
        memoryAfter.internal, memoryBefore.internal);
    const auto contextExported = memoryDelta(
        memoryAfter.exported, memoryBefore.exported);
    const auto contextImported = memoryDelta(
        memoryAfter.imported, memoryBefore.imported);
    const auto insertedContext = this->swapchains.find(swapchain);
    const uint64_t diagnosticsContextId =
        insertedContext != this->swapchains.end()
        ? insertedContext->second.diagnosticsId()
        : 0;

    std::clog << "MAKO Renderer: renderer-memory operation=swapchain-context-create"
        << " context=" << diagnosticsContextId
        << " role=" << layerRoleName
        << " width=" << info.extent.width
        << " height=" << info.extent.height
        << " context_internal_bytes=" << contextInternal.bytes
        << " context_internal_allocations=" << contextInternal.allocations
        << " context_exported_bytes=" << contextExported.bytes
        << " context_exported_allocations=" << contextExported.allocations
        << " context_imported_mapped_bytes=" << contextImported.bytes
        << " context_imported_allocations=" << contextImported.allocations
        << " live_internal_bytes=" << memoryAfter.internal.bytes
        << " live_internal_allocations=" << memoryAfter.internal.allocations
        << " live_exported_bytes=" << memoryAfter.exported.bytes
        << " live_exported_allocations=" << memoryAfter.exported.allocations
        << " peak_internal_bytes=" << memoryAfter.peakInternal.bytes
        << " peak_internal_allocations="
        << memoryAfter.peakInternal.allocations
        << " peak_exported_bytes=" << memoryAfter.peakExported.bytes
        << " peak_exported_allocations="
        << memoryAfter.peakExported.allocations
        << '\n';

    if (present_diagnostics::enabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: operation=swapchain-context-create"
                  << " context=" << diagnosticsContextId
                  << " pid=" << ::getpid()
                  << " role=" << layerRoleName
                  << " swapchain=" << swapchain
                  << " width=" << info.extent.width
                  << " height=" << info.extent.height
                  << " images=" << info.images.size()
                  << " format=" << static_cast<int>(info.format)
                  << " color_space=" << static_cast<int>(info.colorSpace)
                  << " present_mode=" << static_cast<int>(info.presentMode)
                  << " ordered_transport="
                  << (info.privateOrderedTransport ? 1 : 0)
                  << " replacement=" << (info.replacement ? 1 : 0)
                  << " active_contexts=" << this->swapchains.size()
                  << " inserted=" << inserted
                  << " present_retirement="
                  << (insertedContext != this->swapchains.end() &&
                        insertedContext->second.presentRetirementEnabled()
                        ? "maintenance1-fence" : "natural-only")
                  << " layer_forced_recreation=live-profile-resources-one-shot"
                  << '\n';
    }
}

void Root::removeSwapchainContext(VkSwapchainKHR swapchain) {
    static_cast<void>(this->takeSwapchainContext(swapchain));
}

std::optional<Swapchain> Root::takeSwapchainContext(
        const VkSwapchainKHR swapchain) {
    auto context = this->swapchains.extract(swapchain);
    const uint64_t diagnosticsContextId = context.empty()
        ? 0 : context.mapped().diagnosticsId();
    const bool removed = !context.empty();
    if (present_diagnostics::enabled()) {
        std::cerr << "MAKO Renderer: present diagnostics: operation=swapchain-context-destroy"
                  << " context=" << diagnosticsContextId
                  << " role=" << layerRoleName
                  << " swapchain=" << swapchain
                  << " active_contexts=" << this->swapchains.size()
                  << " removed=" << removed << '\n';
    }
    if (context.empty())
        return std::nullopt;
    return std::optional<Swapchain>{std::move(context.mapped())};
}
