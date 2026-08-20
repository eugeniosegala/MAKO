## What's new in MAKO Renderer v2.1.0

![Leviathan Rising — a Renaissance vision of the MAKO Leviathan](https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/leviathan-rising.png)

### Codename: Leviathan Rising

> **The deep has awakened.**

MAKO Renderer 2.1 introduces Adaptive Frame Generation alongside a substantial reliability, compatibility, and packaging update. Frame generation is available now; scaling is coming soon.

### What's new

- **Adaptive Frame Generation:** Target 30 to 240 FPS with a 2x to 4x ceiling, or use the optional Steady 2x FPS Cap for a consistent 2x cadence. Fixed 2x to 4x generation remains available.
- **Live configuration:** Frame Generation mode, supported multipliers, Target FPS, cadence, and FPS-cap settings can update while a game is running. Options that require new GPU resources remain restart-only.
- **Improved standalone use:** The new `mako-launch` helper provides one-command activation, `MAKO_PROFILE` selects a saved profile, and the optional Qt interface exposes the complete Renderer configuration.
- **More reliable presentation:** Swapchain creation, teardown, recovery, Gamescope pacing, and Vulkan layer ownership have been hardened. If MAKO cannot initialize safely, supported paths preserve the game's original presentation instead of turning frame generation into a launch failure.
- **Safe AMD robustness:** `robustImageAccess2` is enabled only when the complete required feature is advertised by the driver. Unsupported hardware keeps the established fallback path.
- **Broader distribution support:** Host packages now contain genuine 64-bit and 32-bit x86 layers, with matching Flatpak extensions for Freedesktop 23.08, 24.08, and 25.08. The optional UI supports Qt 6.2 and newer, including Ubuntu 24.04's Qt 6.4.
- **Stronger validation:** New scheduler, runtime-transition, sanitizer, package, Vulkan-loader, AMD FP32/FP16 image-quality, and native SteamOS checks protect the release path.

**Leviathan Rising is only the beginning.**
