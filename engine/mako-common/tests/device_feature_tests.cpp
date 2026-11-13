/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/vulkan/device_features.hpp"
#include "mako-common/vulkan/vulkan.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace {
    struct FormatSupport {
        uint32_t apiVersion{VK_API_VERSION_1_3};
        bool formatFeatures2Extension{};
        bool storage{true};
        bool writeWithoutFormat{true};
        VkResult extensionResult{VK_SUCCESS};
        uint32_t formatQueries{};
    } formatSupport;

    template<typename T> T fakeHandle() {
        if constexpr (std::is_pointer_v<T>)
            return reinterpret_cast<T>(uintptr_t{1});
        else
            return static_cast<T>(1);
    }

    vk::Vulkan formatVulkan(const bool enabled = true, const bool queryAvailable = true) {
        vk::VulkanInstanceFuncs fi{};
        fi.GetPhysicalDeviceQueueFamilyProperties = [](VkPhysicalDevice,
                uint32_t* count, VkQueueFamilyProperties* properties) {
            *count = 1;
            if (properties) properties[0].queueFlags = VK_QUEUE_COMPUTE_BIT;
        };
        fi.GetPhysicalDeviceProperties = [](VkPhysicalDevice, VkPhysicalDeviceProperties* properties) {
            *properties = {};
            properties->apiVersion = formatSupport.apiVersion;
        };
        fi.GetPhysicalDeviceFormatProperties = [](VkPhysicalDevice, VkFormat, VkFormatProperties* properties) {
            *properties = {};
            if (formatSupport.storage)
                properties->optimalTilingFeatures = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
        };
        fi.EnumerateDeviceExtensionProperties = [](VkPhysicalDevice, const char*,
                uint32_t* count, VkExtensionProperties* properties) -> VkResult {
            if (formatSupport.extensionResult != VK_SUCCESS)
                return formatSupport.extensionResult;
            *count = formatSupport.formatFeatures2Extension ? 1U : 0U;
            if (properties && *count) {
                properties[0] = {};
                std::strcpy(properties[0].extensionName, VK_KHR_FORMAT_FEATURE_FLAGS_2_EXTENSION_NAME);
            }
            return VK_SUCCESS;
        };
        if (queryAvailable) {
            fi.GetPhysicalDeviceFormatProperties2 = [](VkPhysicalDevice, VkFormat,
                    VkFormatProperties2* properties) {
                ++formatSupport.formatQueries;
                auto* features = static_cast<VkFormatProperties3*>(properties->pNext);
                features->optimalTilingFeatures = formatSupport.storage
                    ? VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT : 0;
                if (formatSupport.writeWithoutFormat)
                    features->optimalTilingFeatures |= VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;
            };
        }
        vk::VulkanDeviceFuncs df{};
        df.GetDeviceQueue = [](VkDevice, uint32_t, uint32_t, VkQueue* queue) {
            *queue = fakeHandle<VkQueue>();
        };
        df.CreateCommandPool = [](VkDevice, const VkCommandPoolCreateInfo*,
                const VkAllocationCallbacks*, VkCommandPool* pool) -> VkResult {
            *pool = fakeHandle<VkCommandPool>();
            return VK_SUCCESS;
        };
        df.DestroyCommandPool = [](VkDevice, VkCommandPool, const VkAllocationCallbacks*) {};
        df.CreatePipelineCache = [](VkDevice, const VkPipelineCacheCreateInfo*,
                const VkAllocationCallbacks*, VkPipelineCache* cache) -> VkResult {
            *cache = fakeHandle<VkPipelineCache>();
            return VK_SUCCESS;
        };
        df.DestroyPipelineCache = [](VkDevice, VkPipelineCache, const VkAllocationCallbacks*) {};
        return {fakeHandle<VkInstance>(), fakeHandle<VkDevice>(), fakeHandle<VkPhysicalDevice>(),
            fi, df, 0, false, false, std::nullopt, std::nullopt, false, enabled};
    }

    std::vector<std::string>& unavailableDeviceFunctions() {
        static std::vector<std::string> functions;
        return functions;
    }

    void expect(const bool condition, const std::string_view message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }

    VKAPI_ATTR void VKAPI_CALL mockDeviceFunction() {}

    VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL mockGetDeviceProcAddr(
            VkDevice, const char* name) {
        if (std::find(
                unavailableDeviceFunctions().begin(),
                unavailableDeviceFunctions().end(), name
            ) != unavailableDeviceFunctions().end()) {
            return nullptr;
        }
        return &mockDeviceFunction;
    }

    void makeUnavailable(const std::string_view name) {
        unavailableDeviceFunctions() = {std::string(name)};
    }

    template<typename Operation>
    void expectLoadingFailure(
            Operation operation, const std::string_view message) {
        bool failed = false;
        try {
            operation();
        } catch (const std::exception&) {
            failed = true;
        }
        expect(failed, message);
    }

    void testStorageWriteFormatSupport() {
        for (const auto format : {VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM,
                VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                VK_FORMAT_A2R10G10B10_UNORM_PACK32}) {
            formatSupport = {};
            const auto device = formatVulkan();
            expect(device.supportsStorageImageWriteWithoutFormat(format) && formatSupport.formatQueries == 1,
                "Modern format-specific storage writes must retain the fast path");
            formatSupport.writeWithoutFormat = false;
            expect(!device.supportsStorageImageWriteWithoutFormat(format),
                "Storage support alone must not enable format-less writes");
            formatSupport.writeWithoutFormat = true;
            formatSupport.storage = false;
            expect(!device.supportsStorageImageWriteWithoutFormat(format),
                "A write-without-format bit must not replace storage-image support");

            formatSupport = {};
            expect(!formatVulkan(false).supportsStorageImageWriteWithoutFormat(format) &&
                    formatSupport.formatQueries == 0,
                "Physical support must not replace the logical device's enabled feature");
            expect(!formatVulkan(true, false).supportsStorageImageWriteWithoutFormat(format),
                "A missing format-query entrypoint must retain the copy path");
            formatSupport.apiVersion = VK_API_VERSION_1_2;
            expect(!device.supportsStorageImageWriteWithoutFormat(format),
                "Legacy device-wide features must not stand in for format-specific proof");
            formatSupport.formatFeatures2Extension = true;
            expect(device.supportsStorageImageWriteWithoutFormat(format),
                "Vulkan 1.2 with format-feature-flags2 must retain supported direct writes");
            formatSupport.writeWithoutFormat = false;
            expect(!device.supportsStorageImageWriteWithoutFormat(format),
                "The extension path must also honor an unsupported format");
            formatSupport.extensionResult = VK_ERROR_INITIALIZATION_FAILED;
            expect(!device.supportsStorageImageWriteWithoutFormat(format),
                "Failed optional capability discovery must retain the copy path");
        }
    }

    struct QueueLookup {
        uint32_t legacyCalls{};
        uint32_t queue2Calls{};
        VkDeviceQueueCreateFlags queue2Flags{};
    } queueLookup;

    void testApplicationQueueRetrieval() {
        static_assert(vk::ordinaryQueueCreateFlags(0));
        static_assert(!vk::ordinaryQueueCreateFlags(
            VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT));
#ifdef VK_KHR_internally_synchronized_queues
        constexpr VkDeviceQueueCreateFlags synchronized =
            VK_DEVICE_QUEUE_CREATE_INTERNALLY_SYNCHRONIZED_BIT_KHR;
        static_assert(vk::ordinaryQueueCreateFlags(synchronized));
        static_assert(!vk::ordinaryQueueCreateFlags(
            synchronized | VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT));
#else
        constexpr VkDeviceQueueCreateFlags synchronized = 0x4;
#endif

        vk::VulkanDeviceFuncs fd{};
        fd.GetDeviceQueue = [](VkDevice, uint32_t, uint32_t, VkQueue* queue) {
            ++queueLookup.legacyCalls;
            *queue = fakeHandle<VkQueue>();
        };
        const auto device = fakeHandle<VkDevice>();
        expect(vk::getDeviceQueue(fd, device, 0, 0, synchronized) == VK_NULL_HANDLE &&
                queueLookup.legacyCalls == 0,
            "a flagged queue must not fall back to vkGetDeviceQueue");

        fd.GetDeviceQueue2 = [](VkDevice, const VkDeviceQueueInfo2* info, VkQueue* queue) {
            ++queueLookup.queue2Calls;
            queueLookup.queue2Flags = info->flags;
            *queue = fakeHandle<VkQueue>();
        };
        expect(vk::getDeviceQueue(fd, device, 0, 0, synchronized) != VK_NULL_HANDLE &&
                queueLookup.queue2Calls == 1 && queueLookup.queue2Flags == synchronized &&
                queueLookup.legacyCalls == 0,
            "a flagged queue must be retrieved through vkGetDeviceQueue2 with its flags");
        expect(vk::getDeviceQueue(fd, device, 0, 0, 0) != VK_NULL_HANDLE &&
                queueLookup.legacyCalls == 1 && queueLookup.queue2Calls == 1,
            "an unflagged queue must keep using vkGetDeviceQueue");
    }

    void testDeviceFunctionLoading() {
        const auto device = reinterpret_cast<VkDevice>(uintptr_t{1});
        vk::VulkanInstanceFuncs instanceFunctions{};
        instanceFunctions.GetDeviceProcAddr = &mockGetDeviceProcAddr;

        const std::array interopFunctions{
            "vkSignalSemaphoreKHR",
            "vkWaitSemaphoresKHR",
            "vkGetMemoryFdKHR",
            "vkImportSemaphoreFdKHR",
            "vkGetSemaphoreFdKHR",
        };
        unavailableDeviceFunctions().assign(
            interopFunctions.begin(), interopFunctions.end()
        );
        const auto scalingOnly = vk::initVulkanDeviceFuncs(
            instanceFunctions, device, true, false
        );
        expect(!scalingOnly.SignalSemaphoreKHR &&
                !scalingOnly.WaitSemaphoresKHR &&
                !scalingOnly.GetMemoryFdKHR &&
                !scalingOnly.ImportSemaphoreFdKHR &&
                !scalingOnly.GetSemaphoreFdKHR,
            "Scaling-only loading must tolerate absent frame-generation interop");
        expect(scalingOnly.GetDeviceQueue && scalingOnly.QueueSubmit &&
                scalingOnly.CreateImage && scalingOnly.CmdDispatch,
            "Scaling-only loading must retain required core device functions");
        expect(scalingOnly.CreateSwapchainKHR &&
                scalingOnly.GetSwapchainImagesKHR &&
                scalingOnly.AcquireNextImageKHR &&
                scalingOnly.QueuePresentKHR &&
                scalingOnly.DestroySwapchainKHR,
            "Scaling-only graphical loading must retain every WSI function");
        expect(scalingOnly.AcquireNextImage2KHR,
            "Graphical loading must retain the supported optional acquisition API");

        makeUnavailable("vkAcquireNextImage2KHR");
        for (const bool frameGeneration : {false, true}) {
            const auto functions = vk::initVulkanDeviceFuncs(
                instanceFunctions, device, true, frameGeneration
            );
            expect(!functions.AcquireNextImage2KHR && functions.AcquireNextImageKHR,
                "An absent optional acquisition API must preserve ordinary WSI loading");
        }

        makeUnavailable("vkGetDeviceQueue");
        expectLoadingFailure([&]() {
            static_cast<void>(vk::initVulkanDeviceFuncs(
                instanceFunctions, device, true, false
            ));
        }, "Scaling-only loading must still reject a missing core function");

        const std::array graphicalFunctions{
            "vkCreateSwapchainKHR",
            "vkGetSwapchainImagesKHR",
            "vkAcquireNextImageKHR",
            "vkQueuePresentKHR",
            "vkDestroySwapchainKHR",
        };
        for (const std::string_view function : graphicalFunctions) {
            makeUnavailable(function);
            expectLoadingFailure([&]() {
                static_cast<void>(vk::initVulkanDeviceFuncs(
                    instanceFunctions, device, true, false
                ));
            }, "Scaling-only graphical loading must reject a missing WSI function");
        }

        for (const std::string_view function : interopFunctions) {
            makeUnavailable(function);
            expectLoadingFailure([&]() {
                static_cast<void>(vk::initVulkanDeviceFuncs(
                    instanceFunctions, device, true, true
                ));
            }, "Frame-generation loading must reject missing interop");
        }
        unavailableDeviceFunctions().clear();
    }
}

int main() {
    expect(!vk::selectOptionalDeviceFeatures(false, false).robustImageAccess2,
        "robust image access must remain disabled when unsupported");
    expect(!vk::selectOptionalDeviceFeatures(false, true).robustImageAccess2,
        "a feature bit cannot be used without the extension");
    expect(!vk::selectOptionalDeviceFeatures(true, false).robustImageAccess2,
        "the extension alone cannot enable an unsupported feature bit");
    expect(vk::selectOptionalDeviceFeatures(true, true).robustImageAccess2,
        "robust image access must be selected when fully supported");

    for (const bool enable : {false, true}) {
        VkPhysicalDeviceVulkan12Features features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .shaderInt8 = VK_TRUE,
        };
        VkDeviceCreateInfo info{.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &features};
        {
            const vk::ShaderFloat16FeatureRequest request(info, enable);
            expect(vk::shaderFloat16Enabled(info.pNext) == enable,
                "FP16 request must update the existing promoted feature bit");
            const auto* selected = static_cast<const VkPhysicalDeviceVulkan12Features*>(info.pNext);
            expect(!features.shaderFloat16 && selected->shaderInt8 && !selected->pNext,
                "FP16 request must preserve unrelated features without a duplicate structure");
        }
        expect(!features.shaderFloat16 && info.pNext == &features,
            "FP16 request must restore application-owned feature storage");
        features.shaderFloat16 = VK_TRUE;
        {
            const vk::ShaderFloat16FeatureRequest request(info, false);
            expect(vk::shaderFloat16Enabled(info.pNext),
                "FP32 MAKO arithmetic must not disable application-owned FP16 features");
        }
        VkDeviceCreateInfo standalone{.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        try {
            const vk::ShaderFloat16FeatureRequest request(standalone, enable);
            expect(vk::shaderFloat16Enabled(standalone.pNext) == enable,
                "FP16 request must append a feature only when enabled");
            throw 1;
        } catch (int) {}
        expect(!standalone.pNext, "exception cleanup must restore the original chain");
        VkPhysicalDeviceShaderFloat16Int8Features extension{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,
            .shaderInt8 = VK_TRUE,
        };
        standalone.pNext = &extension;
        {
            const vk::ShaderFloat16FeatureRequest request(standalone, enable);
            const auto* selected = static_cast<const VkPhysicalDeviceShaderFloat16Int8Features*>(standalone.pNext);
            expect(vk::shaderFloat16Enabled(standalone.pNext) == enable &&
                    !extension.shaderFloat16 && selected->shaderInt8 && !selected->pNext,
                "extension feature requests must avoid duplicates and preserve Int8");
        }
        expect(!extension.shaderFloat16,
            "extension feature storage must also be restored");
    }

    // Const input may reside in read-only pages. Copy complete known prefixes,
    // including loader payloads, and share the unchanged suffix.
    static const VkPhysicalDeviceShaderFloat16Int8Features immutable{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,
        .shaderInt8 = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 prefix{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = const_cast<VkPhysicalDeviceShaderFloat16Int8Features*>(&immutable),
        .features = {.robustBufferAccess = VK_TRUE},
    };
    VkLayerDeviceCreateInfo loader{
        .sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,
        .pNext = &prefix,
        .function = VK_LAYER_LINK_INFO,
    };
    VkDeviceCreateInfo immutableInfo{.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &loader};
    {
        const vk::ShaderFloat16FeatureRequest request(immutableInfo, true);
        const auto* selectedLoader = static_cast<const VkLayerDeviceCreateInfo*>(immutableInfo.pNext);
        const auto* selectedPrefix = static_cast<const VkPhysicalDeviceFeatures2*>(selectedLoader->pNext);
        expect(vk::shaderFloat16Enabled(immutableInfo.pNext) && !immutable.shaderFloat16 &&
                selectedLoader->function == loader.function &&
                selectedPrefix->features.robustBufferAccess &&
                loader.pNext == &prefix && prefix.pNext == &immutable,
            "immutable feature storage and full loader/feature prefixes must remain intact");
    }
    expect(immutableInfo.pNext == &loader, "immutable chain cleanup must restore the create-info head");
    VkBaseInStructure unknown{
        .sType = static_cast<VkStructureType>(1234567),
        .pNext = reinterpret_cast<const VkBaseInStructure*>(&immutable),
    };
    immutableInfo.pNext = &unknown;
    {
        const vk::ShaderFloat16FeatureRequest request(immutableInfo, true);
        expect(immutableInfo.pNext == &unknown && !vk::shaderFloat16Enabled(immutableInfo.pNext),
            "unknown feature prefixes must remain intact without substituting or duplicating FP16 structures");
    }

    // The core write-without-format bit lives in pEnabledFeatures or in the
    // one VkPhysicalDeviceFeatures2 that replaces it.
    for (const bool enable : {false, true}) {
        const VkPhysicalDeviceFeatures applicationFeatures{.samplerAnisotropy = VK_TRUE};
        VkDeviceCreateInfo legacy{
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pEnabledFeatures = &applicationFeatures,
        };
        {
            const vk::StorageImageWriteWithoutFormatRequest request(legacy, enable);
            expect(vk::storageImageWriteWithoutFormatEnabled(legacy) == enable &&
                    legacy.pEnabledFeatures->samplerAnisotropy &&
                    !applicationFeatures.shaderStorageImageWriteWithoutFormat,
                "write-without-format must copy pEnabledFeatures and keep other bits");
        }
        expect(legacy.pEnabledFeatures == &applicationFeatures,
            "write-without-format must restore the application's feature pointer");

        VkDeviceCreateInfo bare{.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        {
            const vk::StorageImageWriteWithoutFormatRequest request(bare, enable);
            expect(vk::storageImageWriteWithoutFormatEnabled(bare) == enable &&
                    !bare.pNext,
                "write-without-format must add only core features to a bare request");
        }
        expect(!bare.pEnabledFeatures, "a bare request must be restored");

        static const VkPhysicalDeviceFeatures2 immutableFeatures2{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .features = {.robustBufferAccess = VK_TRUE},
        };
        VkLayerDeviceCreateInfo loaderLink{
            .sType = VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO,
            .pNext = &immutableFeatures2,
            .function = VK_LAYER_LINK_INFO,
        };
        VkDeviceCreateInfo chained{
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &loaderLink,
        };
        {
            const vk::StorageImageWriteWithoutFormatRequest request(chained, enable);
            const auto* selectedLoader = static_cast<const VkLayerDeviceCreateInfo*>(chained.pNext);
            const auto* selected = static_cast<const VkPhysicalDeviceFeatures2*>(selectedLoader->pNext);
            expect(vk::storageImageWriteWithoutFormatEnabled(chained) == enable &&
                    !chained.pEnabledFeatures && selected->features.robustBufferAccess &&
                    selectedLoader->function == loaderLink.function &&
                    !immutableFeatures2.features.shaderStorageImageWriteWithoutFormat &&
                    loaderLink.pNext == &immutableFeatures2,
                "write-without-format must patch a copied Features2 without pEnabledFeatures");
        }
        expect(chained.pNext == &loaderLink && !chained.pEnabledFeatures,
            "a Features2 request must be restored");
    }
    {
        const VkPhysicalDeviceFeatures alreadyEnabled{
            .shaderStorageImageWriteWithoutFormat = VK_TRUE,
        };
        VkDeviceCreateInfo info{
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pEnabledFeatures = &alreadyEnabled,
        };
        const vk::StorageImageWriteWithoutFormatRequest request(info, false);
        expect(info.pEnabledFeatures == &alreadyEnabled &&
                vk::storageImageWriteWithoutFormatEnabled(info),
            "an application's own write-without-format request must be kept");
    }

    testDeviceFunctionLoading();
    testApplicationQueueRetrieval();
    testStorageWriteFormatSupport();

    std::cout << "device feature and function loading tests passed\n";
    return 0;
}
