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

    testDeviceFunctionLoading();

    std::cout << "device feature and function loading tests passed\n";
    return 0;
}
