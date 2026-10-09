/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "gamescope_scaling_surface.hpp"
#include "spatial_scaling_policy.hpp"
#include "presentation_policy.hpp"
#include "present_diagnostics.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
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

    constexpr std::array<VkSurfaceFormatKHR, 3> hdrFormats{{
        {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT},
        {VK_FORMAT_A2R10G10B10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_ST2084_EXT},
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT},
    }};

    std::optional<std::vector<VkSurfaceFormatKHR>> enumerateFormats(
            VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
            PFN_vkGetPhysicalDeviceSurfaceFormatsKHR lowerFormats) {
        if (!lowerFormats)
            return std::nullopt;
        for (uint32_t attempt = 0; attempt < 3; ++attempt) {
            uint32_t count{};
            if (lowerFormats(physicalDevice, surface, &count, nullptr) != VK_SUCCESS ||
                    count == 0 || count > 256)
                return std::nullopt;
            std::vector<VkSurfaceFormatKHR> formats(count);
            const auto result = lowerFormats(physicalDevice, surface, &count, formats.data());
            if (result == VK_SUCCESS && count <= formats.size()) {
                formats.resize(count);
                return formats;
            }
            if (result != VK_INCOMPLETE)
                return std::nullopt;
        }
        return std::nullopt;
    }

    bool supportsHdrStorage(const std::vector<VkSurfaceFormatKHR>& formats,
            const VkFormat format) {
        return std::ranges::any_of(formats, [format](const auto& item) {
            return (item.format == format || item.format == VK_FORMAT_UNDEFINED) &&
                item.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        });
    }
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
    bool allowHdr{};
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
    // swapchain feedback are used. Ordered combined delivery annotates every
    // commit with FIFO and its own timestamp. HDR uses only colour feedback
    // and metadata; the limiter interface remains unused.
    // All v1 events are declared so unsolicited feedback is consumed without
    // retaining history.
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
        bool hdr{};
        std::optional<std::array<uint32_t, 12>> hdrMetadata;
        bool failureLogged{};
        std::optional<VkExtent2D> applicationExtent;
        VkResult acquisitionResult{VK_SUCCESS};
        uint32_t refreshHz{};
        uint32_t presentId{};
        OrderedPresentTimeline presentTimeline;
        std::optional<VkPresentModeKHR> compositorPresentMode;
        std::unique_ptr<present_diagnostics::BridgePresentTiming> timing;
        uint64_t diagnosticId{};
        uint64_t refreshCycleNs{};
        ~Content() {
            if (owner)
                owner->release(proxy);
        }
    };
    struct Surface {
        Impl* owner{};
        wl_proxy* surface{};
        VkInstance instance{VK_NULL_HANDLE};
        PFN_vkGetInstanceProcAddr next{};
        Display* xlibDisplay{};
        uint32_t server{};
        uint32_t window{};
        bool hdrOutput{};
        bool formatPolicyLogged{};
        bool failureLogged{};
        std::optional<VkExtent2D> queriedExtent;
        uint64_t extentQueryGeneration{};
        xcb_connection_t* connection{};
        std::unordered_map<VkSwapchainKHR, std::unique_ptr<Content>> contents;
        ~Surface() {
            if (owner)
                owner->release(*this);
        }
    };
    std::unordered_map<VkSurfaceKHR, std::unique_ptr<Surface>> surfaces;

    VkResult failure(Surface& state, Content* content, VkSurfaceKHR surface,
            VkSwapchainKHR swapchain, VkResult result, const char* reason,
            const char* boundary, int systemError = 0) const {
        auto& logged = content ? content->failureLogged : state.failureLogged;
        if (!logged && present_diagnostics::enabled()) {
            logged = true;
            std::cerr << "MAKO Renderer: spatial scaling surface bridge: "
                      << "operation=bridge-failure; pid=" << getpid()
                      << "; bridge=" << (content ? content->diagnosticId : 0)
                      << "; surface=" << surface << "; swapchain=" << swapchain
                      << "; xwayland_server=" << state.server << "; window=" << state.window
                      << "; boundary=" << boundary << "; reason=" << reason
                      << "; result=" << result << "; errno=" << systemError
                      << "; display_error=" << displayGetError(display) << '\n';
        }
        return result;
    }

    VkResult dispatch(Surface& state, Content* content, VkSurfaceKHR surface,
            VkSwapchainKHR swapchain, const char* boundary) {
        const auto dispatched = dispatchPending(display, queue);
        const auto systemError = dispatched < 0 ? errno : 0;
        if (dispatched < 0 || displayGetError(display) != 0)
            return failure(state, content, surface, swapchain, VK_ERROR_SURFACE_LOST_KHR,
                "connection-lost", boundary, systemError);
        if (content && content->retired)
            return failure(state, content, surface, swapchain, VK_ERROR_OUT_OF_DATE_KHR,
                "association-retired", boundary);
        return VK_SUCCESS;
    }

    std::optional<VkExtent2D> windowExtent(const Surface& state) const {
        Reply<xcb_get_geometry_reply_t> geometry(geometryReply(state.connection,
            getGeometry(state.connection, state.window), nullptr), &std::free);
        if (!geometry)
            return std::nullopt;
        return VkExtent2D{geometry->width, geometry->height};
    }

    void observeWindowExtent(Surface& state, const VkSurfaceKHR surface,
            const VkExtent2D extent, const char* boundary) {
        for (auto& [swapchain, content] : state.contents) {
            if (!content->applicationExtent || content->acquisitionResult != VK_SUCCESS ||
                    sameExtent(*content->applicationExtent, extent))
                continue;
            // A private variable-extent Wayland swapchain never learns that
            // its associated X11 window resized. Preserve the native WSI
            // invalidation until the application replaces this swapchain.
            content->acquisitionResult = VK_ERROR_OUT_OF_DATE_KHR;
            if (present_diagnostics::enabled()) {
                std::cerr << "MAKO Renderer: spatial scaling window extent: "
                          << "operation=swapchain-out-of-date; pid=" << getpid()
                          << "; surface=" << surface
                          << "; swapchain=" << swapchain
                          << "; window=" << state.window
                          << "; application=" << content->applicationExtent->width
                          << 'x' << content->applicationExtent->height
                          << "; current=" << extent.width << 'x' << extent.height
                          << "; boundary=" << boundary
                          << "; action=requery-before-application-acquire\n";
            }
        }
    }

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

    static void pastTiming(void* data, wl_proxy*, uint32_t id,
            uint32_t desiredHi, uint32_t desiredLo, uint32_t actualHi, uint32_t actualLo,
            uint32_t, uint32_t, uint32_t, uint32_t) {
        auto& content = *static_cast<Content*>(data);
        if (content.timing)
            content.timing->feedback(id, (uint64_t{desiredHi} << 32) | desiredLo,
                (uint64_t{actualHi} << 32) | actualLo);
    }
    static void refreshCycle(void* data, wl_proxy*, uint32_t hi, uint32_t lo) {
        auto& content = *static_cast<Content*>(data);
        if (content.timing)
            content.refreshCycleNs = (uint64_t{hi} << 32) | lo;
    }
    static void retired(void* data, wl_proxy*) {
        static_cast<Content*>(data)->retired = true;
    }
    static void ignoreOutput(void*, wl_proxy*, void*) {}

    std::optional<VkResult> create(VkInstance instance,
            PFN_vkGetInstanceProcAddr next, xcb_connection_t* connection,
            Display* xlibDisplay, const uint32_t window,
            const VkAllocationCallbacks* allocator,
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
        state->instance = instance;
        state->next = next;
        state->xlibDisplay = xlibDisplay;
        state->server = *server;
        state->window = window;
        // Gamescope publishes this capability for both internal OLED panels
        // and external HDR displays, even when the current content is SDR.
        state->hdrOutput = allowHdr && cardinal(connection, geometry->root,
            "GAMESCOPE_HDR_OUTPUT_FEEDBACK").value_or(0) == 1;
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

GamescopeScalingSurface::GamescopeScalingSurface(const bool allowHdr) : impl(std::make_unique<Impl>()) {
    impl->allowHdr = allowHdr;
}
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
            &peer, &peerSize) != 0 || peerSize != sizeof(peer) || peer.pid < 0)
        return false;
    // A compositor outside Flatpak's PID namespace legitimately has PID zero.
    // Keep the existing Gamescope protocol/root-property admission in that
    // case; a failed credential lookup is not evidence of namespace isolation.
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
    return impl->create(instance, next, info.connection, nullptr, info.window,
        allocator, surface);
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
    return impl->create(instance, next, impl->getXcbConnection(info.dpy), info.dpy,
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

bool GamescopeScalingSurface::hdrEnabled() const {
    return impl->allowHdr;
}

bool GamescopeScalingSurface::createSwapchain(
        const VkSurfaceKHR surface, const VkSwapchainKHR swapchain,
        const VkSwapchainCreateInfoKHR& info, const VkExtent2D applicationExtent,
        const uint32_t imageCount,
        const std::string_view engineName,
        const std::optional<VkPresentModeKHR> compositorPresentMode,
        const uint32_t refreshHz) {
    const std::lock_guard lock(impl->mutex);
    const auto found = impl->surfaces.find(surface);
    if (found == impl->surfaces.end())
        return true;
    auto& state = *found->second;
    if (state.contents.contains(swapchain))
        return true;

    // Gamescope can retain the X11 window size while an application explicitly
    // chooses another rendering extent. Invalidate only a source that matched
    // an observed window extent, rather than treating every override as stale.
    const auto extent = impl->windowExtent(state);
    if (!extent)
        return false;
    auto content = std::make_unique<Impl::Content>();
    content->owner = impl.get();
    content->hdr = state.hdrOutput &&
        (info.imageColorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT ||
         info.imageColorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT);
    // Explicit swapchain-maintenance scaling can legally use an image size
    // different from the window. Leave that lower-WSI contract in charge.
    bool exactWindowExtent = true;
    for (auto* node = static_cast<const VkBaseInStructure*>(info.pNext);
            node; node = node->pNext) {
#if defined(VK_KHR_swapchain_maintenance1)
        if (node->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_SCALING_CREATE_INFO_KHR)
            exactWindowExtent = reinterpret_cast<const VkSwapchainPresentScalingCreateInfoKHR*>(node)
                ->scalingBehavior == 0;
#elif defined(VK_EXT_swapchain_maintenance1)
        if (node->sType == VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_SCALING_CREATE_INFO_EXT)
            exactWindowExtent = reinterpret_cast<const VkSwapchainPresentScalingCreateInfoEXT*>(node)
                ->scalingBehavior == 0;
#endif
    }
    const bool explicitPresentationScaling = !exactWindowExtent;
    if (exactWindowExtent && !sameExtent(applicationExtent, *extent) &&
            (!state.queriedExtent || !sameExtent(applicationExtent, *state.queriedExtent)))
        exactWindowExtent = false;
    if (exactWindowExtent)
        content->applicationExtent = applicationExtent;
    content->compositorPresentMode = compositorPresentMode;
    content->refreshHz = refreshHz;
    if (present_diagnostics::enabled()) {
        content->timing = std::make_unique<present_diagnostics::BridgePresentTiming>();
        content->diagnosticId = present_diagnostics::allocateContextId();
    }
    wl_argument createArgs[2]{{.o = state.surface}, {.o = nullptr}};
    content->proxy = impl->marshal(
        impl->factory, 1, &impl->contentInterface, 1, 0, createArgs
    );
    static void (*contentListener[])(void){
        reinterpret_cast<void (*)(void)>(Impl::pastTiming),
        reinterpret_cast<void (*)(void)>(Impl::refreshCycle),
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
    // Creation keeps its specified return codes; acquisition requests
    // recreation before touching the application's semaphore or fence.
    impl->observeWindowExtent(state, surface, *extent, "swapchain-create");
    if (present_diagnostics::enabled()) {
        std::cerr << "MAKO Renderer: spatial scaling window extent: "
                  << "operation=swapchain-create; pid=" << getpid()
                  << "; surface=" << surface
                  << "; swapchain=" << swapchain
                  << "; window=" << state.window
                  << "; queried=" << state.queriedExtent.value_or(VkExtent2D{}).width
                  << 'x' << state.queriedExtent.value_or(VkExtent2D{}).height
                  << "; query_generation=" << state.extentQueryGeneration
                  << "; application=" << applicationExtent.width << 'x' << applicationExtent.height
                  << "; current=" << extent->width << 'x' << extent->height
                  << "; presentation=" << info.imageExtent.width << 'x' << info.imageExtent.height
                  << "; exact_window_extent=" << exactWindowExtent
                  << "; extent_contract=" << (explicitPresentationScaling ? "presentation-scaling"
                      : exactWindowExtent ? "window" : "application-override") << '\n';
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

VkResult GamescopeScalingSurface::acquisitionResult(
        const VkSurfaceKHR surface, const VkSwapchainKHR swapchain) const {
    const std::lock_guard lock(impl->mutex);
    const auto found = impl->surfaces.find(surface);
    if (found == impl->surfaces.end())
        return VK_SUCCESS;
    const auto content = found->second->contents.find(swapchain);
    if (content == found->second->contents.end())
        return VK_SUCCESS;
    const auto result = impl->dispatch(*found->second, content->second.get(),
        surface, swapchain, "acquire");
    return result == VK_SUCCESS ? content->second->acquisitionResult : result;
}

std::optional<VkResult> GamescopeScalingSurface::applicationCapabilities(
        const VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR& capabilities) const {
    const std::lock_guard lock(impl->mutex);
    const auto found = impl->surfaces.find(surface);
    if (found == impl->surfaces.end())
        return std::nullopt;
    auto& state = *found->second;
    const auto extent = impl->windowExtent(state);
    if (!extent)
        return VK_ERROR_SURFACE_LOST_KHR;
    if (extent->width < capabilities.minImageExtent.width ||
            extent->height < capabilities.minImageExtent.height ||
            extent->width > capabilities.maxImageExtent.width ||
            extent->height > capabilities.maxImageExtent.height)
        return VK_ERROR_SURFACE_LOST_KHR;
    ++state.extentQueryGeneration;
    if (present_diagnostics::enabled() &&
            (!state.queriedExtent || !sameExtent(*state.queriedExtent, *extent))) {
        std::cerr << "MAKO Renderer: spatial scaling window extent: "
                  << "operation=capability-query; pid=" << getpid()
                  << "; surface=" << surface
                  << "; window=" << state.window
                  << "; previous=" << state.queriedExtent.value_or(VkExtent2D{}).width
                  << 'x' << state.queriedExtent.value_or(VkExtent2D{}).height
                  << "; current=" << extent->width << 'x' << extent->height
                  << "; query_generation=" << state.extentQueryGeneration << '\n';
    }
    state.queriedExtent = *extent;
    impl->observeWindowExtent(state, surface, *extent, "capability-query");
    // Both Xlib and XCB expose a concrete window extent. Leaking Wayland's
    // UINT32_MAX sentinel can break startup before an application has even
    // created a swapchain. Query the live geometry rather than the creation
    // size so recreation follows resizes, without doing X11 work at present.
    capabilities.currentExtent = *extent;
    capabilities.minImageExtent = *extent;
    capabilities.maxImageExtent = *extent;
    return VK_SUCCESS;
}

std::optional<std::vector<VkSurfaceFormatKHR>>
GamescopeScalingSurface::applicationFormats(
        const VkPhysicalDevice physicalDevice, const VkSurfaceKHR surface,
        const PFN_vkGetPhysicalDeviceSurfaceFormatsKHR lowerFormats) const {
    const std::lock_guard lock(impl->mutex);
    const auto found = impl->surfaces.find(surface);
    if (found == impl->surfaces.end() || !lowerFormats)
        return std::nullopt;
    auto& state = *found->second;
    const auto waylandFormats = enumerateFormats(physicalDevice, surface, lowerFormats);
    if (!waylandFormats)
        return std::nullopt;

    const auto destroy = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
        state.next(state.instance, "vkDestroySurfaceKHR"));
    if (!destroy)
        return std::nullopt;
    VkSurfaceKHR nativeSurface{VK_NULL_HANDLE};
    VkResult result = VK_ERROR_EXTENSION_NOT_PRESENT;
    if (state.xlibDisplay) {
        const auto create = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(
            state.next(state.instance, "vkCreateXlibSurfaceKHR"));
        if (create) {
            const VkXlibSurfaceCreateInfoKHR info{
                .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
                .dpy = state.xlibDisplay,
                .window = state.window,
            };
            result = create(state.instance, &info, nullptr, &nativeSurface);
        }
    } else {
        const auto create = reinterpret_cast<PFN_vkCreateXcbSurfaceKHR>(
            state.next(state.instance, "vkCreateXcbSurfaceKHR"));
        if (create) {
            const VkXcbSurfaceCreateInfoKHR info{
                .sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR,
                .connection = state.connection,
                .window = state.window,
            };
            result = create(state.instance, &info, nullptr, &nativeSurface);
        }
    }
    if (result != VK_SUCCESS || nativeSurface == VK_NULL_HANDLE)
        return std::nullopt;
    struct NativeSurfaceGuard {
        VkInstance instance;
        VkSurfaceKHR surface;
        PFN_vkDestroySurfaceKHR destroy;
        ~NativeSurfaceGuard() { destroy(instance, surface, nullptr); }
    } nativeGuard{state.instance, nativeSurface, destroy};
    const auto nativeFormats = enumerateFormats(physicalDevice, nativeSurface, lowerFormats);
    if (!nativeFormats)
        return std::nullopt;

    const auto anyFormat = [](const std::vector<VkSurfaceFormatKHR>& formats) {
        return formats.size() == 1 &&
            formats.front().format == VK_FORMAT_UNDEFINED;
    };
    std::vector<VkSurfaceFormatKHR> exposed;
    if (anyFormat(*nativeFormats)) {
        exposed = *waylandFormats;
    } else if (anyFormat(*waylandFormats)) {
        exposed = *nativeFormats;
    } else {
        for (const auto& native : *nativeFormats) {
            if (std::ranges::any_of(*waylandFormats,
                    [&](const VkSurfaceFormatKHR& wayland) {
                        return native.format == wayland.format &&
                            native.colorSpace == wayland.colorSpace;
                    }) && std::ranges::none_of(exposed,
                    [&](const VkSurfaceFormatKHR& selected) {
                        return native.format == selected.format &&
                            native.colorSpace == selected.colorSpace;
                    })) {
                exposed.push_back(native);
            }
        }
    }
    const bool waylandFallback = exposed.empty();
    if (waylandFallback)
        exposed = *waylandFormats;
    // The bridge owns colour interpretation. Only advertise its supported
    // opt-in HDR pairs, even if a newer lower WSI reports other colour spaces.
    std::erase_if(exposed, [](const auto& format) {
        return format.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    if (state.hdrOutput) {
        for (const auto& hdr : hdrFormats) {
            if (supportsHdrStorage(*waylandFormats, hdr.format) &&
                    std::ranges::none_of(exposed, [&](const auto& item) {
                        return item.format == hdr.format && item.colorSpace == hdr.colorSpace;
                    }))
                exposed.push_back(hdr);
        }
    }
    if (!state.formatPolicyLogged) {
        state.formatPolicyLogged = true;
        std::cerr << "MAKO Renderer: spatial scaling surface formats: "
                  << "native=" << nativeFormats->size()
                  << "; wayland=" << waylandFormats->size()
                  << "; shared=" << exposed.size()
                  << "; action=" << (waylandFallback ? "wayland-fallback" : "intersection")
                  << '\n';
    }
    return exposed;
}

VkResult GamescopeScalingSurface::prepareSwapchain(
        const VkPhysicalDevice physicalDevice, VkSwapchainCreateInfoKHR& info,
        const PFN_vkGetPhysicalDeviceSurfaceFormatsKHR lowerFormats) const {
    const std::lock_guard lock(impl->mutex);
    const auto found = impl->surfaces.find(info.surface);
    if (found == impl->surfaces.end() ||
            info.imageColorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        return VK_SUCCESS;
    const bool hdr = std::ranges::any_of(hdrFormats, [&](const auto& format) {
        return format.format == info.imageFormat && format.colorSpace == info.imageColorSpace;
    });
    // Colour interpretation belongs to Gamescope's swapchain feedback. The
    // lower Wayland driver transports identical pixels in its supported format.
    if (!hdr && (info.imageColorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT ||
            info.imageColorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT))
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if (hdr) {
        if (!found->second->hdrOutput)
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        const auto formats = enumerateFormats(physicalDevice, info.surface, lowerFormats);
        if (!formats || !supportsHdrStorage(*formats, info.imageFormat))
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
        info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    }
    return VK_SUCCESS;
}

bool GamescopeScalingSurface::setHdrMetadata(const VkSurfaceKHR surface,
        const VkSwapchainKHR swapchain, const VkHdrMetadataEXT& metadata) {
    const std::lock_guard lock(impl->mutex);
    const auto found = impl->surfaces.find(surface);
    if (found == impl->surfaces.end())
        return false;
    const auto content = found->second->contents.find(swapchain);
    if (content == found->second->contents.end())
        return false;
    if (!content->second->hdr || content->second->retired)
        return true;
    // CTA-861 units from the pinned Gamescope protocol. Clamp malformed input
    // before integer conversion; no allocation, polling or roundtrip is needed.
    const auto quantize = [](const float value, const double scale) -> uint32_t {
        if (!std::isfinite(value) || value <= 0.0F)
            return 0;
        return static_cast<uint32_t>(std::round(std::min(65535.0, value * scale)));
    };
    const std::array values{
        quantize(metadata.displayPrimaryRed.x, 50000.0),
        quantize(metadata.displayPrimaryRed.y, 50000.0),
        quantize(metadata.displayPrimaryGreen.x, 50000.0),
        quantize(metadata.displayPrimaryGreen.y, 50000.0),
        quantize(metadata.displayPrimaryBlue.x, 50000.0),
        quantize(metadata.displayPrimaryBlue.y, 50000.0),
        quantize(metadata.whitePoint.x, 50000.0),
        quantize(metadata.whitePoint.y, 50000.0),
        quantize(metadata.maxLuminance, 1.0),
        quantize(metadata.minLuminance, 10000.0),
        quantize(metadata.maxContentLightLevel, 1.0),
        quantize(metadata.maxFrameAverageLightLevel, 1.0),
    };
    // Some games submit static metadata each frame. Compare the wire values
    // so insignificant float jitter adds neither requests nor socket flushes.
    if (content->second->hdrMetadata == values)
        return true;
    std::array<wl_argument, 12> args{};
    for (size_t index = 0; index < values.size(); ++index)
        args[index].u = values[index];
    impl->marshal(content->second->proxy, 4, nullptr, 1, 0, args.data());
    content->second->hdrMetadata = values;
    impl->displayFlush(impl->display);
    return true;
}

VkResult GamescopeScalingSurface::preparePresent(
        const VkSurfaceKHR surface, const VkSwapchainKHR swapchain,
        const double outputFps, const uint32_t refreshHz,
        const size_t outputBatchSize, const bool generationEnabled) {
    std::unique_lock lock(impl->mutex);
    auto found = impl->surfaces.find(surface);
    if (found == impl->surfaces.end())
        return VK_SUCCESS;
    // Mesa's WSI dispatches the shared socket for its own event queue. Drain
    // only already-read association events here: no socket poll, roundtrip,
    // allocation or additional worker on the present path. Opt-in diagnostics
    // aggregate timing feedback in bounded storage allocated at creation.
    auto content = found->second->contents.find(swapchain);
    if (content == found->second->contents.end())
        return impl->failure(*found->second, nullptr, surface, swapchain,
            VK_ERROR_SURFACE_LOST_KHR, "missing-swapchain", "present");
    const auto dispatched = impl->dispatch(*found->second, content->second.get(),
        surface, swapchain, "present");
    if (dispatched != VK_SUCCESS)
        return dispatched;
    std::optional<OrderedPresentTimeline::Slot> slot;
    if (content->second->compositorPresentMode == VK_PRESENT_MODE_FIFO_KHR &&
            OrderedPresentTimeline::validRate(content->second->refreshHz)) {
        if (OrderedPresentTimeline::validRate(refreshHz))
            content->second->refreshHz = refreshHz;
        slot = content->second->presentTimeline.schedule(
            OrderedPresentTimeline::Clock::now(),
            OrderedPresentTimeline::validRate(outputFps)
                ? outputFps : content->second->refreshHz,
            content->second->refreshHz, outputBatchSize, generationEnabled);
        if (!slot)
            return impl->failure(*found->second, content->second.get(), surface, swapchain,
                VK_ERROR_SURFACE_LOST_KHR, "invalid-timeline", "present");
        // No GPU-idle wait and no adapter lock held during backpressure.
        lock.unlock();
        std::this_thread::sleep_until(slot->submitAt);
        lock.lock();
        found = impl->surfaces.find(surface);
        if (found == impl->surfaces.end())
            return VK_ERROR_SURFACE_LOST_KHR;
        content = found->second->contents.find(swapchain);
        if (content == found->second->contents.end())
            return impl->failure(*found->second, nullptr, surface, swapchain,
                VK_ERROR_SURFACE_LOST_KHR, "missing-swapchain", "present-after-wait");
        const auto afterWait = impl->dispatch(*found->second, content->second.get(),
            surface, swapchain, "present-after-wait");
        if (afterWait != VK_SUCCESS)
            return afterWait;
    }
    wl_argument args[2]{{.u = found->second->server}, {.u = found->second->window}};
    impl->marshal(content->second->proxy, 1, nullptr, 1, 0, args);
    if (content->second->compositorPresentMode) {
        wl_argument mode{
            .u = static_cast<uint32_t>(
                *content->second->compositorPresentMode
            ),
        };
        impl->marshal(content->second->proxy, 3, nullptr, 1, 0, &mode);
    }
    if (slot) {
        const auto ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                slot->presentAt.time_since_epoch()).count());
        wl_argument timing[3]{
            {.u = ++content->second->presentId},
            {.u = static_cast<uint32_t>(ns >> 32)},
            {.u = static_cast<uint32_t>(ns)},
        };
        impl->marshal(content->second->proxy, 5, nullptr, 1, 0, timing);
        if (content->second->timing) {
            const auto nanos = [](const auto time) {
                return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    time.time_since_epoch()).count());
            };
            const auto now = nanos(OrderedPresentTimeline::Clock::now());
            auto& diagnostics = *content->second->timing;
            diagnostics.request(content->second->presentId, ns, now, nanos(slot->submitAt));
            if (const auto window = diagnostics.take(now))
                present_diagnostics::logBridgeTiming(content->second->diagnosticId,
                    swapchain, *window, diagnostics.outstanding(), content->second->refreshCycleNs);
        }
    }
    if (impl->displayFlush(impl->display) < 0 && errno != EAGAIN)
        return impl->failure(*found->second, content->second.get(), surface, swapchain,
            VK_ERROR_SURFACE_LOST_KHR, "flush-failed", "present", errno);
    return VK_SUCCESS;
}
