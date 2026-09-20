# Configuration guide

The default profile uses Fixed 2x Frame Generation with 80% Flow Scale, the full FG model, Ultra Performance off, FP16 allowed, and Smooth Cadence. If Adaptive is enabled, it starts with a 90 FPS target, a 3x ceiling, and Steady Base Cap. Scaling is off, with LS1 Quality, a 1.5x factor, 80% sharpness, and Quality Supersampling off saved for when it is enabled.

Test one change at a time and compare the game's V-Sync both on and off. Neither setting is universally best; keep whichever option feels smoother and more responsive for that game and display setup.

Controls are grouped under **Frame Generation**, **Spatial Settings**, **Performance Settings**, **Advanced Rendering Settings**, compatibility, external tools, and manual overrides. While a game runs, **Live Status** reports the applied Frame Generation and Upscaling state, active model, resolutions, limits, fallbacks, pending changes, and a temporary Frame Generation suspension while a Steam menu is open.

Press **R1** or tap the small **Hide info** ribbon at the bottom right for a controls-only panel. It hides tutorials, descriptions, tips, optional warnings, and badges, leaving settings-section headings, option names, current settings, and action buttons. The Lossless Scaling and MAKO Renderer installation status card, version number, and release codename remain visible, as does the complete Live Status card while a game runs. Any Lossless Scaling model warning, its troubleshooting guidance, and its update action also remain visible. Press **R1** again or select **Show info** to restore other information. The selected control keeps focus as the panel changes height, with its screen position preserved where the scroll range allows. If the focused help button disappears, focus moves to the next visible, enabled control at the same screen position, or the previous control when none follows. The ribbon receives focus only when no other controls remain. MAKO Decky remembers this display preference on the device across panel openings; it does not change game profiles or individual section-collapse choices. Dialogs retain their instructions and confirmations.

One warning above the controls lists confirmed LS1 and LSFG availability-check failures for the enabled features. Both can appear as bullets in the same warning, with one shared description and update action. Active LS1 fallback replaces the generic LS1 failure bullet. This banner stays hidden when Lossless Scaling is absent or the selected DLL cannot be found; the installation status reports its absence separately. **Check for MAKO Decky updates** opens the release page; it does not install anything or change the profile. Apply a MAKO Renderer update when offered and restart the game. An update is a troubleshooting step, not a guarantee: if the warning persists, verify Lossless Scaling and collect diagnostics. Unavailable or older inspectors produce an unknown result and do not trigger a model-failure warning.

The model checks run outside Decky's event loop after a 500 ms debounce and refresh every 30 seconds while applicable controls are mounted. Each family has one process-local cache entry lasting at most five minutes, invalidated by DLL or inspector replacement. LS1 checks the selected effective method and sharpness; LSFG checks both runtime modes in FP32 and, when FP16 is allowed (including Ultra Performance), FP16 as well. LSFG preflight is independent of LS1 and its translator and does not create Vulkan pipelines. A warning about one permitted precision does not prove the running GPU selected it; Live Status remains authoritative for active behavior. Disabled features are not probed, and checks never write profiles.

## Spatial Scaling

Select **Enable Scaling (Restart)** before the game starts. Frame Generation and Scaling can run independently or together. Gamescope/Game Mode is recommended because it provides a reliable display target; direct desktop scaling uses the configured factor without a proven display target, so the compositor may scale it again or MAKO may fall back safely to native presentation after recreation. On a variable desktop surface, lower the resolution in the game first: raising Scale Factor enlarges MAKO's output and can increase GPU cost. Quality Supersampling off only enforces a proven Gamescope output limit; it does not infer a desktop monitor cap. See [desktop scaling and resolution](../../engine/docs/CONFIGURATION.md#desktop-scaling-and-resolution).

| Method | Behavior | Requirement |
| --- | --- | --- |
| **Native Resolution** | Model-free linear reconstruction | None |
| **MAKO Scaler** | Open single-pass scaling, anti-ringing, and sharpening | None |
| **LS1 Quality** | Full multi-pass neural model | Licensed `Lossless.dll` and matching `libvkd3d-shader.so.1` |
| **LS1 Performance** | Lower-cost LS1 model | Same as LS1 Quality |

- **Scaling Method:** Can be changed while the game runs. If LS1 cannot load its DLL, translator, resources, format, or processing path, that swapchain falls back to MAKO Scaler and records the reason in diagnostics.
- **Scale Factor:** Sets the output-to-input ratio from 1.0x to 2.0x; 1.5x is the default. Set Steam's Game Resolution to the display maximum, then choose a lower in-game resolution. Use a display mode where Live Status shows **Input** smaller than **Display**; some games require Windowed mode because fullscreen or borderless keeps a display-sized input. While a supported game runs, MAKO limits the slider to the useful display ceiling without overwriting a higher saved value. The Renderer enforces its memory limit separately and reports any reduction in Live Status.
- **Quality Supersampling:** On a supported variable Gamescope surface, allows rendering beyond the proven display target before downsampling. It can improve quality but increases GPU and memory use. It does not change fixed-surface or direct non-Gamescope geometry.
- **Scaling Sharpness:** Runs from 0–100%, with an 80% default. MAKO Scaler applies bounded local sharpening; LS1 selects the nearest of five learned variants. It is hidden for Native Resolution.

Method and sharpness changes rebuild only MAKO's private scaler. Factor or supersampling changes apply without recreation when the effective extents stay the same; otherwise MAKO requests one guarded game-owned swapchain recreation when supported, or waits for a natural resolution change or restart. Unsupported surfaces stay native-sized, and memory-limited requests are reduced or rejected safely; Frame Generation remains available. An unavailable LS1 model never rewrites a saved profile. MAKO Scaler takes over when LS1 setup fails, while the dropdown retains the selected LS1 method and explains the fallback. MAKO Decky checks only that method's selected sharpness variant through the installed Renderer's GPU-independent loader, including Ultra Performance's effective LS1 Performance choice. A missing or older inspector leaves availability unknown. The warning below Scaling Method appears only for a failed availability check or a live fallback; selecting LS1 alone shows no fallback notice. This host check cannot prove a game's GPU or Flatpak runtime; live Renderer status takes precedence and identifies when MAKO Scaler is actually active. After repairing or updating Lossless Scaling, restart the game or rebuild its private scaler by changing the method; an existing fallback does not continuously retry LS1 in the frame loop.

## Frame Generation

- **Frame Generation:** Enables or disables generated frames without discarding the selected Fixed or Adaptive settings. It normally applies live.
- **Fixed FPS Multiplier:** Selects 2x–5x generation. Start at 2x; higher values require more GPU and memory headroom. With Smooth Cadence and ordered Gamescope presentation, MAKO can pace a proven stable source to the display divided by this multiplier for even output; disable Smooth Cadence to retain every real frame. With Dynamic Cadence Recovery, the multiplier becomes a ceiling against confirmed Gamescope refresh.
- **Adaptive Frame Generation:** Varies generation toward the Target FPS without slowing a game already above target or exceeding the selected ceiling.
- **Fractional Adaptive:** Mixes generation ratios to retain more real frames, which may reduce latency and ghosting but can feel less smooth. It cannot be combined with Steady Base Cap; changing it also disables Dynamic Cadence Recovery.
- **Target FPS:** Selects 30–240 displayed FPS for Adaptive mode.
- **Steady Base Cap:** The default Adaptive mode. It starts with an even 2x cadence at half the target and may align a validated higher integer rung when Smooth Cadence is enabled. It is usually smoother but retains fewer real frames.
- **Maximum Adaptive Multiplier:** Selects a 2x–5x ceiling. Lower ceilings usually preserve quality; higher ceilings need more headroom.
- **Smooth Cadence:** Prefers a validated constant interpolation cadence. In Fractional Adaptive it stabilizes a validated generated-frame plan without imposing a real-frame cap. In Fixed mode, it can pace a stable source to the selected display/multiplier rung; with Steady Base Cap, it can align a validated higher Adaptive rung. It never overrides an explicit Base FPS Cap, Dynamic Cadence Recovery, transport recovery, or insufficient generated-output capacity. Disable it if the game feels more responsive without it.
- **Base FPS Cap:** Caps real application frames from Off to 120 FPS in MAKO Decky. It is unavailable while Frame Generation is off or Steady Base Cap owns the cap; changing it disables Dynamic Cadence Recovery.
- **Auto-disable Frame Generation by Refresh Rate:** Pauses generation at or below a 30–240 Hz Gamescope threshold and resumes it above the threshold. It does nothing without refresh feedback and never overrides the main Frame Generation switch.

Without confirmed ordered Gamescope refresh, Fixed Dynamic Cadence Recovery and refresh-matched Smooth Cadence refinements are unavailable. Fixed keeps its selected multiplier, while Adaptive continues toward its configured target.

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

A restart-bound change does not block unrelated live-safe changes. **Live Status** distinguishes saved values from applied values and reports pending restarts, scaler rebuilds, and recreations without transition pop-ups. The displayed Target is the configured Adaptive target, not a measurement of delivered FPS.

## Game and process profiles

Start a Steam game or shortcut, then select **Save profile for <game>** after gameplay loads. MAKO records its Steam app ID and safe Linux or Windows process names; selecting the action again updates the profile. Use **Matched Processes** only when a launcher or emulator needs another alias.

Ubisoft Connect's own launcher and web UI executables are excluded from profile capture, including their truncated Linux process names. MAKO Renderer also leaves those exact executables inactive even when they inherit a game profile or an older profile contains their names. The child game retains MAKO's launch environment and matches normally; no separate Flatpak setup or Ubisoft wrapper is required for a Steam/Proton launch.

Both components use the shared [launcher exclusion registry](../../engine/mako-common/launcher_exclusions.json), which records the reason and exact executables for each launcher. See [maintaining launcher exclusions](../../engine/docs/CONFIGURATION.md#profiles) for adding entries and regenerating the component bindings.

The profile dropdown chooses which profile Decky edits; it does not override runtime matching. During play, MAKO follows the matched profile or Default. Outside a game, the selected profile remains available for editing.

Renderer settings are stored in `conf.toml`; profile identity and launcher-only settings use versioned sidecars. Unknown keys are ignored and removed by the next canonical write. Scaling fields and **Game Swapchain Images** stay in Renderer configuration rather than becoming wrapper environment exports; the wrapper derives only the process-start layer chain from Scaling and launcher compatibility settings.

Decky sends typed field patches through one last-value-wins writer with one backend update in flight. Ordinary edits use a 250 ms trailing window, while Base FPS Cap changes use one second so a slider drag does not apply transient low caps to a running game. The writer preserves the profile selected for each edit and flushes pending changes when the quick-access panel closes, preventing rapid controls or profile changes from creating stale write queues.

## Performance and quality

- **Ultra Performance (Restart):** Uses 70% Flow Scale, Lighter FG Model, FP16 where supported, active-policy resource allocation, and LS1 Performance when Scaling is enabled. It does not enable Scaling. Turning it off restores 80% Flow Scale, the full FG model, and FP16 allowed.
- **Flow Scale:** Controls Frame Generation motion-estimation resolution from 25–100%. Lower values reduce GPU work; higher values favour quality. Ultra Performance locks it to 70%.
- **Lighter FG Model:** Reduces GPU work at the cost of more visible artifacts. Ultra Performance locks it on.
- **Allow FP16 (Restart):** Global permission shared by every profile. It normally improves AMD performance; older NVIDIA GPUs may perform better with it disabled.
- **Lossless.dll Path (Restart):** Optional override for LS1 and LSFG discovery. Leave it empty for automatic Steam-library discovery.
- **GPU (Restart):** Optional GPU name, vendor/device ID, or PCI bus ID. Multi-GPU Frame Generation is unsupported.

## Compatibility and external tools

- **Dynamic Cadence Recovery:** For games and emulators that switch native rates, such as 30 FPS gameplay and 60 FPS menus. It periodically probes the real cadence and recalibrates Fixed or Adaptive behavior. Enabling it disables Steady Base Cap and Base FPS Cap; in Adaptive mode it selects Fractional behavior. The interval ranges from 0.1–3 seconds and defaults to 2 seconds.
- **Disable MAKO Renderer on Next Launch:** Prevents the complete Renderer from loading on launches while selected. Restart the game to compare, then turn the option off.
- **Gamescope WSI (Restart):** Optional per-profile compatibility path for coloured or pixelated motion artifacts in supported 64-bit launches, with Scaling, Frame Generation, or both. The option remains independent and editable while Scaling is enabled. With WSI off, the combined Renderer handles scaling and Frame Generation; with both options on, MAKO uses the ordered three-role compatibility chain. MAKO stages only the validated manifest and library, supports direct 64-bit native Vulkan and Proton games plus prepared 64-bit Heroic and EmuDeck Flatpaks, and fails closed in Desktop Mode, mismatched nested Wayland sessions, unprepared Flatpaks, 32-bit WSI presentation, or HDR.
- **Game Swapchain Images (Restart):** Preserves the game's requested swapchain image minimum for titles that fail to start with MAKO's normal generated-output headroom. Generated frames may be skipped when the compositor has no spare image, so leave it off unless needed.
- **Disable Steam Deck Mode (Restart):** Unlocks hidden settings in some games.
- **Enable Zink for OpenGL Games (Restart):** Uses Vulkan-backed OpenGL and may help or destabilize individual games.
- **Force ALSA Audio (Restart):** Selects SDL ALSA and makes Wine/Proton prefer ALSA over PulseAudio. Disable it to restore normal audio defaults.

**Enable MangoHud (Restart)** and **Enable vkBasalt (Restart)** are mutually exclusive per-profile controls under **External Tools**. MangoHud displays performance statistics; vkBasalt applies configured effects such as sharpening, anti-aliasing, and color adjustments. Both require a separate host installation and use the tool's existing configuration. Install the library matching the game's Vulkan process architecture. Either tool can follow MAKO's Gamescope WSI/scaling chain on supported 64-bit launches; 32-bit vkBasalt testing uses Gamescope WSI off. These controls do not enable host external layers inside Flatpak games. See [optional graphics integrations](../../engine/docs/LAYER-CHAINING.md) for setup and game-testing evidence, including [vkBasalt's official documentation and MAKO-specific requirements](../../engine/docs/LAYER-CHAINING.md#vkbasalt-with-mako-decky).

## HDR and isolation

HDR Frame Generation and Scaling are unavailable in this release. **Disable HDR** remains enabled and read-only, and MAKO removes inherited `DXVK_HDR` activation.

Profiles with Gamescope WSI off use the isolated combined Renderer for Scaling and Frame Generation. With WSI on, Scaling uses the managed Renderer → Gamescope WSI → Spatial Scaling order; FG-only profiles omit the lower spatial role. A selected 64-bit MangoHud or vkBasalt layer follows the managed chain on the host. Missing WSI or spatial-layer dependencies leave WSI disabled and select the combined Renderer, whose surface checks may keep scaling native; an unavailable optional external tool is omitted. Scaling never changes the saved WSI choice. Upgrades regenerate wrappers to honor that choice, so a profile that previously received WSI only because Scaling was enabled now uses the combined path unless WSI was explicitly saved as on. Restart the game after changing either option, and check Live Status for active scaling and the actual source/output sizes.

Do not add unknown wrapper or HDR keys such as `enable_wsi` to `conf.toml`. Use the profile controls and leave the file writable so MAKO Decky can maintain it. For implementation details, see [Renderer configuration](../../engine/docs/CONFIGURATION.md), [runtime transitions](../../engine/docs/RUNTIME-TRANSITIONS.md), [spatial scaling](../../engine/docs/SCALING.md), [WSI isolation](../../engine/docs/WSI-ISOLATION.md), [optional graphics integrations](../../engine/docs/LAYER-CHAINING.md), [HDR](../../engine/docs/HDR-PIPELINE.md), and [troubleshooting](TROUBLESHOOTING.md).
