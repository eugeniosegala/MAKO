# Configuration guide

The default profile is a good starting point: Fixed **2x**, Flow Scale **0.90**,
Performance Mode off, and FP16 allowed where it is supported. Adaptive mode
defaults to a **90 FPS** target, 3x ceiling, and Smooth Cadence on.

Test one change at a time. Games, displays, VRR, and compositors differ; try
the game's V-Sync both on and off and keep the setting that feels smoother and
more responsive.

## Frame generation

- **Frame Generation (Live On/Off):** Turns synthesis on or off without losing
  the selected Fixed or Adaptive settings.
- **FPS Multiplier:** Fixed 2x, 3x, or 4x generation. Start at 2x for the
  best balance of image quality and latency.
- **Adaptive Frame Generation:** Varies the generated-frame count toward a
  target. It is a target, not a game FPS limiter: it cannot reduce a game
  already above target or exceed the selected ceiling.
- **Target FPS:** Desired Adaptive output rate, from 30 to 240 FPS in Decky.
- **Adaptive FPS Cap:** Automatically caps the game's real FPS at half the
  Adaptive target for steadier 2x frame generation. Turn it off to use the
  regular **Base FPS Cap** instead.
- **Maximum Adaptive Multiplier:** The 2x, 3x, or 4x Adaptive ceiling. 2x
  usually looks best; 4x can help reach a higher target at the cost of more
  generated frames.
- **Smooth Cadence:** Prefers a sustainable constant interpolation cadence. It
  can improve displayed motion but may reduce responsiveness. It is on by
  default; disable it if the game feels better with stricter target scheduling.
- **Base FPS Cap:** Caps real frames before generation. It applies live and is
  disabled while **Adaptive FPS Cap** controls the cap.

Adaptive target, ceiling, and cadence changes normally apply while a game is
running. Give the game a few seconds to settle before judging the result.
Changes that need a different GPU backend or larger private resources can wait
for a natural swapchain recreation; restarting the game applies them directly.

## Profiles and per-game selection

Choose **New Profile** to copy the current profile, then adjust it for a game.
Set **Active In** to the game's executable or process name to select that
profile automatically at launch. It accepts comma-separated names, including
Linux binaries and Windows `.exe` names.

The frame-generation, quality, GPU, and Active In settings follow automatic
profile matching. The DLL path and FP16 permission are global. Launcher
compatibility settings—such as **Disable MAKO Renderer on Next Launch**,
**Disable HDR**, Steam Deck Mode, and Zink—are selected before
MAKO sees the game's process, so choose their Decky profile manually before
launching when they differ between games.

## Quality and matching

- **Flow Scale:** 0.25–1.0. Lower values reduce GPU cost; higher values favour
  optical-flow quality.
- **Performance Mode:** Uses a lighter model with lower GPU overhead and more
  visible artifacts.
- **Allow FP16:** Usually improves performance on AMD. Disable it if an older
  NVIDIA GPU performs worse.
- **Lossless.dll Path:** Overrides automatic discovery. Leave it empty for
  normal Steam-library discovery.
- **GPU:** Optional GPU name, vendor/device ID, or PCI bus ID. It must identify
  the GPU used by the game; dual-GPU frame generation is not supported.

## Compatibility and HDR

The package includes 64-bit and 32-bit Vulkan layers. Vulkan chooses the right
layer for the game process; the CLI and configuration UI are 64-bit only.

- **Disable MAKO Renderer on Next Launch:** Troubleshooting control that stops
  the layer loading after restart. Use **Frame Generation** for a live on/off
  test instead.
- **Steam Deck Mode:** Per-game compatibility path.
- **Zink:** Vulkan-based OpenGL path for OpenGL games.

HDR frame generation is unavailable in this release. **Disable HDR (Restart)**
is intentionally checked and read-only: MAKO Decky keeps the
renderer on its validated SDR path without changing the game's normal DXVK or
Gamescope policy. Do not add HDR environment variables manually for ordinary
launches. Use **Disable MAKO Renderer on Next Launch** if the layer itself is
the suspected problem.

See [Troubleshooting](TROUBLESHOOTING.md) for diagnostics and update recovery.
