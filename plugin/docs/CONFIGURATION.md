# Configuration guide

The default profile is a good starting point: Fixed **2x**, Flow Scale **0.90**, Performance Mode off, and FP16 allowed where it is supported. Adaptive mode defaults to a **90 FPS** target, 3x ceiling, and Smooth Cadence on.

Test one change at a time. Games, displays, VRR, and compositors differ; try the game's V-Sync both on and off and keep the setting that feels smoother and more responsive.

## Frame generation

- **Frame Generation (Live On/Off):** Turns synthesis on or off without losing the selected Fixed or Adaptive settings.
- **FPS Multiplier:** Fixed 2x, 3x, or 4x generation. Start at 2x for the best balance of image quality and latency.
- **Adaptive Frame Generation:** Varies the generated-frame count toward a target. It is a target, not a game FPS limiter: it cannot reduce a game already above target or exceed the selected ceiling.
- **Target FPS:** Desired Adaptive output rate, from 30 to 240 FPS in Decky.
- **Adaptive FPS Cap:** Automatically caps the game's real FPS at half the Adaptive target for steadier 2x frame generation. Turn it off to use the regular **Base FPS Cap** instead.
- **Maximum Adaptive Multiplier:** The 2x, 3x, or 4x Adaptive ceiling. 2x usually looks best; 4x can help reach a higher target at the cost of more generated frames.
- **Smooth Cadence:** Prefers a sustainable constant interpolation cadence. It can improve displayed motion but may reduce responsiveness. It is on by default; disable it if the game feels better with stricter target scheduling.
- **Base FPS Cap:** Caps real frames before generation. It applies live and is disabled while **Adaptive FPS Cap** controls the cap.

Adaptive target, ceiling, and cadence changes normally apply while a game is running. Give the game a few seconds to settle before judging the result. Changes that need a different GPU backend or larger private resources can wait for a natural swapchain recreation; restarting the game applies them directly.

## Game / Process Profiles

For a Steam game or shortcut, start the game and choose **Save profile for &lt;game&gt;** in MAKO Decky after gameplay has loaded. The plugin records only processes carrying that running Steam app ID, saves their executable names under **Matched Processes**, and keeps the profile for later launches. Choosing the same action again updates the existing profile instead of creating a duplicate.

The saved Steam app ID selects launcher compatibility settings before the game starts. The captured process names select the same renderer profile once its Vulkan process loads. Unrecognised Steam games use the Default profile, so a setting saved for one game does not leak into another game or plugin.

For launchers and emulators, start the title from its Steam or Game Mode shortcut and use the same running-game capture action. Profile creation is not available while no game is running: MAKO relies on process discovery instead of asking you to guess an executable name. After capture, edit **Matched Processes** only if a launcher or emulator needs an additional process alias. Linux binary names and Windows `.exe` names are supported.

Frame-generation, quality, GPU, and matched-process settings belong to the selected profile. Launcher compatibility settings, including **Disable MAKO Renderer on Next Launch**, **Disable HDR**, Steam Deck Mode, Zink, and ALSA, are also stored per profile. The DLL path and FP16 permission remain global.

The Decky dropdown is an editor selection, not a runtime override. Outside a game, choose any saved profile and it remains available for editing without affecting another game's launch. When a live game is detected, MAKO follows its matching profile, or Default if no match exists. On game exit, the runtime and editor return to Default once; after the controls unlock, an offline selection stays in place until another game starts or the plugin is reopened.

### Upgrade and legacy-option safety

MAKO Decky treats the current shared configuration schema as an allowlist. An unknown or retired key in a manually copied Renderer profile is ignored, and it cannot become a launch-wrapper environment variable. Reading the profile alone does not change its file; the next Decky save or installation merge rewrites it in canonical form and removes unknown keys.

Settings that still carry useful meaning are migrated explicitly before their old representation is removed. This is safer than preserving arbitrary legacy variables indefinitely, because a stale option cannot silently reactivate or acquire a different meaning later. Profiles that still use a supported format remain safe to open in an older release, but saving them there can discard settings that older MAKO does not understand.

## Quality and matching

- **Flow Scale:** 0.25–1.0. Lower values reduce GPU cost; higher values favour optical-flow quality.
- **Performance Mode:** Uses a lighter model with lower GPU overhead and more visible artifacts.
- **Allow FP16:** Usually improves performance on AMD. Disable it if an older NVIDIA GPU performs worse.
- **Lossless.dll Path:** Overrides automatic discovery. Leave it empty for normal Steam-library discovery.
- **GPU:** Optional GPU name, vendor/device ID, or PCI bus ID. It must identify the GPU used by the game; dual-GPU frame generation is not supported.

## Compatibility and HDR

The package includes 64-bit and 32-bit Vulkan layers. Vulkan chooses the right layer for the game process; the CLI and configuration UI are 64-bit only.

- **Disable MAKO Renderer on Next Launch:** Troubleshooting control that stops the layer loading after restart. Use **Frame Generation** for a live on/off test instead.
- **Steam Deck Mode:** Per-game compatibility path.
- **Zink:** Vulkan-based OpenGL path for OpenGL games.
- **Force ALSA Audio (Restart):** Forces the native SDL ALSA driver, disables Wine/Proton's Pulse driver, and enables its built-in ALSA driver for the selected profile. This may improve compatibility with modes such as Zink and reduce audio stuttering or sudden loud sounds. Leave it disabled by default. Turning it off removes MAKO's audio override entirely and restores normal Steam/Proton behaviour; restart the game after changing it.

HDR frame generation is unavailable in this release. **Disable HDR (Restart)** is intentionally checked and read-only: MAKO Decky selects the Renderer's validated SDR lane, disables HDR exposure, and removes inherited `DXVK_HDR` activation. MAKO-managed launches use v2's proven private MAKO manifest directory and disable Gamescope WSI for the game process so the Renderer owns the swapchain and a single presentation clock. Steam's Vulkan Fossilize/overlay hooks and system-wide implicit presentation layers are excluded from that application chain; Gamescope and the Steam/Game Mode interface remain active outside it. Do not add HDR environment variables manually for ordinary launches. Use **Disable MAKO Renderer on Next Launch** if the layer itself is the suspected problem. The underlying contracts are documented in the Renderer guides for [WSI isolation](../../engine/docs/WSI-ISOLATION.md) and the [HDR pipeline](../../engine/docs/HDR-PIPELINE.md).

See [Troubleshooting](TROUBLESHOOTING.md) for diagnostics and update recovery.
