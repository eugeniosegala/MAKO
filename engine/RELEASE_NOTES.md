## What's new in MAKO Renderer v2.0.0

![Leviathan Rising — a Renaissance vision of the MAKO Leviathan](https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/leviathan-rising.png)

### Codename: Leviathan Rising

> **The deep has awakened.**

MAKO Renderer 2.0 rises as the adaptive, opt-in Vulkan engine behind MAKO's SteamOS frame-generation experience. Leviathan Rising combines new control with a major presentation and lifecycle hardening pass: broader deployment coverage, safer failure behaviour, clearer diagnostics, and an engine that responds to the game instead of blindly chasing a fixed workload. Frame generation is available now; scaling is coming soon and is not part of this release.

### Take command

- **Adaptive Frame Generation:** Target 30 to 240 FPS with a 2x to 4x ceiling, Smooth Cadence, and the optional Adaptive FPS Cap. When a game becomes GPU- or compositor-bound, Adaptive reduces generated-frame work instead of forcing a fixed amount of interpolation. That can improve performance and frame pacing; test it per game.
- **Live tuning with safe boundaries:** Frame Generation, Fixed or Adaptive mode, supported multiplier changes, Target FPS, cadence, and FPS caps update while the game runs. Changes that require different GPU resources remain restart-only.
- **A complete Desktop Mode helm:** The optional `mako-ui` provides scrollable Fixed, Adaptive, cap, quality, performance, GPU, DLL, and profile controls. Launch it as **MAKO Renderer Configuration** or run `~/.local/bin/mako-ui`.
- **One-command standalone activation:** The new `mako-launch` helper activates MAKO for one native game and automatically blocks known LSFG-VK 1.x and 2.x frame-generation layers from joining the same process. Advanced variables such as `MAKO_PROFILE` can be placed before the helper and are forwarded unchanged.
- **Explicit profile selection:** `MAKO_PROFILE` can select a named renderer profile for launchers, scripts, and advanced setups without changing the saved default.

### Built to surface safely

- **A hardened presentation pipeline:** Swapchain creation now rolls back partial state, imported Vulkan resources close safely on failures, idle and teardown paths respond promptly, and Gamescope HDR feedback no longer holds up shutdown. Standard Vulkan proc-address entrypoints are exported for compatibility with negotiated and traditional loader paths, repeated instance lifetimes reinitialise layer state safely, and presentation and diagnostics were separated into focused components to make future changes easier to verify.
- **Original-frame fallback:** If no profile matches, or MAKO cannot initialise its private frame-generation backend for a supported presentation path, the layer now preserves the application's native presentation instead of turning an optional enhancement into a launch failure.
- **AMD robustness without an AMD-only assumption:** On compatible drivers, MAKO enables `robustImageAccess2` only when the GPU advertises the complete required feature. Unsupported GPUs and drivers retain the established rendering path; robust buffer access and null descriptors are not forced.
- **Good Vulkan citizenship:** MAKO keeps its unique, opt-in layer identity and preserves ordinary Vulkan layer discovery for overlays, capture tools, and utilities. Only known competing LSFG-VK frame-generation layers are excluded while MAKO is active.
- **Consistent diagnostics:** New public engine records use the stable **MAKO Renderer** name, while diagnostic collection remains able to recognise logs from older installations.

### Forged for the full fleet

- **Real 64-bit and 32-bit x86 delivery:** Host packages contain layers for both x86 process architectures, alongside dual-architecture Flatpak extensions for Freedesktop 23.08, 24.08, and 25.08. Native AArch64/Armada packages are not part of this release.
- **Broader Qt compatibility:** The optional UI now accepts Qt 6.2 or newer, including Ubuntu 24.04's system Qt 6.4. Release packaging rejects newer accidental ABI requirements and offers a reproducible Qt 6.2 build through Docker or Podman when the build host only has a newer Qt.
- **Visual regression evidence on AMD:** A deterministic odd-sized scene checks frame boundaries, motion and disocclusion, severe focus errors, and thin detail in both FP32 and FP16. The generated comparison images and metrics are retained with the build, and `robustImageAccess2` status is reported explicitly.
- **Stronger release gates:** GCC and Clang suites, AddressSanitizer and UndefinedBehaviorSanitizer coverage, adaptive-scheduler and runtime-transition tests, Vulkan-loader smoke tests, package-layout and checksum verification, and native SteamOS AMD validation now guard the release path.

**Leviathan Rising is only the beginning.**
