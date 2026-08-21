## What's new in MAKO Renderer v2.1.0

![Leviathan Rising — a Renaissance vision of the MAKO Leviathan](https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/leviathan-rising.png)

### Release codename: Leviathan Rising

> **The deep has awakened.**

MAKO Renderer 2.1 makes Fractional Adaptive practical and can deliver major image-quality gains on affected AMD workloads, pairing those advances with tougher recovery and a cleaner SteamOS presentation path. Frame generation is available now; scaling is coming soon.

- **Fractional Adaptive reaches targets between integer multipliers:** It can mix generation ratios—for example, 60 real FPS to 90 displayed FPS—to preserve more real frames and potentially reduce input lag. The tradeoff is uneven frame spacing that can feel choppy in some games, so MAKO Decky defaults to Steady 2x and offers Fractional as a per-game preset. Multiplier ceilings continue to bound generation.
- **AMD image quality gains a guarded robustness path:** On drivers that expose both `VK_EXT_robustness2` and `robustImageAccess2`, MAKO makes out-of-bounds image reads at compute boundaries predictable instead of accepting undefined data. Where boundary access contributes to artifacts, this can substantially reduce ghost trails, corrupted moving edges, disocclusion errors, and lost thin detail; the improvement varies by game and scene, so no universal percentage is claimed. Unsupported devices retain the established path, while deterministic AMD scenes guard FP32 and FP16 against boundary corruption, motion trails, severe errors, and detail loss.
- **SteamOS presentation stays isolated, with an experimental WSI escape hatch:** Gamescope's Vulkan WSI remains excluded from MAKO's standard SDR presentation sequence, but MAKO Decky now offers an off-by-default, per-profile compatibility toggle for games affected by coloured or pixelated motion artifacts. The guarded 64-bit host path validates and privately stages the real Gamescope WSI manifest, fails closed when it is unavailable or invalid, and leaves every profile that does not select it on the established isolated path. A separately validated opt-in chain still places MAKO before MangoHud so it can report final generated FPS on SteamOS/RADV.
- **Native frames keep moving through generation stalls:** A missed render-fence budget now presents the game's original frame instead of extending the wait, temporarily quarantines generation, and resumes it only after the backend and fence recover. This reduces the risk of a generation-side stall becoming a visible freeze, while broader backend and swapchain failures continue to fall back to native presentation across hitches, focus changes, overlays, swapchain recreation, teardown, and repeated game lifetimes.
- **Packaging, diagnostics, and validation now match production boundaries:** Renderer archives include focused activation, presentation, recovery, and layer-order diagnostics while heavy tracing remains opt-in. Qt ABI guards preserve Ubuntu 24.04 compatibility, and expanded loader, sanitizer, compiler, SteamOS hardware, and AMD quality coverage exercises the paths ordinary unit tests cannot prove.

**Leviathan Rising is only the beginning.**
