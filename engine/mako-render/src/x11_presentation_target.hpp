/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <optional>

struct xcb_connection_t;
struct VkXcbSurfaceCreateInfoKHR;
struct VkXlibSurfaceCreateInfoKHR;

namespace mako::layer {

    /// The X11 window an Xcb or Xlib surface presents to, on the
    /// application's own connection.
    struct X11PresentationTarget {
        xcb_connection_t* connection{nullptr};
        uint32_t window{0};
    };

    /// Sizes of a presentation window and of its top-level ancestor, the
    /// root window's child that contains it.
    struct X11WindowNesting {
        uint32_t width{0};
        uint32_t height{0};
        uint32_t topLevelWidth{0};
        uint32_t topLevelHeight{0};
    };

    [[nodiscard]] X11PresentationTarget x11PresentationTarget(
        const VkXcbSurfaceCreateInfoKHR& info) noexcept;
    /// An Xlib display yields no connection when libX11-xcb is unavailable.
    [[nodiscard]] X11PresentationTarget x11PresentationTarget(
        const VkXlibSurfaceCreateInfoKHR& info) noexcept;

    /// Read the window's size and its top-level ancestor's with a bounded
    /// number of round trips on the application's connection. Empty when
    /// libxcb or the server cannot answer. Call at swapchain creation, never
    /// during presentation.
    [[nodiscard]] std::optional<X11WindowNesting> queryX11WindowNesting(
        const X11PresentationTarget& target) noexcept;

    /// Whether a top-level window whose _NET_WM_PID is this process has exactly
    /// the given size, so an offscreen copy of that size is the game's main
    /// window rather than a child it draws into. Empty when the server cannot
    /// answer. One pipelined batch of requests; call at swapchain creation.
    [[nodiscard]] std::optional<bool> x11ProcessTopLevelHasSize(
        const X11PresentationTarget& target, uint32_t width, uint32_t height) noexcept;

}
