/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "instance.hpp"
#include "color_pipeline.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/pointers.hpp"
#include "mako-common/vulkan/vulkan.hpp"
#include "swapchain.hpp"

#include <algorithm>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <mutex>
#include <string_view>
#include <string>
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
        struct QueueIdentity {
            VkDevice device{VK_NULL_HANDLE};
            uint32_t familyIndex{UINT32_MAX};
        };
        std::unordered_map<VkQueue, QueueIdentity> queueIdentities;
        struct SurfaceScalingEligibility {
            bool supported{false};
        };
        std::mutex surfaceScalingMutex;
        std::unordered_map<
            VkPhysicalDevice,
            std::unordered_map<VkSurfaceKHR, SurfaceScalingEligibility>
        > surfaceScalingEligibility;
        std::unordered_map<VkSurfaceKHR, SpatialScalingExtents>
            variableSurfaceScalingExtents;
        std::unordered_map<VkSwapchainKHR, ls::R<vk::Vulkan>> swapchains;
        std::unordered_map<VkSwapchainKHR, SwapchainInfo> swapchainInfos;
        std::unordered_set<VkSwapchainKHR> nativeSwapchains;
    }* instance_info; // NOLINT (global variable)

    bool initializeLayerInfo();

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
            layer_info->root.frameGenerationConfigured();
        const bool spatialScalingConfigured =
            layer_info->root.spatialScalingConfigured();
        const auto layerQueueFamily = selectLayerQueueFamily(
            physdev, *info, spatialScalingConfigured
        );
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
            layer_info->root.modifyDeviceCreateInfo(newInfo,
                [&, newInfo = &newInfo]() {
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
        };
        try {
            if (!layerQueueFamily) {
                throw ls::error(spatialScalingConfigured
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
            if (spatialScalingConfigured) {
                rollbackCreatedDevice();
                std::cerr << "MAKO Renderer: device initialization failed for "
                             "an enabled spatial-scaling profile; refusing "
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

namespace {
    struct SurfaceScalingEligibilityResult {
        bool supported{false};
        bool firstObservation{false};
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
        if (supported) {
            supported = std::ranges::all_of(
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
            );
        }

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
                }
            );
            supported = cached->second.supported;
            firstObservation = inserted;
        }
        return {
            .supported = supported,
            .firstObservation = firstObservation,
        };
    }

    void maybeVirtualizeSurfaceCapabilities(
            const VkPhysicalDevice physicalDevice,
            const VkSurfaceKHR surface,
            VkSurfaceCapabilitiesKHR& capabilities) {
        if (!layer_info || !instance_info) {
            return;
        }
        try {
            auto candidate = capabilities;
            if (!layer_info->root.modifySurfaceCapabilities(candidate))
                return;
            const auto eligibility = supportsSpatialScalingSurface(
                physicalDevice, surface, capabilities
            );
            if (!eligibility.supported) {
                if (eligibility.firstObservation) {
                    std::cerr << "MAKO Renderer: spatial scaling capability "
                             "virtualization unavailable: not every advertised "
                             "surface format and presentation queue has the "
                             "required SDR blit/compute support\n";
                }
                return;
            }
            if (eligibility.firstObservation) {
                std::cerr << "MAKO Renderer: spatial scaling surface virtualized: "
                          << "source=" << candidate.currentExtent.width << 'x'
                          << candidate.currentExtent.height
                          << "; presentation="
                          << capabilities.currentExtent.width << 'x'
                          << capabilities.currentExtent.height << '\n';
            }
            capabilities = candidate;
        } catch (const std::exception& error) {
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
        const auto result =
            instance_info->funcs.GetPhysicalDeviceSurfaceCapabilitiesKHR(
                physicalDevice, surface, capabilities
            );
        if (result == VK_SUCCESS && capabilities)
            maybeVirtualizeSurfaceCapabilities(
                physicalDevice, surface, *capabilities
            );
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
        if (!lower)
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        const auto result = lower(physicalDevice, surfaceInfo, capabilities);
        if (result == VK_SUCCESS) {
            maybeVirtualizeSurfaceCapabilities(
                physicalDevice, surfaceInfo->surface,
                capabilities->surfaceCapabilities
            );
        }
        return result;
    }

    void myvkDestroySurfaceKHR(
            const VkInstance instance, const VkSurfaceKHR surface,
            const VkAllocationCallbacks* alloc) {
        if (instance_info) {
            const std::lock_guard lock(
                instance_info->surfaceScalingMutex
            );
            for (auto& [physicalDevice, surfaces] :
                    instance_info->surfaceScalingEligibility) {
                static_cast<void>(physicalDevice);
                surfaces.erase(surface);
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

            // create swapchain
            VkSwapchainCreateInfoKHR newInfo = *info;
            std::optional<SpatialScalingExtents> previousVariableExtents;
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
            }
            const auto modification =
                layer_info->root.modifySwapchainCreateInfo(
                it->second, newInfo, previousVariableExtents,
                [&, newInfo = &newInfo]() {
                    auto res = it->second.df().CreateSwapchainKHR(
                        device, newInfo, alloc, swapchain);
                    if (res != VK_SUCCESS)
                        throw ls::vulkan_error(res, "vkCreateSwapchainKHR() failed");
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
                .format = newInfo.imageFormat,
                .colorSpace = newInfo.imageColorSpace,
                .applicationExtent = modification.applicationExtent,
                .extent = newInfo.imageExtent,
                .presentMode = newInfo.presentMode,
                .privateOrderedTransport =
                    modification.privateOrderedTransport,
                .spatialScalingActive =
                    modification.spatialScalingActive,
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
                    it->second, *swapchain, swapchainInfo
                );
            } catch (const std::exception& e) {
                // The lower image is native-sized when scaling is active,
                // while the application intentionally renders only the
                // virtual source rectangle. Native passthrough would expose
                // an incomplete top-left frame, so fail creation and let the
                // normal rollback destroy the lower swapchain instead.
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
            if (configurationUpdate.deferredContexts > 0)
                std::cerr << "MAKO Renderer: configuration changes requiring GPU resource "
                             "reconstruction remain pending until a game-owned "
                             "swapchain recreation; contexts="
                          << configurationUpdate.deferredContexts << '\n';
            if (configurationUpdate.processRestartContexts > 0)
                std::cerr << "MAKO Renderer: process-static configuration changes "
                             "remain pending until game restart; contexts="
                          << configurationUpdate.processRestartContexts << '\n';
            if (configurationUpdate.recreationRequestedContexts > 0)
                std::cerr << "MAKO Renderer: live profile-resource changes will request "
                             "game-owned swapchain recreation after the current lower "
                             "present consumes its wait semaphores; contexts="
                          << configurationUpdate.recreationRequestedContexts
                          << '\n';
            if (configurationUpdate.globalChangeDeferred)
                std::cerr << "MAKO Renderer: global backend construction changed; "
                             "the new DLL or FP16 setting applies on process restart\n";
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
                if (context.requestLiveProfileResourceRecreationAfterPresent(
                        result))
                    result = VK_ERROR_OUT_OF_DATE_KHR;
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

        const auto& info_mapping = instance_info->swapchainInfos.find(swapchain);
        if (info_mapping != instance_info->swapchainInfos.end())
            instance_info->swapchainInfos.erase(info_mapping);

        const auto& mapping = instance_info->swapchains.find(swapchain);
        if (mapping != instance_info->swapchains.end())
            instance_info->swapchains.erase(mapping);
        instance_info->nativeSwapchains.erase(swapchain);

        layer_info->root.removeSwapchainContext(swapchain);

        // destroy swapchain
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
