/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "instance.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/pointers.hpp"
#include "mako-common/vulkan/vulkan.hpp"
#include "swapchain.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <string>
#include <unordered_map>
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
        std::unordered_map<VkSwapchainKHR, ls::R<vk::Vulkan>> swapchains;
        std::unordered_map<VkSwapchainKHR, SwapchainInfo> swapchainInfos;
    }* instance_info; // NOLINT (global variable)

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

        try {
            VkInstanceCreateInfo newInfo = *info;
            layer_info->root.modifyInstanceCreateInfo(newInfo,
                [=, newInfo = &newInfo]() {
                    auto res = vkCreateInstance(newInfo, alloc, instance);
                    if (res != VK_SUCCESS)
                        throw ls::vulkan_error(res, "vkCreateInstance() failed");
                }
            );

            if (!instance_info)
                instance_info = new InstanceInfo{ // NOLINT (memory management)
                    .funcs = vk::initVulkanInstanceFuncs(*instance,
                        layer_info->GetInstanceProcAddr, true),
                };

            instance_info->handles.push_back(*instance);

            return VK_SUCCESS;
        } catch (const ls::vulkan_error& e) {
            if (e.error() == VK_ERROR_EXTENSION_NOT_PRESENT)
                std::cerr << "MAKO Renderer: required Vulkan instance extensions are not present. "
                    "Your GPU driver is not supported.\n";
            return e.error();
        }
    }

    // create device
    VkResult myvkCreateDevice(
            VkPhysicalDevice physdev,
            const VkDeviceCreateInfo* info,
            const VkAllocationCallbacks* alloc,
            VkDevice* device) {
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

        // create device
        try {
            VkDeviceCreateInfo newInfo = *info;
            layer_info->root.modifyDeviceCreateInfo(newInfo,
                [=, newInfo = &newInfo]() {
                    auto res = instance_info->funcs.CreateDevice(physdev, newInfo, alloc, device);
                    if (res != VK_SUCCESS)
                        throw ls::vulkan_error(res, "vkCreateDevice() failed");
                }
            );
        } catch (const ls::vulkan_error& e) {
            if (e.error() == VK_ERROR_EXTENSION_NOT_PRESENT)
                std::cerr << "MAKO Renderer: required Vulkan device extensions are not present. "
                    "Your GPU driver is not supported.\n";
            return e.error();
        }

        // create layer instance
        try {
            instance_info->devices.emplace(
                *device,
                vk::Vulkan(
                    instance_info->handles.front(), *device, physdev,
                    instance_info->funcs, vk::initVulkanDeviceFuncs(instance_info->funcs, *device,
                        true),
                    true, setLoaderData
                )
            );
        } catch (const std::exception& e) {
            std::cerr << "MAKO Renderer: initialization failed:\n";
            std::cerr << "- " << e.what() << '\n';
        }

        return VK_SUCCESS;
    }

    // destroy device
    void myvkDestroyDevice(VkDevice device, const VkAllocationCallbacks* alloc) {
        // destroy layer instance
        auto it = instance_info->devices.find(device);
        if (it != instance_info->devices.end())
            instance_info->devices.erase(it);

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
        if (it != layer_info->map.end())
            return it->second;
        return nullptr;
    }

    // get instance-level function pointers
    PFN_vkVoidFunction myvkGetInstanceProcAddr(VkInstance instance, const char* name) {
        if (!name) return nullptr;

        auto func = getProcAddr(name);
        if (func) return func;

        if (!layer_info->GetInstanceProcAddr) return nullptr;
        return layer_info->GetInstanceProcAddr(instance, name);
    }

    // get device-level function pointers
    PFN_vkVoidFunction myvkGetDeviceProcAddr(VkDevice device, const char* name) {
        if (!name) return nullptr;

        auto func = getProcAddr(name);
        if (func) return func;

        if (!instance_info->funcs.GetDeviceProcAddr) return nullptr;
        return instance_info->funcs.GetDeviceProcAddr(device, name);
    }
}

namespace {
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
            // retire old swapchain
            if (info->oldSwapchain) {
                const auto& info_mapping = instance_info->swapchainInfos.find(info->oldSwapchain);
                if (info_mapping != instance_info->swapchainInfos.end())
                    instance_info->swapchainInfos.erase(info_mapping);

                const auto& mapping = instance_info->swapchains.find(info->oldSwapchain);
                if (mapping != instance_info->swapchains.end())
                    instance_info->swapchains.erase(mapping);

                layer_info->root.removeSwapchainContext(info->oldSwapchain);
            }

            layer_info->root.update(); // ensure config is up to date

            // create swapchain
            VkSwapchainCreateInfoKHR newInfo = *info;
            const bool privateOrderedTransport =
                layer_info->root.modifySwapchainCreateInfo(it->second, newInfo,
                [&, newInfo = &newInfo]() {
                    auto res = it->second.df().CreateSwapchainKHR(
                        device, newInfo, alloc, swapchain);
                    if (res != VK_SUCCESS)
                        throw ls::vulkan_error(res, "vkCreateSwapchainKHR() failed");
                    createdSwapchain = *swapchain;
                    lowerSwapchainCreated = true;
                }
            );

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

            auto& info = instance_info->swapchainInfos.emplace(*swapchain, SwapchainInfo {
                .images = std::move(swapchainImages),
                .format = newInfo.imageFormat,
                .colorSpace = newInfo.imageColorSpace,
                .extent = newInfo.imageExtent,
                .presentMode = newInfo.presentMode,
                .privateOrderedTransport = privateOrderedTransport,
            }).first->second;

            // Create the MAKO Renderer swapchain context.
            layer_info->root.createSwapchainContext(it->second, *swapchain, info);

            instance_info->swapchains.emplace(*swapchain,
                ls::R<vk::Vulkan>(it->second));

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
                             "reconstruction remain pending until a natural game-owned "
                             "swapchain recreation; no recreation was forced; contexts="
                          << configurationUpdate.deferredContexts << '\n';
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

        // Preserve a genuine game/driver out-of-date result across a
        // multi-swapchain present. MAKO itself does not manufacture this
        // result for configuration or recovery transitions.
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

        layer_info->root.removeSwapchainContext(swapchain);

        // destroy swapchain
        it->second.df().DestroySwapchainKHR(device, swapchain, alloc);
    }
}

/// Vulkan layer entrypoint
__attribute__((visibility("default")))
VkResult vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct) {
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
    try {
        layer_info = new LayerInfo { // NOLINT (memory management)
            .map = {
#define VKPTR(name) reinterpret_cast<PFN_vkVoidFunction>(name)
                { "vkCreateInstance", VKPTR(myvkCreateInstance) },
                { "vkCreateDevice", VKPTR(myvkCreateDevice) },
                { "vkDestroyDevice", VKPTR(myvkDestroyDevice) },
                { "vkDestroyInstance", VKPTR(myvkDestroyInstance) },
                { "vkCreateSwapchainKHR", VKPTR(myvkCreateSwapchainKHR) },
                { "vkQueuePresentKHR", VKPTR(myvkQueuePresentKHR) },
                { "vkDestroySwapchainKHR", VKPTR(myvkDestroySwapchainKHR) }
#undef VKPTR
            },
            .root = Root()
        };

        if (!layer_info->root.active()) { // skip inactive
            delete layer_info; // NOLINT (memory management)
            layer_info = nullptr;

            return VK_ERROR_INITIALIZATION_FAILED;
        }
    } catch (const std::exception& e) {
        std::cerr << "MAKO Renderer: layer initialization failed:\n";
        std::cerr << "- " << e.what() << '\n';

        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // emplace function pointers/version
    pVersionStruct->loaderLayerInterfaceVersion = 2;
    pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
    pVersionStruct->pfnGetDeviceProcAddr = myvkGetDeviceProcAddr;
    pVersionStruct->pfnGetInstanceProcAddr = myvkGetInstanceProcAddr;
    return VK_SUCCESS;
}
