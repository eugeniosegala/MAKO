# Configuration

Configure MAKO Renderer with `mako-ui` or `~/.config/mako-render/conf.toml`; both use the same profile format. See [Spatial scaling architecture](SCALING.md) for scaler internals and validation.

The optional desktop UI supports English, Brazilian Portuguese, European Portuguese, Spanish, Korean, Japanese, Ukrainian, and Simplified Chinese, matching MAKO Decky's supported language inventory. It selects a matching system language on first run and stores later language choices as an interface preference, separately from Renderer profiles and `conf.toml`. CLI output currently supports English, Brazilian Portuguese, European Portuguese, and Spanish; place `--lang en`, `--lang pt-BR`, `--lang pt-PT`, or `--lang es` before the command, such as `mako-cli --lang es validate`.

## Profiles

Each `[[profile]]` section is a game profile. `active_in` can match a Linux binary name, Windows executable, process name, or the end of an executable path. Use `MAKO_PROFILE` when you need to select a profile explicitly instead of matching it automatically.

```toml
[[profile]]
name = "My game"
active_in = ["Game.exe"]
scaling_enabled = false
scaling_method = "native"
scaling_factor = 2.0
scaling_sharpness = 0.9
multiplier = 2
frame_generation_enabled = true
frame_generation_refresh_threshold = 0
```

## Global settings

- **`dll`**: Optional full path to `Lossless.dll`, which supplies the LS1 scaling and LSFG frame-generation model shaders. Leave it unset to use MAKO's normal Steam-library discovery. Native Resolution and MAKO Scaler do not use the licensed spatial models. Every matched process reserves the application-device interop needed for live Frame Generation, while a missing backend keeps generation unavailable and leaves an independent spatial scaler usable.
- **`allow_fp16`**: Enables half-precision frame-generation acceleration where it is useful. It is normally beneficial on AMD hardware; disable it for older NVIDIA GPUs if performance is worse. This setting does not select the spatial scaler's working format.

## Profile settings

- **`name`**: Display name and value accepted by `MAKO_PROFILE`.
- **`active_in`**: Executables or process names that select this profile.
- **`scaling_enabled`**: Enables scaling independently from frame generation. Enable it before starting the game; when off, scaling is fully disabled. Changing it requires a game restart because MAKO Decky stages the frame-generation role, Gamescope WSI, and spatial role at process start. Scaling is SDR-only and accepts only the swapchain shapes, queues, formats, and extent contracts defined in [Spatial scaling architecture](SCALING.md). Default: `false`.
- **`scaling_method`**: Selects `native` (**Native Resolution**), `mako` (**MAKO Scaler**), `ls1` (**LS1 Quality**), or `ls1-performance` (**LS1 Performance**). Native is the model-free linear baseline; MAKO is the open single-pass scaler; LS1 Quality and Performance use the user's licensed DLL and an architecture-matched `libvkd3d-shader.so.1`. LS1 initialization failure is logged and falls back to MAKO for that swapchain. With scaling provisioned, method changes rebuild only MAKO's private scaler. Default: `native`.
- **`scaling_factor`**: Source-to-presentation ratio from `1.0` to `2.0`; `1.0` performs no scaling work. Fixed surfaces derive an even source extent from the compositor-owned presentation extent. Variable surfaces enlarge the application's request within surface and device-memory limits. Default: `2.0`.
- **`scaling_sharpness`**: Normalized `0.0` to `1.0` multiplier for MAKO's bounded local sharpening at its static 2x baseline, or nearest selection among LS1's five learned model variants. Default: `0.9`.
- **`multiplier`**: Supported fixed multipliers range from 2x through 5x. 5x is an opt-in high-refresh mode that needs four full-resolution generated outputs per real frame, so it substantially increases GPU and memory use. The direct Renderer default is `2`.
- **`frame_generation_enabled`**: Live on/off switch. `false` presents real frames, reconstructed first when scaling is active, while retaining the backend, private images, and synchronization objects needed for an immediate later enable. The Off path schedules no LSFG work and submits no generated images; the retained resources trade additional startup memory for reliable live switching. If interop or the backend was unavailable when the process created its Vulkan device and swapchain, enabling remains pending for restart instead of pretending to generate. Default: `true`.
- **`frame_generation_refresh_threshold`**: Pauses frame generation when Gamescope confirms that the current display is at or below this refresh rate, then resumes the configured mode above it. `0` disables the guard. Missing refresh feedback fails open, and this setting never overrides `frame_generation_enabled = false`. Direct configuration accepts 0–1000 Hz; MAKO Decky's slider uses 30–240 Hz and starts at 60 Hz when enabled. Default: `0`.
- **`base_fps_cap`**: Caps the game's real frame rate before generation. `0` disables the cap; direct configuration accepts 1–1000 FPS.
- **`adaptive`**: Enables Adaptive Frame Generation. It varies generated frames toward `target_fps` and ignores the fixed `multiplier`. Fractional output keeps its smoothed workload budget while a bounded raw-time placement phase can defer one already-earned output away from a clearly short source interval; generated timestamps stay evenly spaced inside each interval. It keeps more real frames and can reduce input lag and ghosting, but may be less smooth in some games. Default: `false`.
- **`adaptive_auto_base_fps_cap`**: In Adaptive mode, starts with a half-target real-frame cap for an even 2x baseline. When Smooth Cadence, ordered Gamescope SDR, and a target-matching refresh are all active, a proven 3x-5x scheduler level may qualify the exact `target_fps / multiplier` cap for a steadier integer cadence. Ramp, recovery, or lost qualification restores the conservative half-target cap. It can trade real-frame headroom and responsiveness for steadier output, with potentially more input lag and ghosting. Default: `false`.
- **`target_fps`**: Adaptive displayed-frame-rate target. It is not a frame limiter; MAKO cannot reduce a game already rendering above the target or exceed the selected ceiling. A lower validated multiplier may probe available headroom below 98% of target, while established cadence-retention guards remain separate. Direct configuration accepts 10–1000. Default: `120`.
- **`adaptive_max_multiplier`**: Adaptive ceiling from 2x through 5x. Start at 2x for image quality; use a higher ceiling only when the game benefits. 5x is intended for high-refresh displays with substantial GPU and memory headroom. Default: `3`.
- **`adaptive_stable_cadence`**: Prefers a validated constant interpolation cadence when it is sustainable. On ordered Gamescope SDR with display refresh matching the Adaptive target, it may test a nearby 2x cadence and retain it only when output settles within 98–102% of target. With Steady Base Cap, a delivery-validated 3x-5x level can align real pacing to the corresponding target/multiplier rung after a one-second hold. A validated Steady 2x path lets ordered FIFO own pacing instead of retaining a redundant explicit cap; ramp, recovery, or lost qualification restores the conservative cap. This can look smoother but may lower real-frame cadence and increase input lag and ghosting. Default: `false`.
- **`dynamic_cadence_recovery`**: Optional per-profile compatibility recovery for games and emulators that switch native frame rates. It periodically presents a short native-only cadence probe on ordered SDR so FIFO-generated work cannot hide a faster mode. Adaptive recalibrates against `target_fps`; Fixed uses a confirmed Gamescope refresh as its target and treats `multiplier` as a ceiling, falling back to exact Fixed behavior when that signal is unavailable or the multiplier is outside 2x-5x. Enabling it sets `base_fps_cap` to `0` and disables `adaptive_auto_base_fps_cap`; the MAKO UIs keep the controls available and turn Recovery off if either cap is enabled later. A true fixed-rate game can receive a brief pacing check. Default: `false`.
- **`dynamic_cadence_probe_interval_seconds`**: Seconds between Dynamic Cadence Recovery checks, from `0.1` to `3`. The UIs offer `0.1`, `0.2`, `0.25`, `0.5`, `0.75`, `1`, `1.5`, `2`, and `3` seconds. Shorter intervals detect native-rate changes sooner and bound the associated emulator/audio slowdown more tightly, but `0.1` is an aggressive per-game option that can make the brief native-only probe hitch much more frequent in a true fixed-rate game. It applies live without resetting Adaptive's validated cadence or multiplier. Default: `2`.
- **`ultra_performance`**: Restart-bound preset that forces an effective Flow Scale of 0.75, the lighter frame-generation model, FP16 permission when supported, and active-policy-only generated-output allocation. It may improve GPU-limited performance by up to 30% but increases artifacts. Compatible runtime controls remain available; later capacity growth uses the normal private FG replacement boundary, but toggling the preset still requires restart because FP16 and the resource policy are process-static. MAKO Decky restores its 0.90/false/true defaults when disabling the preset, while `mako-ui` restores the direct Renderer defaults of 1.00/false/true. Default: `false`.
- **`flow_scale`**: Frame Generation motion-vector resolution from 0.25 to 1.0. Lower is faster; higher favours image quality. It does not affect spatial scaling. After a 500 ms quiet period, MAKO prepares a replacement private FG context and atomically switches when its old private work is idle. Default: `1.0`.
- **`performance_mode`**: Stable compatibility property presented as **Lighter FG Model**. It uses a lighter model for lower GPU cost and more artifacts. MAKO applies it through the same live private FG context replacement. Default: `false`.
- **`pacing`**: Presentation policy. `none` is the only supported value.
- **`gpu`**: Optional GPU name, vendor/device ID, or PCI bus ID. It must name the GPU used by the game; multi-GPU frame generation is not supported.

MAKO Decky uses its own safer UI defaults: 90 FPS Adaptive target, 0.90 Flow Scale, and Smooth Cadence enabled when it creates a profile. Those defaults do not change the direct Renderer defaults above.

## Profile compatibility

`version = 2` is the current configuration format. MAKO accepts only a format version it understands; a future structural change must use a new version and an explicit migration.

Within a supported version, an unrecognised global or profile key is inert: the Renderer does not turn it into engine state or an environment variable. Merely starting the Renderer does not edit the file. If MAKO Decky or `mako-ui` later saves that configuration, it writes the current canonical schema and removes unknown or retired keys. This keeps ordinary upgrades safe and prevents an old option from unexpectedly regaining meaning in a later release.

When a setting is renamed or its value must be carried forward, add a one-time, tested migration before removing the old field. Do not reuse an old option name for different behavior. Copying a profile that still uses a supported format from a newer MAKO release into an older editor is therefore not lossless: the older runtime ignores unknown settings, and saving with that older editor removes them.

## Applying changes

Frame Generation enablement and scheduling controls apply live when startup provisioning succeeded. With scaling provisioned, method changes rebuild the private scaler at the next present and sharpness changes do so after a 500 ms quiet period. Flow Scale, Lighter FG Model, and generated-capacity growth prepare complete private FG replacements after a 500 ms quiet period, retain the old context until its work drains, atomically switch, and warm temporal history. The replacement temporarily consumes additional memory and driver allocation may produce a brief one-time hitch, but no game-owned swapchain recreation or device-wide idle is used. Scale Factor retains a game-owned recreation boundary; after edits settle, an eligible lower spatial role requests that boundary once after a maintenance1-fenced present, while unsupported paths wait for natural recreation. Pacing remains natural-only. Scaling enablement, DLL, FP16, GPU, HDR/WSI policy, and Ultra Performance require restart. Mixed writes still apply their live-safe subset while reporting requested versus applied fields. [Runtime configuration transitions](RUNTIME-TRANSITIONS.md) owns the exact matrix, state-reset rules, status, and diagnostics.

Test V-Sync both on and off for each game. It can steady the real-frame cadence, but can also add latency or conflict with an FPS cap, VRR, or the compositor.

## Launch variables

Use `mako-launch` as the standalone activation interface. It selects the installed private MAKO-only implicit-layer directory, enables MAKO for the child process, applies the known frame-generation-layer conflict guards and the Gamescope WSI presentation guard before Vulkan starts, and forwards the command and arguments without evaluating or rewriting them:

```text
~/.local/bin/mako-launch %command%
```

Place any launch-specific environment variables before the helper. `MAKO_CONFIG` chooses a TOML file and `MAKO_PROFILE` chooses a named profile, overriding automatic executable and process matching for that launch:

```text
MAKO_CONFIG="$HOME/.config/mako-render/conf.toml" MAKO_PROFILE="My game" ~/.local/bin/mako-launch %command%
```

The profile value must exactly match a configured `name`; quote names containing spaces. `DISABLE_MAKO=1` is a one-launch troubleshooting gate. The launcher gives the child a private SDR layer path, while Gamescope remains the active compositor. See [WSI isolation](WSI-ISOLATION.md), [optional graphics integrations](LAYER-CHAINING.md), and [HDR pipeline architecture](HDR-PIPELINE.md) before changing this boundary.

### Standalone launch compatibility

`mako-ui` stores two off-by-default process-start switches in `~/.config/mako-render/launcher.conf`. They apply globally to games started through standalone `mako-launch`, are separate from profiles, and require restart: **Enable Zink for OpenGL** selects Mesa's Vulkan-backed OpenGL path; **Force ALSA Audio** selects native SDL ALSA and makes Wine/Proton prefer `winealsa.drv` over `winepulse.drv`. Change one at a time and leave them off unless required.

The launcher reads only a strict versioned key/value allowlist and never sources or evaluates the file. An invalid version, unknown key, duplicate key, or non-boolean value makes every stored compatibility option inert for that launch. `MAKO_LAUNCH_CONFIG=/path/to/launcher.conf` selects an alternate file for a controlled standalone test; the variable is removed before the child starts.

Steam Deck mode, Gamescope WSI, MangoHud, and vkBasalt are intentionally absent from the standalone UI. They either change game identity or require the validated manifests, ordering, architecture checks, and fail-closed staging owned by MAKO Decky. Standalone `mako-launch` keeps Gamescope WSI and arbitrary host implicit layers isolated.

If no profile matches the launched process, the Vulkan layer remains dormant and preserves the application's native presentation path. This makes launcher and helper processes safe while ensuring frame generation starts only for an explicitly matched profile.

`MAKO_ALLOW_COMPETING_LAYERS=1` is an advanced, unsupported comparison escape hatch. It tells the launcher not to suppress an installed competing LSFG-VK frame-generation layer for that process. Never use it for ordinary gameplay: concurrent frame-generation layers may both intercept the same swapchain and cause startup, synchronization, presentation, or image-quality failures.

The UI and CLI do not need the launcher. Run `mako-ui` directly to edit the same configuration and run `mako-cli` directly to validate it, benchmark the backend, or execute quality tests. Only Vulkan games and applications that should load MAKO run through `mako-launch`.

For a configuration that comes entirely from environment variables, set `MAKO_ENV=1` and use any of the following:

- `MAKO_DLL_PATH`, `MAKO_NO_FP16`, `MAKO_GPU`
- `MAKO_SCALING_ENABLED`, `MAKO_SCALING_METHOD`, `MAKO_SCALING_FACTOR`, `MAKO_SCALING_SHARPNESS`
- `MAKO_MULTIPLIER`, `MAKO_FRAME_GENERATION_ENABLED`, `MAKO_FRAME_GENERATION_REFRESH_THRESHOLD`, `MAKO_BASE_FPS_CAP`
- `MAKO_ADAPTIVE`, `MAKO_ADAPTIVE_AUTO_BASE_FPS_CAP`, `MAKO_TARGET_FPS`, `MAKO_ADAPTIVE_MAX_MULTIPLIER`, `MAKO_ADAPTIVE_STABLE_CADENCE`, `MAKO_DYNAMIC_CADENCE_RECOVERY`, `MAKO_DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS`
- `MAKO_ULTRA_PERFORMANCE`, `MAKO_FLOW_SCALE`, `MAKO_PERFORMANCE_MODE`, `MAKO_PACING`

`MAKO_DISABLE_HDR_EXPOSURE=1` keeps MAKO's unfinished HDR path disabled. It is part of the normal MAKO Decky and standalone `mako-launch` boundary and is required for every spatially scaled Gamescope surface, even when the selected swapchain would otherwise classify as SDR. `DISABLE_GAMESCOPE_WSI=1` also closes that engine path defensively because the required HDR bridge is unavailable without the WSI layer. HDR and WSI settings are process-start policy and require a game restart; they are not live profile controls.
