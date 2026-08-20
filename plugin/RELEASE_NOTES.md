## What's new in MAKO Decky v2.1.0

![Leviathan Rising — a Renaissance vision of the MAKO Leviathan](https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/leviathan-rising.png)

### Release codename: Leviathan Rising

> **Command the Leviathan.**

MAKO Decky 2.1 brings easier Fractional Adaptive setup and potentially major AMD image-quality gains into Game Mode, alongside steadier SteamOS presentation, safer compatibility handling, and a more polished everyday experience. Frame generation is available now; scaling is coming soon.

- **Fractional Adaptive is now an explicit per-game choice:** It can mix generation ratios—for example, 60 real FPS to 90 displayed FPS—to keep more real frames and potentially reduce input lag. Because that uneven cadence can feel choppy in some games, MAKO Decky now defaults to smoother Steady 2x; a one-click preset makes Fractional easy to test and turns itself off after an incompatible setting change.
- **AMD generated frames can look substantially cleaner:** The bundled Renderer carries upstream AMD robustness work and enables Vulkan's robust image-access path only when the driver fully supports it. In affected games and scenes, predictable image-boundary handling can make a large visible difference to ghost trails, corrupted edges around moving objects, disocclusion noise, and lost thin detail. The gain is workload-dependent rather than a universal percentage; unsupported GPUs keep the established fallback, and dedicated AMD regression scenes guard both FP32 and FP16 paths.
- **SteamOS presentation is smoother and easier to monitor:** MAKO works more reliably alongside Gamescope, Steam, Game Mode, and overlays without competing with Gamescope's Vulkan WSI policy. Private manifests prevent unintended activation, while new per-profile External Tools controls admit either MangoHud or experimental vkBasalt through the guarded 64-bit SteamOS path; MangoHud can report final generated FPS and keeps using the user's existing overlay configuration.
- **Games keep presenting when generation cannot:** More initialization and recovery failures fall back to the game's original frames, with stronger handling of hitches, focus changes, overlays, swapchain recreation, and shutdown.
- **A clearer interface with safer maintenance:** Game Mode gains cleaner spacing, hierarchy, compatibility controls, an inline timed post-install restart recommendation that remains visible before the installed interface appears, and complete Simplified Chinese support. Upgrades now rebuild wrappers from canonical settings and atomically replace MAKO-managed files without following stale symlinks or rewriting them in place; self-contained tester ZIPs fail packaging if Decky would attempt an invalid payload download, diagnostics remain focused, and expanded real-world testing covers SteamOS, AMD FP32/FP16, packaging, frontend, backend, and Vulkan-loader boundaries.

**The Leviathan is awake. Take the helm.**
