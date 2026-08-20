## What's new in MAKO Renderer v2.1.0

![Leviathan Rising — a Renaissance vision of the MAKO Leviathan](https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/leviathan-rising.png)

### Codename: Leviathan Rising

> **The deep has awakened.**

MAKO Renderer 2.1 makes frame generation easier to understand and much harder to disrupt, pairing clearer fractional Adaptive behavior with new AMD image-quality safeguards, tougher recovery, and a cleaner SteamOS presentation path. Frame generation is available now; scaling is coming soon.

- **Fractional Adaptive, finally explained:** Adaptive can average between integer ratios—for example, 60 real FPS targeting 90 displayed FPS uses a 1.5x cadence. The capability was already in MAKO; 2.1 brings it to the foreground with clear examples, explicit multiplier ceilings, and a clearly named **Steady 2x FPS Cap** for players who prefer a fixed cadence.
- **AMD image-quality improvements:** MAKO integrates the upstream robustness path aimed at reducing ghosting and edge corruption, enabling `robustImageAccess2` only when a driver fully supports it. New FP32 and FP16 GPU regression scenes hunt motion trails, disocclusion errors, boundary corruption, and lost thin detail before release.
- **Gamescope and generated frames stop fighting:** Managed launches keep Gamescope's Vulkan WSI policy outside MAKO's generated/original sequence while Gamescope remains the compositor, protecting base FPS without losing Game Mode or Steam.
- **Frame generation fails gracefully:** Backend or swapchain-context initialization problems now preserve the game's original frames where supported instead of turning an optional graphics feature into a launch failure.
- **Recovery built for real gameplay:** Focus changes, overlays, hitches, swapchain recreation, teardown, and repeated game lifetimes have stronger cleanup and recovery, with prompt shutdown instead of long Vulkan waits.
- **HDR groundwork without risking SDR:** Presentation transport is fixed at process start, keeping the experimental HDR path isolated while ordinary launches stay on the supported SDR path.
- **One-command standalone launch:** The new `mako-launch` helper activates MAKO through its private Vulkan manifest and applies the same presentation and competing-layer protections as managed launches.
- **MangoHud can report generated output FPS:** A documented opt-in chain places MAKO before MangoHud on validated 64-bit SteamOS/RADV setups, so the overlay sees the final generated presents rather than only source FPS.
- **Diagnostics that tell the story:** Renderer packages now include focused collection tools for activation, presentation, recovery, and Vulkan layer ordering, while heavy frame tracing remains opt-in so normal play stays fast.
- **A more portable configuration UI:** The Qt interface supports Qt 6.2 and newer, and package guards keep published archives compatible with Ubuntu 24.04's Qt 6.4 runtime.
- **Tested where frame generation breaks:** New Vulkan-loader checks, sanitizers, compiler coverage, native SteamOS validation, and AMD image-quality runs exercise the paths that ordinary unit tests cannot see.

**Leviathan Rising is only the beginning.**
