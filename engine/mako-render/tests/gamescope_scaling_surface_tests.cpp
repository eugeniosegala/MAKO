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
    int mako_test_surface_reads();
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
        for (int mode : {7, 8}) {
            mako_test_surface_mode(mode);
            expect(bridge.create(VK_NULL_HANDLE, next, info, nullptr, &surface) == VK_ERROR_OUT_OF_HOST_MEMORY, "protocol allocation failure");
            expect(mako_test_surface_objects() == connectionObjects, "failed surface setup leaked protocol objects");
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
        const int associations = mako_test_surface_associations();
        expect(associations == 0, "surface creation must not steal existing window content");
        const int reads = mako_test_surface_reads();
        for (int frame = 0; frame < 100; ++frame)
            expect(bridge.preparePresent(surface), "healthy presentation association");
        expect(mako_test_surface_associations() == associations + 1, "association must be sent only once");
        expect(mako_test_surface_reads() == reads, "present path must not poll or read the socket");
        expect(mako_test_surface_window() == 71 && mako_test_surface_server() == 9, "actual X11 window/server binding");

        VkSurfaceKHR replacement{};
        const VkXlibSurfaceCreateInfoKHR xlib{
            .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
            .dpy = reinterpret_cast<Display*>(1), .window = 71,
        };
        expect(bridge.create(VK_NULL_HANDLE, next, xlib, nullptr, &replacement) == VK_SUCCESS, "Xlib replacement surface");
        expect(mako_test_surface_associations() == associations + 1, "replacement preparation stole live surface");
        expect(bridge.preparePresent(replacement), "replacement first present");
        mako_test_surface_retire();
        expect(!bridge.preparePresent(surface), "retired surface must not reclaim window content");
        expect(bridge.preparePresent(replacement), "retiring old surface affected replacement");
        bridge.destroy(surface);
        expect(!bridge.owns(surface) && bridge.owns(replacement), "surface destruction removed wrong owner");
        mako_test_surface_mode(5);
        expect(!bridge.preparePresent(replacement), "lost connection must stop presentation");
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
