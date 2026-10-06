/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "gamescope_scaling_surface.hpp"
#include "spatial_scaling_policy.hpp"
#include "bridge_present_timing.hpp"
#include "present_diagnostics.hpp"
#include <algorithm>
#include <X11/Xlib.h>
#include <xcb/xcb.h>
#include <vulkan/vulkan_xcb.h>
#include <vulkan/vulkan_xlib.h>
#include <vulkan/vulkan_wayland.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>

extern "C" {
    void mako_test_surface_mode(int);
    int mako_test_surface_objects();
    int mako_test_surface_associations();
    int mako_test_surface_feedbacks();
    int mako_test_surface_present_modes();
    int mako_test_surface_present_times();
    uint32_t mako_test_surface_present_id();
    uint64_t mako_test_surface_present_time();
    uint32_t mako_test_surface_present_mode();
    uint32_t mako_test_surface_feedback_image_count();
    const char* mako_test_surface_feedback_engine();
    int mako_test_surface_reads();
    int mako_test_surface_geometry_queries();
    void mako_test_surface_resize(uint16_t, uint16_t);
    uint32_t mako_test_surface_window();
    uint32_t mako_test_surface_server();
    void mako_test_surface_retire();
    void mako_test_surface_timing();
}

using namespace mako::layer;

namespace {
    void expect(bool condition, const char* message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(1);
        }
    }
    bool failVulkan{};
    bool failNativeSurface{};
    int nativeSurfacesDestroyed{};
    const VkAllocationCallbacks* lastAllocator{};
    VkSurfaceKHR testSurface(const uint64_t value) {
        VkSurfaceKHR surface{};
        std::memcpy(&surface, &value, sizeof(surface));
        return surface;
    }
    VkResult VKAPI_PTR createWayland(VkInstance, const VkWaylandSurfaceCreateInfoKHR* info,
            const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) {
        expect(info->sType == VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR &&
            info->display && info->surface && !info->pNext, "invalid private Wayland surface");
        lastAllocator = allocator;
        if (failVulkan)
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        static uint64_t nextSurface = 100;
        ++nextSurface;
        std::memcpy(surface, &nextSurface, sizeof(*surface));
        return VK_SUCCESS;
    }
    VkResult VKAPI_PTR createXcb(VkInstance, const VkXcbSurfaceCreateInfoKHR* info,
            const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) {
        expect(info->window == 71 && info->connection && !allocator,
            "temporary XCB surface must match the application window");
        if (failNativeSurface)
            return VK_ERROR_INITIALIZATION_FAILED;
        *surface = testSurface(9001);
        return VK_SUCCESS;
    }
    VkResult VKAPI_PTR createXlib(VkInstance, const VkXlibSurfaceCreateInfoKHR* info,
            const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) {
        expect(info->window == 71 && info->dpy && !allocator,
            "temporary Xlib surface must match the application window");
        *surface = testSurface(9002);
        return VK_SUCCESS;
    }
    void VKAPI_PTR destroySurface(VkInstance, VkSurfaceKHR surface,
            const VkAllocationCallbacks* allocator) {
        if (surface == testSurface(9001) || surface == testSurface(9002)) {
            expect(!allocator, "temporary surface must use its original allocator");
            ++nativeSurfacesDestroyed;
        }
    }
    VkResult VKAPI_PTR surfaceFormats(VkPhysicalDevice, VkSurfaceKHR surface,
            uint32_t* count, VkSurfaceFormatKHR* formats) {
        const VkSurfaceFormatKHR native[]{
            {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
            {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        };
        const VkSurfaceFormatKHR wayland[]{
            {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
            {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
            {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        };
        const bool nativeQuery = surface == testSurface(9001) ||
            surface == testSurface(9002);
        const auto& selected = nativeQuery ? native : wayland;
        const uint32_t available = nativeQuery ? 2 : 3;
        if (!formats) {
            *count = available;
            return VK_SUCCESS;
        }
        const uint32_t written = std::min(*count, available);
        std::memcpy(formats, selected, written * sizeof(VkSurfaceFormatKHR));
        *count = written;
        return written < available ? VK_INCOMPLETE : VK_SUCCESS;
    }
    PFN_vkVoidFunction VKAPI_PTR next(VkInstance, const char* name) {
        if (std::strcmp(name, "vkCreateWaylandSurfaceKHR") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(createWayland);
        if (std::strcmp(name, "vkCreateXcbSurfaceKHR") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(createXcb);
        if (std::strcmp(name, "vkCreateXlibSurfaceKHR") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(createXlib);
        if (std::strcmp(name, "vkDestroySurfaceKHR") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(destroySurface);
        return nullptr;
    }

    VkSurfaceCapabilitiesKHR driverCapabilities() {
        return {
            .minImageCount = 3,
            .maxImageCount = 8,
            .currentExtent = {UINT32_MAX, UINT32_MAX},
            .minImageExtent = {1, 1},
            .maxImageExtent = {16384, 16384},
            .maxImageArrayLayers = 1,
            .supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
            .currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
            .supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        };
    }

    VkSwapchainCreateInfoKHR swapchainInfo(const VkSurfaceKHR surface) {
        return {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = surface,
            .minImageCount = 4,
            .imageFormat = VK_FORMAT_B8G8R8A8_UNORM,
            .imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
            .imageExtent = {3840, 2160},
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = VK_PRESENT_MODE_FIFO_KHR,
            .clipped = VK_TRUE,
        };
    }

    void checkApplicationExtent(GamescopeScalingSurface& bridge,
            VkSurfaceKHR surface, VkExtent2D extent, const float factor = 2.0F) {
        mako_test_surface_resize(static_cast<uint16_t>(extent.width),
            static_cast<uint16_t>(extent.height));
        const auto lower = driverCapabilities();
        auto application = lower;
        expect(bridge.applicationCapabilities(surface, application) == VK_SUCCESS,
            "X11 capability query must succeed before the first swapchain");
        expect(sameExtent(application.currentExtent, extent) &&
            sameExtent(application.minImageExtent, extent) &&
            sameExtent(application.maxImageExtent, extent),
            "X11 current/minimum/maximum extents must match the live window");
        application.currentExtent = lower.currentExtent;
        application.minImageExtent = lower.minImageExtent;
        application.maxImageExtent = lower.maxImageExtent;
        expect(std::memcmp(&application, &lower, sizeof(lower)) == 0,
            "extent compatibility must preserve all other driver capabilities");
        const auto decision = scalingDecisionForCreate(
            SpatialScalingPolicy{.enabled = true, .factor = factor}, true, 1,
            lower, extent, std::nullopt, std::nullopt, std::nullopt,
            VkExtent2D{3840, 2160}, true);
        expect(decision.extents && sameExtent(decision.extents->source, extent) &&
            decision.extents->presentation.width > extent.width,
            "the concrete application size must still permit internal variable-surface scaling");
    }
}

int main() {
    for (const bool bridge : {false, true}) {
        for (const bool applicationRole : {false, true}) {
            for (const bool generation : {false, true}) {
                expect(needsBridgePresentWaitFallback(bridge, applicationRole, generation) ==
                        (bridge && applicationRole && generation),
                    "presentation-wait fallback escaped the provisioned private bridge");
            }
        }
    }
    VkPhysicalDevicePresentIdFeaturesKHR presentId{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_ID_FEATURES_KHR,
        .presentId = VK_TRUE,
    };
    VkPhysicalDevicePresentWaitFeaturesKHR presentWait{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR,
        .pNext = &presentId,
        .presentWait = VK_TRUE,
    };
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
        .pNext = &presentWait,
        .timelineSemaphore = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &timeline,
        .features = {.robustBufferAccess = VK_TRUE},
    };
#if defined(VK_KHR_present_wait2)
    VkPhysicalDevicePresentWait2FeaturesKHR presentWait2{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_2_FEATURES_KHR,
        .presentWait2 = VK_TRUE,
    };
    presentId.pNext = &presentWait2;
#endif
    expect(requestsApplicationPresentWait(&features),
        "nested device feature request must detect presentation waits");
    expect(presentWait.presentWait == VK_TRUE,
        "device request inspection mutated caller-owned features");
    disableBridgePresentWaitFeatures(features);
    expect(!requestsApplicationPresentWait(&features) && !presentWait.presentWait,
        "bridge must decline both presentation-wait feature generations");
    expect(features.features.robustBufferAccess && timeline.timelineSemaphore &&
            presentId.presentId && features.pNext == &timeline &&
            timeline.pNext == &presentWait && presentWait.pNext == &presentId,
        "bridge changed unrelated features, present IDs or chain linkage");
#if defined(VK_KHR_present_wait2)
    expect(!presentWait2.presentWait2 && presentId.pNext == &presentWait2,
        "wait2 fallback must preserve its feature structure");
    presentWait2.presentWait2 = VK_TRUE;
    expect(requestsApplicationPresentWait(&features),
        "wait2-only device request bypassed the capability contract");
    disableBridgePresentWaitFeatures(features);
#endif
    disableBridgePresentWaitFeatures(features);
    VkPhysicalDeviceFeatures2 emptyFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
    };
    disableBridgePresentWaitFeatures(emptyFeatures);
    expect(!requestsApplicationPresentWait(nullptr) &&
            !requestsApplicationPresentWait(&emptyFeatures),
        "absent wait features must remain supported");
    using namespace std::chrono_literals;
    using present_diagnostics::ApplicationPresentWait;
    using present_diagnostics::PresentWaitApi;
    ApplicationPresentWait waits;
    const auto start = present_diagnostics::Clock::time_point{10s};
    expect(!waits.observe({}, {}, PresentWaitApi::Khr, 100, 0, VK_TIMEOUT,
            start, start + 2ms), "short wait window emitted early");
    expect(!waits.observe({}, {}, PresentWaitApi::Khr, 101, UINT64_MAX, VK_SUCCESS,
            start + 10ms, start + 30ms), "wait sampling must be bounded");
    const auto waited = waits.observe({}, {}, PresentWaitApi::Khr, 102, 50,
        VK_ERROR_DEVICE_LOST, start + 990ms, start + 1010ms);
    expect(waited && waited->calls == 3 && waited->successful == 1 &&
            waited->timeouts == 1 && waited->errors == 1 && waited->polls == 1 &&
            waited->firstPresentId == 100 && waited->lastPresentId == 102 &&
            waited->duration.mean() == 14 && waited->duration.maximum == 20,
        "wait telemetry must distinguish polling, timeout, success and failure");
    expect(!waits.observe({}, {}, PresentWaitApi::Khr2, 1, 1, VK_SUCCESS,
            start + 3s, start + 3001ms), "a different API inherited a stale window");
    const auto replaced = waits.observe({}, {}, PresentWaitApi::Khr2, 2, 1, VK_SUCCESS,
        start + 4s, start + 4001ms);
    expect(replaced && replaced->calls == 2 && replaced->firstPresentId == 1,
        "a changed wait stream merged prior present IDs");
    size_t forwardedWaits{};
    for (const auto expected : {VK_SUCCESS, VK_TIMEOUT, VK_ERROR_DEVICE_LOST}) {
        const auto actual = present_diagnostics::observeApplicationPresentWait(
            {}, {}, PresentWaitApi::Khr, UINT64_MAX, UINT64_MAX,
            [&] { ++forwardedWaits; return expected; });
        expect(actual == expected, "wait diagnostics changed the driver's result");
    }
    expect(forwardedWaits == 3, "wait diagnostics retried or skipped an application wait");
    const VkPresentModeKHR requestedMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    const VkSwapchainPresentModeInfoEXT dynamicMode{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT,
        .swapchainCount = 1, .pPresentModes = &requestedMode};
    expect(present_diagnostics::applicationPresentMode(VK_PRESENT_MODE_FIFO_KHR,
            &dynamicMode) == present_diagnostics::ApplicationPresentMode{0, true},
        "diagnostics lost the application's dynamic VSync-off request");
    expect(present_diagnostics::applicationPresentMode(VK_PRESENT_MODE_FIFO_KHR,
            nullptr) == present_diagnostics::ApplicationPresentMode{2, false},
        "diagnostics fabricated a dynamic mode override");
    auto ambiguousMode = dynamicMode;
    ambiguousMode.swapchainCount = 2;
    expect(present_diagnostics::applicationPresentMode(VK_PRESENT_MODE_FIFO_KHR,
            &ambiguousMode).mode == -1,
        "multi-swapchain modes must not be attributed to the first element");
    using present_diagnostics::BridgePresentTiming;
    BridgePresentTiming timing;
    constexpr uint64_t origin = 10000000000;
    timing.request(UINT32_MAX, origin, origin, origin - 100000);
    timing.feedback(UINT32_MAX, origin, origin + 200000);
    timing.request(0, origin + 8000000, origin + 1000000, origin + 1000000);
    timing.feedback(0, origin + 8000000, origin + 8500000);
    timing.feedback(0, origin + 8000000, origin + 8500000); // Duplicate.
    expect(!timing.take(origin + 999999999), "diagnostic window must be rate limited");
    const auto window = timing.take(origin + 1000000000);
    expect(window && window->requests == 2 && window->feedbacks == 2 && window->unmatched == 1 &&
        window->requestedInterval.mean() == 8 && window->reportedInterval.mean() == 8.3 &&
        window->submitLateness.maximum == 0.1 && timing.outstanding() == 0,
        "timing correlation must preserve 64-bit timestamps and wrapped IDs");
    for (uint32_t id = 1; id <= 129; ++id)
        timing.request(id, origin + id, origin + 1000000001, origin);
    timing.feedback(1, origin + 1, origin + 2); // Overwritten, not a good sample.
    timing.feedback(129, origin + 130, origin + 131); // Mismatched request.
    timing.feedback(129, origin + 129, origin + 9000000); // Gap in feedback.
    const auto gaps = timing.take(origin + 2000000000);
    expect(gaps && gaps->overwritten == 1 && gaps->unmatched == 2 && gaps->feedbacks == 1 &&
        gaps->discontinuities == 1 && gaps->nonconsecutiveIds == 1 &&
        gaps->repeatedTimestamps == 0 && gaps->backwardsTimestamps == 0 &&
        gaps->reportedInterval.count == 0 && timing.outstanding() == 127,
        "missing or mismatched feedback must not fabricate interval samples");
    BridgePresentTiming repeated;
    for (uint32_t id = 1; id <= 10; ++id) {
        repeated.request(id, origin + id, origin, origin);
        repeated.feedback(id, origin + id, origin + 1000 * ((id - 1) / 5 + 1));
    }
    const auto grouped = repeated.take(origin + 1000000000);
    expect(grouped && grouped->feedbacks == 10 && grouped->discontinuities == 8 &&
        grouped->repeatedTimestamps == 8 && grouped->nonconsecutiveIds == 0 &&
        grouped->backwardsTimestamps == 0 && grouped->reportedInterval.count == 1,
        "reused compositor predictions must be distinct from nonconsecutive IDs");
    repeated.request(12, origin + 12, origin + 1000000001, origin);
    repeated.feedback(12, origin + 12, origin + 1999);
    const auto backwards = repeated.take(origin + 2000000000);
    expect(backwards && backwards->nonconsecutiveIds == 1 &&
        backwards->backwardsTimestamps == 1 && backwards->discontinuities == 1,
        "overlapping discontinuity causes must not double-count the interval");
    VkInstanceCreateInfo instanceInfo{.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    expect(!requestsGamescopeScalingSurface(instanceInfo), "headless probe must not open a connection");
    const char* extensions[]{"VK_KHR_surface", "VK_KHR_xlib_surface"};
    instanceInfo.enabledExtensionCount = 2;
    instanceInfo.ppEnabledExtensionNames = extensions;
    expect(requestsGamescopeScalingSurface(instanceInfo), "Xlib instance needs the adapter");
    extensions[1] = "VK_KHR_xcb_surface";
    expect(requestsGamescopeScalingSurface(instanceInfo), "XCB instance needs the adapter");
    extensions[0] = "VK_KHR_get_physical_device_properties2";
    expect(!requestsGamescopeScalingSurface(instanceInfo), "must preserve base surface extension dependency");
    extensions[0] = "VK_KHR_surface";
    extensions[1] = "VK_KHR_wayland_surface";
    expect(!requestsGamescopeScalingSurface(instanceInfo), "native Wayland instance needs no X11 adapter");

    for (bool scaling : {false, true}) {
        for (bool wsi : {false, true}) {
            expect(needsGamescopeScalingSurface(scaling, !wsi, false, false,
                "gamescope-0", "") == (scaling && !wsi), "independent scaling/WSI routing");
        }
    }
    expect(!needsGamescopeScalingSurface(true, true, true, false, "gamescope-0", ""), "lower role must not create a second bridge");
    expect(!needsGamescopeScalingSurface(true, true, false, true, "gamescope-0", ""), "split chain must not create a second bridge");
    expect(!needsGamescopeScalingSurface(true, true, false, false, "", ""), "desktop must retain its surface");
    expect(!needsGamescopeScalingSurface(true, true, false, false, "gamescope-0", "wayland-1"), "unrelated Wayland session must remain isolated");
    expect(needsGamescopeScalingSurface(true, true, false, false, "gamescope-0", "gamescope-0"), "matching active session");
    expect(canAttemptGamescopeScalingSurface(VK_SUCCESS, true),
        "advertised Wayland surface support must permit the bridge");
    expect(!canAttemptGamescopeScalingSurface(VK_SUCCESS, false),
        "a complete extension list without Wayland must reject the bridge");
    expect(canAttemptGamescopeScalingSurface(VK_ERROR_LAYER_NOT_PRESENT, false),
        "an opaque chained-layer extension list must permit the bridge attempt");
    expect(!canAttemptGamescopeScalingSurface(VK_ERROR_INITIALIZATION_FAILED, false),
        "unrelated enumeration failures must reject the bridge");

    for (int mode : {1, 2, 3}) {
        const auto start = std::chrono::steady_clock::now();
        {
            mako_test_surface_mode(mode);
            GamescopeScalingSurface bridge;
            expect(!bridge.connect(), "missing connection/protocol or silent peer must fail closed");
        }
        expect(mako_test_surface_objects() == 0, "failed connection leaked a Wayland object");
        expect(std::chrono::steady_clock::now() - start < std::chrono::seconds(2), "discovery exceeded its deadline");
    }
    mako_test_surface_mode(0);
    const VkXcbSurfaceCreateInfoKHR info{
        .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
        .connection = reinterpret_cast<xcb_connection_t*>(1), .window = 71,
    };
    {
        GamescopeScalingSurface bridge;
        expect(bridge.connect(), "valid Gamescope connection");
        const int connectionObjects = mako_test_surface_objects();
        VkSurfaceKHR surface{};
        for (int mode : {4, 6, 9, 10}) {
            mako_test_surface_mode(mode);
            expect(!bridge.create(VK_NULL_HANDLE, next, info, nullptr, &surface), "unproven X11 window/server must retain original surface");
            expect(mako_test_surface_objects() == connectionObjects, "ineligible window allocated protocol objects");
        }
        mako_test_surface_mode(0);
        auto extended = info;
        extended.pNext = &info;
        expect(!bridge.create(VK_NULL_HANDLE, next, extended, nullptr, &surface), "unknown surface extension must be preserved");
        failVulkan = true;
        expect(bridge.create(VK_NULL_HANDLE, next, info, nullptr, &surface) == VK_ERROR_OUT_OF_DEVICE_MEMORY, "Vulkan surface failure must retain its error");
        expect(mako_test_surface_objects() == connectionObjects, "Vulkan failure leaked protocol objects");
        failVulkan = false;
        VkAllocationCallbacks allocator{};
        expect(bridge.create(VK_NULL_HANDLE, next, info, &allocator, &surface) == VK_SUCCESS, "create private surface");
        expect(lastAllocator == &allocator && bridge.owns(surface), "surface allocator/ownership");
        expect(!bridge.applicationFormats(VK_NULL_HANDLE, VK_NULL_HANDLE,
            surfaceFormats), "unowned surface formats must pass through");
        const int destroyedBeforeFormats = nativeSurfacesDestroyed;
        const auto applicationFormats = bridge.applicationFormats(
            VK_NULL_HANDLE, surface, surfaceFormats);
        expect(applicationFormats && applicationFormats->size() == 2 &&
            (*applicationFormats)[0].format == VK_FORMAT_B8G8R8A8_UNORM &&
            (*applicationFormats)[1].format == VK_FORMAT_R8G8B8A8_UNORM &&
            nativeSurfacesDestroyed == destroyedBeforeFormats + 1,
            "bridge must expose shared formats in the original X11 order");
        failNativeSurface = true;
        expect(!bridge.applicationFormats(VK_NULL_HANDLE, surface, surfaceFormats) &&
            nativeSurfacesDestroyed == destroyedBeforeFormats + 1,
            "failed native format proof must leave driver formats untouched");
        failNativeSurface = false;
        const int surfaceObjects = mako_test_surface_objects();
        const auto feedbackInfo = swapchainInfo(surface);
        for (int mode : {7, 8}) {
            mako_test_surface_mode(mode);
            expect(!bridge.createSwapchain(surface,
                reinterpret_cast<VkSwapchainKHR>(static_cast<uintptr_t>(700 + mode)),
                feedbackInfo, {1920, 1080}, 6, "vkd3d", VK_PRESENT_MODE_FIFO_KHR),
                "protocol allocation failure");
            expect(mako_test_surface_objects() == surfaceObjects,
                "failed swapchain protocol setup leaked objects");
        }
        mako_test_surface_mode(0);
        const auto firstSwapchain = reinterpret_cast<VkSwapchainKHR>(
            static_cast<uintptr_t>(1001));
        const int feedbacks = mako_test_surface_feedbacks();
        expect(bridge.createSwapchain(surface, firstSwapchain,
            feedbackInfo, {1920, 1080}, 6, "vkd3d", VK_PRESENT_MODE_FIFO_KHR),
            "create swapchain protocol object");
        expect(mako_test_surface_feedbacks() == feedbacks + 1 &&
            mako_test_surface_feedback_image_count() == 6 &&
            std::strcmp(mako_test_surface_feedback_engine(), "vkd3d") == 0,
            "swapchain feedback must carry the actual image count and engine");

        // Startup can resize the X11 window between capability query and
        // creation. The private Wayland swapchain accepts the old size, so
        // the bridge must preserve native out-of-date acquisition behavior.
        std::ostringstream extentLog;
        auto* previousExtentLog = std::cerr.rdbuf(extentLog.rdbuf());
        checkApplicationExtent(bridge, surface, {1920, 1080});
        const auto stableQueryLog = extentLog.str();
        checkApplicationExtent(bridge, surface, {1920, 1080});
        expect(extentLog.str() == stableQueryLog,
            "unchanged capability queries must not emit per-frame extent logs");
        const auto overrideSwapchain = reinterpret_cast<VkSwapchainKHR>(static_cast<uintptr_t>(3006));
        expect(bridge.createSwapchain(surface, overrideSwapchain, feedbackInfo,
            {2560, 1440}, 6, "vkd3d", VK_PRESENT_MODE_FIFO_KHR) &&
            bridge.acquisitionResult(surface, overrideSwapchain) == VK_SUCCESS,
            "an explicit render extent must remain valid when Gamescope retains the window size");
        const auto staleSwapchain = reinterpret_cast<VkSwapchainKHR>(static_cast<uintptr_t>(3001));
        mako_test_surface_resize(2560, 1440);
        const int createGeometryQueries = mako_test_surface_geometry_queries();
        expect(bridge.createSwapchain(surface, staleSwapchain, feedbackInfo,
            {1920, 1080}, 6, "vkd3d", VK_PRESENT_MODE_FIFO_KHR),
            "a creation race must keep normal Vulkan creation semantics");
        expect(mako_test_surface_geometry_queries() == createGeometryQueries + 1,
            "creation must validate the live window with one query");
        const int acquireGeometryQueries = mako_test_surface_geometry_queries();
        for (int attempt = 0; attempt < 100; ++attempt)
            expect(bridge.acquisitionResult(surface, staleSwapchain) == VK_ERROR_OUT_OF_DATE_KHR,
                "stale startup dimensions must request recreation before acquisition");
        expect(mako_test_surface_geometry_queries() == acquireGeometryQueries,
            "cached acquisition checks must never query X11");
        expect(bridge.acquisitionResult(VK_NULL_HANDLE, staleSwapchain) == VK_SUCCESS &&
            bridge.acquisitionResult(surface, VK_NULL_HANDLE) == VK_SUCCESS,
            "unowned surfaces and swapchains must preserve lower acquisition");
        checkApplicationExtent(bridge, surface, {2560, 1440}, 1.5F);
        expect(bridge.acquisitionResult(surface, overrideSwapchain) == VK_SUCCESS,
            "window observations must not reinterpret an explicit render extent as stale");
        bridge.destroySwapchain(surface, overrideSwapchain);
        const auto freshSwapchain = reinterpret_cast<VkSwapchainKHR>(static_cast<uintptr_t>(3002));
        expect(bridge.createSwapchain(surface, freshSwapchain, feedbackInfo,
            {2560, 1440}, 6, "vkd3d", VK_PRESENT_MODE_FIFO_KHR) &&
            bridge.acquisitionResult(surface, freshSwapchain) == VK_SUCCESS &&
            bridge.acquisitionResult(surface, staleSwapchain) == VK_ERROR_OUT_OF_DATE_KHR,
            "replacement must use fresh geometry without resetting its predecessor");
        // A genuine low-resolution game window remains valid; the display
        // target is not authority to force the game's source resolution.
        checkApplicationExtent(bridge, surface, {1152, 720});
        expect(bridge.acquisitionResult(surface, freshSwapchain) == VK_ERROR_OUT_OF_DATE_KHR,
            "a later capability query must invalidate the old source size");
        const auto lowSwapchain = reinterpret_cast<VkSwapchainKHR>(static_cast<uintptr_t>(3003));
        auto lowInfo = feedbackInfo;
        lowInfo.imageExtent = {2304, 1440};
        expect(bridge.createSwapchain(surface, lowSwapchain, lowInfo,
            {1152, 720}, 6, "vkd3d", VK_PRESENT_MODE_FIFO_KHR) &&
            bridge.acquisitionResult(surface, lowSwapchain) == VK_SUCCESS,
            "an intentional 720-high input must not be replaced with the display size");
        checkApplicationExtent(bridge, surface, {2560, 1440}, 1.5F);
        checkApplicationExtent(bridge, surface, {1152, 720});
        expect(bridge.acquisitionResult(surface, lowSwapchain) == VK_ERROR_OUT_OF_DATE_KHR,
            "returning to the old size must not revive an invalidated swapchain");
        bridge.destroySwapchain(surface, staleSwapchain);
        bridge.destroySwapchain(surface, freshSwapchain);
        bridge.destroySwapchain(surface, lowSwapchain);
        expect(bridge.acquisitionResult(surface, lowSwapchain) == VK_SUCCESS,
            "destroyed protocol contents must not retain an extent override");

#if defined(VK_KHR_swapchain_maintenance1)
        VkSwapchainPresentScalingCreateInfoKHR explicitScaling{
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_SCALING_CREATE_INFO_KHR,
            .scalingBehavior = VK_PRESENT_SCALING_STRETCH_BIT_KHR,
        };
#elif defined(VK_EXT_swapchain_maintenance1)
        VkSwapchainPresentScalingCreateInfoEXT explicitScaling{
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_SCALING_CREATE_INFO_EXT,
            .scalingBehavior = VK_PRESENT_SCALING_STRETCH_BIT_EXT,
        };
#endif
#if defined(VK_KHR_swapchain_maintenance1) || defined(VK_EXT_swapchain_maintenance1)
        const auto explicitSwapchain = reinterpret_cast<VkSwapchainKHR>(static_cast<uintptr_t>(3004));
        auto explicitInfo = feedbackInfo;
        explicitInfo.pNext = &explicitScaling;
        expect(bridge.createSwapchain(surface, explicitSwapchain, explicitInfo,
            {1920, 1080}, 6, "vkd3d", VK_PRESENT_MODE_FIFO_KHR) &&
            bridge.acquisitionResult(surface, explicitSwapchain) == VK_SUCCESS,
            "negotiated presentation scaling may legally differ from the window");
        checkApplicationExtent(bridge, surface, {2560, 1440}, 1.5F);
        expect(bridge.acquisitionResult(surface, explicitSwapchain) == VK_SUCCESS,
            "window observations must preserve negotiated presentation scaling");
        bridge.destroySwapchain(surface, explicitSwapchain);
        explicitScaling.scalingBehavior = 0;
        mako_test_surface_resize(1920, 1080);
        expect(bridge.createSwapchain(surface, explicitSwapchain, explicitInfo,
            {2560, 1440}, 6, "vkd3d", VK_PRESENT_MODE_FIFO_KHR) &&
            bridge.acquisitionResult(surface, explicitSwapchain) == VK_ERROR_OUT_OF_DATE_KHR,
            "a zero scaling flag must retain the ordinary exact-window contract");
        bridge.destroySwapchain(surface, explicitSwapchain);
#endif
        mako_test_surface_mode(4);
        const auto lostSwapchain = reinterpret_cast<VkSwapchainKHR>(static_cast<uintptr_t>(3005));
        const int objectsBeforeLostWindow = mako_test_surface_objects();
        expect(!bridge.createSwapchain(surface, lostSwapchain, feedbackInfo,
            {1920, 1080}, 6, "vkd3d", VK_PRESENT_MODE_FIFO_KHR) &&
            mako_test_surface_objects() == objectsBeforeLostWindow &&
            bridge.acquisitionResult(surface, lostSwapchain) == VK_SUCCESS,
            "failed window proof must release the new protocol content");
        mako_test_surface_mode(0);
        std::cerr.rdbuf(previousExtentLog);
        if (present_diagnostics::enabled()) {
            expect(extentLog.str().find("operation=capability-query") != std::string::npos &&
                extentLog.str().find("queried=1920x1080") != std::string::npos &&
                extentLog.str().find("application=1920x1080; current=2560x1440") != std::string::npos &&
                extentLog.str().find("extent_contract=application-override") != std::string::npos &&
                extentLog.str().find("action=requery-before-application-acquire") != std::string::npos,
                "startup diagnostics must distinguish queried, requested and live window sizes");
        } else {
            expect(extentLog.str().empty(), "window extent diagnostics must be opt-in");
        }
        checkApplicationExtent(bridge, surface, {640, 480});
        checkApplicationExtent(bridge, surface, {1920, 1080});
        checkApplicationExtent(bridge, surface, {1280, 720});
        // Black Mesa's reported 32-bit path: 1440p source, 1.5x to 4K.
        checkApplicationExtent(bridge, surface, {2560, 1440}, 1.5F);
        auto caps = driverCapabilities();
        const auto unchanged = caps;
        const int geometryQueries = mako_test_surface_geometry_queries();
        expect(!bridge.applicationCapabilities(VK_NULL_HANDLE, caps) &&
            std::memcmp(&caps, &unchanged, sizeof(caps)) == 0 &&
            mako_test_surface_geometry_queries() == geometryQueries,
            "unowned and native surfaces must pass through without an X11 query");
        mako_test_surface_mode(4);
        expect(bridge.applicationCapabilities(surface, caps) == VK_ERROR_SURFACE_LOST_KHR &&
            std::memcmp(&caps, &unchanged, sizeof(caps)) == 0,
            "a lost window must propagate an error instead of exposing a variable extent");
        mako_test_surface_mode(0);
        for (const auto extent : {VkExtent2D{0, 480}, VkExtent2D{640, 0},
                VkExtent2D{17000, 1080}, VkExtent2D{1920, 17000}}) {
            mako_test_surface_resize(static_cast<uint16_t>(extent.width),
                static_cast<uint16_t>(extent.height));
            expect(bridge.applicationCapabilities(surface, caps) == VK_ERROR_SURFACE_LOST_KHR &&
                std::memcmp(&caps, &unchanged, sizeof(caps)) == 0,
                "an unrepresentable extent must not fabricate supported capabilities");
        }
        checkApplicationExtent(bridge, surface, {1920, 1080});
        const auto timedSwapchain = reinterpret_cast<VkSwapchainKHR>(static_cast<uintptr_t>(500));
        expect(bridge.createSwapchain(surface, timedSwapchain, feedbackInfo, {1920, 1080}, 5,
                "MAKO Renderer", VK_PRESENT_MODE_FIFO_KHR, 120),
            "timed bridge creation");
        const int times = mako_test_surface_present_times();
        std::ostringstream timingLog;
        auto* previousLog = std::cerr.rdbuf(timingLog.rdbuf());
        const int timedReads = mako_test_surface_reads();
        uint64_t previousTime = 0;
        for (uint32_t output = 1; output <= 130; ++output) {
            const auto before = std::chrono::steady_clock::now();
            expect(bridge.preparePresent(surface, timedSwapchain, 120, 120, 1, true),
                "timed generated/real bridge output");
            const auto ns = mako_test_surface_present_time();
            const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                before.time_since_epoch()).count();
            expect(mako_test_surface_present_times() == times + static_cast<int>(output) &&
                    mako_test_surface_present_id() == output && ns > previousTime &&
                    (output > 3 || (ns >= static_cast<uint64_t>(nowNs) + 16000000 &&
                    ns <= static_cast<uint64_t>(nowNs) + 50000000)),
                "protocol lost per-output id or monotonic 64-bit future deadline");
            previousTime = ns;
            mako_test_surface_timing();
        }
        std::cerr.rdbuf(previousLog);
        const bool diagnostics = std::getenv("MAKO_PRESENT_DIAGNOSTICS") &&
            std::strcmp(std::getenv("MAKO_PRESENT_DIAGNOSTICS"), "1") == 0;
        expect((timingLog.str().find("operation=gamescope-bridge-timing") != std::string::npos) == diagnostics,
            "bridge timing must be opt-in");
        if (diagnostics)
            expect(timingLog.str().find("refresh_cycle_ns=8333333") != std::string::npos &&
                timingLog.str().find("lateness_mean_ms=0.1") != std::string::npos &&
                timingLog.str().find("unmatched=0") != std::string::npos,
                "protocol timing and refresh events must reach the matching accumulator");
        expect(mako_test_surface_reads() == timedReads, "timing diagnostics must not read the socket");
        bridge.destroySwapchain(surface, timedSwapchain);
        expect(bridge.createSwapchain(surface, timedSwapchain, feedbackInfo, {1920, 1080}, 5,
                "MAKO Renderer", VK_PRESENT_MODE_FIFO_KHR, 120),
            "retained generation-off bridge creation");
        const auto nativeBefore = std::chrono::steady_clock::now();
        expect(bridge.preparePresent(surface, timedSwapchain, 120, 120, 1, false),
            "retained generation-off bridge output");
        const auto nativeAfter = std::chrono::steady_clock::now();
        const auto nativeNs = mako_test_surface_present_time();
        const auto nanos = [](const auto time) {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                time.time_since_epoch()).count());
        };
        expect(nativeNs >= nanos(nativeBefore) && nativeNs <= nanos(nativeAfter),
            "generation-off protocol added an unused generated-batch lead");
        bridge.destroySwapchain(surface, timedSwapchain);
        const int associations = mako_test_surface_associations();
        const int presentModes = mako_test_surface_present_modes();
        expect(associations == 131, "surface creation must not steal existing window content");
        const int reads = mako_test_surface_reads();
        const int presentGeometryQueries = mako_test_surface_geometry_queries();
        for (int frame = 0; frame < 100; ++frame)
            expect(bridge.preparePresent(surface, firstSwapchain), "healthy presentation association");
        expect(mako_test_surface_associations() == associations + 100,
            "presentation must reassert the Gamescope window association");
        expect(mako_test_surface_present_modes() == presentModes + 100 &&
            mako_test_surface_present_mode() == VK_PRESENT_MODE_FIFO_KHR,
            "ordered presentation must reassert compositor-side FIFO");
        expect(mako_test_surface_reads() == reads, "present path must not poll or read the socket");
        expect(mako_test_surface_geometry_queries() == presentGeometryQueries,
            "present path must not query X11 geometry");
        expect(mako_test_surface_window() == 71 && mako_test_surface_server() == 9, "actual X11 window/server binding");

        const auto sameSurfaceReplacement = reinterpret_cast<VkSwapchainKHR>(
            static_cast<uintptr_t>(1002));
        expect(bridge.createSwapchain(surface, sameSurfaceReplacement,
            feedbackInfo, {1920, 1080}, 7, "vkd3d", VK_PRESENT_MODE_FIFO_KHR),
            "create replacement swapchain protocol object");
        expect(mako_test_surface_associations() == associations + 100,
            "creating a replacement must not steal the live image");
        expect(bridge.preparePresent(surface, sameSurfaceReplacement),
            "replacement swapchain first present");
        expect(mako_test_surface_associations() == associations + 101,
            "replacement must establish a fresh compositor association");
        mako_test_surface_retire();
        expect(!bridge.preparePresent(surface, firstSwapchain),
            "retired swapchain protocol object must stop presentation");
        expect(bridge.preparePresent(surface, sameSurfaceReplacement),
            "retiring the predecessor affected the replacement");
        bridge.destroySwapchain(surface, firstSwapchain);

        VkSurfaceKHR replacement{};
        const VkXlibSurfaceCreateInfoKHR xlib{
            .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
            .dpy = reinterpret_cast<Display*>(1), .window = 71,
        };
        expect(bridge.create(VK_NULL_HANDLE, next, xlib, nullptr, &replacement) == VK_SUCCESS, "Xlib replacement surface");
        const auto xlibFormats = bridge.applicationFormats(VK_NULL_HANDLE,
            replacement, surfaceFormats);
        expect(xlibFormats && xlibFormats->size() == 2 &&
            xlibFormats->front().format == VK_FORMAT_B8G8R8A8_UNORM,
            "Xlib bridge must retain the application's SDR format order");
        const auto replacementSwapchain = reinterpret_cast<VkSwapchainKHR>(
            static_cast<uintptr_t>(2001));
        const auto replacementFeedback = swapchainInfo(replacement);
        expect(bridge.createSwapchain(replacement, replacementSwapchain,
            replacementFeedback, {1920, 1080}, 5, "vkd3d", std::nullopt),
            "replacement surface swapchain protocol object");
        checkApplicationExtent(bridge, replacement, {640, 480});
        checkApplicationExtent(bridge, replacement, {1920, 1080});
        checkApplicationExtent(bridge, replacement, {2560, 1440}, 1.5F);
        expect(mako_test_surface_associations() == associations + 102,
            "replacement preparation stole live surface");
        expect(bridge.preparePresent(replacement, replacementSwapchain), "replacement first present");
        bridge.destroy(surface);
        expect(!bridge.owns(surface) && bridge.owns(replacement), "surface destruction removed wrong owner");
        expect(!bridge.applicationCapabilities(surface, caps),
            "destroyed surface must not retain an application extent override");
        mako_test_surface_mode(5);
        expect(!bridge.preparePresent(replacement, replacementSwapchain), "lost connection must stop presentation");
        mako_test_surface_mode(0);
        bridge.destroy(replacement);
        expect(mako_test_surface_objects() == connectionObjects, "surface destruction leaked protocol objects");
    }
    expect(mako_test_surface_objects() == 0, "instance destruction leaked connection objects");

    // RE4/Proton selects the window's exact 1920x1080 rendering size. A real
    // variable Wayland surface permits a separate 3840x2160 output; never
    // weaken the existing fixed-surface override guard to fabricate that split.
    VkSurfaceCapabilitiesKHR caps{};
    caps.currentExtent = {UINT32_MAX, UINT32_MAX};
    caps.minImageExtent = {1, 1};
    caps.maxImageExtent = {16384, 16384};
    const auto decision = scalingDecisionForCreate(
        SpatialScalingPolicy{.enabled = true, .factor = 2.0F}, true, 1, caps,
        {1920, 1080}, std::nullopt, std::nullopt, std::nullopt,
        VkExtent2D{3840, 2160}, true);
    expect(decision.extents && sameExtent(decision.extents->source, {1920, 1080}) &&
        sameExtent(decision.extents->presentation, {3840, 2160}), "RE4 explicit render extent must scale through proven variable surface");
    std::cout << "Gamescope scaling surface tests passed\n";
}
