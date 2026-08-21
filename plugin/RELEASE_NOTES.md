## What's new in MAKO Decky v2.1.0

![Leviathan Rising: a Renaissance vision of the MAKO Leviathan](https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/leviathan-rising.png)

### Release codename: Leviathan Rising

> **Command the Leviathan.**

MAKO Decky 2.1 brings easier Fractional Adaptive setup and potentially major AMD image-quality gains into Game Mode, with steadier SteamOS presentation and a more polished everyday experience. Frame generation is available now; scaling is coming soon.

- **Fractional Adaptive is now an explicit per-game choice:** It can mix generation ratios, such as 60 real FPS to 90 displayed FPS, to keep more real frames and potentially reduce input lag. Uneven cadence can feel choppy in some games, so MAKO Decky defaults to the smoother Steady Base Cap with an even 2x cadence. A one-click preset makes Fractional easy to test.
- **AMD generated frames can look substantially cleaner:** Supported GPUs gain predictable image-boundary handling that can greatly reduce ghost trails, corrupted moving edges, disocclusion noise, and lost thin detail in affected games. Results vary by workload, unsupported GPUs keep the established fallback, and dedicated scenes guard FP32 and FP16 quality.
- **Experimental Gamescope WSI compatibility is now one toggle away:** MAKO's standard SDR path still isolates Gamescope WSI, while an off-by-default, per-profile control can restore it for affected games. The guarded 64-bit host path validates the real Gamescope manifest, fails closed if it is unavailable or invalid, and requires a game restart. The same control supports either MangoHud or experimental vkBasalt, with MangoHud able to report final generated FPS.
- **Games keep presenting through Renderer stalls:** If generation misses its render-fence budget, the bundled Renderer presents the game's original frame, pauses generation, and resumes after recovery instead of allowing the wait to become a visible freeze. More initialization and recovery failures also fall back to native frames.
- **A safer and more consistent foundation:** Installation, upgrades, profiles, generated wrappers, and packaging now follow clearer ownership rules. Managed launches use private Vulkan layer discovery to prevent accidental activation, unsupported compatibility paths fail closed, package checks catch incomplete builds, and MAKO-owned files update safely without rewriting user configuration.

**The Leviathan is awake. Take the helm.**
