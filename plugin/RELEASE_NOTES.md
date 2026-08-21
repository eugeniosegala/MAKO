## What's new in MAKO Decky v2.1.0

![Leviathan Rising — a Renaissance vision of the MAKO Leviathan](https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/leviathan-rising.png)

### Release codename: Leviathan Rising

> **Command the Leviathan.**

MAKO Decky 2.1 brings easier Fractional Adaptive setup and potentially major AMD image-quality gains into Game Mode, alongside steadier SteamOS presentation, safer compatibility handling, and a more polished everyday experience. Frame generation is available now; scaling is coming soon.

- **Fractional Adaptive is now an explicit per-game choice:** It can mix generation ratios—for example, 60 real FPS to 90 displayed FPS—to keep more real frames and potentially reduce input lag. Because that uneven cadence can feel choppy in some games, MAKO Decky now defaults to the smoother Steady Base Cap with an even 2x cadence; a one-click preset makes Fractional easy to test and turns itself off after an incompatible setting change.
- **AMD generated frames can look substantially cleaner:** The bundled Renderer carries upstream AMD robustness work and enables Vulkan's robust image-access path only when the driver fully supports it. In affected games and scenes, predictable image-boundary handling can make a large visible difference to ghost trails, corrupted edges around moving objects, disocclusion noise, and lost thin detail. The gain is workload-dependent rather than a universal percentage; unsupported GPUs keep the established fallback, and dedicated AMD regression scenes guard both FP32 and FP16 paths.
- **Experimental Gamescope WSI compatibility is now one toggle away:** MAKO's standard SDR path continues to isolate Gamescope WSI, while an off-by-default, per-profile compatibility control can reintroduce it for games affected by coloured or pixelated motion artifacts. The guarded 64-bit host path privately stages and validates the real Gamescope manifest, fails closed if it is unavailable or invalid, and requires a game restart before the selected layer changes. The same mutually exclusive control admits either MangoHud or experimental vkBasalt; MangoHud can report final generated FPS while keeping the user's existing overlay configuration.
- **Games keep presenting through Renderer stalls:** When generation misses its render-fence budget, the bundled Renderer now presents the game's original frame, temporarily pauses generation, and resumes only after the backend and fence recover instead of allowing the wait to become a visible freeze. More initialization and recovery failures also fall back to native frames, with stronger handling of hitches, focus changes, overlays, swapchain recreation, and shutdown.
- **A clearer interface with safer maintenance:** Game Mode gains cleaner spacing, hierarchy, compatibility controls, an inline timed post-install restart recommendation, and complete Simplified Chinese support. Installation and upgrades are more dependable, diagnostics remain focused, and expanded real-world testing covers SteamOS, AMD FP32/FP16, packaging, frontend, backend, and Vulkan-loader boundaries.

**The Leviathan is awake. Take the helm.**
