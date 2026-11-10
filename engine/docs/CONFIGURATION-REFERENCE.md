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

Known launchers and helpers stay inactive while their child games can match. The [shared registry](../mako-common/launcher_exclusions.json) owns the exact exclusions; generic `Launcher.exe` is not globally excluded. If only a launcher is detected, wait for gameplay and match the game executable instead. Maintainers can find registry changes in [the compatibility ledger](../../CLEANUPS.md).

## Global settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `dll` | Automatic | Optional absolute path to `Lossless.dll`. Only the optional Lossless Scaling frame generation model and LS1 require it; MAKO FG, MAKO Scaler, Native Resolution, and Shaders do not. |
| `allow_fp16` | `true` | Selects FP16 for supported LSFG models and MAKO Scaler; `false` selects FP32. If a model lacks native FP16, select FP32. LS1 always uses FP32. Requires restart; Ultra Performance respects this choice. [Experimental source builds](BUILDING-FROM-SOURCE.md#experimental-forced-lsfg-fp16) have separate limits. |

## Profile settings

### Frame Generation

| Setting | Default | Meaning |
| --- | --- | --- |
| `frame_generation_provisioned` | `true` | Prepares Frame Generation at process start. Set `false` for Scaling-only use. Requires restart. |
| `frame_generation_enabled` | `true` | Live execution state. `false` is the UI's `0x` position. |
| `multiplier` | `2` | Fixed total output multiplier, from 2–5. |
| `adaptive` | `false` | Enables Adaptive instead of Fixed generation. |
| `target_fps` | `120` | Adaptive generation target, or saved fallback when refresh matching is on. Fractional Automatic permits sustained native FPS above this target when the manual base cap is off or at least the target; the cap remains enforced. MAKO Decky uses a 90 FPS product default. |
| `adaptive_target_refresh_rate` | `false` | Matches Adaptive's output target to confirmed Gamescope display refresh, clamped to the Renderer’s 10–1000 FPS range. Applies live; unavailable or zero refresh uses the saved `target_fps`. Fixed ignores this choice. |
| `adaptive_max_multiplier` | `3` | Adaptive multiplier ceiling, from 2–5. |
| `adaptive_auto_base_fps_cap` | `false` | Enables Steady Base Cap behavior. MAKO Decky defaults this to `true`. |
| `adaptive_fractional_real_frame_priority` | `auto` | Fractional real-frame preference: `auto`, `low`, `medium`, `high`, or `very-high`. |
| `adaptive_stable_cadence` | `true` | Favours a stable validated cadence when available. |
| `gamescope_vrr_mode` | `follow-steam` | Per-profile live Gamescope VRR choice: `follow-steam`, `on`, or `off`. On and Off take effect only when Gamescope reports that the active display supports VRR; otherwise they are saved but leave the live setting untouched. |
| `base_fps_cap` | `0` | Real-frame cap; `0` disables it. Fixed Zink swapchains then use the [refresh-limited rate](ADAPTIVE-VALIDATION.md#integer-pacing-handoffs). |
| `frame_generation_refresh_threshold` | `0` | Pauses generation at or below a confirmed Gamescope refresh; `0` disables it. |
| `dynamic_cadence_recovery` | `false` | Rechecks native cadence for games that switch rates between gameplay and menus. Temporarily reduces generated work while sampling; checks can lower output FPS or cause hitches under sustained GPU load. |
| `dynamic_cadence_probe_interval_seconds` | `2.0` | Recovery interval from 0.1–3 seconds. |

See [Adaptive validation](ADAPTIVE-VALIDATION.md) for scheduling details.

### Scaling

| Setting | Default | Meaning |
| --- | --- | --- |
| `scaling_enabled` | `false` | Enables scaling at process start. Requires restart. |
| `scaling_method` | `mako` | `native`, `mako`, `ls1`, or `ls1-performance`. LS1 failures fall back to MAKO Scaler. |
| `scaling_factor` | `1.5` | Target output-to-source ratio per dimension from 1.0–2.0; the surface, display target, and resource limits may reduce the effective ratio. |
| `scaling_supersampling` | `false` | Allows supported variable Gamescope surfaces to render beyond the display target before downsampling. |
| `scaling_sharpness` | `0.8` | Sharpening strength from 0.0–1.0. |
| `hdr_quality` | `'performance'` | Live HDR quality: `'quality'` retains full-precision shader/scaling buffers; `'balanced'` uses compact shader/scaling buffers; with LSFG it retains full motion analysis and linear-light generation. `'performance'` also uses approximate HDR10 interpolation and halves motion Flow Scale for LSFG, bounded at 0.25. MAKO FG uses its own HDR processing and selected Flow Scale. Output resolution and saved Flow Scale are preserved. SDR ignores it. See [HDR quality](HDR-PIPELINE.md#hdr-quality). |
| `swapchain_image_count_compatibility` | `false` | Preserves the game's requested swapchain image minimum. Use only for games that otherwise fail to start. Requires restart. |
| `mailbox_presentation_compatibility` | `false` | Presents ordered Frame Generation through MAILBOX instead of FIFO when the driver advertises it. Use only for games that hang or fail with FIFO presentation; a newer frame can replace a generated frame before it is shown. Requires restart. |

### Performance and device selection

| Setting | Default | Meaning |
| --- | --- | --- |
| `gpu_headroom_percent` | `0` | GPU Headroom reserve: `0` (Off), `5`, `10`, `15`, or `20` percent. Live, Frame Generation only; see below. |
| `flow_scale` | `0.8` | Motion-estimation resolution from 0.25–1.0. MAKO FG applies it relative to the packaged analysis grid and requires a compatible operator shader. Lower values reduce GPU cost and may reduce quality; RGB output resolution stays unchanged. |
| `performance_mode` | `false` | Uses the lighter LSFG model, shown as **LSFG Performance** in the model picker. |
| `ultra_performance` | `false` | Restart-bound lighter preset for either FG model: 60% Flow Scale for MAKO FG and 70% for Lossless Scaling, active-policy-sized capacity, at least the Performance `mako_fg_mode`, the lighter LSFG model for Lossless Scaling, and MAKO Scaler when scaling is enabled. Respects the global precision choice. |
| `gpu` | Automatic | Optional GPU name, vendor/device ID, or PCI bus ID. Cross-GPU Frame Generation is unsupported. |
| `pacing` | `none` | Compatibility field; `none` is the only supported value. |

### GPU Headroom

GPU Headroom is a soft utilization target for the application's Vulkan GPU. A 10% reserve aims for roughly 90% utilization. It requires readable AMD `gpu_busy_percent` telemetry for that exact DRM render node; unsupported devices, missing permissions and stale readings retain normal pacing. Gamescope is not required. Readings cover the whole GPU, including the compositor and other applications, so this is neither a game-only meter nor a power or temperature limit.

Changes apply live while Frame Generation is active. Off stops Headroom sampling and removes only its additional cap; existing manual and Adaptive caps still apply. Brief spikes may reach 100%. Loading pauses and unavailable telemetry release the extra cap so stale measurements cannot keep throttling a new scene.

Fixed retains its selected multiplier, so reserving capacity may lower both real and output FPS. Releasing Headroom does not force a native-frame probe: ordinary Fixed/FIFO pacing can retain that generated workload after the scene becomes lighter, including with Headroom Off. Use Adaptive when restoring a higher real-frame rate after load changes is important. Adaptive can use more generated frames within its configured ceiling and existing qualification rules. Neither policy changes model quality, Flow Scale or clocks. The controller never deliberately caps below 20 real FPS; Adaptive additionally respects `Target FPS / available maximum multiplier`. For example, a 90 FPS target with a 2× ceiling cannot trade away real frames below 45 FPS. A higher permitted multiplier allows more room but can increase interpolation artifacts and input latency. The utilization target is not guaranteed when generation is expensive, another process consumes the GPU, or the floor is reached.

GPU Headroom is dormant with Frame Generation disabled or refresh-paused, during confirmed Steam-menu suspension, and during ordered-acquire recovery. It does not independently control scaling or shaders. Dynamic Cadence Recovery is mutually exclusive: enabling Headroom in either UI disables Recovery; enabling Recovery clears Headroom. Profile and separate power-mode settings preserve the reserve. Live Status distinguishes unavailable telemetry and a reached real-frame floor; diagnostics record decisions independently of the saved setting.

See [Adaptive validation](ADAPTIVE-VALIDATION.md#gpu-headroom) for controller sampling and recovery, and [Runtime transitions](RUNTIME-TRANSITIONS.md) for when edits take effect.

## Explicit launch selection

```text
MAKO_CONFIG="$HOME/.config/mako-render/conf.toml" MAKO_PROFILE="My game" ~/.local/bin/mako-launch %command%
```

`DISABLE_MAKO=1` bypasses MAKO. Native Remote Play uses `MAKO_FOLLOW_CURRENT_PROFILE=1` to follow a valid root `current_profile` selection on reload, falling back to `MAKO_PROFILE`. Ordinary game launchers use normal matching. See the [Remote Play lifecycle](../../plugin/docs/BACKEND-ARCHITECTURE.md#native-remote-play-lifecycle) for shared selection and shader-cache ownership.

## Environment-only configuration

With `MAKO_ENV=1`, `MAKO_GPU_HEADROOM_PERCENT=0|5|10|15|20` selects the same reserve. Other values are rejected.

Set `MAKO_ENV=1` to build one profile from environment variables instead of TOML:

- global: `MAKO_DLL_PATH`, `MAKO_NO_FP16`;
- Fixed and identity: `MAKO_GPU`, `MAKO_MULTIPLIER`, `MAKO_FRAME_GENERATION_PROVISIONED`, `MAKO_FRAME_GENERATION_ENABLED`, `MAKO_FRAME_GENERATION_REFRESH_THRESHOLD`, `MAKO_BASE_FPS_CAP`;
- Adaptive: `MAKO_ADAPTIVE`, `MAKO_ADAPTIVE_AUTO_BASE_FPS_CAP`, `MAKO_ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY`, `MAKO_TARGET_FPS`, `MAKO_ADAPTIVE_TARGET_REFRESH_RATE`, `MAKO_ADAPTIVE_MAX_MULTIPLIER`, `MAKO_ADAPTIVE_STABLE_CADENCE`, `MAKO_DYNAMIC_CADENCE_RECOVERY`, `MAKO_DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS`;
- Gamescope VRR: `MAKO_GAMESCOPE_VRR_MODE` (`follow-steam`, `on`, or `off`);
- HDR quality: `MAKO_HDR_QUALITY` (`quality`, `balanced`, or `performance`; defaults to `performance`);
- Scaling: `MAKO_SCALING_ENABLED`, `MAKO_SCALING_METHOD`, `MAKO_SCALING_FACTOR`, `MAKO_SCALING_SUPERSAMPLING`, `MAKO_SCALING_SHARPNESS`, `MAKO_SWAPCHAIN_IMAGE_COUNT_COMPATIBILITY`, `MAKO_MAILBOX_PRESENTATION_COMPATIBILITY`; and
- resources: `MAKO_ULTRA_PERFORMANCE`, `MAKO_FG_MODE`, `MAKO_FLOW_SCALE`, `MAKO_PERFORMANCE_MODE`, `MAKO_PACING`.

`WINE_DISABLE_VK_CHILD_WINDOW_RENDERING_HACK` defaults to `1` in MAKO Decky's game wrapper and standalone `mako-launch` when the Renderer is enabled. They set it before Wine starts and preserve explicit caller values, including `0` to opt out. This launch default bypasses the affected offscreen path on Proton 8 and 9; it is not a live Renderer setting or a guarantee of direct presentation on other versions. See [standalone setup](CONFIGURATION.md#proton-offscreen-windows) and the [Wine presentation contract](WSI-ISOLATION.md#wine-offscreen-windows).

`MAKO_DISABLE_HDR_EXPOSURE=1` disables HDR exposure by default. The restart-only HDR control exports `MAKO_DISABLE_HDR_EXPOSURE=0` and `DXVK_HDR=1` while retaining `DISABLE_GAMESCOPE_WSI=1`; see the [HDR launch contract](HDR-PIPELINE.md#toggle-and-launch-contract). Environment-only settings are process-start settings, not live profile controls.

## Presentation acquisition timeout

`MAKO_PRESENT_ACQUIRE_TIMEOUT_MS` limits how long MAKO waits for generated-image availability before native recovery. MAKO Decky uses 50 ms; standalone launches have no explicit default, although Adaptive VRR delivery and constrained Fixed image pools enforce a 50 ms limit. For a focused stall reproduction, try `25` and include the log. See [presentation recovery](LIFECYCLE.md) for deadline and fallback rules.

## Frame generation model

`mako_fg_mode` sets how often MAKO FG solves motion at 2× and 3×: `"quality"` every real-frame pair, `"balanced"` (default) one in two, `"performance"` one in three, or `"extra-performance"` one in four. Pairs in between carry the last motion forward and keep the last static-overlay map, which frees GPU time and can add artifacts. At 4× and above, MAKO FG solves every pair regardless of this setting. Ultra Performance raises Quality and Balanced to Performance and locks Flow Scale at 60%. It applies live through a private frame-generation rebuild, and Lossless Scaling ignores it. `MAKO_FG_MODE` sets it in environment-only mode. See [MAKO FG Mode](MAKO-FG.md#mako-fg-mode).

`frame_generation_model` selects `"mako-fg"` (default) or `"lossless-scaling"` per profile, including AC/battery overrides. Missing fields use the current default; an explicitly saved value remains a choice, including values written by an earlier editor with a different default. The Decky and Qt model pickers present Lossless Scaling as **LSFG Quality** and **LSFG Performance**, which also write `performance_mode`; that lighter-model switch stays live. An unavailable LSFG backend falls back to bundled MAKO FG at startup without rewriting this field. The model actually initialized is fixed at process startup and remains pending across swapchain recreation until restart. `MAKO_FRAME_GENERATION_MODEL` sets it in environment-only mode. `MAKO_FG_PACKAGE` overrides the installed MAKO FG release with an absolute package directory for a test process. Without it, MAKO FG uses the release installed beside the Renderer; the override does not affect LS1 or Lossless Scaling. See [model setup](MAKO-FG.md).
