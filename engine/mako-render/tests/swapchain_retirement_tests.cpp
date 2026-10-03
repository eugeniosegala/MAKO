/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "swapchain/retirement.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <future>
#include <iostream>
#include <string>
#include <string_view>

using namespace mako::layer;

namespace {
    void expect(const bool condition, const std::string& message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(1);
        }
    }
    uint32_t pendingTimings = 0;
    uint32_t timingCalls = 0;
    bool timingQueryFails = false;
    VkResult VKAPI_CALL queryTiming(VkDevice, VkSwapchainKHR, uint32_t* count,
            VkPastPresentationTimingGOOGLE* output) {
        ++timingCalls;
        if (timingQueryFails)
            return VK_ERROR_SURFACE_LOST_KHR;
        if (!output) {
            expect(*count == 0, "timing enumeration supplied a stale count");
            *count = pendingTimings;
            return VK_SUCCESS;
        }
        expect(*count <= pendingTimings && *count <= 128,
            "feedback drain exceeded available records or bounded storage");
        pendingTimings -= *count;
        return pendingTimings ? VK_INCOMPLETE : VK_SUCCESS;
    }
}

int main() {
    expect(std::string_view(
            selectSwapchainMaintenance1Extension(true, true, true)
        ) == khrSwapchainMaintenance1ExtensionName,
        "the promoted maintenance1 extension was not preferred");
    expect(std::string_view(
            selectSwapchainMaintenance1Extension(false, true, true)
        ) == VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
        "the EXT maintenance1 fallback was not selected");
    expect(!selectSwapchainMaintenance1Extension(true, true, false) &&
            !selectSwapchainMaintenance1Extension(false, false, true),
        "maintenance1 was selected without both extension and feature support");

    const VkDeviceCreateInfo untimedDevice{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    };
    expect(rendererOwnsDisplayTiming(untimedDevice, true, true) &&
            !rendererOwnsDisplayTiming(untimedDevice, false, true) &&
            !rendererOwnsDisplayTiming(untimedDevice, true, false),
        "WSI timing did not require both runtime eligibility and capability");
    for (const char* extension : {VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME,
            "VK_EXT_present_timing"}) {
        auto appTimedDevice = untimedDevice;
        appTimedDevice.enabledExtensionCount = 1;
        appTimedDevice.ppEnabledExtensionNames = &extension;
        expect(!rendererOwnsDisplayTiming(appTimedDevice, true, true),
            "MAKO took an application's timing-feedback namespace");
    }
    // A client can serialize source production on its real-frame present ID.
    // Adding MAKO's future timestamp then feeds the generated-output lead back
    // into the next source frame, even though the client owns no timing API.
    // Extension availability alone does not enable that completion contract.
    const std::array<const char*, 4> waitExtensions{
        VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_PRESENT_ID_EXTENSION_NAME,
        VK_KHR_PRESENT_WAIT_EXTENSION_NAME, "VK_KHR_present_wait2",
    };
    auto completionPacedDevice = untimedDevice;
    completionPacedDevice.enabledExtensionCount =
        static_cast<uint32_t>(waitExtensions.size());
    completionPacedDevice.ppEnabledExtensionNames = waitExtensions.data();
    expect(rendererOwnsDisplayTiming(completionPacedDevice, true, true),
        "present-wait extension names alone disabled MAKO timing");
    VkPhysicalDevicePresentWaitFeaturesKHR presentWait{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR,
        .presentWait = VK_TRUE,
    };
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
        .pNext = &presentWait,
        .timelineSemaphore = VK_TRUE,
    };
    const VkPhysicalDeviceFeatures2 deviceFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &timeline,
    };
    completionPacedDevice.pNext = &deviceFeatures;
    expect(!rendererOwnsDisplayTiming(completionPacedDevice, true, true),
        "WSI timestamps delayed an application's enabled present-wait contract");
    expect(deviceFeatures.pNext == &timeline && timeline.pNext == &presentWait &&
            timeline.timelineSemaphore == VK_TRUE &&
            presentWait.presentWait == VK_TRUE && !presentWait.pNext,
        "WSI eligibility rewrote the application's feature chain");
    presentWait.presentWait = VK_FALSE;
    expect(rendererOwnsDisplayTiming(completionPacedDevice, true, true),
        "a disabled present-wait feature disabled MAKO timing");
#if defined(VK_KHR_present_wait2)
    VkPhysicalDevicePresentWait2FeaturesKHR presentWait2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_2_FEATURES_KHR,
        .presentWait2 = VK_TRUE,
    };
    presentWait.pNext = &presentWait2;
    expect(!rendererOwnsDisplayTiming(completionPacedDevice, true, true),
        "WSI timestamps delayed an application's enabled present-wait2 contract");
    expect(presentWait.pNext == &presentWait2 &&
            presentWait2.presentWait2 == VK_TRUE && !presentWait2.pNext,
        "WSI eligibility rewrote the application's present-wait2 feature");
    presentWait2.presentWait2 = VK_FALSE;
    expect(rendererOwnsDisplayTiming(completionPacedDevice, true, true),
        "disabled completion features disabled asynchronous WSI timing");
#endif
    for (const uint32_t pending : {0U, 1U, 127U, 128U, 10000U}) {
        pendingTimings = pending;
        timingCalls = 0;
        expect(discardOwnedDisplayTiming(queryTiming, VK_NULL_HANDLE,
                VK_NULL_HANDLE), "bounded timing drain rejected valid feedback");
        expect(timingCalls == (pending ? 2U : 1U) &&
                pendingTimings == (pending > 128 ? pending - 128 : 0),
            "timing drain looped, over-erased or left available bounded feedback");
    }
    timingCalls = 0;
    timingQueryFails = true;
    expect(!discardOwnedDisplayTiming(nullptr, VK_NULL_HANDLE, VK_NULL_HANDLE) &&
            !discardOwnedDisplayTiming(queryTiming, VK_NULL_HANDLE, VK_NULL_HANDLE) &&
            timingCalls == 1,
        "failed timing feedback did not stop bounded ownership");

    const std::array<const char*, 1> swapchainExtensions{
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    const VkDeviceCreateInfo presentationDevice{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .enabledExtensionCount =
            static_cast<uint32_t>(swapchainExtensions.size()),
        .ppEnabledExtensionNames = swapchainExtensions.data(),
    };
    expect(swapchainPresentationEnabled(presentationDevice),
        "a swapchain presentation device was not recognized");

    const VkDeviceCreateInfo computeOnlyDevice{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    };
    expect(!swapchainPresentationEnabled(computeOnlyDevice),
        "a compute-only device was treated as a presentation device");

    const VkDeviceCreateInfo malformedPresentationDevice{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .enabledExtensionCount = 1,
    };
    expect(!swapchainPresentationEnabled(malformedPresentationDevice),
        "a malformed presentation extension list was accepted");

    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance{
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
        .swapchainMaintenance1 = VK_TRUE,
    };
    const std::array<const char*, 1> extensions{
        VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
    };
    const VkDeviceCreateInfo enabled{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &maintenance,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    expect(swapchainMaintenance1Enabled(enabled),
        "enabled maintenance1 extension and feature were not recognized");

    maintenance.swapchainMaintenance1 = VK_FALSE;
    expect(!swapchainMaintenance1Enabled(enabled),
        "a disabled maintenance1 feature was accepted");
    maintenance.swapchainMaintenance1 = VK_TRUE;

    const VkDeviceCreateInfo missingExtension{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &maintenance,
    };
    expect(!swapchainMaintenance1Enabled(missingExtension),
        "a maintenance1 feature without its extension was accepted");

    const VkDeviceCreateInfo malformedExtensionList{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &maintenance,
        .enabledExtensionCount = 1,
    };
    expect(!swapchainMaintenance1Enabled(malformedExtensionList),
        "a malformed extension list was dereferenced or accepted");

    const std::array<const char*, 1> khrExtensions{
        khrSwapchainMaintenance1ExtensionName,
    };
    const VkDeviceCreateInfo khrEnabled{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &maintenance,
        .enabledExtensionCount = static_cast<uint32_t>(khrExtensions.size()),
        .ppEnabledExtensionNames = khrExtensions.data(),
    };
    expect(swapchainMaintenance1Enabled(khrEnabled),
        "the promoted KHR maintenance1 extension was not recognized");

    VkFence fence = VK_NULL_HANDLE;
    const VkSwapchainPresentFenceInfoEXT presentFence{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT,
        .swapchainCount = 1,
        .pFences = &fence,
    };
    expect(findSwapchainPresentFenceInfo(&presentFence) == &presentFence,
        "present-fence pNext lookup failed");
    expect(!upstreamPresentFenceProtectsSwapchain(&presentFence),
        "a null upstream present fence was accepted as lifetime proof");
    fence = std::bit_cast<VkFence>(uint64_t{1});
    expect(upstreamPresentFenceProtectsSwapchain(&presentFence),
        "a valid upstream present fence was not accepted as lifetime proof");
    expect(!upstreamPresentFenceProtectsSwapchain(&presentFence, 1),
        "an out-of-range upstream present fence was accepted");

    const uint64_t presentId = 1;
    const VkPresentIdKHR precedingNode{
        .sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR,
        .pNext = &presentFence,
        .swapchainCount = 1,
        .pPresentIds = &presentId,
    };
    // Use two real Vulkan structures so the fixture exercises the same mixed
    // ABI chain that production receives from the loader.
    const auto* const chainedPresentFence =
        findSwapchainPresentFenceInfo(&precedingNode);
    expect(chainedPresentFence,
        "present-fence lookup failed behind a preceding pNext node");
    expect(chainedPresentFence->swapchainCount == 1,
        "chained present-fence count changed");
    expect(upstreamPresentFenceProtectsSwapchain(chainedPresentFence),
        "chained present fence was not accepted as lifetime proof");

    expect(swapchainRetirementGracePeriod == std::chrono::milliseconds(50),
        "the compositor retirement grace contract changed unexpectedly");

    std::mutex retirementMutex;
    {
        const auto owner = lockSwapchainRetirement(retirementMutex, UINT64_MAX);
        expect(owner.owns_lock(),
            "terminal retirement did not acquire mutex ownership");
        auto presentationPoll = std::async(std::launch::async, [&]() {
            return lockSwapchainRetirement(retirementMutex, 0).owns_lock();
        });
        expect(presentationPoll.wait_for(std::chrono::seconds(5)) ==
                std::future_status::ready,
            "presentation retirement waited behind a busy destruction boundary");
        expect(!presentationPoll.get(),
            "presentation retirement acquired an already-owned mutex");
    }
    expect(lockSwapchainRetirement(retirementMutex, 0).owns_lock(),
        "presentation retirement could not resume after terminal ownership ended");

    const auto surfaceA = std::bit_cast<VkSurfaceKHR>(uint64_t{1});
    const auto surfaceB = std::bit_cast<VkSurfaceKHR>(uint64_t{2});
    expect(retiredSwapchainBelongsToSurface(surfaceA, surfaceA),
        "the creating surface did not own terminal retirement");
    expect(!retiredSwapchainBelongsToSurface(surfaceA, surfaceB) &&
            !retiredSwapchainBelongsToSurface(VK_NULL_HANDLE, surfaceA),
        "surface-terminal retirement crossed its exact non-null owner");

    const auto deviceA = reinterpret_cast<VkDevice>(1);
    const auto deviceB = reinterpret_cast<VkDevice>(2);
    const auto swapchainA = std::bit_cast<VkSwapchainKHR>(uint64_t{1});
    expect(shouldRetireRetainedSwapchainBeforeNullOldReplacement(
            VK_NULL_HANDLE, deviceA, surfaceA, deviceA, surfaceA),
        "a retained lower swapchain was not selected for pre-create retirement");
    expect(!shouldRetireRetainedSwapchainBeforeNullOldReplacement(
            swapchainA, deviceA, surfaceA, deviceA, surfaceA) &&
            !shouldRetireRetainedSwapchainBeforeNullOldReplacement(
                VK_NULL_HANDLE, deviceA, surfaceA, deviceB, surfaceA) &&
            !shouldRetireRetainedSwapchainBeforeNullOldReplacement(
                VK_NULL_HANDLE, deviceA, surfaceA, deviceA, surfaceB),
        "pre-create retirement ignored old-swapchain, device, or surface ownership");

    expect(swapchainCreateIsReplacement(swapchainA, false) &&
            swapchainCreateIsReplacement(VK_NULL_HANDLE, true) &&
            !swapchainCreateIsReplacement(VK_NULL_HANDLE, false),
        "replacement classification depended on forwarding a retained old handle");

    expect(nullOldReplacementRequiresDirectPresentPrime(true, false, true),
        "a scaled null-old replacement did not require a direct WSI prime");
    expect(!nullOldReplacementRequiresDirectPresentPrime(false, false, true) &&
            !nullOldReplacementRequiresDirectPresentPrime(true, true, true) &&
            !nullOldReplacementRequiresDirectPresentPrime(true, false, false),
        "direct WSI priming escaped the scaled null-old replacement boundary");

    expect(presentFenceWillSignal(VK_SUCCESS) &&
            presentFenceWillSignal(VK_SUBOPTIMAL_KHR) &&
            presentFenceWillSignal(VK_ERROR_OUT_OF_DATE_KHR) &&
            presentFenceWillSignal(VK_ERROR_SURFACE_LOST_KHR) &&
            presentFenceWillSignal(
                VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT
            ),
        "a queued presentation result lost its retirement fence");
    expect(!presentFenceWillSignal(VK_ERROR_OUT_OF_HOST_MEMORY) &&
            !presentFenceWillSignal(VK_ERROR_OUT_OF_DEVICE_MEMORY) &&
            !presentFenceWillSignal(VK_ERROR_DEVICE_LOST),
        "an unqueueable failure retained an unsignalable fence");

    std::cout << "swapchain retirement tests passed\n";
    return 0;
}
