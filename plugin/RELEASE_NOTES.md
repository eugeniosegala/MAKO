## What's new in MAKO Decky v2.1.0

![Leviathan Rising — a Renaissance vision of the MAKO Leviathan](https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/leviathan-rising.png)

### Release codename: Leviathan Rising

> **Command the Leviathan.**

MAKO Decky 2.1 brings the Renderer improvements into Game Mode with clearer Adaptive controls, steadier SteamOS presentation, safer compatibility handling, and a more polished everyday experience. Frame generation is available now; scaling is coming soon.

- **Fractional Adaptive becomes a real choice:** Adaptive can now average between integer ratios—such as 80 real FPS targeting 120 displayed FPS at 1.5x. Updated controls clearly distinguish fractional generation from the Steady 2x FPS Cap, including multiplier limits and restart requirements.
- **AMD video quality is improved:** The bundled Renderer includes upstream AMD robustness improvements designed to reduce ghosting and edge corruption in generated frames. Enhancements activate only on supported GPUs, with safe fallbacks elsewhere; unvalidated native AArch64 and Armada overrides remain disabled.
- **SteamOS presentation is smoother and easier to monitor:** MAKO works more reliably alongside Gamescope, Steam, Game Mode, and overlays without competing with Gamescope's Vulkan WSI policy. Private manifests prevent unintended activation, while new per-profile External Tools controls admit either MangoHud or experimental vkBasalt through the guarded 64-bit SteamOS path; MangoHud can report final generated FPS and keeps using the user's existing overlay configuration.
- **Games keep presenting when generation cannot:** More initialization and recovery failures fall back to the game's original frames, with stronger handling of hitches, focus changes, overlays, swapchain recreation, and shutdown.
- **A clearer interface with safer maintenance:** Game Mode gains cleaner spacing, hierarchy, compatibility controls, installation messaging, and complete Simplified Chinese support. Upgrades now rebuild wrappers from canonical settings, diagnostics remain focused, and expanded real-world testing covers SteamOS, AMD FP32/FP16, packaging, frontend, backend, and Vulkan-loader boundaries.

**The Leviathan is awake. Take the helm.**
