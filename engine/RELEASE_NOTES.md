## What's new in MAKO Renderer v2.1.0

<img src="https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/leviathan-rising.png" alt="Leviathan Rising: a Renaissance vision of the MAKO Leviathan" width="100%">

### Release codename: Leviathan Rising

> _“The deep has awakened. Even the oldest roots can feel it.”_
>
> **Ilyra, Warden of the Drowned Grove**

---

MAKO Renderer 2.1 makes Fractional Adaptive practical and can deliver major AMD image-quality gains, with stronger recovery and a cleaner SteamOS presentation path. Frame generation is available now; scaling is coming soon.

- **Fractional Adaptive reaches targets between integer multipliers:** It can mix generation ratios, such as 60 real FPS to 90 displayed FPS, to preserve more real frames and potentially reduce input lag. Uneven frame spacing can feel choppy in some games, so MAKO Decky defaults to Steady 2x and offers Fractional as a per-game preset.
- **AMD image quality gains a guarded robustness path:** Supported drivers gain predictable image-boundary handling that can substantially reduce ghost trails, corrupted moving edges, disocclusion errors, and lost thin detail. Results vary by game and scene, unsupported devices keep the established path, and dedicated AMD scenes guard FP32 and FP16 quality.
- **SteamOS presentation stays isolated, with an experimental WSI option:** Gamescope WSI remains excluded from MAKO's standard SDR path, but MAKO Decky now offers an off-by-default, per-profile compatibility toggle for affected games. The guarded 64-bit host path validates the real Gamescope manifest and fails closed if it is unavailable or invalid. The validated MangoHud chain still reports final generated FPS on SteamOS/RADV.
- **Native frames keep moving through generation stalls:** A missed render-fence budget now presents the game's original frame, pauses generation, and resumes it after recovery. This reduces the risk of a Renderer stall becoming a visible freeze, while broader backend and swapchain failures continue to fall back to native presentation.
- **Standalone tools now speak more languages:** The optional desktop UI automatically selects and remembers English, Brazilian Portuguese, European Portuguese, or Spanish. The CLI offers the same language choices through an explicit `--lang` option without changing Renderer profiles or machine-oriented quality output.
- **A stronger foundation for future releases:** Presentation responsibilities are separated, runtime policy is centralized, and managed launches use private Vulkan layer discovery for a predictable chain. Build and package gates reject missing or misidentified layers, compatibility paths fail closed, and broader validation protects supported production boundaries.
