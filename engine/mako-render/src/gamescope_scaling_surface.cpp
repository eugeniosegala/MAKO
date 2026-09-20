/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "gamescope_scaling_surface.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <sys/socket.h>
#include <unordered_map>
#include <utility>
#include <X11/Xlib.h>
#include <xcb/xcb.h>
#include <vulkan/vulkan_xcb.h>
#include <vulkan/vulkan_xlib.h>
#include <vulkan/vulkan_wayland.h>

// Public libwayland ABI declarations, kept private to this optional adapter.
// Resolving the client at runtime preserves the existing non-Wayland launch
// and package dependency boundary. No Wayland development package is needed.
struct wl_proxy;
struct wl_event_queue;
struct wl_message;
struct wl_interface {
    const char* name;
    int version;
    int method_count;
    const wl_message* methods;
    int event_count;
    const wl_message* events;
};
struct wl_message {
    const char* name;
    const char* signature;
    const wl_interface** types;
};
union wl_argument {
    int32_t i;
    uint32_t u;
    const char* s;
    void* o;
};

using namespace mako::layer;

namespace {
    template<typename Function>
    bool symbol(void* library, Function& function, const char* name) {
        function = reinterpret_cast<Function>(dlsym(library, name));
        return function != nullptr;
    }

    template<typename T>
    using Reply = std::unique_ptr<T, decltype(&std::free)>;

    constexpr uint32_t destroyFlag = 1;
    constexpr auto discoveryTimeout = std::chrono::milliseconds(500);
}

struct GamescopeScalingSurface::Impl {
    mutable std::mutex mutex;
    void* waylandLibrary{};
    void* xcbLibrary{};
    void* xlibLibrary{};
    wl_display* display{};
    wl_event_queue* queue{};
    wl_proxy* registry{};
    wl_proxy* compositor{};
    wl_proxy* factory{};
    bool ready{};
    pid_t peerPid{};

    wl_display* (*displayConnect)(const char*){};
    void (*displayDisconnect)(wl_display*){};
    int (*displayGetFd)(wl_display*){};
    int (*displayFlush)(wl_display*){};
    int (*displayGetError)(wl_display*){};
    wl_event_queue* (*createQueue)(wl_display*){};
    void (*destroyQueue)(wl_event_queue*){};
    int (*dispatchPending)(wl_display*, wl_event_queue*){};
    int (*prepareRead)(wl_display*, wl_event_queue*){};
    int (*readEvents)(wl_display*){};
    void (*cancelRead)(wl_display*){};
    wl_proxy* (*marshalConstructor)(wl_proxy*, uint32_t, wl_argument*,
        const wl_interface*, uint32_t){};
    void (*marshalRequest)(wl_proxy*, uint32_t, wl_argument*){};
    int (*addListener)(wl_proxy*, void (**)(void), void*){};
    void (*setQueue)(wl_proxy*, wl_event_queue*){};
    void (*destroyProxy)(wl_proxy*){};

    decltype(&xcb_get_geometry) getGeometry{};
    decltype(&xcb_get_geometry_reply) geometryReply{};
    decltype(&xcb_intern_atom) internAtom{};
    decltype(&xcb_intern_atom_reply) atomReply{};
    decltype(&xcb_get_property) getProperty{};
    decltype(&xcb_get_property_reply) propertyReply{};
    decltype(&xcb_get_property_value) propertyValue{};
    xcb_connection_t* (*getXcbConnection)(Display*){};

    const wl_interface* registryInterface{};
    const wl_interface* compositorInterface{};
    const wl_interface* surfaceInterface{};
    const wl_interface* callbackInterface{};

    // Minimal wire subset of Gamescope's MIT-licensed swapchain protocol at
    // 2d217a16c7e5b56c7417257279bf102320cff024. Association plus create-time
    // swapchain feedback are used. Ordered combined delivery also moves its
    // FIFO constraint from the lower Wayland WSI to this compositor protocol;
    // timing, limiter and HDR control remain inactive. All v1 events are
    // declared so unsolicited feedback is consumed without retaining history.
    // See THIRD_PARTY_NOTICES.md.
    std::array<const wl_interface*, 2> createTypes{};
    std::array<wl_message, 2> factoryRequests{{
        {"destroy", "", nullptr}, {"create_swapchain", "on", createTypes.data()},
    }};
    std::array<wl_message, 6> contentRequests{{
        {"destroy", "", nullptr},
        {"override_window_content", "uu", nullptr},
        {"swapchain_feedback", "uuuuuus", nullptr},
        {"set_present_mode", "u", nullptr},
        {"set_hdr_metadata", "uuuuuuuuuuuu", nullptr},
        {"set_present_time", "uuu", nullptr},
    }};
    std::array<wl_message, 3> contentEvents{{
        {"past_present_timing", "uuuuuuuuu", nullptr},
        {"refresh_cycle", "uu", nullptr}, {"retired", "", nullptr},
    }};
    wl_interface factoryInterface{
        "gamescope_swapchain_factory_v2", 1, 2, factoryRequests.data(), 0, nullptr,
    };
    wl_interface contentInterface{
        "gamescope_swapchain", 1, 6, contentRequests.data(), 3, contentEvents.data(),
    };

    struct Content {
        Impl* owner{};
        wl_proxy* proxy{};
        bool retired{};
        std::optional<VkPresentModeKHR> compositorPresentMode;
        ~Content() {
            if (owner)
                owner->release(proxy);
        }
    };
    struct Surface {
        Impl* owner{};
        wl_proxy* surface{};
        uint32_t server{};
        uint32_t window{};
        xcb_connection_t* connection{};
        std::unordered_map<VkSwapchainKHR, std::unique_ptr<Content>> contents;
        ~Surface() {
            if (owner)
                owner->release(*this);
        }
    };
    std::unordered_map<VkSurfaceKHR, std::unique_ptr<Surface>> surfaces;

    wl_proxy* marshal(wl_proxy* proxy, uint32_t opcode,
            const wl_interface* interface, uint32_t version,
            uint32_t flags, wl_argument* arguments) {
        if (interface)
            return marshalConstructor(proxy, opcode, arguments, interface, version);
        marshalRequest(proxy, opcode, arguments);
        if (flags & destroyFlag)
            destroyProxy(proxy);
        return nullptr;
    }

    void release(wl_proxy*& proxy, const bool requestDestroy = true) {
        if (!proxy)
            return;
        if (requestDestroy)
            marshal(proxy, 0, nullptr, 1, destroyFlag, nullptr);
        else
            destroyProxy(proxy);
        proxy = nullptr;
    }

    void release(Surface& surface) {
        for (auto& [swapchain, content] : surface.contents) {
            static_cast<void>(swapchain);
            release(content->proxy);
        }
        surface.contents.clear();
        release(surface.surface);
    }

    ~Impl() {
        for (auto& [handle, surface] : surfaces) {
            static_cast<void>(handle);
            release(*surface);
        }
        release(factory);
        release(compositor, false);
        release(registry, false);
        if (display) {
            displayFlush(display);
            if (queue)
                destroyQueue(queue);
            displayDisconnect(display);
        }
        if (xlibLibrary)
            dlclose(xlibLibrary);
        if (xcbLibrary)
            dlclose(xcbLibrary);
        if (waylandLibrary)
            dlclose(waylandLibrary);
    }

    bool resolve() {
        waylandLibrary = dlopen("libwayland-client.so.0", RTLD_NOW | RTLD_LOCAL);
        xcbLibrary = dlopen("libxcb.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!waylandLibrary || !xcbLibrary)
            return false;
#define WAYLAND(member, name) if (!symbol(waylandLibrary, member, name)) return false
        WAYLAND(displayConnect, "wl_display_connect");
        WAYLAND(displayDisconnect, "wl_display_disconnect");
        WAYLAND(displayGetFd, "wl_display_get_fd");
        WAYLAND(displayFlush, "wl_display_flush");
        WAYLAND(displayGetError, "wl_display_get_error");
        WAYLAND(createQueue, "wl_display_create_queue");
        WAYLAND(destroyQueue, "wl_event_queue_destroy");
        WAYLAND(dispatchPending, "wl_display_dispatch_queue_pending");
        WAYLAND(prepareRead, "wl_display_prepare_read_queue");
        WAYLAND(readEvents, "wl_display_read_events");
        WAYLAND(cancelRead, "wl_display_cancel_read");
        WAYLAND(marshalConstructor, "wl_proxy_marshal_array_constructor_versioned");
        WAYLAND(marshalRequest, "wl_proxy_marshal_array");
        WAYLAND(addListener, "wl_proxy_add_listener");
        WAYLAND(setQueue, "wl_proxy_set_queue");
        WAYLAND(destroyProxy, "wl_proxy_destroy");
#undef WAYLAND
#define XCB(member, name) if (!symbol(xcbLibrary, member, name)) return false
        XCB(getGeometry, "xcb_get_geometry");
        XCB(geometryReply, "xcb_get_geometry_reply");
        XCB(internAtom, "xcb_intern_atom");
        XCB(atomReply, "xcb_intern_atom_reply");
        XCB(getProperty, "xcb_get_property");
        XCB(propertyReply, "xcb_get_property_reply");
        XCB(propertyValue, "xcb_get_property_value");
#undef XCB
        registryInterface = static_cast<const wl_interface*>(
            dlsym(waylandLibrary, "wl_registry_interface"));
        compositorInterface = static_cast<const wl_interface*>(
            dlsym(waylandLibrary, "wl_compositor_interface"));
        surfaceInterface = static_cast<const wl_interface*>(
            dlsym(waylandLibrary, "wl_surface_interface"));
        callbackInterface = static_cast<const wl_interface*>(
            dlsym(waylandLibrary, "wl_callback_interface"));
        createTypes = {surfaceInterface, &contentInterface};
        return registryInterface && compositorInterface && surfaceInterface &&
            callbackInterface;
    }

    wl_proxy* bind(const uint32_t name, const wl_interface& interface) {
        wl_argument args[4]{{.u = name}, {.s = interface.name}, {.u = 1}, {.o = nullptr}};
        auto* proxy = marshal(registry, 0, &interface, 1, 0, args);
        if (proxy)
            setQueue(proxy, queue);
        return proxy;
    }

    static void global(void* data, wl_proxy*, const uint32_t name,
            const char* interface, const uint32_t version) {
        auto& self = *static_cast<Impl*>(data);
        if (version == 0)
            return;
        if (!self.compositor && std::strcmp(interface, "wl_compositor") == 0)
            self.compositor = self.bind(name, *self.compositorInterface);
        else if (!self.factory && std::strcmp(interface, self.factoryInterface.name) == 0)
            self.factory = self.bind(name, self.factoryInterface);
    }

    static void globalRemoved(void*, wl_proxy*, uint32_t) {}
    static void syncDone(void* data, wl_proxy*, uint32_t) {
        *static_cast<bool*>(data) = true;
    }

    bool discover() {
        wl_argument newId{.o = nullptr};
        registry = marshal(reinterpret_cast<wl_proxy*>(display), 1,
            registryInterface, 1, 0, &newId);
        if (!registry)
            return false;
        setQueue(registry, queue);
        static void (*registryListener[])(void){
            reinterpret_cast<void (*)(void)>(global),
            reinterpret_cast<void (*)(void)>(globalRemoved),
        };
        if (addListener(registry, registryListener, this) != 0)
            return false;

        bool done{};
        auto* callback = marshal(reinterpret_cast<wl_proxy*>(display), 0,
            callbackInterface, 1, 0, &newId);
        if (!callback)
            return false;
        setQueue(callback, queue);
        static void (*callbackListener[])(void){
            reinterpret_cast<void (*)(void)>(syncDone),
        };
        bool success = addListener(callback, callbackListener, &done) == 0;
        const auto deadline = std::chrono::steady_clock::now() + discoveryTimeout;
        while (success && !done) {
            if (dispatchPending(display, queue) < 0) {
                success = false;
                break;
            }
            if (done)
                break;
            if (prepareRead(display, queue) != 0)
                continue;
            const int flush = displayFlush(display);
            const bool writeBlocked = flush < 0 && errno == EAGAIN;
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            pollfd fd{displayGetFd(display), static_cast<short>(POLLIN | (writeBlocked ? POLLOUT : 0)), 0};
            if (remaining <= 0 || (flush < 0 && !writeBlocked) ||
                    poll(&fd, 1, static_cast<int>(remaining)) <= 0 ||
                    (fd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
                cancelRead(display);
                success = false;
                break;
            }
            if (fd.revents & POLLIN)
                success = readEvents(display) >= 0;
            else
                cancelRead(display);
        }
        destroyProxy(callback);
        release(registry, false);
        return success && done && compositor && factory && displayFlush(display) >= 0;
    }

    std::optional<uint32_t> cardinal(xcb_connection_t* connection,
            const xcb_window_t root, const std::string_view name) {
        Reply<xcb_intern_atom_reply_t> atom(atomReply(connection,
            internAtom(connection, 1, static_cast<uint16_t>(name.size()), name.data()),
            nullptr), &std::free);
        if (!atom || atom->atom == XCB_ATOM_NONE)
            return std::nullopt;
        Reply<xcb_get_property_reply_t> value(propertyReply(connection,
            getProperty(connection, 0, root, atom->atom, XCB_ATOM_CARDINAL, 0, 1),
            nullptr), &std::free);
        if (!value || value->type != XCB_ATOM_CARDINAL || value->format != 32 ||
                value->value_len != 1 || value->bytes_after != 0)
            return std::nullopt;
        uint32_t result{};
        std::memcpy(&result, propertyValue(value.get()), sizeof(result));
        return result;
    }

    static void ignoreTiming(void*, wl_proxy*, uint32_t, uint32_t, uint32_t,
            uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
    static void ignoreRefresh(void*, wl_proxy*, uint32_t, uint32_t) {}
    static void retired(void* data, wl_proxy*) {
        static_cast<Content*>(data)->retired = true;
    }
    static void ignoreOutput(void*, wl_proxy*, void*) {}

    std::optional<VkResult> create(VkInstance instance,
            PFN_vkGetInstanceProcAddr next, xcb_connection_t* connection,
            const uint32_t window, const VkAllocationCallbacks* allocator,
            VkSurfaceKHR* output) {
        if (!ready || !connection || !window || !output)
            return std::nullopt;
        Reply<xcb_get_geometry_reply_t> geometry(geometryReply(connection,
            getGeometry(connection, window), nullptr), &std::free);
        if (!geometry)
            return std::nullopt;
        const auto server = cardinal(connection, geometry->root,
            "GAMESCOPE_XWAYLAND_SERVER_ID");
        const auto pid = cardinal(connection, geometry->root, "GAMESCOPE_PID");
        if (!server || !pid || *pid == 0 ||
                (peerPid > 0 && *pid != static_cast<uint32_t>(peerPid)))
            return std::nullopt;
        auto createWayland = reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>(
            next(instance, "vkCreateWaylandSurfaceKHR"));
        if (!createWayland)
            return std::nullopt;

        auto state = std::make_unique<Surface>();
        state->owner = this;
        state->server = *server;
        state->window = window;
        state->connection = connection;
        wl_argument newId{.o = nullptr};
        state->surface = marshal(compositor, 0, surfaceInterface, 1, 0, &newId);
        if (!state->surface)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        setQueue(state->surface, queue);
        static void (*surfaceListener[])(void){
            reinterpret_cast<void (*)(void)>(ignoreOutput),
            reinterpret_cast<void (*)(void)>(ignoreOutput),
        };
        if (addListener(state->surface, surfaceListener, nullptr) != 0) {
            release(*state);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        const VkWaylandSurfaceCreateInfoKHR info{
            .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
            .display = display,
            .surface = reinterpret_cast<wl_surface*>(state->surface),
        };
        const VkResult result = createWayland(instance, &info, allocator, output);
        if (result != VK_SUCCESS) {
            std::cerr << "MAKO Renderer: spatial scaling surface bridge: "
                      << "operation=create-wayland-surface; result=" << result << '\n';
            release(*state);
            return result;
        }
        try {
            surfaces.emplace(*output, std::move(state));
        } catch (...) {
            const auto destroy = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
                next(instance, "vkDestroySurfaceKHR"));
            destroy(instance, *output, allocator);
            if (state)
                release(*state);
            *output = VK_NULL_HANDLE;
            throw;
        }
        displayFlush(display);
        std::cerr << "MAKO Renderer: spatial scaling surface bridge: "
                  << "surface=" << *output << "; xwayland_server=" << *server
                  << "; window=" << window
                  << "; transport=wayland; gamescope_wsi=isolated"
                     "; application_surface=x11; extent_contract=window\n";
        return VK_SUCCESS;
    }
};

GamescopeScalingSurface::GamescopeScalingSurface() : impl(std::make_unique<Impl>()) {}
GamescopeScalingSurface::~GamescopeScalingSurface() = default;

bool GamescopeScalingSurface::connect() {
    if (!impl->resolve())
        return false;
    impl->display = impl->displayConnect(std::getenv("GAMESCOPE_WAYLAND_DISPLAY"));
    if (!impl->display)
        return false;
    ucred peer{};
    socklen_t peerSize = sizeof(peer);
    if (getsockopt(impl->displayGetFd(impl->display), SOL_SOCKET, SO_PEERCRED,
            &peer, &peerSize) == 0 && peerSize == sizeof(peer))
        impl->peerPid = peer.pid;
    impl->queue = impl->createQueue(impl->display);
    impl->ready = impl->queue && impl->discover();
    return impl->ready;
}

std::optional<VkResult> GamescopeScalingSurface::create(VkInstance instance,
        PFN_vkGetInstanceProcAddr next, const VkXcbSurfaceCreateInfoKHR& info,
        const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) {
    const std::lock_guard lock(impl->mutex);
    if (info.pNext || info.flags)
        return std::nullopt;
    return impl->create(instance, next, info.connection, info.window, allocator, surface);
}

std::optional<VkResult> GamescopeScalingSurface::create(VkInstance instance,
        PFN_vkGetInstanceProcAddr next, const VkXlibSurfaceCreateInfoKHR& info,
        const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface) {
    const std::lock_guard lock(impl->mutex);
    if (info.pNext || info.flags || !impl->ready)
        return std::nullopt;
    if (!impl->xlibLibrary) {
        impl->xlibLibrary = dlopen("libX11-xcb.so.1", RTLD_NOW | RTLD_LOCAL);
        if (impl->xlibLibrary)
            symbol(impl->xlibLibrary, impl->getXcbConnection, "XGetXCBConnection");
    }
    if (!impl->getXcbConnection)
        return std::nullopt;
    return impl->create(instance, next, impl->getXcbConnection(info.dpy),
        static_cast<uint32_t>(info.window), allocator, surface);
}

void GamescopeScalingSurface::destroy(const VkSurfaceKHR surface) {
    const std::lock_guard lock(impl->mutex);
    const auto found = impl->surfaces.find(surface);
    if (found == impl->surfaces.end())
        return;
    impl->release(*found->second);
    impl->surfaces.erase(found);
    impl->displayFlush(impl->display);
}

bool GamescopeScalingSurface::owns(const VkSurfaceKHR surface) const {
    const std::lock_guard lock(impl->mutex);
    return impl->surfaces.contains(surface);
}

bool GamescopeScalingSurface::createSwapchain(
        const VkSurfaceKHR surface, const VkSwapchainKHR swapchain,
        const VkSwapchainCreateInfoKHR& info, const uint32_t imageCount,
        const std::string_view engineName,
        const std::optional<VkPresentModeKHR> compositorPresentMode) {
    const std::lock_guard lock(impl->mutex);
    const auto found = impl->surfaces.find(surface);
    if (found == impl->surfaces.end())
        return true;
    auto& state = *found->second;
    if (state.contents.contains(swapchain))
        return true;

    auto content = std::make_unique<Impl::Content>();
    content->owner = impl.get();
    content->compositorPresentMode = compositorPresentMode;
    wl_argument createArgs[2]{{.o = state.surface}, {.o = nullptr}};
    content->proxy = impl->marshal(
        impl->factory, 1, &impl->contentInterface, 1, 0, createArgs
    );
    static void (*contentListener[])(void){
        reinterpret_cast<void (*)(void)>(Impl::ignoreTiming),
        reinterpret_cast<void (*)(void)>(Impl::ignoreRefresh),
        reinterpret_cast<void (*)(void)>(Impl::retired),
    };
    if (!content->proxy || impl->addListener(
            content->proxy, contentListener, content.get()) != 0) {
        impl->release(content->proxy);
        return false;
    }
    impl->setQueue(content->proxy, impl->queue);
    const std::string engine(engineName);
    wl_argument feedback[7]{
        {.u = imageCount},
        {.u = static_cast<uint32_t>(info.imageFormat)},
        {.u = static_cast<uint32_t>(info.imageColorSpace)},
        {.u = static_cast<uint32_t>(info.compositeAlpha)},
        {.u = static_cast<uint32_t>(info.preTransform)},
        {.u = static_cast<uint32_t>(info.clipped)},
        {.s = engine.c_str()},
    };
    impl->marshal(content->proxy, 2, nullptr, 1, 0, feedback);
    state.contents.emplace(swapchain, std::move(content));
    if (impl->displayFlush(impl->display) < 0 && errno != EAGAIN) {
        const auto inserted = state.contents.find(swapchain);
        impl->release(inserted->second->proxy);
        state.contents.erase(inserted);
        return false;
    }
    return true;
}

void GamescopeScalingSurface::destroySwapchain(
        const VkSurfaceKHR surface, const VkSwapchainKHR swapchain) {
    const std::lock_guard lock(impl->mutex);
    const auto found = impl->surfaces.find(surface);
    if (found == impl->surfaces.end())
        return;
    const auto content = found->second->contents.find(swapchain);
    if (content == found->second->contents.end())
        return;
    impl->release(content->second->proxy);
    found->second->contents.erase(content);
    impl->displayFlush(impl->display);
}

std::optional<VkResult> GamescopeScalingSurface::applicationCapabilities(
        const VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR& capabilities) const {
    const std::lock_guard lock(impl->mutex);
    const auto found = impl->surfaces.find(surface);
    if (found == impl->surfaces.end())
        return std::nullopt;
    const auto& state = *found->second;
    Reply<xcb_get_geometry_reply_t> geometry(impl->geometryReply(state.connection,
        impl->getGeometry(state.connection, state.window), nullptr), &std::free);
    if (!geometry)
        return VK_ERROR_SURFACE_LOST_KHR;
    const VkExtent2D extent{geometry->width, geometry->height};
    if (extent.width < capabilities.minImageExtent.width ||
            extent.height < capabilities.minImageExtent.height ||
            extent.width > capabilities.maxImageExtent.width ||
            extent.height > capabilities.maxImageExtent.height)
        return VK_ERROR_SURFACE_LOST_KHR;
    // Both Xlib and XCB expose a concrete window extent. Leaking Wayland's
    // UINT32_MAX sentinel can break startup before an application has even
    // created a swapchain. Query the live geometry rather than the creation
    // size so recreation follows resizes, without doing X11 work at present.
    capabilities.currentExtent = extent;
    capabilities.minImageExtent = extent;
    capabilities.maxImageExtent = extent;
    return VK_SUCCESS;
}

bool GamescopeScalingSurface::preparePresent(
        const VkSurfaceKHR surface, const VkSwapchainKHR swapchain) {
    const std::lock_guard lock(impl->mutex);
    const auto found = impl->surfaces.find(surface);
    if (found == impl->surfaces.end())
        return true;
    // Mesa's WSI dispatches the shared socket for its own event queue. Drain
    // only already-read association events here: no socket poll, roundtrip,
    // timing history, allocation, or additional worker on the present path.
    if (impl->dispatchPending(impl->display, impl->queue) < 0 ||
            impl->displayGetError(impl->display) != 0)
        return false;
    auto& state = *found->second;
    const auto content = state.contents.find(swapchain);
    if (content == state.contents.end() || content->second->retired)
        return false;
    wl_argument args[2]{{.u = state.server}, {.u = state.window}};
    impl->marshal(content->second->proxy, 1, nullptr, 1, 0, args);
    if (content->second->compositorPresentMode) {
        wl_argument mode{
            .u = static_cast<uint32_t>(
                *content->second->compositorPresentMode
            ),
        };
        impl->marshal(content->second->proxy, 3, nullptr, 1, 0, &mode);
    }
    if (impl->displayFlush(impl->display) < 0 && errno != EAGAIN)
        return false;
    return true;
}
