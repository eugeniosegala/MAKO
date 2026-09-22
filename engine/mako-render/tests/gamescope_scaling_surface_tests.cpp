/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "gamescope_scaling_surface.hpp"
#include "spatial_scaling_policy.hpp"
#include <X11/Xlib.h>
#include <xcb/xcb.h>
#include <vulkan/vulkan_xcb.h>
#include <vulkan/vulkan_xlib.h>
#include <vulkan/vulkan_wayland.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>

extern "C" {
    void mako_test_surface_mode(int);
    int mako_test_surface_objects();
    int mako_test_surface_associations();
    int mako_test_surface_feedbacks();
    int mako_test_surface_present_modes();
    uint32_t mako_test_surface_present_mode();
    uint32_t mako_test_surface_feedback_image_count();
    const char* mako_test_surface_feedback_engine();
    int mako_test_surface_reads();
    int mako_test_surface_geometry_queries();
    void mako_test_surface_resize(uint16_t, uint16_t);
    uint32_t mako_test_surface_window();
    uint32_t mako_test_surface_server();
    void mako_test_surface_retire();
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
    const VkAllocationCallbacks* lastAllocator{};
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
    void VKAPI_PTR destroySurface(VkInstance, VkSurfaceKHR, const VkAllocationCallbacks*) {}
    PFN_vkVoidFunction VKAPI_PTR next(VkInstance, const char* name) {
        if (std::strcmp(name, "vkCreateWaylandSurfaceKHR") == 0)
            return reinterpret_cast<PFN_vkVoidFunction>(createWayland);
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
        const int surfaceObjects = mako_test_surface_objects();
        const auto feedbackInfo = swapchainInfo(surface);
        for (int mode : {7, 8}) {
            mako_test_surface_mode(mode);
            expect(!bridge.createSwapchain(surface,
                reinterpret_cast<VkSwapchainKHR>(static_cast<uintptr_t>(700 + mode)),
                feedbackInfo, 6, "vkd3d", VK_PRESENT_MODE_FIFO_KHR),
                "protocol allocation failure");
            expect(mako_test_surface_objects() == surfaceObjects,
                "failed swapchain protocol setup leaked objects");
        }
        mako_test_surface_mode(0);
        const auto firstSwapchain = reinterpret_cast<VkSwapchainKHR>(
            static_cast<uintptr_t>(1001));
        const int feedbacks = mako_test_surface_feedbacks();
        expect(bridge.createSwapchain(surface, firstSwapchain,
            feedbackInfo, 6, "vkd3d", VK_PRESENT_MODE_FIFO_KHR),
            "create swapchain protocol object");
        expect(mako_test_surface_feedbacks() == feedbacks + 1 &&
            mako_test_surface_feedback_image_count() == 6 &&
            std::strcmp(mako_test_surface_feedback_engine(), "vkd3d") == 0,
            "swapchain feedback must carry the actual image count and engine");
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
        const int associations = mako_test_surface_associations();
        const int presentModes = mako_test_surface_present_modes();
        expect(associations == 0, "surface creation must not steal existing window content");
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
            feedbackInfo, 7, "vkd3d", VK_PRESENT_MODE_FIFO_KHR),
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
        const auto replacementSwapchain = reinterpret_cast<VkSwapchainKHR>(
            static_cast<uintptr_t>(2001));
        const auto replacementFeedback = swapchainInfo(replacement);
        expect(bridge.createSwapchain(replacement, replacementSwapchain,
            replacementFeedback, 5, "vkd3d", std::nullopt),
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
