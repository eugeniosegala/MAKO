## What's new in MAKO Renderer v2.1.0

![Leviathan Rising — a Renaissance vision of the MAKO Leviathan](https://raw.githubusercontent.com/eugeniosegala/MAKO/refs/heads/main/assets/leviathan-rising.png)

### Codename: Leviathan Rising

> **The deep has awakened.**

MAKO Renderer 2.1 makes frame generation easier to understand and much harder to disrupt, pairing clearer fractional Adaptive behavior with new AMD image-quality safeguards, tougher recovery, and a cleaner SteamOS presentation path. Frame generation is available now; scaling is coming soon.

- **Fractional Adaptive is explicit and controllable:** The scheduler can average between integer ratios—such as 80 real FPS targeting 120 displayed FPS at 1.5x—while multiplier ceilings and the distinct Steady 2x FPS Cap make fractional and fixed-cadence behavior unambiguous.
- **AMD frame quality gains guarded upstream robustness:** `robustImageAccess2` is enabled only when the driver fully supports it, targeting ghosting and edge corruption without weakening compatibility. Deterministic FP32 and FP16 GPU regression scenes now probe motion trails, disocclusion errors, boundary corruption, and lost thin detail.
- **SteamOS presentation has one clear owner:** Private manifests and process-start policy keep Gamescope's Vulkan WSI outside MAKO's supported SDR presentation sequence, while `mako-launch` applies the same protections for standalone use. A separately validated opt-in chain places MAKO before MangoHud so it can report final generated FPS on 64-bit SteamOS/RADV.
- **Native frames survive generation failures:** More backend and swapchain-context initialization failures fall back to the game's original presentation path. Recovery and cleanup are stronger across hitches, focus changes, overlays, swapchain recreation, teardown, and repeated game lifetimes, with prompt shutdown instead of long Vulkan waits.
- **Packaging, diagnostics, and validation now match production boundaries:** Renderer archives include focused activation, presentation, recovery, and layer-order diagnostics while heavy tracing remains opt-in. Qt ABI guards preserve Ubuntu 24.04 compatibility, and expanded loader, sanitizer, compiler, SteamOS hardware, and AMD quality coverage exercises the paths ordinary unit tests cannot prove.

**Leviathan Rising is only the beginning.**
