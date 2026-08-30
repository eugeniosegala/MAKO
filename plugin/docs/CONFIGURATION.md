# Configuration guide

The default profile starts with Scaling off and **Native Resolution** selected. Its saved scaling values are 2.0x and 90% sharpness. Frame Generation defaults to Fixed 2x, 0.90 Flow Scale, the full model, Ultra Performance off, and FP16 allowed. Adaptive defaults to Steady 2x, a 90 FPS target, 3x ceiling, and Smooth Cadence on; Fractional Adaptive is opt-in.

Test one change at a time. Games, displays, VRR, and compositors differ, so compare the game's V-Sync both on and off.

## Spatial Scaling

Select **Enable Scaling (Restart)** before starting the game, choose an in-game resolution below the display resolution, then switch methods while playing. Frame Generation and Scaling are independent and can run alone or together.

| Method | Behavior | Requirement |
| --- | --- | --- |
| **Native Resolution** | Model-free linear reconstruction using transfer images; no compute model or licensed resource | None |
| **MAKO Scaler** | Repository-owned single-pass reconstruction, anti-ringing, and sharpening | No licensed spatial model |
| **LS1 Quality** | Full proprietary multi-pass neural graph | Licensed `Lossless.dll` and architecture-matched vkd3d-shader translator |
| **LS1 Performance** | Lower-cost proprietary graph | Same as LS1 Quality |

- **Enable Scaling (Restart):** Adds MAKO frame generation, Gamescope WSI, and MAKO spatial scaling roles at process start when the game is running inside the active Gamescope session. Changing the toggle is saved immediately but cannot change the Vulkan layer chain of a running game. Outside that session, including Desktop Mode, the wrapper keeps Gamescope WSI and the spatial role disabled and writes a one-line fallback record to stderr instead of allowing Gamescope WSI to show a modal hooking-error dialog. Once enabled at startup, the method remains live. Leave it off when scaling is not needed: scaling is then fully disabled, the path is not provisioned, and it adds no scaling-path overhead.
- **Scaling Method:** Selects Native Resolution, MAKO Scaler, LS1 Quality, or LS1 Performance. Method changes rebuild only MAKO's private scaler. LS1 falls back to MAKO Scaler for that swapchain when its DLL, translator, resources, format, or pipeline is unavailable; diagnostics record the requested method, active method, and reason. To use scaling, first set Steam's Game Resolution to the display's maximum resolution: 1280 × 800 on Steam Deck, or 3840 × 2160 / 4K on Steam Machine. Then choose a lower in-game resolution, such as 480p or 720p, and use a Scale Factor to enlarge it; 2x is a good starting point. This reduces the resolution the game renders, then scales it back to the display resolution. It can substantially improve performance, with an image-quality trade-off. Alternatively, if the display supports it, use MAKO to scale from 2K to 4K.
- **Scale Factor:** Sets the output-to-input dimension ratio from 1.0x to 2.0x (default 2.0x). After the control settles, MAKO asks the game for one guarded swapchain recreation on the managed Gamescope path. If the same saved profile also changes Fixed or Adaptive policy, multiplier, target, cap, or generated capacity, those fields continue through their ordinary live/private boundary and the replacement consumes the latest profile. If retirement proof is unavailable, only the new factor waits for the next natural resolution change or restart.
- **Scaling Sharpness:** Uses a 0–100% multiplier (default 90%). MAKO applies it to its bounded local-sharpening baseline; LS1 selects the nearest of five learned variants. Changes rebuild the private scaler after controls settle.

Native Resolution uses transfer images but no MAKO compute model or licensed spatial resource; the process still carries the provisioned scaling/WSI path so another method can be selected live. Method and sharpness changes never recreate the game swapchain. In MAKO Decky's managed Gamescope lane, a settled Scale Factor edit requests one game-owned recreation only after the lower spatial role has attached maintenance1 retirement proof to a successful present; MAKO does not destroy the game's swapchain. Flow Scale, Lighter FG Model, and generated-capacity changes retain their old applied value while MAKO prepares a complete private FG replacement, then switch atomically without recreating the game swapchain.

Use MAKO's fixed-extent Gamescope/X11 path for a stable source/presentation split. A variable Wayland compositor may echo the enlarged output extent during recreation; MAKO then stays native-sized to prevent recursive scaling. Requests beyond the conservative device-memory envelope also stay native and report `inactive_reason=variable-surface-memory-budget`.

## Frame Generation

- **Frame Generation:** Enables or disables synthesis without discarding Fixed or Adaptive settings. The switch normally applies live.
- **FPS Multiplier:** Fixed 2x–5x generation. Start at 2x; higher ratios require more GPU and memory headroom. With Dynamic Cadence Recovery, the selected ratio becomes a ceiling against confirmed Gamescope refresh.
- **Adaptive Frame Generation:** Varies generation toward the Target FPS without slowing a game already above target or exceeding the selected ceiling.
- **Fractional Adaptive:** Mixes ratios over time to retain more real frames. This can reduce input latency and ghosting, but uneven spacing can feel choppy. Enabling it also enables Frame Generation and Adaptive, disables Steady Base Cap, and turns off Dynamic Cadence Recovery when changed directly.
- **Target FPS:** Desired displayed rate from 30–240 FPS. Fractional mixes ratios toward it; Steady caps real FPS at half the target.
- **Steady Base Cap:** Default Adaptive mode. It uses an even 2x cadence while the game sustains half the target. This is usually smoother but has fewer real frames and can increase latency or ghosting.
- **Maximum Adaptive Multiplier:** 2x–5x ceiling. Lower ceilings usually preserve quality; 5x is intended for high-refresh displays with substantial headroom.
- **Smooth Cadence:** Tests a validated constant interpolation cadence when Target FPS matches Gamescope refresh. It rolls back when output cannot remain near target or recovery begins.
- **Base FPS Cap:** Caps real application frames before generation. It is disabled while Steady Base Cap owns the cap.
- **Auto-disable Frame Generation by Refresh Rate:** Pauses generation at or below a selected 30–240 Hz Gamescope threshold and resumes it above the threshold. It does nothing without refresh feedback and never enables a profile whose main switch is off.

Frame Generation, Fixed/Adaptive mode, multiplier, target, refresh guard, and mode-independent recovery changes normally apply while a game runs. Matched processes reserve the interop, backend, images, and synchronization needed for Off→On; while off, MAKO schedules, copies, and presents no generated frames. If startup resources were unavailable, enabling remains pending for restart. Capacity growth, Flow Scale, and Lighter FG Model use a 500 ms last-value-wins private FG replacement: MAKO prepares the new images, timeline, and backend context, briefly presents only real frames while old private work drains, then atomically switches and warms history. The temporary allocation can cause a brief one-time hitch and uses additional memory until handoff; failure keeps the previous context active and retries.

## Runtime boundaries

| Setting | Runtime behavior |
| --- | --- |
| Enable Scaling | Game restart |
| Scaling Method | Live private scaler rebuild |
| Scaling Sharpness | Live, debounced private scaler rebuild |
| Scale Factor | One guarded game-owned recreation after controls settle; natural resolution change or restart when retirement proof is unavailable |
| Frame Generation, Fixed/Adaptive, target, multiplier, refresh guard, recovery | Live when startup resources are available; generated-capacity growth follows the next row |
| Flow Scale, Lighter FG Model, generated capacity | Live, debounced private FG replacement with atomic handoff and rollback-safe retry |
| Ultra Performance | Game restart |
| Lossless.dll Path, Allow FP16, GPU selection | Game restart |
| Gamescope WSI compatibility, MangoHud, vkBasalt, Steam Deck Mode, Zink, Force ALSA, and other launcher compatibility | Game restart |

A restart-bound or private-resource edit does not block unrelated compatible controls: MAKO applies the safe subset while the remaining boundary stays pending. Renderer-owned requested-versus-applied records remain available to diagnostics, but MAKO Decky does not poll or show a transition pop-up; a saved profile is not itself proof that every field is active.

## Game and process profiles

Start a Steam game or shortcut, then choose **Save profile for <game>** after gameplay loads. MAKO records the Steam app ID and safe game-process names; repeating the action updates the profile. Linux binary and Windows `.exe` names are supported. Launchers and emulators use the same capture flow. Edit **Matched Processes** only when a launcher needs another alias.

The profile dropdown selects what Decky edits; it is not a runtime override. During a game, MAKO follows the matching profile or Default. Outside a game, an offline selection remains available for editing until another game starts.

Renderer fields live in `conf.toml`; profile identity and launcher-only compatibility settings live in versioned sidecars. `profile_storage.py` owns sidecar normalization, `configuration.py` owns transactions and regeneration, and `wrapper_generation.py` converts normalized inputs into disposable wrapper text. Unknown profile keys are inert and removed by the next canonical write.

`scaling_enabled`, `scaling_method`, `scaling_factor`, and `scaling_sharpness` never become wrapper environment exports. The method values are `native`, `mako`, `ls1`, and `ls1-performance`. The wrapper derives only the process-start layer chain from Scaling and the independent `gamescope_wsi_compatibility` setting. A missing dependency fails closed to top-only MAKO with scaling suppressed.

Decky sends typed field patches through one 250 ms, last-value-wins writer with one backend update in flight. It preserves the profile selected for each edit and flushes pending changes when the quick-access panel closes, preventing rapid controls or profile changes from creating stale write queues.

## Performance and quality

- **Ultra Performance (Restart):** Per-profile preset that uses 75% Flow Scale, Lighter FG Model, FP16 where supported, and active-policy resource allocation. It can improve frame-generation performance by up to 30% in favorable GPU-limited cases but increases artifacts. Other compatible settings remain available after startup.
- **Flow Scale:** Frame Generation only, from 0.25–1.0. Lower values reduce GPU work; higher values favor optical-flow quality. Ultra Performance locks it to 0.75 and otherwise restores 0.90.
- **Lighter FG Model:** Selects a lower-cost model with more visible artifacts. Ultra Performance locks it on.
- **Allow FP16 (Restart):** Global permission used by every profile and applied at the next game start. It normally improves AMD performance; older NVIDIA GPUs may perform better with it disabled. Ultra Performance forces effective permission on supported hardware.
- **Lossless.dll Path (Restart):** Optional process-start override for LS1 and LSFG discovery. Leave empty for normal Steam-library discovery; restart the game after changing it.
- **GPU (Restart):** Optional name, vendor/device ID, or PCI bus ID for the GPU used by the game. Dual-GPU frame generation is unsupported.

## Compatibility and external tools

- **Dynamic Cadence Recovery:** Opt-in for games and emulators that change native rate, such as 30 FPS gameplay and 60 FPS menus. It periodically exposes the real cadence and recalibrates Fixed or Adaptive behavior. Enabling it disables Steady Base Cap and Base FPS Cap; in Adaptive mode it enables Fractional. The 0.1–3 second interval defaults to 2 seconds, with shorter intervals reacting faster but probing more often.
- **Disable MAKO Renderer on Next Launch:** One-launch troubleshooting bypass for the complete layer.
- **Gamescope WSI (Restart):** Adds the guarded MAKO → Gamescope WSI path for FG-only native profiles inside the active Gamescope session. Scaling requires and locks its validated managed WSI path automatically; independently enabled FG-only compatibility is limited to supported 64-bit host launches. It fails closed for missing host manifests, Flatpak runtimes, Desktop Mode, and nested Wayland sessions whose `WAYLAND_DISPLAY` does not match `GAMESCOPE_WAYLAND_DISPLAY`; those session fallbacks are logged without opening an interactive error dialog.
- **Disable Steam Deck Mode (Restart):** Per-game launch compatibility path; changes apply at the next game start.
- **Enable Zink for OpenGL Games (Restart):** Vulkan-based OpenGL launch path; changes apply at the next game start.
- **Force ALSA Audio (Restart):** Replaces MAKO's Pulse/Wine audio path with SDL ALSA for the selected profile. Disable it to restore normal defaults.

**Enable MangoHud (Restart)** and **Enable vkBasalt (Restart)** are mutually exclusive per-profile controls under **External Tools**. Either can follow MAKO's Gamescope WSI/scaling chain; appearance remains controlled by the tool's own configuration. Current runtime evidence covers 64-bit native Vulkan or Proton launched directly by Steam on SteamOS. Flatpak and 32-bit compatibility remain separate validation boundaries. See [optional graphics integrations](../../engine/docs/LAYER-CHAINING.md).

## HDR and isolation

HDR frame generation and scaling are unavailable in this release. **Disable HDR** remains checked and read-only; MAKO removes inherited `DXVK_HDR` activation. Profiles without Scaling or explicit WSI use the isolated top-role manifest. Scaling uses an explicitly ordered frame-generation → WSI → spatial-scaling instance-layer chain even at Native Resolution, followed by the selected 64-bit MangoHud or vkBasalt role. Flatpak or missing-manifest cases fail closed.

Do not add unknown wrapper or HDR keys such as `enable_wsi` to `conf.toml`. Use the profile controls and leave the file writable so MAKO Decky can maintain it. See [WSI isolation](../../engine/docs/WSI-ISOLATION.md), [optional graphics integrations](../../engine/docs/LAYER-CHAINING.md), [HDR pipeline](../../engine/docs/HDR-PIPELINE.md), and [Troubleshooting](TROUBLESHOOTING.md).
