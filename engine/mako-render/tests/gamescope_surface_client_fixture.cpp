/* SPDX-License-Identifier: GPL-3.0-or-later */

// A local protocol peer fixture. The production adapter loads these ordinary
// runtime symbols through its real dlopen path, with no test branch in MAKO.
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <sys/socket.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <xcb/xcb.h>

struct wl_display;
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
union wl_argument { int32_t i; uint32_t u; const char* s; void* o; };
struct wl_proxy {
    std::string_view kind;
    void (**listener)(void){};
    void* data{};
    bool pending{};
    wl_proxy* surface{};
};

namespace {
    std::vector<wl_proxy*> objects;
    int mode{};
    int associations{};
    int feedbacks{};
    uint32_t hdrOutput{};
    bool externalOutput{};
    int hdrQueries{};
    uint32_t feedbackColorSpace{};
    int hdrMetadataCalls{};
    uint32_t hdrMetadata[12]{};
    int presentModes{};
    int presentTimes{};
    uint32_t presentId{};
    wl_proxy* lastTimedProxy{};
    uint64_t presentTime{};
    uint32_t presentMode{};
    uint32_t feedbackImageCount{};
    std::string feedbackEngine;
    int queues{};
    int reads{};
    int geometryQueries{};
    uint16_t windowWidth{1920};
    uint16_t windowHeight{1080};
    int sockets[2]{-1, -1};
    uint32_t associatedServer{};
    uint32_t associatedWindow{};
    struct Override { wl_proxy* surface{}; wl_proxy* content{}; };
    std::unordered_map<uint64_t, Override> overrides;
    bool ownershipModel{};
    bool delayedEvents{};

    uint64_t windowKey(uint32_t server, uint32_t window) {
        return (uint64_t{server} << 32) | window;
    }

    void associate(wl_proxy* proxy, uint32_t server, uint32_t window) {
        auto& current = overrides[windowKey(server, window)];
        if (current.content == proxy)
            return;
        if (current.content)
            current.content->pending = true;
        current = {proxy->surface, proxy};
    }

    wl_proxy* make(std::string_view kind) {
        auto* proxy = new wl_proxy{kind};
        objects.push_back(proxy);
        return proxy;
    }
    void drop(wl_proxy* proxy) {
        // Gamescope clears the override attached to the resource's wl_surface,
        // including when an older protocol resource shares a replacement's
        // surface. A later present must reassert the surviving association.
        std::erase_if(overrides, [proxy](const auto& entry) {
            return entry.second.surface == proxy ||
                (proxy->surface && entry.second.surface == proxy->surface);
        });
        if (lastTimedProxy == proxy)
            lastTimedProxy = nullptr;
        std::erase(objects, proxy);
        delete proxy;
    }
}

extern "C" {
    // Keep peer identity deterministic in PID-isolated test sandboxes too.
    int getsockopt(int, int level, int option, void* value, socklen_t* size) noexcept {
        if (mode == 16 || level != SOL_SOCKET || option != SO_PEERCRED || *size < sizeof(ucred))
            return -1;
        const ucred peer{mode == 18 ? 0 : getpid(), getuid(), getgid()};
        std::memcpy(value, &peer, sizeof(peer));
        *size = mode == 17 ? sizeof(peer) - 1 : sizeof(peer);
        return 0;
    }
    extern const wl_interface wl_registry_interface{"wl_registry", 1, 0, nullptr, 0, nullptr};
    extern const wl_interface wl_compositor_interface{"wl_compositor", 1, 0, nullptr, 0, nullptr};
    extern const wl_interface wl_surface_interface{"wl_surface", 1, 0, nullptr, 0, nullptr};
    extern const wl_interface wl_callback_interface{"wl_callback", 1, 0, nullptr, 0, nullptr};

    void mako_test_surface_mode(int value) { mode = value; }
    void mako_test_surface_ownership(bool value) { ownershipModel = value; }
    void mako_test_surface_delay_events(bool value) { delayedEvents = value; }
    int mako_test_surface_overrides() { return static_cast<int>(overrides.size()); }
    void mako_test_surface_competing_override(uint32_t server, uint32_t window) {
        const auto found = overrides.find(windowKey(server, window));
        if (found != overrides.end()) {
            if (found->second.content)
                found->second.content->pending = true;
            overrides.erase(found);
        }
    }
    int mako_test_surface_objects() { return static_cast<int>(objects.size()) + queues; }
    int mako_test_surface_associations() { return associations; }
    int mako_test_surface_feedbacks() { return feedbacks; }
    void mako_test_surface_hdr_output(uint32_t value) { hdrOutput = value; }
    void mako_test_surface_external_output(bool value) { externalOutput = value; }
    int mako_test_surface_hdr_queries() { return hdrQueries; }
    uint32_t mako_test_surface_feedback_color() { return feedbackColorSpace; }
    int mako_test_surface_hdr_metadata_calls() { return hdrMetadataCalls; }
    uint32_t mako_test_surface_hdr_metadata(unsigned index) { return hdrMetadata[index]; }

    int mako_test_surface_present_times() { return presentTimes; }
    uint32_t mako_test_surface_present_id() { return presentId; }
    uint64_t mako_test_surface_present_time() { return presentTime; }
    void mako_test_surface_timing() {
        auto* proxy = lastTimedProxy;
        if (!proxy || !proxy->listener)
            std::abort();
        using Timing = void (*)(void*, wl_proxy*, uint32_t, uint32_t, uint32_t,
            uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
        using Refresh = void (*)(void*, wl_proxy*, uint32_t, uint32_t);
        const uint64_t actual = presentTime + 100000;
        reinterpret_cast<Timing>(proxy->listener[0])(proxy->data, proxy, presentId,
            static_cast<uint32_t>(presentTime >> 32), static_cast<uint32_t>(presentTime),
            static_cast<uint32_t>(actual >> 32), static_cast<uint32_t>(actual), 0, 0, 0, 0);
        reinterpret_cast<Refresh>(proxy->listener[1])(proxy->data, proxy, 0, 8333333);
    }
    int mako_test_surface_present_modes() { return presentModes; }
    uint32_t mako_test_surface_present_mode() { return presentMode; }
    uint32_t mako_test_surface_feedback_image_count() { return feedbackImageCount; }
    const char* mako_test_surface_feedback_engine() { return feedbackEngine.c_str(); }
    int mako_test_surface_reads() { return reads; }
    int mako_test_surface_geometry_queries() { return geometryQueries; }
    void mako_test_surface_resize(uint16_t width, uint16_t height) {
        windowWidth = width;
        windowHeight = height;
    }
    uint32_t mako_test_surface_window() { return associatedWindow; }
    uint32_t mako_test_surface_server() { return associatedServer; }
    void mako_test_surface_retire() {
        for (auto* proxy : objects) {
            if (proxy->kind == "gamescope_swapchain") {
                proxy->pending = true;
                break;
            }
        }
    }

    wl_display* wl_display_connect(const char*) {
        if (mode == 1)
            return nullptr;
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
            std::abort();
        return reinterpret_cast<wl_display*>(make("wl_display"));
    }
    void wl_display_disconnect(wl_display* display) {
        drop(reinterpret_cast<wl_proxy*>(display));
        close(sockets[0]);
        close(sockets[1]);
    }
    // Match the public opaque pointer signatures even though the fixture uses
    // proxies/counters as storage. dlopen callers retain the real API types.
    int wl_display_get_fd(wl_display*) { return sockets[0]; }
    int wl_display_flush(wl_display*) {
        if (mode == 12 || mode == 13) {
            errno = mode == 12 ? EAGAIN : EPIPE;
            return -1;
        }
        return 0;
    }
    int wl_display_get_error(wl_display*) { return mode == 5 ? 32 : 0; }
    wl_event_queue* wl_display_create_queue(wl_display*) {
        ++queues;
        return reinterpret_cast<wl_event_queue*>(&queues);
    }
    void wl_event_queue_destroy(wl_event_queue*) { --queues; }
    int wl_display_prepare_read_queue(wl_display*, wl_event_queue*) { ++reads; return 0; }
    int wl_display_read_events(wl_display*) { return 0; }
    void wl_display_cancel_read(wl_display*) {}
    void wl_proxy_set_queue(wl_proxy*, wl_event_queue*) {}
    void wl_proxy_destroy(wl_proxy* proxy) { drop(proxy); }
    int wl_proxy_add_listener(wl_proxy* proxy, void (**listener)(void), void* data) {
        if (mode == 8 && proxy->kind == "gamescope_swapchain")
            return -1;
        proxy->listener = listener;
        proxy->data = data;
        return 0;
    }
    wl_proxy* wl_proxy_marshal_array_flags(wl_proxy* proxy, uint32_t opcode,
            const wl_interface* interface, uint32_t, uint32_t flags, wl_argument* args) {
        if (flags & 1) {
            drop(proxy);
            return nullptr;
        }
        if (proxy->kind == "gamescope_swapchain") {
            if (opcode == 1) {
                ++associations;
                associatedServer = args[0].u;
                associatedWindow = args[1].u;
                if (ownershipModel)
                    associate(proxy, args[0].u, args[1].u);
            } else if (opcode == 2) {
                ++feedbacks;
                feedbackImageCount = args[0].u;
                feedbackColorSpace = args[2].u;
                feedbackEngine = args[6].s;
            } else if (opcode == 3) {
                ++presentModes;
                presentMode = args[0].u;
            } else if (opcode == 4) {
                ++hdrMetadataCalls;
                for (unsigned index = 0; index < 12; ++index)
                    hdrMetadata[index] = args[index].u;
            } else if (opcode == 5) {
                lastTimedProxy = proxy;
                ++presentTimes;
                presentId = args[0].u;
                presentTime = (uint64_t{args[1].u} << 32) | args[2].u;
            } else {
                std::abort(); // No limiter control requests.
            }
            return nullptr;
        }
        if (!interface)
            std::abort();
        if (mode == 7 && std::string_view(interface->name) == "gamescope_swapchain")
            return nullptr;
        auto* created = make(interface->name);
        if (created->kind == "gamescope_swapchain")
            created->surface = static_cast<wl_proxy*>(args[0].o);
        if (created->kind == "wl_registry" || created->kind == "wl_callback")
            created->pending = true;
        return created;
    }
    int wl_display_dispatch_queue_pending(wl_display*, wl_event_queue*) {
        if (mode == 14) {
            errno = EPROTO;
            return -1;
        }
        if (mode == 3)
            return 0; // A silent peer must hit the bounded discovery deadline.
        const auto pending = objects;
        for (auto* proxy : pending) {
            if (!proxy->pending || !proxy->listener)
                continue;
            if (delayedEvents && proxy->kind == "gamescope_swapchain")
                continue;
            proxy->pending = false;
            if (proxy->kind == "wl_registry") {
                auto callback = reinterpret_cast<void (*)(void*, wl_proxy*, uint32_t, const char*, uint32_t)>(proxy->listener[0]);
                callback(proxy->data, proxy, 1, "wl_compositor", 4);
                if (mode != 2)
                    callback(proxy->data, proxy, 2, "gamescope_swapchain_factory_v2", 1);
            } else if (proxy->kind == "wl_callback") {
                reinterpret_cast<void (*)(void*, wl_proxy*, uint32_t)>(proxy->listener[0])(proxy->data, proxy, 0);
            } else if (proxy->kind == "gamescope_swapchain") {
                reinterpret_cast<void (*)(void*, wl_proxy*)>(proxy->listener[2])(proxy->data, proxy);
            }
        }
        return 0;
    }

    wl_proxy* wl_proxy_marshal_array_constructor_versioned(wl_proxy* proxy,
            uint32_t opcode, wl_argument* args, const wl_interface* interface,
            uint32_t version) {
        return wl_proxy_marshal_array_flags(proxy, opcode, interface, version, 0, args);
    }
    void wl_proxy_marshal_array(wl_proxy* proxy, uint32_t opcode, wl_argument* args) {
        if (opcode == 0)
            return; // Destructor request; the following proxy destroy owns cleanup.
        wl_proxy_marshal_array_flags(proxy, opcode, nullptr, 1, 0, args);
    }

    xcb_get_geometry_cookie_t xcb_get_geometry(xcb_connection_t*, xcb_drawable_t) {
        ++geometryQueries;
        return {1};
    }
    xcb_get_geometry_reply_t* xcb_get_geometry_reply(xcb_connection_t*, xcb_get_geometry_cookie_t, xcb_generic_error_t**) {
        if (mode == 4)
            return nullptr;
        auto* reply = static_cast<xcb_get_geometry_reply_t*>(std::calloc(1, sizeof(xcb_get_geometry_reply_t)));
        reply->root = 42;
        reply->width = windowWidth;
        reply->height = windowHeight;
        return reply;
    }
    xcb_intern_atom_cookie_t xcb_intern_atom(xcb_connection_t*, uint8_t, uint16_t size, const char* name) {
        if (std::string_view(name, size) == "GAMESCOPE_HDR_OUTPUT_FEEDBACK") {
            ++hdrQueries;
            return {102};
        }
        if (std::string_view(name, size) == "GAMESCOPE_DISPLAY_IS_EXTERNAL")
            return {103};
        return {std::string_view(name, size) == "GAMESCOPE_PID" ? 100u : 101u};
    }
    xcb_intern_atom_reply_t* xcb_intern_atom_reply(xcb_connection_t*, xcb_intern_atom_cookie_t cookie, xcb_generic_error_t**) {
        auto* reply = static_cast<xcb_intern_atom_reply_t*>(std::calloc(1, sizeof(xcb_intern_atom_reply_t)));
        reply->atom = mode == 6 ? XCB_ATOM_NONE : cookie.sequence;
        return reply;
    }
    xcb_get_property_cookie_t xcb_get_property(xcb_connection_t*, uint8_t, xcb_window_t, xcb_atom_t atom, xcb_atom_t, uint32_t, uint32_t) { return {atom}; }
    xcb_get_property_reply_t* xcb_get_property_reply(xcb_connection_t*, xcb_get_property_cookie_t cookie, xcb_generic_error_t**) {
        auto* reply = static_cast<xcb_get_property_reply_t*>(std::calloc(1, sizeof(xcb_get_property_reply_t) + 4));
        reply->type = XCB_ATOM_CARDINAL;
        reply->format = mode == 9 ? 8 : 32;
        reply->value_len = 1;
        const uint32_t value = cookie.sequence == 100
            ? static_cast<uint32_t>(getpid()) + (mode == 10 ? 1 : 0)
            : cookie.sequence == 102 ? hdrOutput : cookie.sequence == 103 ? externalOutput : 9;
        std::memcpy(reply + 1, &value, 4);
        return reply;
    }
    void* xcb_get_property_value(const xcb_get_property_reply_t* reply) { return const_cast<xcb_get_property_reply_t*>(reply) + 1; }
    xcb_connection_t* XGetXCBConnection(Display*) { return reinterpret_cast<xcb_connection_t*>(1); }
}
