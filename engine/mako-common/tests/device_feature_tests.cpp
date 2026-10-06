/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/vulkan/device_features.hpp"
#include "mako-common/vulkan/vulkan.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {
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

    testDeviceFunctionLoading();

    std::cout << "device feature and function loading tests passed\n";
    return 0;
}
