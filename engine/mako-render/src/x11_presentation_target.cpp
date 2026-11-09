/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "x11_presentation_target.hpp"

#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <memory>
#include <string_view>
#include <unistd.h>
#include <vector>
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
        decltype(&xcb_query_tree_children) treeChildren{};
        decltype(&xcb_query_tree_children_length) treeChildrenLength{};
        decltype(&xcb_intern_atom) internAtom{};
        decltype(&xcb_intern_atom_reply) atomReply{};
        decltype(&xcb_get_property) getProperty{};
        decltype(&xcb_get_property_reply) propertyReply{};
        decltype(&xcb_get_property_value) propertyValue{};
        decltype(&xcb_get_property_value_length) propertyValueLength{};
        bool ready{false};

        XcbFunctions() {
            void* const library = dlopen("libxcb.so.1", RTLD_NOW | RTLD_LOCAL);
            this->ready = library &&
                symbol(library, this->getGeometry, "xcb_get_geometry") &&
                symbol(library, this->geometryReply, "xcb_get_geometry_reply") &&
                symbol(library, this->queryTree, "xcb_query_tree") &&
                symbol(library, this->treeReply, "xcb_query_tree_reply") &&
                symbol(library, this->treeChildren, "xcb_query_tree_children") &&
                symbol(library, this->treeChildrenLength,
                    "xcb_query_tree_children_length") &&
                symbol(library, this->internAtom, "xcb_intern_atom") &&
                symbol(library, this->atomReply, "xcb_intern_atom_reply") &&
                symbol(library, this->getProperty, "xcb_get_property") &&
                symbol(library, this->propertyReply, "xcb_get_property_reply") &&
                symbol(library, this->propertyValue, "xcb_get_property_value") &&
                symbol(library, this->propertyValueLength,
                    "xcb_get_property_value_length");
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

std::optional<bool> mako::layer::x11ProcessTopLevelHasSize(
        const X11PresentationTarget& target, const uint32_t width,
        const uint32_t height) noexcept {
    try {
        const auto& xcb = xcbFunctions();
        if (!xcb.ready || !target.connection || target.window == XCB_WINDOW_NONE)
            return std::nullopt;
        auto* const connection = target.connection;
        const Reply<xcb_query_tree_reply_t> own(xcb.treeReply(connection,
            xcb.queryTree(connection, target.window), nullptr), &std::free);
        if (!own)
            return std::nullopt;
        constexpr std::string_view pidName = "_NET_WM_PID";
        const auto pidCookie = xcb.internAtom(connection, 1,
            static_cast<uint16_t>(pidName.size()), pidName.data());
        const auto rootCookie = xcb.queryTree(connection, own->root);
        const Reply<xcb_intern_atom_reply_t> pidAtom(xcb.atomReply(connection,
            pidCookie, nullptr), &std::free);
        const Reply<xcb_query_tree_reply_t> roots(xcb.treeReply(connection,
            rootCookie, nullptr), &std::free);
        if (!pidAtom || !roots)
            return std::nullopt;
        if (pidAtom->atom == XCB_ATOM_NONE)
            return false;
        const xcb_window_t* const children = xcb.treeChildren(roots.get());
        const int count = xcb.treeChildrenLength(roots.get());
        std::vector<xcb_get_property_cookie_t> properties;
        std::vector<xcb_get_geometry_cookie_t> geometries;
        properties.reserve(static_cast<size_t>(count));
        geometries.reserve(static_cast<size_t>(count));
        for (int index = 0; index < count; ++index) {
            properties.push_back(xcb.getProperty(connection, 0, children[index],
                pidAtom->atom, XCB_ATOM_CARDINAL, 0, 1));
            geometries.push_back(xcb.getGeometry(connection, children[index]));
        }
        // Read every reply, even after a match, so none is left queued.
        const auto pid = static_cast<uint32_t>(::getpid());
        bool found = false;
        for (int index = 0; index < count; ++index) {
            const Reply<xcb_get_property_reply_t> property(xcb.propertyReply(
                connection, properties[static_cast<size_t>(index)], nullptr), &std::free);
            const Reply<xcb_get_geometry_reply_t> geometry(xcb.geometryReply(
                connection, geometries[static_cast<size_t>(index)], nullptr), &std::free);
            if (found || !property || !geometry ||
                    xcb.propertyValueLength(property.get()) <
                        static_cast<int>(sizeof(uint32_t)))
                continue;
            uint32_t owner{};
            std::memcpy(&owner, xcb.propertyValue(property.get()), sizeof owner);
            found = owner == pid && geometry->width == width &&
                geometry->height == height;
        }
        return found;
    } catch (...) {
        return std::nullopt;
    }
}
