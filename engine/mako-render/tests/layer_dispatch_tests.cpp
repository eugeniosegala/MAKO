/* SPDX-License-Identifier: GPL-3.0-or-later */
// Drives the built layer through the loader interface over a fake lower chain,
// so no Vulkan driver or loader is needed.

#include <X11/Xlib.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xlib.h>
#include <vulkan/vk_layer.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <dlfcn.h>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// Before Vulkan-Loader's terminator fix (1.3.239 in Steam's runtime), a lower
// lookup made after vkCreateInstance returned answers from the instance's
// top-level dispatch table instead of reaching the driver.
bool instanceCreated = false;
int lowerSurfaces = 0;
int topOfChainSurfaces = 0;
void* instanceObject = nullptr;

VkResult VKAPI_CALL lowerCreateInstance(const VkInstanceCreateInfo*, const VkAllocationCallbacks*,
        VkInstance* instance) {
    *instance = reinterpret_cast<VkInstance>(&instanceObject);
    return VK_SUCCESS;
}
void VKAPI_CALL lowerDestroyInstance(VkInstance, const VkAllocationCallbacks*) {}
VkResult VKAPI_CALL lowerCreateXlibSurface(VkInstance, const VkXlibSurfaceCreateInfoKHR*,
        const VkAllocationCallbacks*, VkSurfaceKHR* surface) {
    ++lowerSurfaces;
    *surface = reinterpret_cast<VkSurfaceKHR>(uintptr_t{0x1234});
    return VK_SUCCESS;
}
VkResult VKAPI_CALL topOfChainCreateXlibSurface(VkInstance, const VkXlibSurfaceCreateInfoKHR*,
        const VkAllocationCallbacks*, VkSurfaceKHR*) {
    ++topOfChainSurfaces;
    return VK_ERROR_INITIALIZATION_FAILED;
}
void VKAPI_CALL unusedCommand() {}

PFN_vkVoidFunction VKAPI_CALL lowerGetInstanceProcAddr(VkInstance, const char* name) {
    const std::string_view command{name};
    if (command == "vkCreateInstance")
        return reinterpret_cast<PFN_vkVoidFunction>(lowerCreateInstance);
    if (command == "vkDestroyInstance")
        return reinterpret_cast<PFN_vkVoidFunction>(lowerDestroyInstance);
    if (command == "vkCreateXlibSurfaceKHR")
        return instanceCreated
            ? reinterpret_cast<PFN_vkVoidFunction>(topOfChainCreateXlibSurface)
            : reinterpret_cast<PFN_vkVoidFunction>(lowerCreateXlibSurface);
    return reinterpret_cast<PFN_vkVoidFunction>(unusedCommand);
}
}

int main(int argc, char** argv) {
    expect(argc == 2, "layer library path is required");
    char directory[] = "/tmp/mako-layer-dispatch.XXXXXX";
    expect(mkdtemp(directory) != nullptr, "temporary directory must be created");
    const std::filesystem::path root{directory};
    std::ofstream(root / "conf.toml") << "version = 2\n\n[[profile]]\nname = \"layer-dispatch\"\n";
    setenv("HOME", directory, 1);
    setenv("XDG_CONFIG_HOME", directory, 1);
    setenv("MAKO_CONFIG", (root / "conf.toml").c_str(), 1);
    setenv("MAKO_PROFILE", "layer-dispatch", 1);
    for (const char* name : {"DISPLAY", "WAYLAND_DISPLAY", "GAMESCOPE_WAYLAND_DISPLAY", "MAKO_PRESENT_DIAGNOSTICS"})
        unsetenv(name);

    void* layer = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    expect(layer != nullptr, "the layer must load");
    const auto negotiate = reinterpret_cast<PFN_vkNegotiateLoaderLayerInterfaceVersion>(
        dlsym(layer, "vkNegotiateLoaderLayerInterfaceVersion"));
    expect(negotiate != nullptr, "the layer must export interface negotiation");
    VkNegotiateLayerInterface interface{
        .sType = LAYER_NEGOTIATE_INTERFACE_STRUCT,
        .loaderLayerInterfaceVersion = 2,
    };
    expect(negotiate(&interface) == VK_SUCCESS, "interface negotiation must succeed");
    const auto getInstanceProcAddr = interface.pfnGetInstanceProcAddr;
    const auto getDeviceProcAddr = interface.pfnGetDeviceProcAddr;

    // A layer above resolves every command it does not intercept through
    // these, as Steam's Fossilize layer does with vkGetDeviceProcAddr.
    expect(getInstanceProcAddr(VK_NULL_HANDLE, "vkGetInstanceProcAddr") ==
        reinterpret_cast<PFN_vkVoidFunction>(getInstanceProcAddr),
        "vkGetInstanceProcAddr must return the layer's own");
    expect(getInstanceProcAddr(VK_NULL_HANDLE, "vkGetDeviceProcAddr") ==
        reinterpret_cast<PFN_vkVoidFunction>(getDeviceProcAddr),
        "vkGetInstanceProcAddr must return the layer's vkGetDeviceProcAddr");
    expect(getDeviceProcAddr(VK_NULL_HANDLE, "vkGetDeviceProcAddr") ==
        reinterpret_cast<PFN_vkVoidFunction>(getDeviceProcAddr),
        "vkGetDeviceProcAddr must return the layer's own");

    VkLayerInstanceLink link{.pfnNextGetInstanceProcAddr = lowerGetInstanceProcAddr};
    VkLayerInstanceCreateInfo chain{
        .sType = VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO,
        .function = VK_LAYER_LINK_INFO,
    };
    chain.u.pLayerInfo = &link;
    const char* extensions[]{"VK_KHR_surface", "VK_KHR_xlib_surface"};
    const VkInstanceCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = &chain,
        .enabledExtensionCount = 2,
        .ppEnabledExtensionNames = extensions,
    };
    const auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(
        getInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
    VkInstance instance{};
    expect(createInstance(&info, nullptr, &instance) == VK_SUCCESS, "the instance must be created");
    instanceCreated = true;

    const auto createXlibSurface = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
        getInstanceProcAddr(instance, "vkCreateXlibSurfaceKHR"));
    expect(createXlibSurface != reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(lowerCreateXlibSurface),
        "the active layer must hook X11 surface creation");
    const VkXlibSurfaceCreateInfoKHR surfaceInfo{.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
    VkSurfaceKHR surface{};
    expect(createXlibSurface(instance, &surfaceInfo, nullptr, &surface) == VK_SUCCESS &&
        lowerSurfaces == 1 && topOfChainSurfaces == 0,
        "a surface created after the instance must reach the lower command resolved at creation");

    const auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(
        getInstanceProcAddr(instance, "vkDestroyInstance"));
    destroyInstance(instance, nullptr);
    std::filesystem::remove_all(root);
    return 0;
}
