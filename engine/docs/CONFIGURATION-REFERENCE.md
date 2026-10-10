# Advanced configuration reference

For normal setup, use the [configuration guide](CONFIGURATION.md). This page covers manual `conf.toml` editing and environment variables. The file is `~/.config/mako-render/conf.toml`; edit shared profiles in one UI at a time.

## Format compatibility

Only `version = 2` is supported. Unknown keys are ignored and removed on the next UI save. Launcher, shader, and HDR exposure options belong in their UI controls or supported launcher files; HDR buffer precision is a native profile field.

## Profiles

`active_in` matches executable names, process names, or executable-path suffixes. `MAKO_PROFILE` selects an exact profile and overrides automatic matching. If nothing matches, MAKO Renderer stays inactive. A minimal example:

```toml
version = 2

[global]
allow_fp16 = true

[[profile]]
name = "My game"
active_in = ["Game.exe"]
frame_generation_provisioned = true
frame_generation_enabled = true
multiplier = 2

scaling_enabled = false
scaling_method = "ls1"
scaling_factor = 1.5
scaling_supersampling = false
scaling_sharpness = 0.8
```

### AC and battery settings

Both optional `handheld` and `docked` tables must exist when enabled. They inherit omitted Renderer values from the preceding profile and cannot override its name or matches. DLL, FP16, Shaders, and launcher settings stay shared.

```toml
[[profile]]
name = "My game"
active_in = ["Game.exe"]
adaptive = true
target_fps = 90

[profile.handheld]
target_fps = 60

[profile.docked]
target_fps = 120
```

Battery selects Handheld; confirmed AC selects Docked. Unavailable startup detection uses Base; failures during play retain the last confirmed source. Use current matching editors, as older versions may discard these tables.

### Launcher exclusions

Known launchers and helpers stay inactive while their child games can match. The [shared registry](../mako-common/launcher_exclusions.json) owns the exact exclusions; generic `Launcher.exe` is not globally excluded. To change the registry, update [the compatibility ledger](../../CLEANUPS.md), run `just generate-launcher-exclusions`, and validate with `just check-launcher-exclusions`.

## Global settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `dll` | Automatic | Optional absolute path to `Lossless.dll`. LSFG and LS1 require it; MAKO Scaler and Native Resolution do not. |
| `allow_fp16` | `true` | Selects FP16 LSFG and MAKO Scaler colour arithmetic; `false` selects FP32. Ordinary builds fail explicitly when native FP16 LSFG is unavailable; experimental builds may force conversion of a supported FP32 graph with unqualified image quality. LS1 Quality and LS1 Performance always use FP32 independently of this setting. Coordinates and transport retain their required precision. Requires restart; Ultra Performance respects this choice. |

## Profile settings

### Frame Generation

| Setting | Default | Meaning |
| --- | --- | --- |
| `frame_generation_provisioned` | `true` | Prepares Frame Generation at process start. Set `false` for Scaling-only use. Requires restart. |
| `frame_generation_enabled` | `true` | Live execution state. `false` is the UI's `0x` position. |
| `multiplier` | `2` | Fixed total output multiplier, from 2–5. |
| `adaptive` | `false` | Enables Adaptive instead of Fixed generation. |
| `target_fps` | `120` | Adaptive output target, or saved fallback when refresh matching is on. MAKO Decky uses a 90 FPS product default. |
| `adaptive_target_refresh_rate` | `false` | Matches Adaptive's output target to confirmed Gamescope display refresh, clamped to the Renderer’s 10–1000 FPS range. Applies live; unavailable or zero refresh uses the saved `target_fps`. Fixed ignores this choice. |
| `adaptive_max_multiplier` | `3` | Adaptive multiplier ceiling, from 2–5. |
| `adaptive_auto_base_fps_cap` | `false` | Enables Steady Base Cap behavior. |
| `adaptive_fractional_real_frame_priority` | `auto` | Fractional real-frame preference: `auto`, `low`, `medium`, `high`, or `very-high`. |
| `adaptive_stable_cadence` | `true` | Favours a stable validated cadence when available. |
| `gamescope_vrr_mode` | `follow-steam` | Per-profile live Gamescope VRR choice: `follow-steam`, `on`, or `off`. On and Off take effect only when Gamescope reports that the active display supports VRR; otherwise they are saved but leave the live setting untouched. |
| `base_fps_cap` | `0` | Real-frame cap; `0` disables it. |
| `frame_generation_refresh_threshold` | `0` | Pauses generation at or below a confirmed Gamescope refresh; `0` disables it. |
| `dynamic_cadence_recovery` | `false` | Rechecks native cadence for games that switch rates between gameplay and menus. |
| `dynamic_cadence_probe_interval_seconds` | `2.0` | Recovery interval from 0.1–3 seconds. |

See [Adaptive validation](ADAPTIVE-VALIDATION.md) for scheduling details.

### Scaling

| Setting | Default | Meaning |
| --- | --- | --- |
| `scaling_enabled` | `false` | Enables scaling at process start. Requires restart. |
| `scaling_method` | `ls1` | `native`, `mako`, `ls1`, or `ls1-performance`. LS1 failures fall back to MAKO Scaler. |
| `scaling_factor` | `1.5` | Target output-to-source ratio per dimension from 1.0–2.0; the surface, display target, and resource limits may reduce the effective ratio. |
| `scaling_supersampling` | `false` | Allows supported variable Gamescope surfaces to render beyond the display target before downsampling. |
| `scaling_sharpness` | `0.8` | Sharpening strength from 0.0–1.0. |
| `hdr_reduced_precision` | `true` | Approximate PQ-domain HDR10 Frame Generation without linear conversion images/passes, plus compact 10-bit PQ shader intermediates and HDR10 scaling inputs. Applies live through private resource replacement; may add banding, bright-edge artifacts or altered effects. Off restores full linear HDR10 generation. scRGB Frame Generation, scaler outputs, and application/WSI formats retain their precision. Inert for SDR. |
| `swapchain_image_count_compatibility` | `false` | Preserves the game's requested swapchain image minimum. Use only for games that otherwise fail to start. Requires restart. |

### Performance and device selection

| Setting | Default | Meaning |
| --- | --- | --- |
| `flow_scale` | `0.8` | LSFG motion-estimation resolution from 0.25–1.0. Lower values reduce GPU cost and may reduce quality. |
| `performance_mode` | `false` | Uses the lighter LSFG model. |
| `ultra_performance` | `false` | Restart-bound lighter preset using 70% Flow Scale, the lighter model and LS1 Performance when scaling is enabled. Respects the global precision choice. |
| `gpu` | Automatic | Optional GPU name, vendor/device ID, or PCI bus ID. Cross-GPU Frame Generation is unsupported. |
| `pacing` | `none` | Compatibility field; `none` is the only supported value. |

See [Runtime transitions](RUNTIME-TRANSITIONS.md) for when edits take effect.

## Explicit launch selection

```text
MAKO_CONFIG="$HOME/.config/mako-render/conf.toml" MAKO_PROFILE="My game" ~/.local/bin/mako-launch %command%
```

`DISABLE_MAKO=1` bypasses MAKO. Native Remote Play uses `MAKO_FOLLOW_CURRENT_PROFILE=1` to follow a valid root `current_profile` selection on reload, falling back to `MAKO_PROFILE`. Ordinary game launchers use normal matching. See the [Remote Play lifecycle](../../plugin/docs/BACKEND-ARCHITECTURE.md#native-remote-play-lifecycle) for shared selection and shader-cache ownership.

## Environment-only configuration

Set `MAKO_ENV=1` to build one profile from environment variables instead of TOML:

- global: `MAKO_DLL_PATH`, `MAKO_NO_FP16`;
- Fixed and identity: `MAKO_GPU`, `MAKO_MULTIPLIER`, `MAKO_FRAME_GENERATION_PROVISIONED`, `MAKO_FRAME_GENERATION_ENABLED`, `MAKO_FRAME_GENERATION_REFRESH_THRESHOLD`, `MAKO_BASE_FPS_CAP`;
- Adaptive: `MAKO_ADAPTIVE`, `MAKO_ADAPTIVE_AUTO_BASE_FPS_CAP`, `MAKO_ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY`, `MAKO_TARGET_FPS`, `MAKO_ADAPTIVE_TARGET_REFRESH_RATE`, `MAKO_ADAPTIVE_MAX_MULTIPLIER`, `MAKO_ADAPTIVE_STABLE_CADENCE`, `MAKO_DYNAMIC_CADENCE_RECOVERY`, `MAKO_DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS`;
- Gamescope VRR: `MAKO_GAMESCOPE_VRR_MODE` (`follow-steam`, `on`, or `off`);
- HDR buffer precision: `MAKO_HDR_REDUCED_PRECISION` (`1` by default, `0` for full precision);
- Scaling: `MAKO_SCALING_ENABLED`, `MAKO_SCALING_METHOD`, `MAKO_SCALING_FACTOR`, `MAKO_SCALING_SUPERSAMPLING`, `MAKO_SCALING_SHARPNESS`, `MAKO_SWAPCHAIN_IMAGE_COUNT_COMPATIBILITY`; and
- resources: `MAKO_ULTRA_PERFORMANCE`, `MAKO_FLOW_SCALE`, `MAKO_PERFORMANCE_MODE`, `MAKO_PACING`.

`MAKO_DISABLE_HDR_EXPOSURE=1` disables HDR exposure by default. The restart-only HDR control exports `MAKO_DISABLE_HDR_EXPOSURE=0` and `DXVK_HDR=1` while retaining `DISABLE_GAMESCOPE_WSI=1`; the isolated bridge carries HDR colour and metadata without full WSI. Environment-only settings are process-start settings, not live profile controls.

## Presentation acquisition timeout

`MAKO_PRESENT_ACQUIRE_TIMEOUT_MS` sets one shared deadline for all ordered generated-image acquisitions in an application present, so higher multipliers cannot multiply the wait. Exhaustion or elapsed-time overrun enters native recovery. MAKO Decky uses 50 ms. A pool that fits the generated batch but has no additional relief image always uses at most 50 ms, including standalone launches; a shorter configured deadline remains authoritative. Other standalone ordered paths retain the unbounded compatibility default when unset. For a focused stall reproduction, try `25` and include the log.
