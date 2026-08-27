/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/vulkan/vulkan.hpp"
#include "mako-common/vulkan/device_features.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/pointers.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <dlfcn.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan_core.h>

using namespace vk;

namespace {
    /// load libvulkan.so.1 and return its handle
    void* get_vulkan_handle() {
        static void* handle{nullptr}; // NOLINT (const correctness)
        if (handle) return handle;

        handle = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!handle) handle = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
        if (!handle)
            throw ls::vulkan_error("failed to load libvulkan.so.1");

        return handle;
    }

    /// get the main proc addr function
    PFN_vkGetInstanceProcAddr get_mpa() {
        static PFN_vkGetInstanceProcAddr mpa{nullptr};
        if (mpa) return mpa;

        mpa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
            dlsym(get_vulkan_handle(), "vkGetInstanceProcAddr"));
        if (!mpa)
            throw ls::vulkan_error("failed to get vkGetInstanceProcAddr symbol");

        return mpa;
    }
}

namespace {
    template<typename T>
    T ipa(PFN_vkGetInstanceProcAddr mpa, VkInstance instance, const char* name) {
        T func = reinterpret_cast<T>(
            mpa(instance, name));
        if (!func)
            throw ls::vulkan_error("failed to get instance proc addr for " + std::string(name));
        return func;
    }

    template<typename T>
    T optionalIpa(PFN_vkGetInstanceProcAddr mpa, VkInstance instance,
            const char* name, const char* alias = nullptr) {
        auto func = reinterpret_cast<T>(mpa(instance, name));
        if (!func && alias)
            func = reinterpret_cast<T>(mpa(instance, alias));
        return func;
    }

    /// create a vulkan instance
    ls::owned_ptr<VkInstance> createInstance(
            const std::string& appName, version appVersion,
            const std::string& engineName, version engineVersion) {
        VkInstance handle{};

        auto vkCreateInstance =
            ipa<PFN_vkCreateInstance>(get_mpa(), VK_NULL_HANDLE, "vkCreateInstance");
        if (!vkCreateInstance)
            throw ls::vulkan_error("failed to get vkCreateInstance symbol");

        const VkApplicationInfo appInfo{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pApplicationName = appName.c_str(),
            .applicationVersion = appVersion.into(),
            .pEngineName = engineName.c_str(),
            .engineVersion = engineVersion.into(),
            .apiVersion = VK_API_VERSION_1_2 // seems 1.2 is supported on all Vulkan-capable GPUs
        };
        const VkInstanceCreateInfo instanceInfo{
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &appInfo
        };
        auto res = vkCreateInstance(&instanceInfo, VK_NULL_HANDLE, &handle);
        if (res != VK_SUCCESS)
            throw ls::vulkan_error(res, "vkCreateInstance() failed");

        auto defunc =
            ipa<PFN_vkDestroyInstance>(get_mpa(), handle, "vkDestroyInstance");
        if (!defunc)
            throw ls::vulkan_error("failed to get vkDestroyInstance symbol");
        return ls::owned_ptr<VkInstance>(
            new VkInstance(handle),
            [defunc](VkInstance& instance) {
                defunc(instance, VK_NULL_HANDLE);
            }
        );
    }

    /// filter for a physical device
    VkPhysicalDevice findPhysicalDevice(const VulkanInstanceFuncs& fi,
            VkInstance instance,
            PhysicalDeviceSelector filter) {
        uint32_t phydevCount{};
        auto res = fi.EnumeratePhysicalDevices(instance, &phydevCount, VK_NULL_HANDLE);
        if (res != VK_SUCCESS || phydevCount == 0)
            throw ls::vulkan_error(res, "vkEnumeratePhysicalDevices() failed");

        std::vector<VkPhysicalDevice> phydevs(phydevCount);
        res = fi.EnumeratePhysicalDevices(instance, &phydevCount, phydevs.data());
        if (res != VK_SUCCESS)
            throw ls::vulkan_error(res, "vkEnumeratePhysicalDevices() failed");

        VkPhysicalDevice selected = filter(fi, phydevs);
        if (!selected)
            throw ls::vulkan_error("no suitable physical device found");

        return selected;
    }

    /// find the queue family index with given flags
    uint32_t findQFI(const VulkanInstanceFuncs& fi,
            VkPhysicalDevice physdev, VkQueueFlags flags) {
        uint32_t queueCount{};
        fi.GetPhysicalDeviceQueueFamilyProperties(physdev, &queueCount, VK_NULL_HANDLE);

        std::vector<VkQueueFamilyProperties> queues(queueCount);
        fi.GetPhysicalDeviceQueueFamilyProperties(physdev, &queueCount, queues.data());

        for (uint32_t i = 0; i < queueCount; ++i) {
            if ((queues.at(i).queueFlags & flags) == flags)
                return i;
        }

        throw ls::vulkan_error("no queue family with requested flags found");
    }

    VkQueueFlags queryQueueFamilyFlags(const VulkanInstanceFuncs& fi,
            const VkPhysicalDevice physdev, const uint32_t familyIndex) {
        uint32_t queueCount{};
        fi.GetPhysicalDeviceQueueFamilyProperties(
            physdev, &queueCount, VK_NULL_HANDLE
        );
        if (familyIndex >= queueCount)
            throw ls::vulkan_error("queue family index is out of range");
        std::vector<VkQueueFamilyProperties> queues(queueCount);
        fi.GetPhysicalDeviceQueueFamilyProperties(
            physdev, &queueCount, queues.data()
        );
        return queues.at(familyIndex).queueFlags;
    }

    /// check for fp16 support
    VkPhysicalDeviceFeatures2 queryFeatures(
            const VulkanInstanceFuncs& fi, VkPhysicalDevice physdev,
            VkPhysicalDeviceVulkan12Features* vulkan12 = nullptr) {
        VkPhysicalDeviceVulkan12Features supportedFeaturesVulkan12{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES
        };
        VkPhysicalDeviceFeatures2 supportedFeatures{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = vulkan12 ? vulkan12 : &supportedFeaturesVulkan12
        };
        fi.GetPhysicalDeviceFeatures2(physdev, &supportedFeatures);
        return supportedFeatures;
    }

    bool checkFP16(const VulkanInstanceFuncs& fi, VkPhysicalDevice physdev) {
        VkPhysicalDeviceVulkan12Features vulkan12{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES
        };
        static_cast<void>(queryFeatures(fi, physdev, &vulkan12));
        return vulkan12.shaderFloat16 == VK_TRUE;
    }

    bool supportsDeviceExtension(const VulkanInstanceFuncs& fi,
            const VkPhysicalDevice physdev, const std::string_view name) {
        uint32_t extensionCount{};
        auto res = fi.EnumerateDeviceExtensionProperties(
            physdev, nullptr, &extensionCount, nullptr
        );
        if (res != VK_SUCCESS)
            throw ls::vulkan_error(res, "vkEnumerateDeviceExtensionProperties() failed");

        std::vector<VkExtensionProperties> extensions(extensionCount);
        res = fi.EnumerateDeviceExtensionProperties(
            physdev, nullptr, &extensionCount, extensions.data()
        );
        if (res != VK_SUCCESS)
            throw ls::vulkan_error(res, "vkEnumerateDeviceExtensionProperties() failed");

        return std::any_of(extensions.begin(), extensions.end(),
                [name](const auto& extension) {
            return name == extension.extensionName;
        });
    }

    OptionalDeviceFeatures checkOptionalDeviceFeatures(
            const VulkanInstanceFuncs& fi, const VkPhysicalDevice physdev) {
        constexpr std::string_view robustness2Extension =
            VK_EXT_ROBUSTNESS_2_EXTENSION_NAME;
        const bool hasRobustness2 = supportsDeviceExtension(
            fi, physdev, robustness2Extension
        );
        if (!hasRobustness2)
            return {};

        VkPhysicalDeviceRobustness2FeaturesEXT robustness2{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
        };
        VkPhysicalDeviceFeatures2 features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &robustness2,
        };
        fi.GetPhysicalDeviceFeatures2(physdev, &features);
        return selectOptionalDeviceFeatures(
            hasRobustness2, robustness2.robustImageAccess2 == VK_TRUE
        );
    }

    template<typename T>
    T dpa(const VulkanInstanceFuncs& funcs, VkDevice device, const char* name) {
        T func = reinterpret_cast<T>(
            funcs.GetDeviceProcAddr(device, name));
        if (!func)
            throw ls::vulkan_error("failed to get device proc addr for " + std::string(name));
        return func;
    }

    /// create a logical device
    ls::owned_ptr<VkDevice> createLogicalDevice(const VulkanInstanceFuncs& fi,
            VkPhysicalDevice physdev, uint32_t cfi, bool fp16,
            const OptionalDeviceFeatures optionalFeatures) {
        VkDevice handle{};

        const float queuePriority{1.0F}; // highest priority
        VkPhysicalDeviceRobustness2FeaturesEXT requestedRobustness2{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
            .robustBufferAccess2 = VK_FALSE,
            .robustImageAccess2 = optionalFeatures.robustImageAccess2,
            .nullDescriptor = VK_FALSE,
        };
        const VkPhysicalDeviceVulkan12Features requestedFeaturesVulkan12{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = optionalFeatures.robustImageAccess2 ? &requestedRobustness2 : nullptr,
            .shaderFloat16 = fp16,
            .timelineSemaphore = VK_TRUE
        };
        const auto supportedFeatures = queryFeatures(fi, physdev);
        const VkPhysicalDeviceFeatures requestedFeatures{
            .shaderStorageImageExtendedFormats =
                supportedFeatures.features.shaderStorageImageExtendedFormats,
        };
        const VkDeviceQueueCreateInfo requestedQueueInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = cfi,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority
        };
        std::vector<const char*> requestedExtensions{
            VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
            VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME
        };
        if (optionalFeatures.robustImageAccess2)
            requestedExtensions.push_back(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
        const VkDeviceCreateInfo deviceInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &requestedFeaturesVulkan12,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &requestedQueueInfo,
            .enabledExtensionCount = static_cast<uint32_t>(requestedExtensions.size()),
            .ppEnabledExtensionNames = requestedExtensions.data(),
            .pEnabledFeatures = &requestedFeatures,
        };
        auto res = fi.CreateDevice(physdev, &deviceInfo, VK_NULL_HANDLE, &handle);
        if (res != VK_SUCCESS)
            throw ls::vulkan_error(res, "vkCreateDevice() failed");

        auto defunc =
            dpa<PFN_vkDestroyDevice>(fi, handle, "vkDestroyDevice");
        if (!defunc)
            throw ls::vulkan_error("failed to get vkDestroyDevice symbol");
        return ls::owned_ptr<VkDevice>(
            new VkDevice(handle),
            [defunc](VkDevice& device) {
                defunc(device, VK_NULL_HANDLE);
            }
        );
    }

    /// get a queue from the logical device
    VkQueue getQueue(const VulkanDeviceFuncs& fd, VkDevice device,
            std::optional<PFN_vkSetDeviceLoaderData> setLoaderData,
            uint32_t cfi) {
        VkQueue queue{};

        fd.GetDeviceQueue(device, cfi, 0, &queue);

        if (setLoaderData) { // optionally set loader data
            auto res = (*setLoaderData)(device, queue);
            if (res != VK_SUCCESS)
                throw ls::vulkan_error(res, "vkSetDeviceLoaderData() failed");
        }
        return queue;
    }

    /// create a command pool
    ls::owned_ptr<VkCommandPool> createCommandPool(const VulkanDeviceFuncs& fd,
            VkDevice device, uint32_t cfi) {
        VkCommandPool handle{};

        const VkCommandPoolCreateInfo cmdpoolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = cfi
        };
        auto res = fd.CreateCommandPool(device, &cmdpoolInfo, VK_NULL_HANDLE, &handle);
        if (res != VK_SUCCESS)
            throw ls::vulkan_error(res, "vkCreateCommandPool() failed");

        return ls::owned_ptr<VkCommandPool>(
            new VkCommandPool(handle),
            [dev = device, defunc = fd.DestroyCommandPool](VkCommandPool& pool) {
                defunc(dev, pool, VK_NULL_HANDLE);
            }
        );
    }

    /// try to read the pipeline cache from file
    void readCacheFile(const std::filesystem::path& cachefile, std::vector<uint8_t>& data) {
        std::ifstream file(cachefile, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            return;

        const std::streamsize size = static_cast<std::streamsize>(file.tellg());
        data = std::vector<uint8_t>(static_cast<size_t>(size));

        file.seekg(0, std::ios::beg);
        if (!file.read(reinterpret_cast<char*>(data.data()), size))
            return;
    }

    /// create a pipeline cache
    ls::owned_ptr<VkPipelineCache> createPipelineCache(
            const VulkanDeviceFuncs& fd, VkDevice device,
            const std::optional<std::filesystem::path>& cachefile) {
        VkPipelineCache handle{};

        std::vector<uint8_t> cache{};
        if (cachefile && std::filesystem::exists(*cachefile))
            readCacheFile(*cachefile, cache);

        const VkPipelineCacheCreateInfo pipelineCacheInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
            .initialDataSize = cache.size(),
            .pInitialData = cache.data()
        };
        auto res = fd.CreatePipelineCache(device, &pipelineCacheInfo, VK_NULL_HANDLE, &handle);
        if (res != VK_SUCCESS)
            throw ls::vulkan_error(res, "vkCreatePipelineCache() failed");

        return ls::owned_ptr<VkPipelineCache>(
            new VkPipelineCache(handle),
            [dev = device, defunc = fd.DestroyPipelineCache](VkPipelineCache& cache) {
                defunc(dev, cache, VK_NULL_HANDLE);
            }
        );
    }
}

/// initialize vulkan instance function pointers
VulkanInstanceFuncs vk::initVulkanInstanceFuncs(VkInstance i, PFN_vkGetInstanceProcAddr mpa,
        bool graphical) {
    return {
        .DestroyInstance = ipa<PFN_vkDestroyInstance>(mpa, i, "vkDestroyInstance"),
        .EnumeratePhysicalDevices = ipa<PFN_vkEnumeratePhysicalDevices>(mpa, i,
            "vkEnumeratePhysicalDevices"),
        .EnumerateDeviceExtensionProperties = ipa<PFN_vkEnumerateDeviceExtensionProperties>(mpa, i,
            "vkEnumerateDeviceExtensionProperties"),
        .GetPhysicalDeviceProperties = ipa<PFN_vkGetPhysicalDeviceProperties>(
            mpa, i, "vkGetPhysicalDeviceProperties"),
        .GetPhysicalDeviceProperties2 = graphical ?
            optionalIpa<PFN_vkGetPhysicalDeviceProperties2>(
                mpa, i, "vkGetPhysicalDeviceProperties2",
                "vkGetPhysicalDeviceProperties2KHR"
            ) : ipa<PFN_vkGetPhysicalDeviceProperties2>(
                mpa, i, "vkGetPhysicalDeviceProperties2"
            ),
        .GetPhysicalDeviceQueueFamilyProperties =
            ipa<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(mpa, i,
                "vkGetPhysicalDeviceQueueFamilyProperties"),
        .GetPhysicalDeviceFeatures2 = graphical ?
            optionalIpa<PFN_vkGetPhysicalDeviceFeatures2>(
                mpa, i, "vkGetPhysicalDeviceFeatures2",
                "vkGetPhysicalDeviceFeatures2KHR"
            ) : ipa<PFN_vkGetPhysicalDeviceFeatures2>(
                mpa, i, "vkGetPhysicalDeviceFeatures2"
            ),
        .GetPhysicalDeviceFormatProperties =
            ipa<PFN_vkGetPhysicalDeviceFormatProperties>(mpa, i,
                "vkGetPhysicalDeviceFormatProperties"),
        .GetPhysicalDeviceImageFormatProperties2 = graphical ?
            optionalIpa<PFN_vkGetPhysicalDeviceImageFormatProperties2>(
                mpa, i, "vkGetPhysicalDeviceImageFormatProperties2",
                "vkGetPhysicalDeviceImageFormatProperties2KHR"
            ) : ipa<PFN_vkGetPhysicalDeviceImageFormatProperties2>(
                mpa, i, "vkGetPhysicalDeviceImageFormatProperties2"
            ),
        .GetPhysicalDeviceMemoryProperties = ipa<PFN_vkGetPhysicalDeviceMemoryProperties>(mpa, i,
            "vkGetPhysicalDeviceMemoryProperties"),
        .CreateDevice = ipa<PFN_vkCreateDevice>(mpa, i, "vkCreateDevice"),
        .GetDeviceProcAddr = ipa<PFN_vkGetDeviceProcAddr>(mpa, i, "vkGetDeviceProcAddr"),

        .GetPhysicalDeviceSurfaceCapabilitiesKHR = graphical ?
            ipa<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(mpa, i,
                "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") : nullptr,
        .GetPhysicalDeviceSurfaceFormatsKHR = graphical ?
            ipa<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(mpa, i,
                "vkGetPhysicalDeviceSurfaceFormatsKHR") : nullptr,
        .GetPhysicalDeviceSurfaceSupportKHR = graphical ?
            ipa<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(mpa, i,
                "vkGetPhysicalDeviceSurfaceSupportKHR") : nullptr
    };
}

/// initialize vulkan device function pointers
VulkanDeviceFuncs vk::initVulkanDeviceFuncs(const VulkanInstanceFuncs& f, VkDevice d,
        bool graphical, const bool frameGenerationInteropEnabled) {
    const auto optionalDeviceProc = [&f, d](const char* name) {
        return f.GetDeviceProcAddr(d, name);
    };
    return {
        .GetDeviceQueue = dpa<PFN_vkGetDeviceQueue>(f, d, "vkGetDeviceQueue"),
        .DeviceWaitIdle = dpa<PFN_vkDeviceWaitIdle>(f, d, "vkDeviceWaitIdle"),
        .CreateCommandPool = dpa<PFN_vkCreateCommandPool>(f, d, "vkCreateCommandPool"),
        .DestroyCommandPool = dpa<PFN_vkDestroyCommandPool>(f, d, "vkDestroyCommandPool"),
        .CreateDescriptorPool = dpa<PFN_vkCreateDescriptorPool>(f, d, "vkCreateDescriptorPool"),
        .DestroyDescriptorPool = dpa<PFN_vkDestroyDescriptorPool>(f, d, "vkDestroyDescriptorPool"),
        .CreateBuffer = dpa<PFN_vkCreateBuffer>(f, d, "vkCreateBuffer"),
        .DestroyBuffer = dpa<PFN_vkDestroyBuffer>(f, d, "vkDestroyBuffer"),
        .GetBufferMemoryRequirements = dpa<PFN_vkGetBufferMemoryRequirements>(f, d,
            "vkGetBufferMemoryRequirements"),
        .AllocateMemory = dpa<PFN_vkAllocateMemory>(f, d, "vkAllocateMemory"),
        .FreeMemory = dpa<PFN_vkFreeMemory>(f, d, "vkFreeMemory"),
        .BindBufferMemory = dpa<PFN_vkBindBufferMemory>(f, d, "vkBindBufferMemory"),
        .MapMemory = dpa<PFN_vkMapMemory>(f, d, "vkMapMemory"),
        .UnmapMemory = dpa<PFN_vkUnmapMemory>(f, d, "vkUnmapMemory"),
        .AllocateCommandBuffers = dpa<PFN_vkAllocateCommandBuffers>(f, d,
            "vkAllocateCommandBuffers"),
        .FreeCommandBuffers = dpa<PFN_vkFreeCommandBuffers>(f, d, "vkFreeCommandBuffers"),
        .BeginCommandBuffer = dpa<PFN_vkBeginCommandBuffer>(f, d, "vkBeginCommandBuffer"),
        .EndCommandBuffer = dpa<PFN_vkEndCommandBuffer>(f, d, "vkEndCommandBuffer"),
        .CmdPipelineBarrier = dpa<PFN_vkCmdPipelineBarrier>(f, d, "vkCmdPipelineBarrier"),
        .CmdBlitImage = dpa<PFN_vkCmdBlitImage>(f, d, "vkCmdBlitImage"),
        .CmdClearColorImage = dpa<PFN_vkCmdClearColorImage>(f, d, "vkCmdClearColorImage"),
        .CmdBindPipeline = dpa<PFN_vkCmdBindPipeline>(f, d, "vkCmdBindPipeline"),
        .CmdBindDescriptorSets = dpa<PFN_vkCmdBindDescriptorSets>(f, d, "vkCmdBindDescriptorSets"),
        .CmdDispatch = dpa<PFN_vkCmdDispatch>(f, d, "vkCmdDispatch"),
        .CmdCopyBufferToImage = dpa<PFN_vkCmdCopyBufferToImage>(f, d, "vkCmdCopyBufferToImage"),
        .CmdCopyImageToBuffer = dpa<PFN_vkCmdCopyImageToBuffer>(f, d, "vkCmdCopyImageToBuffer"),
        .QueueSubmit = dpa<PFN_vkQueueSubmit>(f, d, "vkQueueSubmit"),
        .AllocateDescriptorSets = dpa<PFN_vkAllocateDescriptorSets>(f, d,
            "vkAllocateDescriptorSets"),
        .FreeDescriptorSets = dpa<PFN_vkFreeDescriptorSets>(f, d, "vkFreeDescriptorSets"),
        .UpdateDescriptorSets = dpa<PFN_vkUpdateDescriptorSets>(f, d, "vkUpdateDescriptorSets"),
        .CreateFence = dpa<PFN_vkCreateFence>(f, d, "vkCreateFence"),
        .DestroyFence = dpa<PFN_vkDestroyFence>(f, d, "vkDestroyFence"),
        .ResetFences = dpa<PFN_vkResetFences>(f, d, "vkResetFences"),
        .WaitForFences = dpa<PFN_vkWaitForFences>(f, d, "vkWaitForFences"),
        .CreateImage = dpa<PFN_vkCreateImage>(f, d, "vkCreateImage"),
        .DestroyImage = dpa<PFN_vkDestroyImage>(f, d, "vkDestroyImage"),
        .GetImageMemoryRequirements = dpa<PFN_vkGetImageMemoryRequirements>(f, d,
            "vkGetImageMemoryRequirements"),
        .BindImageMemory = dpa<PFN_vkBindImageMemory>(f, d, "vkBindImageMemory"),
        .CreateImageView = dpa<PFN_vkCreateImageView>(f, d, "vkCreateImageView"),
        .DestroyImageView = dpa<PFN_vkDestroyImageView>(f, d, "vkDestroyImageView"),
        .CreateSampler = dpa<PFN_vkCreateSampler>(f, d, "vkCreateSampler"),
        .DestroySampler = dpa<PFN_vkDestroySampler>(f, d, "vkDestroySampler"),
        .CreateSemaphore = dpa<PFN_vkCreateSemaphore>(f, d, "vkCreateSemaphore"),
        .DestroySemaphore = dpa<PFN_vkDestroySemaphore>(f, d, "vkDestroySemaphore"),
        .CreateShaderModule = dpa<PFN_vkCreateShaderModule>(f, d, "vkCreateShaderModule"),
        .DestroyShaderModule = dpa<PFN_vkDestroyShaderModule>(f, d, "vkDestroyShaderModule"),
        .CreateDescriptorSetLayout = dpa<PFN_vkCreateDescriptorSetLayout>(f, d,
            "vkCreateDescriptorSetLayout"),
        .DestroyDescriptorSetLayout = dpa<PFN_vkDestroyDescriptorSetLayout>(f, d,
            "vkDestroyDescriptorSetLayout"),
        .CreatePipelineLayout = dpa<PFN_vkCreatePipelineLayout>(f, d, "vkCreatePipelineLayout"),
        .DestroyPipelineLayout = dpa<PFN_vkDestroyPipelineLayout>(f, d, "vkDestroyPipelineLayout"),
        .CreatePipelineCache = dpa<PFN_vkCreatePipelineCache>(f, d, "vkCreatePipelineCache"),
        .DestroyPipelineCache = dpa<PFN_vkDestroyPipelineCache>(f, d, "vkDestroyPipelineCache"),
        .GetPipelineCacheData = dpa<PFN_vkGetPipelineCacheData>(f, d, "vkGetPipelineCacheData"),
        .CreateComputePipelines = dpa<PFN_vkCreateComputePipelines>(f, d, "vkCreateComputePipelines"),
        .DestroyPipeline = dpa<PFN_vkDestroyPipeline>(f, d, "vkDestroyPipeline"),

        .SignalSemaphoreKHR = frameGenerationInteropEnabled
            ? dpa<PFN_vkSignalSemaphoreKHR>(f, d, "vkSignalSemaphoreKHR")
            : reinterpret_cast<PFN_vkSignalSemaphoreKHR>(
                optionalDeviceProc("vkSignalSemaphoreKHR")),
        .WaitSemaphoresKHR = frameGenerationInteropEnabled
            ? dpa<PFN_vkWaitSemaphoresKHR>(f, d, "vkWaitSemaphoresKHR")
            : reinterpret_cast<PFN_vkWaitSemaphoresKHR>(
                optionalDeviceProc("vkWaitSemaphoresKHR")),
        .GetMemoryFdKHR = frameGenerationInteropEnabled
            ? dpa<PFN_vkGetMemoryFdKHR>(f, d, "vkGetMemoryFdKHR")
            : reinterpret_cast<PFN_vkGetMemoryFdKHR>(
                optionalDeviceProc("vkGetMemoryFdKHR")),
        .ImportSemaphoreFdKHR = frameGenerationInteropEnabled
            ? dpa<PFN_vkImportSemaphoreFdKHR>(f, d, "vkImportSemaphoreFdKHR")
            : reinterpret_cast<PFN_vkImportSemaphoreFdKHR>(
                optionalDeviceProc("vkImportSemaphoreFdKHR")),
        .GetSemaphoreFdKHR = frameGenerationInteropEnabled
            ? dpa<PFN_vkGetSemaphoreFdKHR>(f, d, "vkGetSemaphoreFdKHR")
            : reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(
                optionalDeviceProc("vkGetSemaphoreFdKHR")),

        .CreateSwapchainKHR = graphical ?
            dpa<PFN_vkCreateSwapchainKHR>(f, d, "vkCreateSwapchainKHR") : nullptr,
        .GetSwapchainImagesKHR = graphical ?
            dpa<PFN_vkGetSwapchainImagesKHR>(f, d, "vkGetSwapchainImagesKHR") : nullptr,
        .AcquireNextImageKHR = graphical ?
            dpa<PFN_vkAcquireNextImageKHR>(f, d, "vkAcquireNextImageKHR") : nullptr,
        .QueuePresentKHR = graphical ?
            dpa<PFN_vkQueuePresentKHR>(f, d, "vkQueuePresentKHR") : nullptr,
        .DestroySwapchainKHR = graphical ?
            dpa<PFN_vkDestroySwapchainKHR>(f, d, "vkDestroySwapchainKHR") : nullptr
    };
}

Vulkan::Vulkan(const std::string& appName, version appVersion,
        const std::string& engineName, version engineVersion,
        PhysicalDeviceSelector selectPhysicalDevice,
        bool isGraphical,
        std::optional<PFN_vkSetDeviceLoaderData> setLoaderData,
        const std::optional<std::filesystem::path>& cachefile) :
    instance(createInstance(
        appName, appVersion,
        engineName, engineVersion
    )),
    instance_funcs(initVulkanInstanceFuncs(*this->instance, get_mpa(), false)),
    phys_dev(findPhysicalDevice(this->instance_funcs,
        *this->instance,
        selectPhysicalDevice
    )),
    queueFamilyIdx(findQFI(this->instance_funcs, this->phys_dev,
        isGraphical ? VK_QUEUE_GRAPHICS_BIT : VK_QUEUE_COMPUTE_BIT)),
    queueFamilyFlags(queryQueueFamilyFlags(
        this->instance_funcs, this->phys_dev, this->queueFamilyIdx
    )),
    fp16(checkFP16(this->instance_funcs, this->phys_dev)),
    robustImageAccess2(checkOptionalDeviceFeatures(
        this->instance_funcs, this->phys_dev
    ).robustImageAccess2),
    device(createLogicalDevice(this->instance_funcs,
        this->phys_dev,
        this->queueFamilyIdx,
        this->fp16,
        OptionalDeviceFeatures{
            .robustImageAccess2 = this->robustImageAccess2,
        }
    )),
    setLoaderData(setLoaderData),
    device_funcs(initVulkanDeviceFuncs(
        this->instance_funcs,
        *this->device, false
    )),
    computeQueue(getQueue(this->device_funcs, *this->device,
        this->setLoaderData,
        this->queueFamilyIdx)),
    cmdPool(createCommandPool(this->device_funcs,
        *this->device,
        this->queueFamilyIdx
    )),
    pipelineCache(createPipelineCache(this->device_funcs,
        *this->device, cachefile
    )),
    cachefile(cachefile) {
}

Vulkan::Vulkan(VkInstance instance, VkDevice device,
        VkPhysicalDevice physdev,
        VulkanInstanceFuncs instanceFuncs,
        VulkanDeviceFuncs deviceFuncs,
        const uint32_t queueFamilyIndex,
        const bool frameGenerationInteropEnabled,
        bool isGraphical,
        std::optional<PFN_vkSetDeviceLoaderData> setLoaderData,
        const std::optional<std::filesystem::path>& cachefile) :
    instance(new VkInstance(instance)),
    instance_funcs(instanceFuncs),
    phys_dev(physdev),
    queueFamilyIdx(queueFamilyIndex),
    queueFamilyFlags(queryQueueFamilyFlags(
        this->instance_funcs, this->phys_dev, this->queueFamilyIdx
    )),
    frameGenerationInterop(frameGenerationInteropEnabled),
    fp16(false),
    robustImageAccess2(false),
    device(new VkDevice(device)),
    setLoaderData(setLoaderData),
    device_funcs(deviceFuncs),
    computeQueue(getQueue(this->device_funcs, *this->device,
        this->setLoaderData,
        this->queueFamilyIdx)),
    cmdPool(createCommandPool(this->device_funcs,
        *this->device,
        this->queueFamilyIdx
    )),
    pipelineCache(createPipelineCache(this->device_funcs,
        *this->device, cachefile
    )),
    cachefile(cachefile) {
}

std::optional<uint32_t> Vulkan::findMemoryTypeIndex(
        std::bitset<32> validTypes, bool hostVisibility) const {
    const VkMemoryPropertyFlags desiredProps = hostVisibility ?
        (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) :
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VkPhysicalDeviceMemoryProperties props;
    this->instance_funcs.GetPhysicalDeviceMemoryProperties(this->phys_dev, &props);

    std::array<VkMemoryType, 32> memTypes = std::to_array(props.memoryTypes);
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i)
        if (validTypes.test(i) && (memTypes.at(i).propertyFlags & desiredProps) == desiredProps)
            return i;

    return std::nullopt;
}

bool Vulkan::supportsExternalImageFormat(
        const VkFormat format,
        const VkImageUsageFlags usage,
        const VkExternalMemoryFeatureFlags requiredExternalFeatures) const {
    if (!this->instance_funcs.GetPhysicalDeviceImageFormatProperties2)
        return false;

    const VkPhysicalDeviceExternalImageFormatInfo externalInfo{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR,
    };
    const VkPhysicalDeviceImageFormatInfo2 formatInfo{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        .pNext = &externalInfo,
        .format = format,
        .type = VK_IMAGE_TYPE_2D,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
    };
    VkExternalImageFormatProperties externalProperties{
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES,
    };
    VkImageFormatProperties2 properties{
        .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
        .pNext = &externalProperties,
    };
    const auto result = this->instance_funcs.GetPhysicalDeviceImageFormatProperties2(
        this->phys_dev, &formatInfo, &properties
    );
    if (result != VK_SUCCESS)
        return false;

    const auto& memoryProperties =
        externalProperties.externalMemoryProperties;
    return (memoryProperties.compatibleHandleTypes &
                VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR) != 0 &&
        (memoryProperties.externalMemoryFeatures & requiredExternalFeatures) ==
            requiredExternalFeatures;
}

bool Vulkan::supportsOptimalTilingFormatFeatures(
        const VkFormat format,
        const VkFormatFeatureFlags requiredFeatures) const {
    VkFormatProperties properties{};
    this->instance_funcs.GetPhysicalDeviceFormatProperties(
        this->phys_dev, format, &properties
    );
    return (properties.optimalTilingFeatures & requiredFeatures) ==
        requiredFeatures;
}

bool Vulkan::supportsStorageImageExtendedFormats() const {
    if (!this->instance_funcs.GetPhysicalDeviceFeatures2)
        return false;
    const auto features = queryFeatures(
        this->instance_funcs, this->phys_dev
    );
    return features.features.shaderStorageImageExtendedFormats == VK_TRUE;
}

void Vulkan::persistPipelineCache() const noexcept {
    if (!this->cachefile)
        return;

    size_t cacheSize{};
    auto res = this->device_funcs.GetPipelineCacheData(*this->device,
        *this->pipelineCache,
        &cacheSize, nullptr);
    if (res != VK_SUCCESS)
        return;

    std::vector<uint8_t> cacheData(cacheSize);
    res = this->device_funcs.GetPipelineCacheData(*this->device,
        *this->pipelineCache,
        &cacheSize, cacheData.data());
    if (res != VK_SUCCESS)
        return;

    std::ofstream file(*this->cachefile, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return;

    file.write(reinterpret_cast<const char*>(cacheData.data()),
        static_cast<std::streamsize>(cacheData.size()));
    if (!file.good())
        return;
}
