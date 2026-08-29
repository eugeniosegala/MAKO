/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "instance.hpp"
#include "color_pipeline.hpp"
#include "layer_role.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/pointers.hpp"
#include "mako-common/vulkan/vulkan.hpp"
#include "present_diagnostics.hpp"
#include "swapchain.hpp"
#include "swapchain_retirement.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <dlfcn.h>
#include <link.h>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <string_view>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <vulkan/vk_layer.h>
#include <vulkan/vulkan_core.h>

using namespace mako::layer;

namespace {
    // global layer info initialized at layer negotiation
    struct LayerInfo {
        std::unordered_map<std::string, PFN_vkVoidFunction> map; //!< function pointer override map
        PFN_vkGetInstanceProcAddr GetInstanceProcAddr;

        Root root;
    }* layer_info; // NOLINT (global variable)

    // instance-wide info initialized at instance creation(s)
    struct InstanceInfo {
        std::vector<VkInstance> handles; // there may be several instances
        vk::VulkanInstanceFuncs funcs;

        std::unordered_map<VkDevice, vk::Vulkan> devices;
        std::unordered_set<VkDevice> nativeDevices;
        std::unordered_set<VkDevice> presentRetirementDevices;
        struct QueueIdentity {
            VkDevice device{VK_NULL_HANDLE};
            uint32_t familyIndex{UINT32_MAX};
        };
        std::unordered_map<VkQueue, QueueIdentity> queueIdentities;
        struct SurfaceScalingEligibility {
            bool supported{false};
            bool queueSupported{false};
            uint32_t advertisedFormatCount{0};
            uint32_t compatibleFormatCount{0};
        };
        std::mutex surfaceScalingMutex;
        std::unordered_map<
            VkPhysicalDevice,
            std::unordered_map<VkSurfaceKHR, SurfaceScalingEligibility>
        > surfaceScalingEligibility;
        uint64_t nextSurfaceScalingQueryGeneration{1};
        std::unordered_map<
            VkPhysicalDevice,
            std::unordered_map<VkSurfaceKHR, FixedSurfaceScalingContract>
        > fixedSurfaceScalingContracts;
        std::unordered_map<VkSurfaceKHR, SpatialScalingExtents>
            variableSurfaceScalingExtents;
        std::unordered_map<VkSwapchainKHR, ls::R<vk::Vulkan>> swapchains;
        std::unordered_map<VkSwapchainKHR, SwapchainInfo> swapchainInfos;
        std::unordered_set<VkSwapchainKHR> nativeSwapchains;
        struct RetiredSwapchain {
            VkDevice device{VK_NULL_HANDLE};
            VkSurfaceKHR surface{VK_NULL_HANDLE};
            ls::R<vk::Vulkan> vk;
            std::optional<Swapchain> context;
            std::chrono::steady_clock::time_point notBefore{};
            bool replacementHandoffConsumed{false};
        };
        std::atomic_size_t retiredSwapchainCount{0};
        std::mutex retiredSwapchainsMutex;
        std::unordered_map<VkSwapchainKHR, RetiredSwapchain>
            retiredSwapchains;
    }* instance_info; // NOLINT (global variable)

    enum class FixedSurfaceCapabilityRelayOperation : uint32_t {
        Begin,
        Consume,
    };

#if defined(MAKO_LAYER_ROLE_SPATIAL_SCALING)
    thread_local FixedSurfaceCapabilityRelaySlot fixedSurfaceCapabilityRelay;
#endif

    bool initializeLayerInfo();

    bool finalizeRetiredSwapchain(
            const VkSwapchainKHR swapchain, const uint64_t timeoutNs,
            const std::string_view trigger) {
        const std::lock_guard retirementLock(
            instance_info->retiredSwapchainsMutex
        );
        const auto retired = instance_info->retiredSwapchains.find(swapchain);
        if (retired == instance_info->retiredSwapchains.end())
            return true;
        uint64_t remainingTimeoutNs = timeoutNs;
        const auto now = std::chrono::steady_clock::now();
        if (now < retired->second.notBefore) {
            if (timeoutNs == 0)
                return false;
            const auto delay = std::chrono::duration_cast<
                std::chrono::nanoseconds>(
                    retired->second.notBefore - now
                );
            if (timeoutNs != UINT64_MAX &&
                    static_cast<uint64_t>(delay.count()) > timeoutNs) {
                return false;
            }
            std::this_thread::sleep_for(delay);
            if (timeoutNs != UINT64_MAX)
                remainingTimeoutNs -= static_cast<uint64_t>(delay.count());
        }
        if (retired->second.context &&
                !retired->second.context->waitForPresentRetirement(
                    retired->second.vk.get(), remainingTimeoutNs
                )) {
            return false;
        }

        auto node = instance_info->retiredSwapchains.extract(retired);
        auto& state = node.mapped();
        state.context.reset();
        state.vk.get().df().DestroySwapchainKHR(
            state.device, swapchain, VK_NULL_HANDLE
        );
        instance_info->retiredSwapchainCount.fetch_sub(
            1, std::memory_order_acq_rel
        );
        if (present_diagnostics::enabled()) {
            std::cerr << "MAKO Renderer: present diagnostics: "
                         "operation=swapchain-retirement-complete"
                      << " role=" << layerRoleName
                      << " swapchain=" << swapchain
                      << " trigger=" << trigger
                      << " pending="
                      << instance_info->retiredSwapchains.size() << '\n';
        }
        return true;
    }

    void collectRetiredSwapchains(
            const VkDevice device, const uint64_t timeoutNs,
            const std::string_view trigger) {
        if (instance_info->retiredSwapchainCount.load(
                std::memory_order_acquire) == 0) {
            return;
        }
        const auto started = std::chrono::steady_clock::now();
        std::vector<VkSwapchainKHR> candidates;
        {
            const std::lock_guard retirementLock(
                instance_info->retiredSwapchainsMutex
            );
            candidates.reserve(instance_info->retiredSwapchains.size());
            for (const auto& [swapchain, retired] :
                    instance_info->retiredSwapchains) {
                if (retired.device == device)
                    candidates.push_back(swapchain);
            }
        }
        for (const auto swapchain : candidates) {
            uint64_t remainingTimeoutNs = timeoutNs;
            if (timeoutNs != UINT64_MAX) {
                const auto elapsed = std::chrono::duration_cast<
                    std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - started
                    ).count();
                remainingTimeoutNs = elapsed >=
                        static_cast<int64_t>(timeoutNs)
                    ? 0
                    : timeoutNs - static_cast<uint64_t>(elapsed);
            }
            static_cast<void>(finalizeRetiredSwapchain(
                swapchain, remainingTimeoutNs, trigger
            ));
        }
    }

    void collectRetiredSwapchainsAfterPresent(
            const VkDevice device, const VkPresentInfoKHR& presentInfo) {
        if (instance_info->retiredSwapchainCount.load(
                std::memory_order_acquire) == 0) {
            return;
        }
        std::vector<VkSwapchainKHR> candidates;
        {
            const std::lock_guard retirementLock(
                instance_info->retiredSwapchainsMutex
            );
            candidates.reserve(instance_info->retiredSwapchains.size());
            for (const auto& [retiredHandle, retired] :
                    instance_info->retiredSwapchains) {
                if (retired.device != device)
                    continue;
                for (uint32_t index = 0;
                        index < presentInfo.swapchainCount; ++index) {
                    const auto live = instance_info->swapchainInfos.find(
                        presentInfo.pSwapchains[index]
                    );
                    if (live != instance_info->swapchainInfos.end() &&
                            retiredSwapchainBelongsToSurface(
                                retired.surface, live->second.surface)) {
                        candidates.push_back(retiredHandle);
                        break;
                    }
                }
            }
        }
        for (const auto swapchain : candidates) {
            static_cast<void>(finalizeRetiredSwapchain(
                swapchain, 0, "same-surface-present"
            ));
        }
    }

    std::optional<VkSwapchainKHR> claimRetainedSwapchainForReplacement(
            const VkDevice device, const VkSurfaceKHR surface,
            const VkSwapchainKHR requestedOldSwapchain) {
        const std::lock_guard retirementLock(
            instance_info->retiredSwapchainsMutex
        );
        for (auto& [swapchain, retired] :
                instance_info->retiredSwapchains) {
            if (!shouldHandoffRetainedSwapchainAsOld(
                    requestedOldSwapchain,
                    device,
                    surface,
                    retired.device,
                    retired.surface,
                    retired.replacementHandoffConsumed)) {
                continue;
            }
            // Vulkan retires oldSwapchain even when creation fails, so this
            // candidate must never be supplied to a later retry.
            retired.replacementHandoffConsumed = true;
            return swapchain;
        }
        return std::nullopt;
    }

    void forceFinalizeRetiredSwapchains(const VkDevice device) {
        std::vector<VkSwapchainKHR> candidates;
        {
            const std::lock_guard retirementLock(
                instance_info->retiredSwapchainsMutex
            );
            for (const auto& [swapchain, retired] :
                    instance_info->retiredSwapchains) {
                if (retired.device == device)
                    candidates.push_back(swapchain);
            }
        }
        for (const auto swapchain : candidates) {
            const std::lock_guard retirementLock(
                instance_info->retiredSwapchainsMutex
            );
            auto node = instance_info->retiredSwapchains.extract(swapchain);
            if (node.empty())
                continue;
            auto& state = node.mapped();
            std::cerr << "MAKO Renderer: forcing pending swapchain retirement "
                         "during device destruction\n";
            state.context.reset();
            state.vk.get().df().DestroySwapchainKHR(
                state.device, swapchain, VK_NULL_HANDLE
            );
            instance_info->retiredSwapchainCount.fetch_sub(
                1, std::memory_order_acq_rel
            );
        }
    }

    void collectRetiredSwapchainsForSurface(const VkSurfaceKHR surface) {
        if (instance_info->retiredSwapchainCount.load(
                std::memory_order_acquire) == 0) {
            return;
        }
        std::vector<VkSwapchainKHR> candidates;
        {
            const std::lock_guard retirementLock(
                instance_info->retiredSwapchainsMutex
            );
            candidates.reserve(instance_info->retiredSwapchains.size());
            for (const auto& [swapchain, retired] :
                    instance_info->retiredSwapchains) {
                if (retiredSwapchainBelongsToSurface(
                        retired.surface, surface)) {
                    candidates.push_back(swapchain);
                }
            }
        }
        for (const auto swapchain : candidates)
            static_cast<void>(finalizeRetiredSwapchain(
                swapchain, UINT64_MAX, "surface-destroy"
            ));
    }

    std::optional<uint32_t> selectLayerQueueFamily(
            const VkPhysicalDevice physicalDevice,
            const VkDeviceCreateInfo& createInfo,
            const bool spatialScalingConfigured) {
        uint32_t familyCount{};
        instance_info->funcs.GetPhysicalDeviceQueueFamilyProperties(
            physicalDevice, &familyCount, nullptr
        );
        std::vector<VkQueueFamilyProperties> families(familyCount);
        instance_info->funcs.GetPhysicalDeviceQueueFamilyProperties(
            physicalDevice, &familyCount, families.data()
        );
        const VkQueueFlags required = VK_QUEUE_GRAPHICS_BIT |
            (spatialScalingConfigured ? VK_QUEUE_COMPUTE_BIT : 0);
        for (uint32_t i = 0; i < createInfo.queueCreateInfoCount; ++i) {
            const auto& requested = createInfo.pQueueCreateInfos[i];
            if (requested.queueCount == 0 || requested.flags != 0 ||
                    requested.queueFamilyIndex >= familyCount) {
                continue;
            }
            if ((families.at(requested.queueFamilyIndex).queueFlags &
                    required) == required) {
                return requested.queueFamilyIndex;
            }
        }
        return std::nullopt;
    }

    std::optional<const char*> supportedSwapchainMaintenance1Extension(
            const VkPhysicalDevice physicalDevice,
            const bool scalingEngineProvisioned) {
        if (!scalingEngineProvisioned ||
                !instance_info->funcs.GetPhysicalDeviceFeatures2) {
            return std::nullopt;
        }

        uint32_t extensionCount{};
        auto result = instance_info->funcs.EnumerateDeviceExtensionProperties(
            physicalDevice, nullptr, &extensionCount, nullptr
        );
        if (result != VK_SUCCESS)
            return std::nullopt;
        std::vector<VkExtensionProperties> extensions(extensionCount);
        result = instance_info->funcs.EnumerateDeviceExtensionProperties(
            physicalDevice, nullptr, &extensionCount, extensions.data()
        );
        if (result != VK_SUCCESS)
            return std::nullopt;

        const auto hasExtension = [&](const char* const name) {
            return std::ranges::any_of(
                extensions,
                [name](const VkExtensionProperties& extension) {
                    return std::strcmp(extension.extensionName, name) == 0;
                }
            );
        };
        const bool hasKhr = hasExtension(
            khrSwapchainMaintenance1ExtensionName
        );
        const bool hasExt = hasExtension(
            VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME
        );
        if (!hasKhr && !hasExt)
            return std::nullopt;

        VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance1{
            .sType =
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
        };
        VkPhysicalDeviceFeatures2 features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &maintenance1,
        };
        instance_info->funcs.GetPhysicalDeviceFeatures2(
            physicalDevice, &features
        );
        const char* const selected = selectSwapchainMaintenance1Extension(
            hasKhr, hasExt, maintenance1.swapchainMaintenance1 == VK_TRUE
        );
        if (!selected)
            return std::nullopt;
        return selected;
    }

    // create instance
    VkResult myvkCreateInstance(
            const VkInstanceCreateInfo* info,
            const VkAllocationCallbacks* alloc,
            VkInstance* instance) {
        // apply layer chaining
        auto* layerInfo = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(info->pNext));
        while (layerInfo && (layerInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO
                || layerInfo->function != VK_LAYER_LINK_INFO)) {
            layerInfo = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(layerInfo->pNext));
        }
        if (!layerInfo) {
            std::cerr << "MAKO Renderer: no layer info found in pNext chain, "
                "the previous layer does not follow spec\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        auto* linkInfo = layerInfo->u.pLayerInfo;
        if (!linkInfo) {
            std::cerr << "MAKO Renderer: link info is null, "
                "the previous layer does not follow spec\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        layer_info->GetInstanceProcAddr = linkInfo->pfnNextGetInstanceProcAddr;
        if (!layer_info->GetInstanceProcAddr) {
            std::cerr << "MAKO Renderer: next layer's vkGetInstanceProcAddr is null, "
                "the previous layer does not follow spec\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        layerInfo->u.pLayerInfo = linkInfo->pNext; // advance for next layer

        // create instance
        auto* vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
            layer_info->GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
        if (!vkCreateInstance) {
            std::cerr << "MAKO Renderer: failed to get next layer's vkCreateInstance, "
                "the previous layer does not follow spec\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        bool lowerInstanceCreated = false;
        const auto rollbackCreatedInstance = [&]() noexcept {
            if (!lowerInstanceCreated || !instance || *instance == VK_NULL_HANDLE)
                return;

            auto* vkDestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(
                layer_info->GetInstanceProcAddr(*instance, "vkDestroyInstance"));
            if (vkDestroyInstance) {
                vkDestroyInstance(*instance, alloc);
                *instance = VK_NULL_HANDLE;
            } else {
                std::cerr << "MAKO Renderer: failed to roll back a lower Vulkan instance "
                             "after layer initialization failed\n";
            }
            lowerInstanceCreated = false;
        };
        const auto releaseIdleLayerState = []() noexcept {
            if (instance_info)
                return;
            delete layer_info; // NOLINT (memory management)
            layer_info = nullptr;
        };

        try {
            VkInstanceCreateInfo newInfo = *info;
            layer_info->root.modifyInstanceCreateInfo(newInfo,
                [&, newInfo = &newInfo]() {
                    auto res = vkCreateInstance(newInfo, alloc, instance);
                    if (res != VK_SUCCESS)
                        throw ls::vulkan_error(res, "vkCreateInstance() failed");
                    lowerInstanceCreated = true;
                }
            );

            if (!instance_info)
                instance_info = new InstanceInfo{ // NOLINT (memory management)
                    .funcs = vk::initVulkanInstanceFuncs(*instance,
                        layer_info->GetInstanceProcAddr, true),
                };

            instance_info->handles.push_back(*instance);
            lowerInstanceCreated = false;

            return VK_SUCCESS;
        } catch (const ls::vulkan_error& e) {
            rollbackCreatedInstance();
            releaseIdleLayerState();
            if (e.error() == VK_ERROR_EXTENSION_NOT_PRESENT)
                std::cerr << "MAKO Renderer: required Vulkan instance extensions are not present. "
                    "Your GPU driver is not supported.\n";
            return e.error();
        } catch (const std::exception& e) {
            rollbackCreatedInstance();
            releaseIdleLayerState();
            std::cerr << "MAKO Renderer: instance initialization failed:\n";
            std::cerr << "- " << e.what() << '\n';
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    // create device
    VkResult myvkCreateDevice(
            VkPhysicalDevice physdev,
            const VkDeviceCreateInfo* info,
            const VkAllocationCallbacks* alloc,
            VkDevice* device) {
        if (!layer_info || !instance_info) {
            std::cerr << "MAKO Renderer: device creation requested before "
                         "instance initialization\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        // apply layer chaining
        auto* layerInfo = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(info->pNext));
        while (layerInfo && (layerInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO
                || layerInfo->function != VK_LAYER_LINK_INFO)) {
            layerInfo = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(layerInfo->pNext));
        }
        if (!layerInfo) {
            std::cerr << "MAKO Renderer: no layer info found in pNext chain, "
                "the previous layer does not follow spec\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        auto* linkInfo = layerInfo->u.pLayerInfo;
        if (!linkInfo) {
            std::cerr << "MAKO Renderer: link info is null, "
                "the previous layer does not follow spec\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        instance_info->funcs.GetDeviceProcAddr = linkInfo->pfnNextGetDeviceProcAddr;
        if (!linkInfo->pfnNextGetDeviceProcAddr) {
            std::cerr << "MAKO Renderer: next layer's vkGetDeviceProcAddr is null, "
                "the previous layer does not follow spec\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        layerInfo->u.pLayerInfo = linkInfo->pNext; // advance for next layer

        // fetch device loader functions
        layerInfo = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(info->pNext));
        while (layerInfo && (layerInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO
                || layerInfo->function != VK_LOADER_DATA_CALLBACK)) {
            layerInfo = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(layerInfo->pNext));
        }
        if (!layerInfo) {
            std::cerr << "MAKO Renderer: no layer loader data found in pNext chain.\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        auto* setLoaderData = layerInfo->u.pfnSetDeviceLoaderData;
        if (!setLoaderData) {
            std::cerr << "MAKO Renderer: instance loader data function is null.\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        const bool frameGenerationInteropEnabled =
            layer_info->root.frameGenerationInteropProvisioned();
        const bool scalingEngineProvisioned =
            layer_info->root.scalingEngineProvisioned();
        const auto swapchainMaintenance1Extension =
            supportedSwapchainMaintenance1Extension(
                physdev, scalingEngineProvisioned
            );
        const auto layerQueueFamily = selectLayerQueueFamily(
            physdev, *info, scalingEngineProvisioned
        );
        bool presentRetirementEnabled = false;
        bool lowerDeviceCreated = false;
        const auto rollbackCreatedDevice = [&]() noexcept {
            if (!lowerDeviceCreated || !device || *device == VK_NULL_HANDLE)
                return;

            auto* vkDestroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(
                instance_info->funcs.GetDeviceProcAddr(
                    *device, "vkDestroyDevice"
                )
            );
            if (vkDestroyDevice) {
                vkDestroyDevice(*device, alloc);
                *device = VK_NULL_HANDLE;
            } else {
                std::cerr << "MAKO Renderer: failed to roll back a lower Vulkan device "
                             "after layer initialization failed\n";
            }
            lowerDeviceCreated = false;
        };

        // create device
        try {
            VkDeviceCreateInfo newInfo = *info;
            layer_info->root.modifyDeviceCreateInfo(
                newInfo,
                swapchainMaintenance1Extension.value_or(nullptr),
                [&, newInfo = &newInfo]() {
                    presentRetirementEnabled =
                        swapchainMaintenance1Enabled(*newInfo);
                    auto res = instance_info->funcs.CreateDevice(physdev, newInfo, alloc, device);
                    if (res != VK_SUCCESS)
                        throw ls::vulkan_error(res, "vkCreateDevice() failed");
                    lowerDeviceCreated = true;
                }
            );
        } catch (const ls::vulkan_error& e) {
            rollbackCreatedDevice();
            if (e.error() == VK_ERROR_EXTENSION_NOT_PRESENT)
                std::cerr << "MAKO Renderer: required Vulkan device extensions are not present. "
                    "Your GPU driver is not supported.\n";
            return e.error();
        } catch (const std::exception& e) {
            rollbackCreatedDevice();
            std::cerr << "MAKO Renderer: device creation failed:\n";
            std::cerr << "- " << e.what() << '\n';
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        if (presentRetirementEnabled)
            instance_info->presentRetirementDevices.insert(*device);

        // No game profile matched when this device was created. Keep only the
        // layer lifecycle hooks needed to chain and clean up correctly; all
        // presentation entrypoints are forwarded directly by the GPA paths.
        if (!layer_info->root.active()) {
            instance_info->nativeDevices.insert(*device);
            lowerDeviceCreated = false;
            return VK_SUCCESS;
        }

        // create layer instance
        bool layerDeviceInserted = false;
        const auto discardLayerDevice = [&]() noexcept {
            std::erase_if(
                instance_info->queueIdentities,
                [deviceHandle = *device](const auto& entry) {
                    return entry.second.device == deviceHandle;
                }
            );
            if (layerDeviceInserted) {
                instance_info->devices.erase(*device);
                layerDeviceInserted = false;
            }
            instance_info->presentRetirementDevices.erase(*device);
        };
        try {
            if (!layerQueueFamily) {
                throw ls::error(scalingEngineProvisioned
                    ? "the application did not create an ordinary queue family "
                      "with graphics and compute support"
                    : "the application did not create an ordinary graphics queue family");
            }
            auto [wrapper, inserted] = instance_info->devices.emplace(
                *device,
                vk::Vulkan(
                    instance_info->handles.front(), *device, physdev,
                    instance_info->funcs, vk::initVulkanDeviceFuncs(instance_info->funcs, *device,
                        true, frameGenerationInteropEnabled),
                    *layerQueueFamily,
                    frameGenerationInteropEnabled,
                    true, setLoaderData
                )
            );
            if (!inserted)
                throw ls::error("duplicate Vulkan device handle");
            layerDeviceInserted = true;

            // Queue handles are stable for the device lifetime. Register
            // every ordinary queue the application requested so scaling can
            // submit only on the exact externally-synchronized presentation
            // queue and only with a compatible command-pool family.
            for (uint32_t i = 0; i < info->queueCreateInfoCount; ++i) {
                const auto& requested = info->pQueueCreateInfos[i];
                if (requested.flags != 0) {
                    continue;
                }
                for (uint32_t queueIndex = 0;
                        queueIndex < requested.queueCount; ++queueIndex) {
                    VkQueue queue{VK_NULL_HANDLE};
                    wrapper->second.df().GetDeviceQueue(
                        *device, requested.queueFamilyIndex,
                        queueIndex, &queue
                    );
                    if (queue == VK_NULL_HANDLE)
                        continue;
                    if (queue != wrapper->second.queue() &&
                            wrapper->second.loaderdatafunc()) {
                        const auto loaderResult =
                            (*wrapper->second.loaderdatafunc())(*device, queue);
                        if (loaderResult != VK_SUCCESS) {
                            throw ls::vulkan_error(
                                loaderResult,
                                "vkSetDeviceLoaderData() failed for application queue"
                            );
                        }
                    }
                    instance_info->queueIdentities.insert_or_assign(
                        queue,
                        InstanceInfo::QueueIdentity{
                            .device = *device,
                            .familyIndex = requested.queueFamilyIndex,
                        }
                    );
                }
            }
        } catch (const std::exception& e) {
            // Destroy layer-owned command pools/cache while the valid lower
            // VkDevice still exists; only then may rollback destroy the device
            // or native fallback expose it without a stale wrapper.
            discardLayerDevice();
            if (scalingEngineProvisioned) {
                rollbackCreatedDevice();
                std::cerr << "MAKO Renderer: device initialization failed for "
                             "a provisioned Scaling Engine profile; refusing "
                             "native fallback after fixed-surface capability "
                             "virtualization:\n"
                          << "- " << e.what() << '\n';
                return VK_ERROR_FEATURE_NOT_PRESENT;
            }
            // The lower layer already returned a valid VkDevice. Do not leave
            // it exposed to MAKO's swapchain hooks without a matching Vulkan
            // wrapper: retain the device as a completely native pass-through.
            instance_info->nativeDevices.insert(*device);
            std::cerr << "MAKO Renderer: device initialization failed; "
                         "native Vulkan device retained:\n";
            std::cerr << "- " << e.what() << '\n';
        }

        lowerDeviceCreated = false;

        return VK_SUCCESS;
    }

    // destroy device
    void myvkDestroyDevice(VkDevice device, const VkAllocationCallbacks* alloc) {
        if (!instance_info)
            return;

        collectRetiredSwapchains(
            device, 250'000'000, "device-destroy"
        );
        forceFinalizeRetiredSwapchains(device);

        // destroy layer instance
        auto it = instance_info->devices.find(device);
        if (it != instance_info->devices.end())
            instance_info->devices.erase(it);
        std::erase_if(
            instance_info->queueIdentities,
            [device](const auto& entry) {
                return entry.second.device == device;
            }
        );
        instance_info->nativeDevices.erase(device);
        instance_info->presentRetirementDevices.erase(device);

        // destroy device
        auto vkDestroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(
            instance_info->funcs.GetDeviceProcAddr(device, "vkDestroyDevice"));
        if (!vkDestroyDevice) {
            std::cerr << "MAKO Renderer: failed to get next layer's vkDestroyDevice, "
                "the previous layer does not follow spec\n";
            return;
        }

        vkDestroyDevice(device, alloc);
    }

    // destroy instance
    void myvkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* alloc) {
        // remove instance handle
        auto it = std::ranges::find(instance_info->handles, instance);
        if (it != instance_info->handles.end())
            instance_info->handles.erase(it);

        const bool lastInstance = instance_info->handles.empty();

        // destroy instance
        auto vkDestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(
            layer_info->GetInstanceProcAddr(instance, "vkDestroyInstance"));
        if (!vkDestroyInstance) {
            std::cerr << "MAKO Renderer: failed to get next layer's vkDestroyInstance, "
                "the previous layer does not follow spec\n";

            if (lastInstance) {
                delete instance_info; // NOLINT (memory management)
                instance_info = nullptr;
                delete layer_info; // NOLINT (memory management)
                layer_info = nullptr;
            }
            return;
        }

        vkDestroyInstance(instance, alloc);

        // The Vulkan loader may unload this shared object as soon as the final
        // vkDestroyInstance() wrapper returns. Root owns a Gamescope feedback
        // monitor thread, so leaving the process-global LayerInfo allocated
        // lets that worker resume inside unmapped layer code after dlclose().
        // Destroy all layer-owned state, including joining the monitor, while
        // our code is still resident. A later loader negotiation can recreate
        // the state if the application opens another Vulkan instance.
        if (lastInstance) {
            delete instance_info; // NOLINT (memory management)
            instance_info = nullptr;
            delete layer_info; // NOLINT (memory management)
            layer_info = nullptr;
        }
    }

    // get optional function pointer override
    PFN_vkVoidFunction getProcAddr(const std::string& name) {
        auto it = layer_info->map.find(name);
        if (it == layer_info->map.end()) return nullptr;

        if (layer_info->root.active()) return it->second;

        // A dormant layer still owns its instance/device lifecycle wrappers,
        // which establish the next-layer pointers and release process state.
        // Swapchain and present entrypoints must bypass MAKO completely because
        // an unmatched application did not enable MAKO's required extensions.
        if (name == "vkCreateInstance" || name == "vkDestroyInstance"
                || name == "vkCreateDevice" || name == "vkDestroyDevice")
            return it->second;

        return nullptr;
    }

    // get instance-level function pointers
    PFN_vkVoidFunction myvkGetInstanceProcAddr(VkInstance instance, const char* name) {
        if (!name) return nullptr;
        // A loader may cache the negotiated GPA function across multiple
        // VkInstance lifetimes without negotiating again. The final instance
        // teardown destroys Root so its monitor thread is joined before a
        // possible dlclose; recreate that process state on the next GPA query.
        if (!layer_info && !initializeLayerInfo()) return nullptr;

        if (std::string_view(name) ==
                "vkGetPhysicalDeviceSurfaceCapabilities2KHR") {
            if (!instance || !layer_info->GetInstanceProcAddr ||
                    !layer_info->GetInstanceProcAddr(instance, name)) {
                return nullptr;
            }
        }

        auto func = getProcAddr(name);
        if (func) return func;

        if (!layer_info->GetInstanceProcAddr) return nullptr;
        return layer_info->GetInstanceProcAddr(instance, name);
    }

    // get device-level function pointers
    PFN_vkVoidFunction myvkGetDeviceProcAddr(VkDevice device, const char* name) {
        if (!name) return nullptr;
        if (!layer_info || !instance_info) return nullptr;

        if (!instance_info->funcs.GetDeviceProcAddr) return nullptr;

        // A lower VkDevice can outlive a failed optional MAKO wrapper
        // initialization. Forward every command for that device directly,
        // retaining only our destruction hook so the pass-through registry is
        // cleaned before the driver destroys the handle.
        if (instance_info->nativeDevices.contains(device)) {
            if (std::string(name) == "vkDestroyDevice")
                return reinterpret_cast<PFN_vkVoidFunction>(myvkDestroyDevice);
            return instance_info->funcs.GetDeviceProcAddr(device, name);
        }

        auto func = getProcAddr(name);
        if (func) return func;

        return instance_info->funcs.GetDeviceProcAddr(device, name);
    }
}

#if defined(MAKO_LAYER_ROLE_SPATIAL_SCALING)
// The split roles are separate DSOs, and Gamescope WSI may replace the surface
// handle between them. Export only a same-thread, one-shot capability relay
// from the lower spatial role. The upper role brackets one downstream query
// and never gains resource or swapchain ownership.
extern "C" __attribute__((visibility("default"))) VkBool32
makoSpatialScalingLookupFixedContract(
        const uint32_t rawOperation,
        const VkPhysicalDevice physicalDevice,
        VkSurfaceKHR* const lowerSurface,
        VkExtent2D* const source,
        VkExtent2D* const presentation,
        float* const factor,
        uint64_t* const policyRevision,
        uint64_t* const queryGeneration) noexcept {
    const auto operation = static_cast<FixedSurfaceCapabilityRelayOperation>(
        rawOperation
    );
    if (operation == FixedSurfaceCapabilityRelayOperation::Begin) {
        fixedSurfaceCapabilityRelay.begin();
        return VK_TRUE;
    }
    if (operation != FixedSurfaceCapabilityRelayOperation::Consume ||
            !lowerSurface || !source || !presentation || !factor ||
            !policyRevision || !queryGeneration) {
        fixedSurfaceCapabilityRelay.begin();
        return VK_FALSE;
    }

    const auto record = fixedSurfaceCapabilityRelay.consume(physicalDevice);
    if (!record)
        return VK_FALSE;

    *lowerSurface = record->lowerSurface;
    *source = record->contract.extents.source;
    *presentation = record->contract.extents.presentation;
    *factor = record->contract.factor;
    *policyRevision = record->contract.policyRevision;
    *queryGeneration = record->contract.queryGeneration;
    return VK_TRUE;
}
#endif

namespace {
    using LowerFixedSurfaceContractLookup = VkBool32 (*) (
        uint32_t, VkPhysicalDevice, VkSurfaceKHR*, VkExtent2D*, VkExtent2D*,
        float*, uint64_t*, uint64_t*
    );

    constexpr std::string_view lowerFixedSurfaceContractSymbol =
        "makoSpatialScalingLookupFixedContract";
    constexpr std::string_view lowerSpatialLayerLibrary =
        "libmako-render-scaling.so";

    struct LowerFixedSurfaceContractLookupState {
        void* handle{nullptr};
        LowerFixedSurfaceContractLookup lookup{nullptr};
        VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
        bool armed{false};

        LowerFixedSurfaceContractLookupState() = default;
        LowerFixedSurfaceContractLookupState(
            const LowerFixedSurfaceContractLookupState&
        ) = delete;
        LowerFixedSurfaceContractLookupState& operator=(
            const LowerFixedSurfaceContractLookupState&
        ) = delete;

        ~LowerFixedSurfaceContractLookupState() {
            reset();
            if (handle)
                dlclose(handle);
        }

        void reset() noexcept {
            if (armed && lookup) {
                static_cast<void>(lookup(
                    static_cast<uint32_t>(
                        FixedSurfaceCapabilityRelayOperation::Begin
                    ),
                    physicalDevice, nullptr, nullptr, nullptr, nullptr,
                    nullptr, nullptr
                ));
            }
            armed = false;
        }

        void begin(const VkPhysicalDevice requestedPhysicalDevice) noexcept {
            physicalDevice = requestedPhysicalDevice;
            if (!lookup)
                return;
            armed = lookup(
                static_cast<uint32_t>(
                    FixedSurfaceCapabilityRelayOperation::Begin
                ),
                physicalDevice, nullptr, nullptr, nullptr, nullptr, nullptr,
                nullptr
            ) == VK_TRUE;
        }

        [[nodiscard]] std::optional<FixedSurfaceCapabilityRelayRecord>
        consume() noexcept {
            if (!armed || !lookup)
                return std::nullopt;

            FixedSurfaceCapabilityRelayRecord record{
                .physicalDevice = physicalDevice,
            };
            const VkBool32 found = lookup(
                static_cast<uint32_t>(
                    FixedSurfaceCapabilityRelayOperation::Consume
                ),
                physicalDevice, &record.lowerSurface,
                &record.contract.extents.source,
                &record.contract.extents.presentation,
                &record.contract.factor, &record.contract.policyRevision,
                &record.contract.queryGeneration
            );
            armed = false;
            if (found != VK_TRUE)
                return std::nullopt;
            return record;
        }
    };

    int findLowerFixedSurfaceContractLookup(
            struct dl_phdr_info* const info, const size_t,
            void* const rawState) {
        auto& state = *static_cast<LowerFixedSurfaceContractLookupState*>(
            rawState
        );
        const std::string_view libraryPath(
            info && info->dlpi_name ? info->dlpi_name : ""
        );
        if (!libraryPath.ends_with(lowerSpatialLayerLibrary))
            return 0;

        void* const handle = dlopen(
            libraryPath.data(), RTLD_NOW | RTLD_LOCAL | RTLD_NOLOAD
        );
        if (!handle)
            return 0;
        const auto lookup = reinterpret_cast<LowerFixedSurfaceContractLookup>(
            dlsym(handle, lowerFixedSurfaceContractSymbol.data())
        );
        if (!lookup) {
            dlclose(handle);
            return 0;
        }
        state.handle = handle;
        state.lookup = lookup;
        return 1;
    }

    void beginLowerFixedSurfaceCapabilityRelay(
            LowerFixedSurfaceContractLookupState& state,
            const VkPhysicalDevice physicalDevice) {
        if (!spatialScalingCapabilityRelayByLayer())
            return;

        dl_iterate_phdr(findLowerFixedSurfaceContractLookup, &state);
        if (!state.lookup)
            return;
        state.begin(physicalDevice);
    }

    struct SurfaceScalingEligibilityResult {
        bool supported{false};
        bool firstObservation{false};
        bool queueSupported{false};
        uint32_t advertisedFormatCount{0};
        uint32_t compatibleFormatCount{0};
    };

    SurfaceScalingEligibilityResult supportsSpatialScalingSurface(
            const VkPhysicalDevice physicalDevice,
            const VkSurfaceKHR surface,
            const VkSurfaceCapabilitiesKHR& capabilities) {
        if (!spatialScalingSurfaceCapabilitiesSupported(capabilities)) {
            return {};
        }
        {
            const std::lock_guard lock(
                instance_info->surfaceScalingMutex
            );
            const auto physical =
                instance_info->surfaceScalingEligibility.find(physicalDevice);
            if (physical !=
                    instance_info->surfaceScalingEligibility.end()) {
                const auto cached = physical->second.find(surface);
                if (cached != physical->second.end()) {
                    return {
                        .supported = cached->second.supported,
                        .firstObservation = false,
                        .queueSupported = cached->second.queueSupported,
                        .advertisedFormatCount =
                            cached->second.advertisedFormatCount,
                        .compatibleFormatCount =
                            cached->second.compatibleFormatCount,
                    };
                }
            }
        }

        bool supported = true;

        bool queueSupported = false;
        if (supported &&
                instance_info->funcs.GetPhysicalDeviceSurfaceSupportKHR) {
            uint32_t familyCount{};
            instance_info->funcs.GetPhysicalDeviceQueueFamilyProperties(
                physicalDevice, &familyCount, nullptr
            );
            std::vector<VkQueueFamilyProperties> families(familyCount);
            instance_info->funcs.GetPhysicalDeviceQueueFamilyProperties(
                physicalDevice, &familyCount, families.data()
            );
            constexpr VkQueueFlags required =
                VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            for (uint32_t family = 0; family < familyCount; ++family) {
                if ((families.at(family).queueFlags & required) != required)
                    continue;
                VkBool32 presentationSupported = VK_FALSE;
                const auto result =
                    instance_info->funcs.GetPhysicalDeviceSurfaceSupportKHR(
                        physicalDevice, family, surface,
                        &presentationSupported
                    );
                if (result == VK_SUCCESS &&
                        presentationSupported == VK_TRUE) {
                    queueSupported = true;
                    break;
                }
            }
        }
        supported = supported && queueSupported;

        constexpr VkFormatFeatureFlags scalerFeatures =
            VK_FORMAT_FEATURE_BLIT_SRC_BIT |
            VK_FORMAT_FEATURE_BLIT_DST_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
            VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
        uint32_t formatCount{};
        std::vector<VkSurfaceFormatKHR> formats;
        if (supported) {
            bool complete = false;
            constexpr uint32_t maximumEnumerationAttempts = 3;
            for (uint32_t attempt = 0;
                    attempt < maximumEnumerationAttempts && !complete;
                    ++attempt) {
                auto result =
                    instance_info->funcs.GetPhysicalDeviceSurfaceFormatsKHR(
                        physicalDevice, surface, &formatCount, nullptr
                    );
                if (result != VK_SUCCESS || formatCount == 0)
                    return {};
                formats.resize(formatCount);
                uint32_t writtenCount = formatCount;
                result =
                    instance_info->funcs.GetPhysicalDeviceSurfaceFormatsKHR(
                        physicalDevice, surface, &writtenCount,
                        formats.data()
                    );
                if (result == VK_SUCCESS) {
                    if (writtenCount == 0)
                        return {};
                    formats.resize(writtenCount);
                    formatCount = writtenCount;
                    complete = true;
                } else if (result != VK_INCOMPLETE) {
                    return {};
                }
            }
            // A partial prefix cannot prove that every advertised format is
            // safe. Leave transient INCOMPLETE results uncached so a later
            // stable query can retry the eligibility decision.
            if (!complete)
                return {};
        }

        constexpr VkFormatFeatureFlags swapchainFeatures =
            VK_FORMAT_FEATURE_BLIT_SRC_BIT |
            VK_FORMAT_FEATURE_BLIT_DST_BIT;
        uint32_t compatibleFormatCount = 0;
        if (supported) {
            compatibleFormatCount = static_cast<uint32_t>(
                std::ranges::count_if(
                    formats.begin(), formats.begin() + formatCount,
                    [&](const VkSurfaceFormatKHR& format) {
                    if (format.format == VK_FORMAT_UNDEFINED)
                        return false;
                    const auto pipeline = classifySwapchainColor(
                        format.format, format.colorSpace, false
                    );
                    if (!spatialScalingColorSupported(pipeline)) {
                        return false;
                    }

                    VkFormatProperties surfaceProperties{};
                    instance_info->funcs.GetPhysicalDeviceFormatProperties(
                        physicalDevice, format.format, &surfaceProperties
                    );
                    if ((surfaceProperties.optimalTilingFeatures &
                            swapchainFeatures) != swapchainFeatures) {
                        return false;
                    }

                    VkFormatProperties workingProperties{};
                    instance_info->funcs.GetPhysicalDeviceFormatProperties(
                        physicalDevice, pipeline.exchangeFormat,
                        &workingProperties
                    );
                    return (workingProperties.optimalTilingFeatures &
                        scalerFeatures) == scalerFeatures;
                    }
                )
            );
        }
        supported = spatialScalingFixedSurfacePreflightSupported(
            queueSupported, formatCount, compatibleFormatCount
        );

        bool firstObservation = false;
        {
            const std::lock_guard lock(
                instance_info->surfaceScalingMutex
            );
            auto& surfaces =
                instance_info->surfaceScalingEligibility[physicalDevice];
            const auto [cached, inserted] = surfaces.emplace(
                surface,
                InstanceInfo::SurfaceScalingEligibility{
                    .supported = supported,
                    .queueSupported = queueSupported,
                    .advertisedFormatCount = formatCount,
                    .compatibleFormatCount = compatibleFormatCount,
                }
            );
            supported = cached->second.supported;
            queueSupported = cached->second.queueSupported;
            formatCount = cached->second.advertisedFormatCount;
            compatibleFormatCount = cached->second.compatibleFormatCount;
            firstObservation = inserted;
        }
        return {
            .supported = supported,
            .firstObservation = firstObservation,
            .queueSupported = queueSupported,
            .advertisedFormatCount = formatCount,
            .compatibleFormatCount = compatibleFormatCount,
        };
    }

    void clearFixedSurfaceScalingContract(
            const VkPhysicalDevice physicalDevice,
            const VkSurfaceKHR surface) {
        if (!instance_info)
            return;

        const std::lock_guard lock(instance_info->surfaceScalingMutex);
        const auto physicalContracts =
            instance_info->fixedSurfaceScalingContracts.find(physicalDevice);
        if (physicalContracts !=
                instance_info->fixedSurfaceScalingContracts.end()) {
            physicalContracts->second.erase(surface);
            if (physicalContracts->second.empty()) {
                instance_info->fixedSurfaceScalingContracts.erase(
                    physicalContracts
                );
            }
        }
    }

    void maybeVirtualizeSurfaceCapabilities(
            const VkPhysicalDevice physicalDevice,
            const VkSurfaceKHR surface,
            VkSurfaceCapabilitiesKHR& capabilities,
            const std::optional<FixedSurfaceCapabilityRelayRecord>&
                lowerRelay) {
        if (!layer_info || !instance_info) {
            return;
        }
        try {
            auto candidate = capabilities;
            const auto relayInput = capabilities.currentExtent;
            const FixedSurfaceScalingContract* const lowerContract =
                lowerRelay ? &lowerRelay->contract : nullptr;
            if (spatialScalingCapabilityRelayByLayer() &&
                    (!lowerContract || !prepareFixedSurfaceCapabilityRelay(
                        candidate, *lowerContract
                    ))) {
                clearFixedSurfaceScalingContract(physicalDevice, surface);
                return;
            }
            const auto selection =
                layer_info->root.modifySurfaceCapabilities(candidate);
            if (!selection) {
                clearFixedSurfaceScalingContract(physicalDevice, surface);
                return;
            }
            if (lowerContract &&
                    (!sameExtent(
                        selection->extents.source,
                        lowerContract->extents.source
                    ) || !sameExtent(
                        selection->extents.presentation,
                        lowerContract->extents.presentation
                    ) || selection->factor != lowerContract->factor)) {
                clearFixedSurfaceScalingContract(physicalDevice, surface);
                std::cerr << "MAKO Renderer: spatial scaling capability relay "
                             "failed closed: reason=lower-contract-mismatch\n";
                return;
            }
            const auto eligibility = supportsSpatialScalingSurface(
                physicalDevice, surface, capabilities
            );
            if (!eligibility.supported) {
                clearFixedSurfaceScalingContract(physicalDevice, surface);
                if (eligibility.firstObservation) {
                    std::cerr << "MAKO Renderer: spatial scaling capability "
                             "virtualization unavailable: reason="
                          << (eligibility.queueSupported
                                ? "no-compatible-sdr-format"
                                : "no-presentation-capable-graphics-compute-queue")
                          << "; queue_supported="
                          << eligibility.queueSupported
                          << "; advertised_formats="
                          << eligibility.advertisedFormatCount
                          << "; compatible_formats="
                          << eligibility.compatibleFormatCount
                          << '\n';
                }
                return;
            }
            uint64_t queryGeneration{};
            FixedSurfaceScalingContract publishedContract;
            {
                const std::lock_guard lock(
                    instance_info->surfaceScalingMutex
                );
                queryGeneration =
                    instance_info->nextSurfaceScalingQueryGeneration++;
                publishedContract = FixedSurfaceScalingContract{
                    .extents = selection->extents,
                    .factor = selection->factor,
                    .policyRevision = selection->policyRevision,
                    .queryGeneration = queryGeneration,
                };
                instance_info->fixedSurfaceScalingContracts[physicalDevice]
                    .insert_or_assign(surface, publishedContract);
            }
#if defined(MAKO_LAYER_ROLE_SPATIAL_SCALING)
            fixedSurfaceCapabilityRelay.publish(
                physicalDevice, surface, publishedContract
            );
#endif
            if (eligibility.firstObservation) {
                std::cerr << "MAKO Renderer: spatial scaling surface virtualized: "
                          << "role=" << layerRoleName
                          << "; source=" << candidate.currentExtent.width << 'x'
                          << candidate.currentExtent.height
                          << "; presentation="
                          << selection->extents.presentation.width << 'x'
                          << selection->extents.presentation.height
                          << "; policy_revision="
                          << selection->policyRevision
                          << "; query_generation=" << queryGeneration
                          << "; queue_supported="
                          << eligibility.queueSupported
                          << "; advertised_formats="
                          << eligibility.advertisedFormatCount
                          << "; compatible_formats="
                          << eligibility.compatibleFormatCount;
                if (lowerContract) {
                    std::cerr << "; relay_input=" << relayInput.width << 'x'
                              << relayInput.height
                              << "; relay_mode="
                              << (sameExtent(
                                      relayInput,
                                      lowerContract->extents.source
                                  )
                                      ? "source-normalized"
                                      : "presentation-preserved")
                              << "; relay_surface_mode="
                              << (lowerRelay->lowerSurface == surface
                                      ? "shared"
                                      : "aliased");
                }
                std::cerr << '\n';
            }
            capabilities = candidate;
        } catch (const std::exception& error) {
            clearFixedSurfaceScalingContract(physicalDevice, surface);
            std::cerr << "MAKO Renderer: spatial scaling capability policy "
                         "failed closed: " << error.what() << '\n';
        }
    }

    VkResult myvkGetPhysicalDeviceSurfaceCapabilitiesKHR(
            const VkPhysicalDevice physicalDevice,
            const VkSurfaceKHR surface,
            VkSurfaceCapabilitiesKHR* capabilities) {
        if (!instance_info ||
                !instance_info->funcs.GetPhysicalDeviceSurfaceCapabilitiesKHR)
            return VK_ERROR_INITIALIZATION_FAILED;
        LowerFixedSurfaceContractLookupState lowerRelayState;
        beginLowerFixedSurfaceCapabilityRelay(
            lowerRelayState, physicalDevice
        );
        const auto result =
            instance_info->funcs.GetPhysicalDeviceSurfaceCapabilitiesKHR(
                physicalDevice, surface, capabilities
            );
        const auto lowerRelay = lowerRelayState.consume();
        if (result == VK_SUCCESS && capabilities) {
            maybeVirtualizeSurfaceCapabilities(
                physicalDevice, surface, *capabilities, lowerRelay
            );
        } else {
            clearFixedSurfaceScalingContract(physicalDevice, surface);
        }
        return result;
    }

    VkResult myvkGetPhysicalDeviceSurfaceCapabilities2KHR(
            const VkPhysicalDevice physicalDevice,
            const VkPhysicalDeviceSurfaceInfo2KHR* surfaceInfo,
            VkSurfaceCapabilities2KHR* capabilities) {
        if (!layer_info || !instance_info || !surfaceInfo || !capabilities)
            return VK_ERROR_INITIALIZATION_FAILED;
        const auto lower = reinterpret_cast<
            PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR>(
                layer_info->GetInstanceProcAddr(
                    instance_info->handles.front(),
                    "vkGetPhysicalDeviceSurfaceCapabilities2KHR"
                )
            );
        if (!lower) {
            clearFixedSurfaceScalingContract(
                physicalDevice, surfaceInfo->surface
            );
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
        LowerFixedSurfaceContractLookupState lowerRelayState;
        beginLowerFixedSurfaceCapabilityRelay(
            lowerRelayState, physicalDevice
        );
        const auto result = lower(physicalDevice, surfaceInfo, capabilities);
        const auto lowerRelay = lowerRelayState.consume();
        if (result == VK_SUCCESS) {
            maybeVirtualizeSurfaceCapabilities(
                physicalDevice, surfaceInfo->surface,
                capabilities->surfaceCapabilities, lowerRelay
            );
        } else {
            clearFixedSurfaceScalingContract(
                physicalDevice, surfaceInfo->surface
            );
        }
        return result;
    }

    void myvkDestroySurfaceKHR(
            const VkInstance instance, const VkSurfaceKHR surface,
            const VkAllocationCallbacks* alloc) {
        if (instance_info) {
            // Destroying the application surface is an explicit terminal
            // boundary: no replacement swapchain on this surface can follow.
            // Drain the maintenance1 lifetime proof and release every deferred
            // lower swapchain before forwarding the surface destruction.
            collectRetiredSwapchainsForSurface(surface);
            const std::lock_guard lock(
                instance_info->surfaceScalingMutex
            );
            for (auto& [physicalDevice, surfaces] :
                    instance_info->surfaceScalingEligibility) {
                static_cast<void>(physicalDevice);
                surfaces.erase(surface);
            }
            for (auto& [physicalDevice, contracts] :
                    instance_info->fixedSurfaceScalingContracts) {
                static_cast<void>(physicalDevice);
                contracts.erase(surface);
            }
            instance_info->variableSurfaceScalingExtents.erase(surface);
        }
        const auto lower = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
            layer_info->GetInstanceProcAddr(instance, "vkDestroySurfaceKHR")
        );
        if (lower)
            lower(instance, surface, alloc);
    }

    VkResult myvkCreateSwapchainKHR(
            VkDevice device,
            const VkSwapchainCreateInfoKHR* info,
            const VkAllocationCallbacks* alloc,
            VkSwapchainKHR* swapchain) {
        const auto& it = instance_info->devices.find(device);
        if (it == instance_info->devices.end())
            return VK_ERROR_INITIALIZATION_FAILED;

        VkSwapchainKHR createdSwapchain{VK_NULL_HANDLE};
        bool lowerSwapchainCreated{false};
        const auto rollbackCreatedSwapchain = [&]() noexcept {
            if (!lowerSwapchainCreated || createdSwapchain == VK_NULL_HANDLE)
                return;

            instance_info->swapchains.erase(createdSwapchain);
            instance_info->swapchainInfos.erase(createdSwapchain);
            instance_info->nativeSwapchains.erase(createdSwapchain);
            try {
                layer_info->root.removeSwapchainContext(createdSwapchain);
            } catch (const std::exception& e) {
                std::cerr << "MAKO Renderer: failed to retire a partially-created "
                             "swapchain context: " << e.what() << '\n';
            }
            it->second.df().DestroySwapchainKHR(
                device, createdSwapchain, alloc
            );
            *swapchain = VK_NULL_HANDLE;
            lowerSwapchainCreated = false;
        };

        try {
            // `oldSwapchain` is retired for new acquisition by the lower WSI,
            // but images acquired before retirement may still be presented.
            // Keep MAKO's old context and scaler resources alive until the
            // application explicitly destroys that handle.

            layer_info->root.update(); // ensure config is up to date

            if (present_diagnostics::enabled()) {
                std::cerr << "MAKO Renderer: present diagnostics: "
                             "operation=swapchain-create-observed"
                          << " role=" << layerRoleName
                          << " surface=" << info->surface
                          << " requested_old_swapchain="
                          << info->oldSwapchain
                          << " requested_width=" << info->imageExtent.width
                          << " requested_height=" << info->imageExtent.height
                          << '\n';
            }

            // create swapchain
            VkSwapchainCreateInfoKHR newInfo = *info;
            std::optional<SpatialScalingExtents> previousVariableExtents;
            std::optional<FixedSurfaceScalingContract> fixedSurfaceContract;
            {
                const std::lock_guard lock(
                    instance_info->surfaceScalingMutex
                );
                const auto previous =
                    instance_info->variableSurfaceScalingExtents.find(
                        info->surface
                    );
                if (previous !=
                        instance_info->variableSurfaceScalingExtents.end()) {
                    previousVariableExtents = previous->second;
                }
                const auto physicalContracts =
                    instance_info->fixedSurfaceScalingContracts.find(
                        it->second.physdev()
                    );
                if (physicalContracts !=
                        instance_info->fixedSurfaceScalingContracts.end()) {
                    const auto contract = physicalContracts->second.find(
                        info->surface
                    );
                    if (contract != physicalContracts->second.end())
                        fixedSurfaceContract = contract->second;
                }
            }
            const auto modification =
                layer_info->root.modifySwapchainCreateInfo(
                    it->second, newInfo, previousVariableExtents,
                    fixedSurfaceContract,
                    [&, newInfo = &newInfo]() {
                        const auto retainedOldSwapchain =
                            claimRetainedSwapchainForReplacement(
                                device,
                                newInfo->surface,
                                newInfo->oldSwapchain
                            );
                        if (retainedOldSwapchain) {
                            newInfo->oldSwapchain = *retainedOldSwapchain;
                            if (present_diagnostics::enabled()) {
                                std::cerr << "MAKO Renderer: present diagnostics: "
                                             "operation=swapchain-retirement-handoff"
                                          << " role=" << layerRoleName
                                          << " swapchain="
                                          << *retainedOldSwapchain
                                          << " surface=" << newInfo->surface
                                          << " reason=null-upper-old-swapchain"
                                          << '\n';
                            }
                        }
                        auto res = it->second.df().CreateSwapchainKHR(
                            device, newInfo, alloc, swapchain);
                        if (res != VK_SUCCESS) {
                            throw ls::vulkan_error(
                                res, "vkCreateSwapchainKHR() failed"
                            );
                        }
                        createdSwapchain = *swapchain;
                        lowerSwapchainCreated = true;
                    }
                );
            const auto commitVariableSurfaceScaling = [&]() {
                const std::lock_guard lock(
                    instance_info->surfaceScalingMutex
                );
                const auto committed = committedVariableSurfaceScalingExtents(
                    previousVariableExtents,
                    layer_info->root.active(),
                    modification.variableSurface,
                    modification.spatialScalingActive,
                    modification.variableFeedbackSuppressed,
                    modification.applicationExtent,
                    modification.presentationExtent
                );
                if (committed) {
                    instance_info->variableSurfaceScalingExtents.insert_or_assign(
                        info->surface, *committed
                    );
                } else {
                    instance_info->variableSurfaceScalingExtents.erase(
                        info->surface
                    );
                }
            };

            // get all swapchain images
            uint32_t imageCount{};
            auto res = it->second.df().GetSwapchainImagesKHR(device, *swapchain,
                &imageCount, VK_NULL_HANDLE);
            if (res != VK_SUCCESS)
                throw ls::vulkan_error(res, "vkGetSwapchainImagesKHR() failed");
            if (imageCount == 0)
                throw ls::vulkan_error(
                    VK_ERROR_INITIALIZATION_FAILED,
                    "vkGetSwapchainImagesKHR() returned no images"
                );

            std::vector<VkImage> swapchainImages(imageCount);
            res = it->second.df().GetSwapchainImagesKHR(device, *swapchain,
                &imageCount, swapchainImages.data());
            if (res != VK_SUCCESS)
                throw ls::vulkan_error(res, "vkGetSwapchainImagesKHR() failed");

            auto& swapchainInfo = instance_info->swapchainInfos.emplace(*swapchain, SwapchainInfo {
                .images = std::move(swapchainImages),
                .surface = info->surface,
                .format = newInfo.imageFormat,
                .colorSpace = newInfo.imageColorSpace,
                .applicationExtent = modification.applicationExtent,
                .extent = newInfo.imageExtent,
                .presentMode = newInfo.presentMode,
                .privateOrderedTransport =
                    modification.privateOrderedTransport,
                .spatialScalingActive =
                    modification.spatialScalingActive,
                .replacement = newInfo.oldSwapchain != VK_NULL_HANDLE,
            }).first->second;

            // An enabled implicit layer can run in a process that does not
            // match a configured game profile (launchers and helper processes
            // are common examples). Keep that process on its original Vulkan
            // presentation path without attempting to construct MAKO's
            // optional backend.
            if (!layer_info->root.active()) {
                instance_info->swapchains.emplace(
                    *swapchain, ls::R<vk::Vulkan>(it->second)
                );
                instance_info->nativeSwapchains.insert(*swapchain);
                commitVariableSurfaceScaling();
                lowerSwapchainCreated = false;
                return res;
            }

            // Creating interpolation resources is optional. If the game's
            // lower swapchain is valid but MAKO's private backend cannot start
            // (for example an unavailable DLL or unsupported private-device
            // capability), retain the real-frame presentation path instead of
            // turning an engine failure into a game startup failure.
            try {
                layer_info->root.createSwapchainContext(
                    it->second, *swapchain, swapchainInfo,
                    instance_info->presentRetirementDevices.contains(device)
                );
            } catch (const std::exception& e) {
                // The lower image is native-sized when scaling is active,
                // while the application intentionally renders only the
                // virtual source rectangle. Falling back to direct native
                // presentation would expose an incomplete top-left frame, so
                // fail creation and let the normal rollback destroy the lower
                // swapchain instead.
                if (swapchainInfo.spatialScalingActive)
                    throw ls::error(
                        "spatial scaling context initialization failed", e
                    );
                instance_info->swapchains.emplace(
                    *swapchain, ls::R<vk::Vulkan>(it->second)
                );
                instance_info->nativeSwapchains.insert(*swapchain);
                commitVariableSurfaceScaling();
                lowerSwapchainCreated = false;
                std::cerr << "MAKO Renderer: frame-generation context initialization "
                             "failed; native presentation retained:\n"
                          << "- " << e.what() << '\n';
                return res;
            }

            instance_info->swapchains.emplace(*swapchain,
                ls::R<vk::Vulkan>(it->second));

            commitVariableSurfaceScaling();
            lowerSwapchainCreated = false;
            return res;
        } catch (const ls::vulkan_error& e) {
            rollbackCreatedSwapchain();
            std::cerr << "MAKO Renderer: swapchain creation failed:\n";
            std::cerr << "- " << e.what() << '\n';
            return e.error();
        } catch (const std::exception& e) {
            rollbackCreatedSwapchain();
            std::cerr << "MAKO Renderer: swapchain creation failed:\n";
            std::cerr << "- " << e.what() << '\n';
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    VkResult myvkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
        VkResult result = VK_SUCCESS;
        bool swapchainOutOfDate = false;

        // Binary application wait semaphores belong to the whole present
        // batch and may be consumed only once. The existing per-swapchain FG
        // transport cannot yet preserve that contract for multiple scaled
        // swapchains, so reject before submitting any work rather than wait
        // twice, deadlock, or misassociate per-swapchain pNext arrays.
        if (info->swapchainCount > 1) {
            bool containsSpatialScaling = false;
            for (uint32_t i = 0; i < info->swapchainCount; ++i) {
                const auto metadata = instance_info->swapchainInfos.find(
                    info->pSwapchains[i]
                );
                containsSpatialScaling = containsSpatialScaling ||
                    (metadata != instance_info->swapchainInfos.end() &&
                     metadata->second.spatialScalingActive);
            }
            if (containsSpatialScaling) {
                constexpr VkResult unsupported =
                    VK_ERROR_UNKNOWN;
                if (info->pResults) {
                    std::fill_n(
                        info->pResults, info->swapchainCount, unsupported
                    );
                }
                std::cerr << "MAKO Renderer: multi-swapchain spatial-scaling "
                             "present rejected before semaphore consumption; "
                             "batch scaling is not supported\n";
                return unsupported;
            }
        }

        // A context that failed after the lower swapchain was created remains
        // a normal application swapchain. Bypass MAKO for the whole batch so
        // the driver's wait-semaphore and per-swapchain result semantics stay
        // exactly as the application supplied them.
        for (size_t i = 0; i < info->swapchainCount; ++i) {
            const auto swapchain = info->pSwapchains[i];
            if (!instance_info->nativeSwapchains.contains(swapchain))
                continue;

            const auto mapping = instance_info->swapchains.find(swapchain);
            if (mapping == instance_info->swapchains.end())
                return VK_ERROR_INITIALIZATION_FAILED;
            return mapping->second.get().df().QueuePresentKHR(queue, info);
        }

        // ensure layer config is up to date
        ConfigurationUpdateResult configurationUpdate;
        try {
            configurationUpdate = layer_info->root.update();
        } catch (const std::exception&) {
            configurationUpdate = {}; // retain the last valid configuration
        }

        if (configurationUpdate.reloaded) {
            std::cerr << "MAKO Renderer: updated configuration in place; contexts="
                      << configurationUpdate.liveContextsUpdated << '\n';
            if (configurationUpdate.swapchainRecreationDeferredContexts > 0)
                std::cerr << "MAKO Renderer: configuration changes requiring GPU resource "
                             "reconstruction remain pending until a game-owned "
                             "swapchain recreation; contexts="
                          << configurationUpdate.swapchainRecreationDeferredContexts
                          << '\n';
            if (configurationUpdate.recreationRequestedContexts > 0)
                std::cerr << "MAKO Renderer: live profile-resource changes will request "
                             "game-owned swapchain recreation after the current lower "
                             "present accepts a maintenance1 retirement fence; contexts="
                          << configurationUpdate.recreationRequestedContexts
                          << '\n';
            if (configurationUpdate.processRestartDeferredContexts > 0 ||
                    configurationUpdate.processProfileChangeDeferred ||
                    configurationUpdate.globalChangeDeferred) {
                std::cerr << "MAKO Renderer: process-static backend configuration remains "
                             "pending until the game restarts; contexts="
                          << configurationUpdate.processRestartDeferredContexts
                          << "; profile="
                          << configurationUpdate.processProfileChangeDeferred
                          << "; global="
                          << configurationUpdate.globalChangeDeferred << '\n';
            }
        }

        // present each swapchain
        for (size_t i = 0; i < info->swapchainCount; i++) {
            const auto& swapchain = info->pSwapchains[i];

            const auto& it = instance_info->swapchains.find(swapchain);
            if (it == instance_info->swapchains.end())
                return VK_ERROR_INITIALIZATION_FAILED;

            try {
                const auto metadata =
                    instance_info->swapchainInfos.find(swapchain);
                if (metadata != instance_info->swapchainInfos.end() &&
                        metadata->second.spatialScalingActive) {
                    const auto identity =
                        instance_info->queueIdentities.find(queue);
                    if (identity == instance_info->queueIdentities.end() ||
                            identity->second.familyIndex !=
                                it->second.get().queueFamilyIndex() ||
                            queue != it->second.get().queue()) {
                        throw ls::vulkan_error(
                            VK_ERROR_UNKNOWN,
                            "spatial scaling requires the application's "
                            "ordinary queue 0 from MAKO's graphics/compute "
                            "presentation family"
                        );
                    }
                }
                const auto waitSemaphores = info->waitSemaphoreCount > 0
                    ? std::span<const VkSemaphore>(
                        info->pWaitSemaphores, info->waitSemaphoreCount
                    )
                    : std::span<const VkSemaphore>{};

                auto& context = layer_info->root.getSwapchainContext(swapchain);
                result = context.present(it->second,
                    queue, swapchain,
                    const_cast<void*>(info->pNext),
                    info->pImageIndices[i],
                    waitSemaphores
                );
                const bool liveProfileRecreationRequested =
                    context.requestLiveProfileResourceRecreationAfterPresent(
                        result
                    );
                if (liveProfileRecreationRequested)
                    result = VK_ERROR_OUT_OF_DATE_KHR;
                if (result == VK_ERROR_OUT_OF_DATE_KHR &&
                        present_diagnostics::enabled()) {
                    std::cerr << "MAKO Renderer: present diagnostics: "
                                 "operation=swapchain-recreation-observed"
                              << " context=" << context.diagnosticsId()
                              << " role=" << layerRoleName
                              << " swapchain=" << swapchain
                              << " source="
                              << (liveProfileRecreationRequested
                                    ? "guarded-live-profile-request"
                                    : "upstream-or-driver")
                              << '\n';
                }
            } catch (const ls::vulkan_error& e) {
                if (e.error() != VK_ERROR_OUT_OF_DATE_KHR) {
                    std::cerr << "MAKO Renderer: swapchain presentation failed:\n";
                    std::cerr << "- " << e.what() << '\n';
                } // silently swallow out-of-date errors

                result = e.error();
            } catch (const std::exception& e) {
                std::cerr << "MAKO Renderer: swapchain presentation failed:\n";
                std::cerr << "- " << e.what() << '\n';
                result = VK_ERROR_UNKNOWN;
            }

            if (result != VK_SUCCESS && info->pResults)
                info->pResults[i] = result;

            if (result == VK_ERROR_OUT_OF_DATE_KHR)
                swapchainOutOfDate = true;
        }

        const auto queueIdentity = instance_info->queueIdentities.find(queue);
        if (queueIdentity != instance_info->queueIdentities.end())
            collectRetiredSwapchainsAfterPresent(
                queueIdentity->second.device, *info
            );

        // Preserve a genuine game/driver out-of-date result, or MAKO's guarded
        // one-shot live-scaling recreation request, across the present batch.
        return swapchainOutOfDate ? VK_ERROR_OUT_OF_DATE_KHR : result;
#pragma clang diagnostic pop
    }

    void myvkDestroySwapchainKHR(
            VkDevice device,
            VkSwapchainKHR swapchain,
            const VkAllocationCallbacks* alloc) {
        const auto& it = instance_info->devices.find(device);
        if (it == instance_info->devices.end())
            return;
        {
            const std::lock_guard retirementLock(
                instance_info->retiredSwapchainsMutex
            );
            if (instance_info->retiredSwapchains.contains(swapchain)) {
                std::cerr << "MAKO Renderer: duplicate deferred swapchain "
                             "destruction ignored\n";
                return;
            }
        }

        const auto swapchainMetadata =
            instance_info->swapchainInfos.find(swapchain);
        const VkSurfaceKHR surface = swapchainMetadata ==
                instance_info->swapchainInfos.end()
            ? VK_NULL_HANDLE
            : swapchainMetadata->second.surface;
        if (present_diagnostics::enabled()) {
            std::cerr << "MAKO Renderer: present diagnostics: "
                         "operation=swapchain-destroy-observed"
                      << " role=" << layerRoleName
                      << " swapchain=" << swapchain
                      << " surface=" << surface
                      << " allocation_callbacks=" << (alloc ? 1 : 0)
                      << '\n';
        }
        instance_info->swapchainInfos.erase(swapchain);
        instance_info->swapchains.erase(swapchain);
        const bool native = instance_info->nativeSwapchains.erase(swapchain) > 0;
        auto context = layer_info->root.takeSwapchainContext(swapchain);

        if (!native && context && context->presentRetirementEnabled()) {
            if (!alloc) {
                const auto now = std::chrono::steady_clock::now();
                bool inserted = false;
                size_t pending = 0;
                {
                    const std::lock_guard retirementLock(
                        instance_info->retiredSwapchainsMutex
                    );
                    inserted = instance_info->retiredSwapchains.emplace(
                            swapchain,
                            InstanceInfo::RetiredSwapchain{
                                .device = device,
                                .surface = surface,
                                .vk = ls::R<vk::Vulkan>(it->second),
                                .context = std::move(context),
                                .notBefore = now + swapchainRetirementGracePeriod,
                                .replacementHandoffConsumed = false,
                            }
                        ).second;
                    if (inserted) {
                        instance_info->retiredSwapchainCount.fetch_add(
                            1, std::memory_order_release
                        );
                    }
                    pending = instance_info->retiredSwapchains.size();
                }
                if (!inserted) {
                    std::cerr << "MAKO Renderer: duplicate deferred swapchain "
                                 "retirement ignored\n";
                    return;
                }
                if (present_diagnostics::enabled()) {
                    std::cerr << "MAKO Renderer: present diagnostics: "
                                 "operation=swapchain-retirement-deferred"
                              << " role=" << layerRoleName
                              << " swapchain=" << swapchain
                              << " reason=await-later-present-and-fence"
                              << " grace_ms="
                              << swapchainRetirementGracePeriod.count()
                              << " pending="
                              << pending
                              << '\n';
                }
                return;
            }
        }

        // A custom allocator cannot be retained because its pUserData lifetime
        // ends with this call. The same synchronous wait also covers
        // layer-owned fences recorded before an upstream present-fence owner
        // was observed. A valid owner has already made this wait immediately
        // satisfiable before requesting destruction.
        if (context && !context->waitForPresentRetirement(
                it->second, UINT64_MAX)) {
            std::cerr << "MAKO Renderer: synchronous swapchain retirement "
                         "failed; forwarding lower destruction without a "
                         "completed layer-owned fence\n";
        }
        context.reset();
        it->second.df().DestroySwapchainKHR(device, swapchain, alloc);
    }
}

namespace {
    bool initializeLayerInfo() {
        if (layer_info) return true;

        try {
            layer_info = new LayerInfo { // NOLINT (memory management)
                .map = {
#define VKPTR(name) reinterpret_cast<PFN_vkVoidFunction>(name)
                    { "vkCreateInstance", VKPTR(myvkCreateInstance) },
                    { "vkCreateDevice", VKPTR(myvkCreateDevice) },
                    { "vkDestroyDevice", VKPTR(myvkDestroyDevice) },
                    { "vkDestroyInstance", VKPTR(myvkDestroyInstance) },
                    { "vkDestroySurfaceKHR", VKPTR(myvkDestroySurfaceKHR) },
                    { "vkGetPhysicalDeviceSurfaceCapabilitiesKHR",
                        VKPTR(myvkGetPhysicalDeviceSurfaceCapabilitiesKHR) },
                    { "vkGetPhysicalDeviceSurfaceCapabilities2KHR",
                        VKPTR(myvkGetPhysicalDeviceSurfaceCapabilities2KHR) },
                    { "vkCreateSwapchainKHR", VKPTR(myvkCreateSwapchainKHR) },
                    { "vkQueuePresentKHR", VKPTR(myvkQueuePresentKHR) },
                    { "vkDestroySwapchainKHR", VKPTR(myvkDestroySwapchainKHR) }
#undef VKPTR
                },
                .root = Root()
            };

        } catch (const std::exception& e) {
            std::cerr << "MAKO Renderer: layer initialization failed:\n";
            std::cerr << "- " << e.what() << '\n';
            return false;
        }

        return true;
    }
}

// Interface-version negotiation is the modern layer ABI. Keep the traditional
// proc-address exports as a compatibility path for older loaders.
__attribute__((visibility("default")))
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(
        VkInstance instance, const char* name) {
    return myvkGetInstanceProcAddr(instance, name);
}

__attribute__((visibility("default")))
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(
        VkDevice device, const char* name) {
    return myvkGetDeviceProcAddr(device, name);
}

/// Vulkan layer entrypoint
__attribute__((visibility("default")))
VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(
        VkNegotiateLayerInterface* pVersionStruct) {
    // ensure loader compatibility
    if (!pVersionStruct
        || pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT
        || pVersionStruct->loaderLayerInterfaceVersion < 2)
        return VK_ERROR_INITIALIZATION_FAILED;

    // if the layer has already been initialized, skip
    if (layer_info) {
        pVersionStruct->loaderLayerInterfaceVersion = 2;
        pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
        pVersionStruct->pfnGetDeviceProcAddr = myvkGetDeviceProcAddr;
        pVersionStruct->pfnGetInstanceProcAddr = myvkGetInstanceProcAddr;
        return VK_SUCCESS;
    }

    // load the layer configuration
    if (!initializeLayerInfo())
        return VK_ERROR_INITIALIZATION_FAILED;

    // emplace function pointers/version
    pVersionStruct->loaderLayerInterfaceVersion = 2;
    pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
    pVersionStruct->pfnGetDeviceProcAddr = myvkGetDeviceProcAddr;
    pVersionStruct->pfnGetInstanceProcAddr = myvkGetInstanceProcAddr;
    return VK_SUCCESS;
}
