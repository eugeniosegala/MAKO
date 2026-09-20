# Configuration

Configure MAKO Renderer with `mako-ui` or by editing `~/.config/mako-render/conf.toml`. Both use configuration format `version = 2`. [Runtime configuration transitions](RUNTIME-TRANSITIONS.md) defines when each setting reaches a running game; [Spatial scaling architecture](SCALING.md) covers scaler internals and limits.

The UI saves after edits have settled for 500 ms and flushes pending profile and launcher edits when it closes. Saving runs on the UI's owning thread, with no background polling when idle. Renderer configuration writers stage and synchronize complete files before replacing the previous version, preserving it on permission or short-write failures; existing configuration symlinks keep pointing to their updated target. Edit shared profiles in one editor at a time when using both MAKO Decky and the standalone UI. If the UI cannot parse `conf.toml`, it preserves it as `.old`; it refuses to overwrite an existing backup and leaves both files in place for repair.

The UI supports English, Brazilian Portuguese, European Portuguese, Spanish, Korean, Japanese, Ukrainian, and Simplified Chinese. CLI output supports English, Brazilian Portuguese, European Portuguese, and Spanish; place `--lang en`, `--lang pt-BR`, `--lang pt-PT`, or `--lang es` before the command.

## Profiles

Each `[[profile]]` is selected by `active_in`. Entries may match a Linux executable, Windows executable, process name, or executable-path suffix. `MAKO_PROFILE` selects a profile by its exact `name` and takes precedence over automatic matching.

The standalone UI's **Detect Running Game…** captures the actual executable from a running native or Wine/Proton game, including non-Steam games. Start the game, reach gameplay, and select it in the picker. **Use Game Profile** creates a uniquely named profile or opens an existing executable match; **Add to Selected Profile** preserves the selected profile's settings and manual matches, opening an existing match instead when present. The UI uses the Renderer's own executable identification and launcher exclusions, preserves executable case and spaces, and checks the process again before saving. Capture stores an ordinary `active_in` entry, so no configuration migration or Decky installation is required.

Detection takes a snapshot of the current user's readable processes only when the picker opens or you request a refresh. It runs off the UI thread with no idle polling, reads neither command lines nor environments, and filters common Steam/Wine helpers. The default list includes mapped Windows executables and native processes with Vulkan/OpenGL libraries; **Show all applications** also includes other native executables when graphics detection is unavailable. Restricted procfs access or an unreadable Wine mapping can prevent discovery; manual **Matched Processes** remains available. A detected application is not proof of Vulkan compatibility or MAKO activation. Save the profile, complete the [standalone launch setup](#standalone-launcher) or Flatpak preparation, and restart the game.

Ubisoft Connect's `UbisoftConnect.exe`, `upc.exe`, and `UplayWebCore.exe` stay on MAKO's inactive native-presentation path even when they inherit `MAKO_PROFILE`, `MAKO_PROFILE_FALLBACK`, or `MAKO_ENV`, or appear in an older profile's `active_in`. The guard compares the exact executable basename without case sensitivity, preferring the mapped Windows executable under Wine; a launcher directory or thread name does not exclude the game. It changes no environment variables, so the launched game can still select its profile normally. This excludes MAKO's own rendering work in the launcher, not other Vulkan layers or Proton behavior.

The shared [launcher exclusion registry](../mako-common/launcher_exclusions.json) owns the excluded executables for both MAKO Renderer and MAKO Decky. Each entry records a `launcher` name, a `reason`, and its `executables`. To add a launcher, document the observed need and add only its exact ASCII Windows `.exe` basenames; paths, wildcards, and duplicate names are rejected. Names with spaces require extending Decky's process scanner before they can be registered. Decky derives the truncated Linux process names automatically. Keep removal conditions in [the compatibility ledger](../../CLEANUPS.md), and validate launcher inactivity and child-game activation for each addition.

After editing the registry, run `just generate-launcher-exclusions` from the repository root and include both generated bindings in the change. `just check-launcher-exclusions`, Renderer CTest, and Decky's generated-contract gate check freshness without rewriting files. The Renderer compiles its generated list and Decky imports its packaged Python binding; neither reads the JSON at runtime or depends on the other component's installation. This is a source-maintained compatibility list, not a `conf.toml` setting.

```toml
version = 2

[global]
allow_fp16 = true

[[profile]]
name = "My game"
active_in = ["Game.exe"]
frame_generation_enabled = true
multiplier = 2

scaling_enabled = false
scaling_method = "ls1"
scaling_factor = 1.5
scaling_supersampling = false
scaling_sharpness = 0.8
```

## Global settings

| Setting | Meaning |
| --- | --- |
| `dll` | Optional absolute path to `Lossless.dll`, shared by LSFG and LS1. When omitted, MAKO searches the normal Steam library locations. Model compatibility uses validated resources, not a SHA allowlist; see [shared model resolution](SCALING.md). Native Resolution and MAKO Scaler do not need this file. |
| `allow_fp16` | Defaults to `true`, including when the setting or `[global]` section is omitted. Uses LSFG FP16 when the selected GPU supports it, otherwise FP32. Set `false` to use FP32; existing explicit choices are preserved. Changing it requires a game restart. |

The CLI's `benchmark`, `debug`, `quality-regression`, and `combined-quality-regression` commands also allow LSFG FP16 by default, independently of `conf.toml`. Pass `--no-fp16` to use FP32, or `--allow-fp16` (`-a`) to explicitly allow FP16. If both flags are supplied, the last one wins. For environment-only Renderer configuration (`MAKO_ENV=1`), `MAKO_NO_FP16=1` disables FP16. The backend still checks the selected device's Vulkan `shaderFloat16` support before choosing FP16 shaders.

## Profile settings

| Setting | Accepted values | Default | Meaning |
| --- | --- | --- | --- |
| `name` | String | `unnamed` | Display name and `MAKO_PROFILE` value when reading a profile from TOML. |
| `active_in` | String or string array | Empty | Executable or process identities that select the profile. |
| `frame_generation_enabled` | Boolean | `true` | Enables Fixed or Adaptive Frame Generation. It can change live when startup provisioning succeeded. Off performs no generation work, although provisioned resources stay available for a later live enable. |
| `multiplier` | 2–5 | `2` | Total output multiplier in Fixed mode. Higher values need more GPU time, private outputs, and WSI headroom. |
| `frame_generation_refresh_threshold` | 0–1000 Hz | `0` | Pauses generation at or below a confirmed Gamescope refresh; `0` disables the guard. Missing refresh feedback does not pause generation. |
| `base_fps_cap` | 0–1000 FPS | `0` | Caps real frames while generation is active; `0` disables the cap. The saved value is dormant while Frame Generation is off. |
| `adaptive` | Boolean | `false` | Uses Adaptive rather than Fixed policy and varies the generated count toward `target_fps`. |
| `adaptive_auto_base_fps_cap` | Boolean | `false` | Starts Adaptive with a half-target real-frame cap and may select a proven integer cadence with Smooth Cadence. Recovery can release only this automatic cap when it becomes the bottleneck. |
| `target_fps` | 10–1000 FPS | `120` | Adaptive output target. It is not a limiter for a game already above target and cannot override the multiplier ceiling. |
| `adaptive_max_multiplier` | 2–5 | `3` | Maximum total multiplier Adaptive may select. Start at 2 for the lowest generated-frame share. |
| `adaptive_stable_cadence` | Boolean | `true` | Allows a delivery-validated constant cadence in Adaptive. In eligible Fixed mode, ordered Gamescope FIFO paces the full multiplier without an automatic real-frame CPU cap. This may trade real-frame cadence and latency for smoother output. |
| `dynamic_cadence_recovery` | Boolean | `false` | Periodically exposes native cadence on ordered SDR to detect a faster game mode hidden by FIFO backpressure. It is per-profile and automatically disables both manual and automatic base caps. |
| `dynamic_cadence_probe_interval_seconds` | 0.1–3.0 | `2.0` | Delay between optional cadence probes. Short values react faster but make rejected probes more frequent. |
| `scaling_enabled` | Boolean | `false` | Provisions scaling at process start. Changing it requires a game restart. |
| `scaling_method` | `native`, `mako`, `ls1`, `ls1-performance` | `ls1` | Selects Native Resolution linear scaling, MAKO Scaler, LS1 Quality, or LS1 Performance. LS1 failures fall back to MAKO Scaler for that swapchain. |
| `scaling_factor` | 1.0–2.0 | `1.5` | Source-to-presentation ratio; `1.0` performs no scaling. Surface, display-target, and memory limits may reduce the effective factor. |
| `scaling_supersampling` | Boolean | `false` | Lets a variable managed Gamescope surface exceed its proven display target. Vulkan and memory limits still apply; fixed and direct non-Gamescope geometry is unchanged. |
| `scaling_sharpness` | 0.0–1.0 | `0.8` | MAKO Scaler sharpening strength or nearest selection among LS1's five model variants. |
| `swapchain_image_count_compatibility` | Boolean | `false` | Preserves the application's requested minimum WSI image count instead of reserving generated-output headroom. Use only for games that fail to create the normal swapchain; generated frames may be skipped under pressure. Requires restart. |
| `flow_scale` | 0.25–1.0 | `0.8` | LSFG motion-vector resolution. Lower values reduce cost and may reduce quality. |
| `performance_mode` | Boolean | `false` | Selects the lighter LSFG model. The UIs label this **Lighter FG Model**. |
| `ultra_performance` | Boolean | `false` | Restart-bound preset that selects Flow Scale 0.7, the lighter LSFG model, FP16 permission, active-policy-sized resources, and LS1 Performance when scaling is enabled. It does not enable scaling. |
| `pacing` | `none` | `none` | Presentation-policy compatibility field; `none` is the only supported value. |
| `gpu` | GPU name, vendor/device ID, or PCI bus ID | Unset | Selects the application's GPU. MAKO does not support cross-GPU Frame Generation. |

MAKO Decky creates profiles with product-level defaults that may differ from the direct Renderer defaults, including a 90 FPS Adaptive target and Smooth Cadence.

## Desktop scaling and resolution

MAKO reads the game's requested image size directly from Vulkan when it creates a swapchain. This is the image presented by the game, which may already include the game's own upscaling; it is not necessarily the game's internal 3D rendering resolution. Some games keep this image at the display size in fullscreen or borderless mode and require Windowed mode to expose a smaller scaling source. There is no desktop resolution scan in the per-frame scaling path and no need to enter that source size separately in `mako-ui` or the configuration file.

Scale Factor has two effects depending on the surface. With a fixed presentation size, a 1920×1080 surface at 1.5× advertises a 1280×720 source for the game to render. On a variable desktop surface, MAKO retains the game's requested source size: set the game to 1280×720 and use 1.5× to request a 1920×1080 output. Raising the factor alone on that surface enlarges MAKO's output rather than lowering the game's resolution, and can increase GPU and memory use. Surface and memory limits may reduce the effective factor.

Outside Gamescope, MAKO does not infer the destination monitor size from an arbitrary desktop window. Choose the factor to fit the intended output; the desktop compositor may otherwise scale the result again. Turning Quality Supersampling off does not provide a monitor-size cap on this path. Diagnostics report the actual `source` and `presentation` sizes when scaling activates. If the game ignores MAKO's advertised smaller source and requests the full presentation size, MAKO keeps native presentation with `application-extent-override-no-source-presentation-split`. A manual source-size field would not make the game render a smaller image and could crop its output instead.

[Gamescope also supports nested use on X11 and Wayland desktops](https://github.com/ValveSoftware/gamescope#examples); Game Mode is not required. Its `-w`/`-h` options select the virtual game resolution and `-W`/`-H` select the nested output size. When using standalone MAKO with it, start Gamescope outside `mako-launch` and put only the game command through the launcher. The launcher's Gamescope WSI isolation disables the extra Vulkan layer, not the compositor. Nested Gamescope can provide a controlled virtual display, but games still need to respect the source/presentation contract for MAKO's own scaling to activate. See [extent ownership](SCALING.md#extent-ownership) for the supported paths and safe fallbacks.

## Applying changes

| Boundary | Settings |
| --- | --- |
| Live policy | Frame Generation enable/disable, refresh threshold, supported Fixed/Adaptive mode changes, target, caps, Smooth Cadence, and Dynamic Cadence Recovery. |
| Private resource replacement | Scaler method, sharpness, Flow Scale, Lighter FG Model, and generated-output capacity when the current WSI pool has enough headroom. Method changes apply at the next present; continuous controls coalesce for 500 ms. |
| Game-owned swapchain recreation | Effective Scale Factor or Quality Supersampling extent changes, and capacity growth that exceeds current WSI headroom. Eligible maintenance1 contexts may request one recreation; other paths wait for a natural recreation. |
| Process restart | Scaling enablement, Game Swapchain Images compatibility, DLL, FP16, GPU, Ultra Performance, layer membership, HDR exposure, and launcher compatibility. |

A mixed save still applies its live-safe subset. Requested, applied, and pending values remain distinct, and failed private replacement retains the old resources. See [Runtime configuration transitions](RUNTIME-TRANSITIONS.md) for the precise merge, rollback, and diagnostics contract.

## Format compatibility

MAKO accepts only configuration format `version = 2`. Unknown keys in a supported file are inert and are not exported as environment variables. Reading a file does not rewrite it, but saving it through `mako-ui` or MAKO Decky writes the current schema and removes unknown or retired keys. A renamed setting needs an explicit, tested migration; old names must not be reused for new behavior.

## Standalone launcher

Saving a profile configures the Renderer; the game must also start with MAKO enabled. For a native Steam or Proton game, put this in **Steam Properties > General > Launch Options**:

```text
~/.local/bin/mako-launch %command%
```

Keep `%command%` in Steam. In a terminal, replace it with the actual executable and arguments, for example `~/.local/bin/mako-launch "/path/to/your-game"`. The configuration UI can be closed during play. See [Renderer usage](../README.md#usage) for profile preparation and launcher-specific steps.

Flatpak apps need a matching runtime extension and per-application sandbox setup; follow the [Flatpak guide](FLATPAK-GUIDE.md), then launch the prepared app normally. The host `mako-launch` command and its `launcher.conf` settings do not configure the sandbox.

`MAKO_CONFIG` selects a TOML file and `MAKO_PROFILE` selects an exact profile name:

```text
MAKO_CONFIG="$HOME/.config/mako-render/conf.toml" MAKO_PROFILE="My game" ~/.local/bin/mako-launch %command%
```

`DISABLE_MAKO=1` bypasses MAKO for every launch where that variable remains set. `mako-launch` otherwise selects the installed private MAKO manifests, disables competing LSFG-VK layers and Gamescope WSI in the child, and chooses the supported SDR boundary. If no profile matches, the Renderer remains dormant.

The UI stores two optional, global process-start settings in `~/.config/mako-render/launcher.conf`: **Enable Zink for OpenGL (Restart)** and **Force ALSA Audio (Restart)**. The launcher accepts only its versioned allowlist; malformed, duplicate, unknown, or non-Boolean entries make all stored options inert for that launch. `MAKO_LAUNCH_CONFIG` may select another file for testing and is removed before the child starts.

`MAKO_ALLOW_COMPETING_LAYERS=1` is an unsupported comparison escape hatch that stops the launcher from disabling another installed LSFG-VK layer. Do not use two frame-generation layers on one game.

Steam Deck mode, Gamescope WSI compatibility, MangoHud, and vkBasalt remain MAKO Decky features because they require managed manifests and deterministic ordering. See [WSI isolation](WSI-ISOLATION.md) and [Optional graphics integrations](LAYER-CHAINING.md).

## Environment-only configuration

Set `MAKO_ENV=1` to build one profile entirely from environment variables:

- global: `MAKO_DLL_PATH`, `MAKO_NO_FP16`;
- identity and Fixed policy: `MAKO_GPU`, `MAKO_MULTIPLIER`, `MAKO_FRAME_GENERATION_ENABLED`, `MAKO_FRAME_GENERATION_REFRESH_THRESHOLD`, `MAKO_BASE_FPS_CAP`;
- Adaptive policy: `MAKO_ADAPTIVE`, `MAKO_ADAPTIVE_AUTO_BASE_FPS_CAP`, `MAKO_TARGET_FPS`, `MAKO_ADAPTIVE_MAX_MULTIPLIER`, `MAKO_ADAPTIVE_STABLE_CADENCE`, `MAKO_DYNAMIC_CADENCE_RECOVERY`, `MAKO_DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS`;
- scaling: `MAKO_SCALING_ENABLED`, `MAKO_SCALING_METHOD`, `MAKO_SCALING_FACTOR`, `MAKO_SCALING_SUPERSAMPLING`, `MAKO_SCALING_SHARPNESS`, `MAKO_SWAPCHAIN_IMAGE_COUNT_COMPATIBILITY`; and
- resource and pacing policy: `MAKO_ULTRA_PERFORMANCE`, `MAKO_FLOW_SCALE`, `MAKO_PERFORMANCE_MODE`, `MAKO_PACING`.

`MAKO_DISABLE_HDR_EXPOSURE=1` closes the unfinished HDR lane. `DISABLE_GAMESCOPE_WSI=1` also closes it because the required WSI bridge is absent. These are process-start launch policies, not live profile settings.
