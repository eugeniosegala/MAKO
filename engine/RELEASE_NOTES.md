## What's new in MAKO Renderer v2.0.0

MAKO Renderer 2.0 is the adaptive, opt-in Vulkan layer behind MAKO’s SteamOS frame-generation experience.

- **Adaptive Frame Generation:** Target 30 to 240 FPS with a 2x to 4x ceiling, Smooth Cadence, and the optional Adaptive FPS Cap. When a game becomes GPU- or compositor-bound, Adaptive reduces generated-frame work instead of forcing a fixed amount of interpolation. That can improve performance and frame pacing; test it per game.
- **Live tuning with safe limits:** Frame Generation, Fixed or Adaptive mode, supported multiplier changes, target FPS, cadence, and FPS caps update while the game runs. Changes that need different GPU resources remain restart-only.
- **Private by design:** A uniquely named Vulkan layer, explicit per-game activation, targeted exclusion of competing LSFG-VK layers, process matching, and context-specific diagnostics keep MAKO isolated from unrelated games and applications without hiding ordinary Vulkan utilities.
- **Complete standalone control:** `mako-ui` now provides scrollable Fixed, Adaptive, cap, quality, performance, GPU, DLL, and profile controls. Launch it from Desktop Mode as **MAKO Renderer Configuration** or run `~/.local/bin/mako-ui`.
- **Verified everywhere MAKO runs:** Releases contain genuine 64-bit and 32-bit host layers plus dual-architecture Flatpak extensions for Freedesktop 23.08, 24.08, and 25.08. Scheduler tests, package-layout checks, and checksum validation run before publication.
