# Configuration guide

The default profile uses Fixed 2x Frame Generation with 90% Flow Scale, the full FG model, Ultra Performance off, and FP16 allowed. If Adaptive is enabled, it starts with a 90 FPS target, a 3x ceiling, Steady Base Cap, and Smooth Cadence. Scaling is off, with LS1 Quality, a 1.5x factor, 80% sharpness, and Quality Supersampling off saved for when it is enabled.

Test one change at a time and compare the game's V-Sync both on and off. Results vary with the game, display, VRR, and compositor.

Controls are grouped under **Frame Generation**, **Spatial Settings**, **Performance Settings**, **Advanced Rendering Settings**, compatibility, external tools, and manual overrides. While a game runs, **Live Status** reports the applied Frame Generation and Upscaling state, active model, resolutions, limits, fallbacks, and pending changes.

## Spatial Scaling

Select **Enable Scaling (Restart)** before the game starts. Frame Generation and Scaling can run independently or together. Gamescope/Game Mode is recommended because it provides a reliable display target; direct desktop scaling uses the configured factor without a proven display target, so the compositor may scale it again or MAKO may fall back safely to native presentation after recreation.

| Method | Behavior | Requirement |
| --- | --- | --- |
| **Native Resolution** | Model-free linear reconstruction | None |
| **MAKO Scaler** | Open single-pass scaling, anti-ringing, and sharpening | None |
| **LS1 Quality** | Full multi-pass neural model | Licensed `Lossless.dll` and matching `libvkd3d-shader.so.1` |
| **LS1 Performance** | Lower-cost LS1 model | Same as LS1 Quality |

- **Scaling Method:** Can be changed while the game runs. If LS1 cannot load its DLL, translator, resources, format, or processing path, that swapchain falls back to MAKO Scaler and records the reason in diagnostics.
- **Scale Factor:** Sets the output-to-input ratio from 1.0x to 2.0x; 1.5x is the default. Set Steam's Game Resolution to the display maximum, then choose a lower in-game resolution. While a supported game runs, MAKO limits the slider to the useful display ceiling without overwriting a higher saved value. The Renderer enforces its memory limit separately and reports any reduction in Live Status.
- **Quality Supersampling:** On a supported variable Gamescope surface, allows rendering beyond the proven display target before downsampling. It can improve quality but increases GPU and memory use. It does not change fixed-surface or direct non-Gamescope geometry.
- **Scaling Sharpness:** Runs from 0–100%, with an 80% default. MAKO Scaler applies bounded local sharpening; LS1 selects the nearest of five learned variants. It is hidden for Native Resolution.

Method and sharpness changes rebuild only MAKO's private scaler. Factor or supersampling changes apply without recreation when the effective extents stay the same; otherwise MAKO requests one guarded game-owned swapchain recreation when supported, or waits for a natural resolution change or restart. Unsupported surfaces stay native-sized, and memory-limited requests are reduced or rejected safely; Frame Generation remains available.

## Frame Generation

- **Frame Generation:** Enables or disables generated frames without discarding the selected Fixed or Adaptive settings. It normally applies live.
- **Fixed FPS Multiplier:** Selects 2x–5x generation. Start at 2x; higher values require more GPU and memory headroom. With Dynamic Cadence Recovery, it becomes a ceiling against confirmed Gamescope refresh.
- **Adaptive Frame Generation:** Varies generation toward the Target FPS without slowing a game already above target or exceeding the selected ceiling.
- **Fractional Adaptive:** Mixes generation ratios to retain more real frames, which may reduce latency and ghosting but can feel less smooth. It cannot be combined with Steady Base Cap; changing it also disables Dynamic Cadence Recovery.
- **Target FPS:** Selects 30–240 displayed FPS for Adaptive mode.
- **Steady Base Cap:** The default Adaptive mode. It starts with an even 2x cadence at half the target and may align a validated higher integer rung when Smooth Cadence is enabled. It is usually smoother but retains fewer real frames.
- **Maximum Adaptive Multiplier:** Selects a 2x–5x ceiling. Lower ceilings usually preserve quality; higher ceilings need more headroom.
- **Smooth Cadence:** Prefers a validated constant interpolation cadence. Disable it if the game feels more responsive without it.
- **Base FPS Cap:** Caps real application frames from Off to 120 FPS in MAKO Decky. It is unavailable while Frame Generation is off or Steady Base Cap owns the cap; changing it disables Dynamic Cadence Recovery.
- **Auto-disable Frame Generation by Refresh Rate:** Pauses generation at or below a 30–240 Hz Gamescope threshold and resumes it above the threshold. It does nothing without refresh feedback and never overrides the main Frame Generation switch.

Without confirmed Gamescope refresh, Fixed Dynamic Cadence Recovery and refresh-matched Smooth Cadence refinements are unavailable. Fixed keeps its selected multiplier, while Adaptive continues toward its configured target.

Most generation controls apply live. Flow Scale and Lighter FG Model use a 500 ms last-value-wins private-context replacement. A multiplier change that needs more generated-frame capacity uses the same replacement when the current WSI pool has enough headroom; otherwise it waits for recreation. MAKO keeps the previous context active until a replacement is ready, so the brief overlap can cause a one-time hitch or use extra memory.

## Runtime boundaries

| Setting | Runtime behavior |
| --- | --- |
| Enable Scaling | Game restart |
| Scaling Method | Live private-scaler rebuild |
| Scaling Sharpness | Live, debounced private-scaler rebuild |
| Scale Factor | Live when effective extents do not change; otherwise guarded game-owned or natural recreation |
| Quality Supersampling | Same effective-extent and recreation boundary as Scale Factor |
| Frame Generation, Fixed/Adaptive, target, Smooth Cadence, Base FPS Cap, refresh guard, and recovery | Live when startup resources are available |
| Fixed or Adaptive multiplier | Live within current capacity; otherwise private FG replacement or recreation |
| Flow Scale and Lighter FG Model | Live, debounced private FG replacement |
| Ultra Performance | Game restart |
| Lossless.dll Path, Allow FP16, and GPU | Game restart |
| Game Swapchain Images | Game restart |
| Disable MAKO Renderer on Next Launch | Game restart; remains selected until turned off |
| Gamescope WSI, MangoHud, vkBasalt, Steam Deck Mode, Zink, Force ALSA, and other launcher controls | Game restart |

A restart-bound change does not block unrelated live-safe changes. **Live Status** distinguishes saved values from applied values and reports pending restarts, scaler rebuilds, and recreations without transition pop-ups.

## Game and process profiles

Start a Steam game or shortcut, then select **Save profile for <game>** after gameplay loads. MAKO records its Steam app ID and safe Linux or Windows process names; selecting the action again updates the profile. Use **Matched Processes** only when a launcher or emulator needs another alias.

The profile dropdown chooses which profile Decky edits; it does not override runtime matching. During play, MAKO follows the matched profile or Default. Outside a game, the selected profile remains available for editing.

Renderer settings are stored in `conf.toml`; profile identity and launcher-only settings use versioned sidecars. Unknown keys are ignored and removed by the next canonical write. Scaling fields and **Game Swapchain Images** stay in Renderer configuration rather than becoming wrapper environment exports; the wrapper derives only the process-start layer chain from Scaling and launcher compatibility settings.

Decky coalesces rapid edits through one last-value-wins writer and flushes pending changes when the quick-access panel closes.

## Performance and quality

- **Ultra Performance (Restart):** Uses 75% Flow Scale, Lighter FG Model, FP16 where supported, active-policy resource allocation, and LS1 Performance when Scaling is enabled. It does not enable Scaling. Turning it off restores 90% Flow Scale, the full FG model, and FP16 allowed.
- **Flow Scale:** Controls Frame Generation motion-estimation resolution from 25–100%. Lower values reduce GPU work; higher values favour quality. Ultra Performance locks it to 75%.
- **Lighter FG Model:** Reduces GPU work at the cost of more visible artifacts. Ultra Performance locks it on.
- **Allow FP16 (Restart):** Global permission shared by every profile. It normally improves AMD performance; older NVIDIA GPUs may perform better with it disabled.
- **Lossless.dll Path (Restart):** Optional override for LS1 and LSFG discovery. Leave it empty for automatic Steam-library discovery.
- **GPU (Restart):** Optional GPU name, vendor/device ID, or PCI bus ID. Multi-GPU Frame Generation is unsupported.

## Compatibility and external tools

- **Dynamic Cadence Recovery:** For games and emulators that switch native rates, such as 30 FPS gameplay and 60 FPS menus. It periodically probes the real cadence and recalibrates Fixed or Adaptive behavior. Enabling it disables Steady Base Cap and Base FPS Cap; in Adaptive mode it selects Fractional behavior. The interval ranges from 0.1–3 seconds and defaults to 2 seconds.
- **Disable MAKO Renderer on Next Launch:** Prevents the complete Renderer from loading on launches while selected. Restart the game to compare, then turn the option off.
- **Gamescope WSI (Restart):** Optional per-profile compatibility path for coloured or pixelated motion artifacts in supported 64-bit FG-only host launches. Scaling enables and locks the same managed WSI requirement. MAKO stages only the validated manifest and library, supports direct 64-bit native Vulkan and Proton games plus prepared 64-bit Heroic and EmuDeck Flatpaks, and fails closed in Desktop Mode, mismatched nested Wayland sessions, unprepared Flatpaks, 32-bit WSI presentation, or HDR.
- **Game Swapchain Images (Restart):** Preserves the game's requested swapchain image minimum for titles that fail to start with MAKO's normal generated-output headroom. Generated frames may be skipped when the compositor has no spare image, so leave it off unless needed.
- **Disable Steam Deck Mode (Restart):** Unlocks hidden settings in some games.
- **Enable Zink for OpenGL Games (Restart):** Uses Vulkan-backed OpenGL and may help or destabilize individual games.
- **Force ALSA Audio (Restart):** Selects SDL ALSA and makes Wine/Proton prefer ALSA over PulseAudio. Disable it to restore normal audio defaults.

**Enable MangoHud (Restart)** and experimental **Enable vkBasalt (Restart)** are mutually exclusive per-profile controls under **External Tools**. Either can follow MAKO's Gamescope WSI/scaling chain and uses the tool's existing configuration. Current runtime evidence covers direct 64-bit native Vulkan or Proton launches on SteamOS; Flatpak and 32-bit compatibility remain separate boundaries. See [optional graphics integrations](../../engine/docs/LAYER-CHAINING.md).

## HDR and isolation

HDR Frame Generation and Scaling are unavailable in this release. **Disable HDR** remains enabled and read-only, and MAKO removes inherited `DXVK_HDR` activation.

Profiles without Scaling or explicit WSI use the isolated top-role manifest. Scaling uses the managed Frame Generation → Gamescope WSI → Spatial Scaling order, followed by a selected 64-bit MangoHud or vkBasalt layer on the host. Missing required WSI or spatial-layer dependencies fail closed to top-only MAKO with Scaling suppressed; an unavailable optional external tool is omitted.

Do not add unknown wrapper or HDR keys such as `enable_wsi` to `conf.toml`. Use the profile controls and leave the file writable so MAKO Decky can maintain it. For implementation details, see [Renderer configuration](../../engine/docs/CONFIGURATION.md), [runtime transitions](../../engine/docs/RUNTIME-TRANSITIONS.md), [spatial scaling](../../engine/docs/SCALING.md), [WSI isolation](../../engine/docs/WSI-ISOLATION.md), [optional graphics integrations](../../engine/docs/LAYER-CHAINING.md), [HDR](../../engine/docs/HDR-PIPELINE.md), and [troubleshooting](TROUBLESHOOTING.md).
