/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "x11_presentation_target.hpp"

#include <cstdlib>
#include <dlfcn.h>
#include <memory>
#include <X11/Xlib.h>
#include <xcb/xcb.h>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_xcb.h>
#include <vulkan/vulkan_xlib.h>

using namespace mako::layer;

namespace {
    template<typename Function>
    bool symbol(void* library, Function& function, const char* name) {
        function = reinterpret_cast<Function>(dlsym(library, name));
        return function != nullptr;
    }

    template<typename T>
    using Reply = std::unique_ptr<T, decltype(&std::free)>;

    // A window nested deeper than any toolkit nests ends the walk instead of
    // following a malformed tree.
    constexpr int maximumAncestors = 64;

    // The application already maps these libraries for its surface, so the
    // handles only add references; they live for the process.
    struct XcbFunctions {
        decltype(&xcb_get_geometry) getGeometry{};
        decltype(&xcb_get_geometry_reply) geometryReply{};
        decltype(&xcb_query_tree) queryTree{};
        decltype(&xcb_query_tree_reply) treeReply{};
        bool ready{false};

        XcbFunctions() {
            void* const library = dlopen("libxcb.so.1", RTLD_NOW | RTLD_LOCAL);
            this->ready = library &&
                symbol(library, this->getGeometry, "xcb_get_geometry") &&
                symbol(library, this->geometryReply, "xcb_get_geometry_reply") &&
                symbol(library, this->queryTree, "xcb_query_tree") &&
                symbol(library, this->treeReply, "xcb_query_tree_reply");
        }
    };

    const XcbFunctions& xcbFunctions() {
        static const XcbFunctions functions;
        return functions;
    }
}

X11PresentationTarget mako::layer::x11PresentationTarget(
        const VkXcbSurfaceCreateInfoKHR& info) noexcept {
    return {.connection = info.connection, .window = info.window};
}

X11PresentationTarget mako::layer::x11PresentationTarget(
        const VkXlibSurfaceCreateInfoKHR& info) noexcept {
    using GetXcbConnection = xcb_connection_t* (*)(Display*);
    static const GetXcbConnection getConnection = [] {
        GetXcbConnection function{};
        if (void* const library = dlopen("libX11-xcb.so.1", RTLD_NOW | RTLD_LOCAL))
            symbol(library, function, "XGetXCBConnection");
        return function;
    }();
    return {
        .connection = info.dpy && getConnection ? getConnection(info.dpy) : nullptr,
        .window = static_cast<uint32_t>(info.window),
    };
}

std::optional<X11WindowNesting> mako::layer::queryX11WindowNesting(
        const X11PresentationTarget& target) noexcept {
    const auto& xcb = xcbFunctions();
    if (!xcb.ready || !target.connection || target.window == XCB_WINDOW_NONE)
        return std::nullopt;
    const auto geometry = [&](const xcb_window_t window) {
        return Reply<xcb_get_geometry_reply_t>(xcb.geometryReply(target.connection,
            xcb.getGeometry(target.connection, window), nullptr), &std::free);
    };
    const auto own = geometry(target.window);
    if (!own)
        return std::nullopt;
    xcb_window_t window = target.window;
    for (int depth = 0; depth < maximumAncestors; ++depth) {
        const Reply<xcb_query_tree_reply_t> tree(xcb.treeReply(target.connection,
            xcb.queryTree(target.connection, window), nullptr), &std::free);
        if (!tree)
            return std::nullopt;
        if (tree->parent == tree->root || tree->parent == XCB_WINDOW_NONE) {
            const auto topLevel = window == target.window
                ? Reply<xcb_get_geometry_reply_t>(nullptr, &std::free)
                : geometry(window);
            if (window != target.window && !topLevel)
                return std::nullopt;
            const auto& top = topLevel ? *topLevel : *own;
            return X11WindowNesting{
                .width = own->width,
                .height = own->height,
                .topLevelWidth = top.width,
                .topLevelHeight = top.height,
            };
        }
        window = tree->parent;
    }
    return std::nullopt;
}
