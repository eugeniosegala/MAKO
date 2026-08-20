## What's new in MAKO Renderer v2.1.0

![Leviathan Rising — a Renaissance vision of the MAKO Leviathan](https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/leviathan-rising.png)

### Release codename: Leviathan Rising

> **The deep has awakened.**

MAKO Renderer 2.1 makes Fractional Adaptive practical and can deliver major image-quality gains on affected AMD workloads, pairing those advances with tougher recovery and a cleaner SteamOS presentation path. Frame generation is available now; scaling is coming soon.

- **Fractional Adaptive reaches targets between integer multipliers:** It can mix generation ratios—for example, 60 real FPS to 90 displayed FPS—to preserve more real frames and potentially reduce input lag. The tradeoff is uneven frame spacing that can feel choppy in some games, so MAKO Decky defaults to Steady 2x and offers Fractional as a per-game preset. Multiplier ceilings continue to bound generation.
- **AMD image quality gains a guarded robustness path:** On drivers that expose both `VK_EXT_robustness2` and `robustImageAccess2`, MAKO makes out-of-bounds image reads at compute boundaries predictable instead of accepting undefined data. Where boundary access contributes to artifacts, this can substantially reduce ghost trails, corrupted moving edges, disocclusion errors, and lost thin detail; the improvement varies by game and scene, so no universal percentage is claimed. Unsupported devices retain the established path, while deterministic AMD scenes guard FP32 and FP16 against boundary corruption, motion trails, severe errors, and detail loss.
- **SteamOS presentation has one clear owner:** Private manifests and process-start policy keep Gamescope's Vulkan WSI outside MAKO's supported SDR presentation sequence, while `mako-launch` applies the same protections for standalone use. A separately validated opt-in chain places MAKO before MangoHud so it can report final generated FPS on 64-bit SteamOS/RADV.
- **Native frames survive generation failures:** More backend and swapchain-context initialization failures fall back to the game's original presentation path. Recovery and cleanup are stronger across hitches, focus changes, overlays, swapchain recreation, teardown, and repeated game lifetimes, with prompt shutdown instead of long Vulkan waits.
- **Packaging, diagnostics, and validation now match production boundaries:** Renderer archives include focused activation, presentation, recovery, and layer-order diagnostics while heavy tracing remains opt-in. Qt ABI guards preserve Ubuntu 24.04 compatibility, and expanded loader, sanitizer, compiler, SteamOS hardware, and AMD quality coverage exercises the paths ordinary unit tests cannot prove.

**Leviathan Rising is only the beginning.**
