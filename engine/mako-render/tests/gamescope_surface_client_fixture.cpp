/* SPDX-License-Identifier: GPL-3.0-or-later */

// A local protocol peer fixture. The production adapter loads these ordinary
// runtime symbols through its real dlopen path, with no test branch in MAKO.
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>
#include <xcb/xcb.h>

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
};

namespace {
    std::vector<wl_proxy*> objects;
    int mode{};
    int associations{};
    int queues{};
    int reads{};
    int sockets[2]{-1, -1};
    uint32_t associatedServer{};
    uint32_t associatedWindow{};

    wl_proxy* make(std::string_view kind) {
        auto* proxy = new wl_proxy{kind};
        objects.push_back(proxy);
        return proxy;
    }
    void drop(wl_proxy* proxy) {
        std::erase(objects, proxy);
        delete proxy;
    }
}

extern "C" {
    // Keep peer identity deterministic in PID-isolated test sandboxes too.
    int getsockopt(int, int level, int option, void* value, socklen_t* size) noexcept {
        if (level != SOL_SOCKET || option != SO_PEERCRED || *size < sizeof(ucred))
            return -1;
        const ucred peer{getpid(), getuid(), getgid()};
        std::memcpy(value, &peer, sizeof(peer));
        *size = sizeof(peer);
        return 0;
    }
    extern const wl_interface wl_registry_interface{"wl_registry", 1, 0, nullptr, 0, nullptr};
    extern const wl_interface wl_compositor_interface{"wl_compositor", 1, 0, nullptr, 0, nullptr};
    extern const wl_interface wl_surface_interface{"wl_surface", 1, 0, nullptr, 0, nullptr};
    extern const wl_interface wl_callback_interface{"wl_callback", 1, 0, nullptr, 0, nullptr};

    void mako_test_surface_mode(int value) { mode = value; }
    int mako_test_surface_objects() { return static_cast<int>(objects.size()) + queues; }
    int mako_test_surface_associations() { return associations; }
    int mako_test_surface_reads() { return reads; }
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

    wl_proxy* wl_display_connect(const char*) {
        if (mode == 1)
            return nullptr;
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
            std::abort();
        return make("wl_display");
    }
    void wl_display_disconnect(wl_proxy* proxy) {
        drop(proxy);
        close(sockets[0]);
        close(sockets[1]);
    }
    int wl_display_get_fd(void*) { return sockets[0]; }
    int wl_display_flush(void*) { return 0; }
    int wl_display_get_error(void*) { return mode == 5 ? 32 : 0; }
    void* wl_display_create_queue(void*) { ++queues; return &queues; }
    void wl_event_queue_destroy(void*) { --queues; }
    int wl_display_prepare_read_queue(void*, void*) { ++reads; return 0; }
    int wl_display_read_events(void*) { return 0; }
    void wl_display_cancel_read(void*) {}
    void wl_proxy_set_queue(wl_proxy*, void*) {}
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
            if (opcode != 1)
                std::abort(); // No limiter, timing, present-mode or HDR requests.
            ++associations;
            associatedServer = args[0].u;
            associatedWindow = args[1].u;
            return nullptr;
        }
        if (!interface)
            std::abort();
        if (mode == 7 && std::string_view(interface->name) == "gamescope_swapchain")
            return nullptr;
        auto* created = make(interface->name);
        if (created->kind == "wl_registry" || created->kind == "wl_callback")
            created->pending = true;
        return created;
    }
    int wl_display_dispatch_queue_pending(void*, void*) {
        if (mode == 3)
            return 0; // A silent peer must hit the bounded discovery deadline.
        const auto pending = objects;
        for (auto* proxy : pending) {
            if (!proxy->pending || !proxy->listener)
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

    xcb_get_geometry_cookie_t xcb_get_geometry(xcb_connection_t*, xcb_drawable_t) { return {1}; }
    xcb_get_geometry_reply_t* xcb_get_geometry_reply(xcb_connection_t*, xcb_get_geometry_cookie_t, xcb_generic_error_t**) {
        if (mode == 4)
            return nullptr;
        auto* reply = static_cast<xcb_get_geometry_reply_t*>(std::calloc(1, sizeof(xcb_get_geometry_reply_t)));
        reply->root = 42;
        return reply;
    }
    xcb_intern_atom_cookie_t xcb_intern_atom(xcb_connection_t*, uint8_t, uint16_t size, const char* name) {
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
            ? static_cast<uint32_t>(getpid()) + (mode == 10 ? 1 : 0) : 9;
        std::memcpy(reply + 1, &value, 4);
        return reply;
    }
    void* xcb_get_property_value(const xcb_get_property_reply_t* reply) { return const_cast<xcb_get_property_reply_t*>(reply) + 1; }
    xcb_connection_t* XGetXCBConnection(void*) { return reinterpret_cast<xcb_connection_t*>(1); }
}
