# Configuration

Configure MAKO Renderer with `mako-ui` or by editing `~/.config/mako-render/conf.toml`. The UI writes the same TOML file. MAKO creates profiles so different games can use different settings.

The optional desktop UI supports English, Brazilian Portuguese, European Portuguese, Spanish, Korean, Japanese, Ukrainian, and Simplified Chinese, matching MAKO Decky's supported language inventory. It selects a matching system language on first run and stores later language choices as an interface preference, separately from Renderer profiles and `conf.toml`. CLI output currently supports English, Brazilian Portuguese, European Portuguese, and Spanish; place `--lang en`, `--lang pt-BR`, `--lang pt-PT`, or `--lang es` before the command, such as `mako-cli --lang es validate`.

## Profiles

Each `[[profile]]` section is a game profile. `active_in` can match a Linux binary name, Windows executable, process name, or the end of an executable path. Use `MAKO_PROFILE` when you need to select a profile explicitly instead of matching it automatically.

```toml
[[profile]]
name = "My game"
active_in = ["Game.exe"]
multiplier = 2
frame_generation_enabled = true
frame_generation_refresh_threshold = 0
```

## Global settings

- **`dll`**: Optional full path to `Lossless.dll`. Leave it unset to use MAKO's normal Steam-library discovery.
- **`allow_fp16`**: Enables half-precision acceleration where it is useful. It is normally beneficial on AMD hardware; disable it for older NVIDIA GPUs if performance is worse.

## Profile settings

- **`name`**: Display name and value accepted by `MAKO_PROFILE`.
- **`active_in`**: Executables or process names that select this profile.
- **`multiplier`**: Supported fixed multipliers are 2x, 3x, and 4x; do not configure larger values. The direct Renderer default is `2`.
- **`frame_generation_enabled`**: Live on/off switch. `false` presents real frames while keeping the layer loaded. Default: `true`.
- **`frame_generation_refresh_threshold`**: Pauses frame generation when Gamescope confirms that the current display is at or below this refresh rate, then resumes the configured mode above it. `0` disables the guard. Missing refresh feedback fails open, and this setting never overrides `frame_generation_enabled = false`. Direct configuration accepts 0–1000 Hz; MAKO Decky's slider uses 30–240 Hz and starts at 60 Hz when enabled. Default: `0`.
- **`base_fps_cap`**: Caps the game's real frame rate before generation. `0` disables the cap; direct configuration accepts 1–1000 FPS.
- **`adaptive`**: Enables Adaptive Frame Generation. It varies generated frames toward `target_fps` and ignores the fixed `multiplier`. Fractional output keeps its smoothed workload budget while a bounded raw-time placement phase can defer one already-earned output away from a clearly short source interval; generated timestamps stay evenly spaced inside each interval. Default: `false`.
- **`adaptive_auto_base_fps_cap`**: In Adaptive mode, caps the real frame rate to half of `target_fps` for an even 2x baseline. It can trade real-frame headroom and responsiveness for steadier output. Default: `false`.
- **`target_fps`**: Adaptive displayed-frame-rate target. It is not a frame limiter; MAKO cannot reduce a game already rendering above the target or exceed the selected ceiling. Direct configuration accepts 10–1000. Default: `120`.
- **`adaptive_max_multiplier`**: Adaptive ceiling of 2x, 3x, or 4x. Start at 2x for image quality; use a higher ceiling only when the game benefits. Default: `3`.
- **`adaptive_stable_cadence`**: Prefers a constant interpolation cadence when it is sustainable. It can look smoother but may increase input lag. Default: `false`.
- **`dynamic_cadence_recovery`**: Optional per-profile compatibility recovery for games and emulators that switch native frame rates. It periodically presents a short native-only cadence probe on ordered SDR so FIFO-generated work cannot hide a faster mode. Adaptive recalibrates against `target_fps`; Fixed uses a confirmed Gamescope refresh as its target and treats `multiplier` as a ceiling, falling back to exact Fixed behavior when that signal is unavailable or the multiplier is outside 2x-4x. Enabling it sets `base_fps_cap` to `0` and disables `adaptive_auto_base_fps_cap`; the MAKO UIs keep the controls available and turn Recovery off if either cap is enabled later. A true fixed-rate game can receive a brief pacing check. Default: `false`.
- **`dynamic_cadence_probe_interval_seconds`**: Seconds between Dynamic Cadence Recovery checks, from `0.25` to `3`. A shorter interval detects native-rate changes sooner and bounds the associated emulator/audio slowdown more tightly, but can make the brief native-only probe hitch more frequent in a true fixed-rate game. It applies live without resetting Adaptive's validated cadence or multiplier. Default: `2`.
- **`ultra_performance`**: Restart-only per-profile setting that may improve frame-generation performance by up to 30% in favourable GPU-limited scenarios by forcing an effective Flow Scale of 0.75, the lighter frame-generation model, and FP16 permission when supported, and skips live profile-configuration checks to reduce GPU work. It allocates generated-output resources only for the active Fixed or Adaptive policy and increases visual artifacts. Profile changes do not apply while it is enabled, so restart the game after changing settings. Gamescope refresh and HDR feedback, presentation safety, and recovery remain active. MAKO Decky stores 0.75/true/true when enabling it and restores its 0.90/false/true defaults when disabling it; `mako-ui` restores the direct Renderer defaults of 1.00/false/true. The Renderer applies the forced effective values defensively even to a manually inconsistent profile. Default: `false`.
- **`flow_scale`**: Motion-vector resolution from 0.25 to 1.0. Lower is faster; higher favours image quality. Default: `1.0`.
- **`performance_mode`**: Stable compatibility property presented as **Lighter FG Model**. It uses a lighter model for lower GPU cost and more artifacts. Default: `false`.
- **`pacing`**: Presentation policy. `none` is the only supported value.
- **`gpu`**: Optional GPU name, vendor/device ID, or PCI bus ID. It must name the GPU used by the game; multi-GPU frame generation is not supported.

MAKO Decky uses its own safer UI defaults: 90 FPS Adaptive target, 0.90 Flow Scale, and Smooth Cadence enabled when it creates a profile. Those defaults do not change the direct Renderer defaults above.

## Profile compatibility

`version = 2` is the current configuration format. MAKO accepts only a format version it understands; a future structural change must use a new version and an explicit migration.

Within a supported version, an unrecognised global or profile key is inert: the Renderer does not turn it into engine state or an environment variable. Merely starting the Renderer does not edit the file. If MAKO Decky or `mako-ui` later saves that configuration, it writes the current canonical schema and removes unknown or retired keys. This keeps ordinary upgrades safe and prevents an old option from unexpectedly regaining meaning in a later release.

When a setting is renamed or its value must be carried forward, add a one-time, tested migration before removing the old field. Do not reuse an old option name for different behavior. Copying a profile that still uses a supported format from a newer MAKO release into an older editor is therefore not lossless: the older runtime ignores unknown settings, and saving with that older editor removes them.

## Applying changes

Frame Generation, its refresh-rate threshold, Fixed/Adaptive mode, multiplier within existing capacity, Adaptive target/ceiling, Smooth Cadence, Dynamic Cadence Recovery, and its probe interval can usually apply while the game is running. Interval-only changes reschedule the next probe without resetting the validated scheduling policy. The threshold guard and Fixed recovery also react live when Gamescope reports a refresh-rate change. Restart the game after changing the DLL path, FP16 policy, GPU, Flow Scale, Lighter FG Model, HDR-related settings, or a setting that requires more private GPU resources. Ultra Performance freezes every user-profile change for the running process; Gamescope refresh and HDR feedback continue updating because they are runtime safety inputs rather than user toggles.

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

The profile value must exactly match a configured profile `name`. Quote names that contain spaces; shell backticks are not profile delimiters and must not be used. `DISABLE_MAKO=1` remains a one-launch troubleshooting gate and is honoured even when `mako-launch` is present. The launcher disables Gamescope WSI only inside the child process so MAKO owns a single presentation clock; Gamescope itself remains the active compositor. It also disables HDR exposure because an isolated WSI layer cannot be added back after Vulkan starts. Steam's Vulkan Fossilize/overlay hooks and system-wide implicit layers are deliberately excluded from MAKO-managed processes on the supported SDR path unless MAKO Decky selects one guarded External Tool. The design, tradeoffs, and future process-start HDR lane are documented in [WSI isolation](WSI-ISOLATION.md), [optional graphics integrations](LAYER-CHAINING.md), and [HDR pipeline architecture](HDR-PIPELINE.md).

### Standalone launch compatibility

`mako-ui` stores two off-by-default, process-start compatibility switches in `~/.config/mako-render/launcher.conf`. They are global to every game started through standalone `mako-launch`, are deliberately separate from the selected Renderer profile and `conf.toml`, and require a game restart: **Enable Zink for OpenGL** selects Mesa's Zink OpenGL-over-Vulkan path, allowing an OpenGL game to enter MAKO's Vulkan layer; and **Force ALSA Audio** selects native SDL ALSA while adding idempotent Wine/Proton overrides that disable `winepulse.drv` and prefer `winealsa.drv`. Use one compatibility change at a time and leave it off unless the game needs it.

The launcher reads only a strict versioned key/value allowlist and never sources or evaluates the file. An invalid version, unknown key, duplicate key, or non-boolean value makes every stored compatibility option inert for that launch. `MAKO_LAUNCH_CONFIG=/path/to/launcher.conf` selects an alternate file for a controlled standalone test; the variable is removed before the child starts.

Steam Deck mode, Gamescope WSI, MangoHud, and vkBasalt are intentionally absent from the standalone UI. `SteamDeck=0` changes a game's platform identity without enabling or improving MAKO Renderer and belongs in MAKO Decky's per-game launch compatibility. The other options change the Vulkan layer chain and require validated host manifests, architecture checks, deterministic ordering, and fail-closed staging that MAKO Decky currently owns. Standalone `mako-launch` continues to isolate Gamescope WSI and arbitrary host implicit layers; use MAKO Decky for its guarded per-profile compatibility lanes.

If no profile matches the launched process, the Vulkan layer remains dormant and preserves the application's native presentation path. This makes launcher and helper processes safe while ensuring frame generation starts only for an explicitly matched profile.

`MAKO_ALLOW_COMPETING_LAYERS=1` is an advanced, unsupported comparison escape hatch. It tells the launcher not to suppress an installed competing LSFG-VK frame-generation layer for that process. Never use it for ordinary gameplay: concurrent frame-generation layers may both intercept the same swapchain and cause startup, synchronization, presentation, or image-quality failures.

The UI and CLI do not need the launcher. Run `mako-ui` directly to edit the same configuration and run `mako-cli` directly to validate it, benchmark the backend, or execute quality tests. Only Vulkan games and applications that should load MAKO run through `mako-launch`.

For a configuration that comes entirely from environment variables, set `MAKO_ENV=1` and use any of the following:

- `MAKO_DLL_PATH`, `MAKO_NO_FP16`, `MAKO_GPU`
- `MAKO_MULTIPLIER`, `MAKO_FRAME_GENERATION_ENABLED`, `MAKO_FRAME_GENERATION_REFRESH_THRESHOLD`, `MAKO_BASE_FPS_CAP`
- `MAKO_ADAPTIVE`, `MAKO_ADAPTIVE_AUTO_BASE_FPS_CAP`, `MAKO_TARGET_FPS`, `MAKO_ADAPTIVE_MAX_MULTIPLIER`, `MAKO_ADAPTIVE_STABLE_CADENCE`, `MAKO_DYNAMIC_CADENCE_RECOVERY`, `MAKO_DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS`
- `MAKO_ULTRA_PERFORMANCE`, `MAKO_FLOW_SCALE`, `MAKO_PERFORMANCE_MODE`, `MAKO_PACING`

`MAKO_DISABLE_HDR_EXPOSURE=1` keeps MAKO's unfinished HDR path disabled. It is part of the normal MAKO Decky and standalone `mako-launch` boundary. `DISABLE_GAMESCOPE_WSI=1` also closes that engine path defensively because the required HDR bridge is unavailable without the WSI layer. HDR and WSI settings are process-start policy and require a game restart; they are not live profile controls.
