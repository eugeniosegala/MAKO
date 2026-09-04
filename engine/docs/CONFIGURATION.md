# Configuration

Configure MAKO Renderer with `mako-ui` or by editing `~/.config/mako-render/conf.toml`. Both use configuration format `version = 2`. [Runtime configuration transitions](RUNTIME-TRANSITIONS.md) defines when each setting reaches a running game; [Spatial scaling architecture](SCALING.md) covers scaler internals and limits.

The UI supports English, Brazilian Portuguese, European Portuguese, Spanish, Korean, Japanese, Ukrainian, and Simplified Chinese. CLI output supports English, Brazilian Portuguese, European Portuguese, and Spanish; place `--lang en`, `--lang pt-BR`, `--lang pt-PT`, or `--lang es` before the command.

## Profiles

Each `[[profile]]` is selected by `active_in`. Entries may match a Linux executable, Windows executable, process name, or executable-path suffix. `MAKO_PROFILE` selects a profile by its exact `name` and takes precedence over automatic matching.

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
| `dll` | Optional absolute path to `Lossless.dll`. When omitted, MAKO searches the normal Steam library locations. LSFG and LS1 need this user-supplied file; Native Resolution and MAKO Scaler do not. |
| `allow_fp16` | Permits LSFG FP16 on supported hardware. The generated configuration defaults to `true`; disable it if the selected GPU performs worse or is incompatible. |

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
| `adaptive_stable_cadence` | Boolean | `false` | Allows Adaptive to retain a delivery-validated constant cadence. This may trade real-frame cadence and latency for smoother output. |
| `dynamic_cadence_recovery` | Boolean | `false` | Periodically exposes native cadence on ordered SDR to detect a faster game mode hidden by FIFO backpressure. It is per-profile and automatically disables both manual and automatic base caps. |
| `dynamic_cadence_probe_interval_seconds` | 0.1–3.0 | `2.0` | Delay between optional cadence probes. Short values react faster but make rejected probes more frequent. |
| `scaling_enabled` | Boolean | `false` | Provisions scaling at process start. Changing it requires a game restart. |
| `scaling_method` | `native`, `mako`, `ls1`, `ls1-performance` | `ls1` | Selects Native Resolution linear scaling, MAKO Scaler, LS1 Quality, or LS1 Performance. LS1 failures fall back to MAKO Scaler for that swapchain. |
| `scaling_factor` | 1.0–2.0 | `1.5` | Source-to-presentation ratio; `1.0` performs no scaling. Surface, display-target, and memory limits may reduce the effective factor. |
| `scaling_supersampling` | Boolean | `false` | Lets a variable managed Gamescope surface exceed its proven display target. Vulkan and memory limits still apply; fixed and direct non-Gamescope geometry is unchanged. |
| `scaling_sharpness` | 0.0–1.0 | `0.8` | MAKO Scaler sharpening strength or nearest selection among LS1's five model variants. |
| `swapchain_image_count_compatibility` | Boolean | `false` | Preserves the application's requested minimum WSI image count instead of reserving generated-output headroom. Use only for games that fail to create the normal swapchain; generated frames may be skipped under pressure. Requires restart. |
| `flow_scale` | 0.25–1.0 | `0.9` | LSFG motion-vector resolution. Lower values reduce cost and may reduce quality. |
| `performance_mode` | Boolean | `false` | Selects the lighter LSFG model. The UIs label this **Lighter FG Model**. |
| `ultra_performance` | Boolean | `false` | Restart-bound preset that selects Flow Scale 0.75, the lighter LSFG model, FP16 permission, active-policy-sized resources, and LS1 Performance when scaling is enabled. It does not enable scaling. |
| `pacing` | `none` | `none` | Presentation-policy compatibility field; `none` is the only supported value. |
| `gpu` | GPU name, vendor/device ID, or PCI bus ID | Unset | Selects the application's GPU. MAKO does not support cross-GPU Frame Generation. |

MAKO Decky creates profiles with product-level defaults that may differ from the direct Renderer defaults, including a 90 FPS Adaptive target and Smooth Cadence.

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

Use `mako-launch` to activate MAKO for one native process:

```text
~/.local/bin/mako-launch %command%
```

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
