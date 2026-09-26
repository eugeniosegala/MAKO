# Configuration

Use **MAKO Renderer Configuration** (`mako-ui`) or edit `~/.config/mako-render/conf.toml`. The UI saves automatically, and the configuration format is `version = 2`.

## Quick start

1. Open `~/.local/bin/mako-ui`.
2. Create a profile and add the game's executable under **Matched Processes**. Alternatively, start the game normally, use **Detect Running Game…**, save the match, and close the game. Detection does not activate MAKO in the already-running process.
3. Configure Frame Generation, Scaling, and/or Shaders.
4. Add the launch option shown by the UI to the game's Steam properties. For a basic native or Proton profile, use `~/.local/bin/mako-launch %command%`.
5. Restart the game. The configuration window can be closed during play.

Options marked **Restart** apply on the next launch. See [Runtime transitions](RUNTIME-TRANSITIONS.md) for the complete live-update contract.

## Profiles

Profiles are selected automatically through `active_in`, which may contain Linux executables, Windows executables, process names, or executable-path suffixes. `MAKO_PROFILE` selects an exact profile name and takes priority over automatic matching.

If detection cannot find a game, use **Show all applications** or add the executable manually. Match the rendering executable, not a launcher title, Steam display name, ROM filename, or Flatpak application ID.

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

### Launcher exclusions

MAKO keeps known launcher and web-helper processes inactive while allowing their child games to match normally. The shared [launcher exclusion registry](../mako-common/launcher_exclusions.json) is the source for both MAKO Renderer and MAKO Decky. Contributors changing it must update [the compatibility ledger](../../CLEANUPS.md), run `just generate-launcher-exclusions`, and verify with `just check-launcher-exclusions`.

## Global settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `dll` | Automatic | Optional absolute path to `Lossless.dll`. LSFG and LS1 require it; MAKO Scaler and Native Resolution do not. |
| `allow_fp16` | `true` | Allows LSFG FP16 when supported by the selected GPU. Set `false` for FP32. Requires restart. |

MAKO Decky and `mako-ui` share this configuration. Edit a profile in one UI at a time.

## Profile settings

### Frame Generation

| Setting | Default | Meaning |
| --- | --- | --- |
| `frame_generation_provisioned` | `true` | Prepares Frame Generation at process start. Set `false` for Scaling-only use. Requires restart. |
| `frame_generation_enabled` | `true` | Live execution state. `false` is the UI's `0x` position. |
| `multiplier` | `2` | Fixed total output multiplier, from 2–5. |
| `adaptive` | `false` | Enables Adaptive instead of Fixed generation. |
| `target_fps` | `120` | Adaptive output target. MAKO Decky uses a 90 FPS product default. |
| `adaptive_max_multiplier` | `3` | Adaptive multiplier ceiling, from 2–5. |
| `adaptive_auto_base_fps_cap` | `false` | Enables Steady Base Cap behavior. |
| `adaptive_fractional_real_frame_priority` | `auto` | Fractional real-frame preference: `auto`, `low`, `medium`, `high`, or `very-high`. |
| `adaptive_stable_cadence` | `true` | Favours a stable validated cadence when available. |
| `base_fps_cap` | `0` | Real-frame cap; `0` disables it. |
| `frame_generation_refresh_threshold` | `0` | Pauses generation at or below a confirmed Gamescope refresh; `0` disables it. |
| `dynamic_cadence_recovery` | `false` | Rechecks native cadence for games that switch rates between gameplay and menus. |
| `dynamic_cadence_probe_interval_seconds` | `2.0` | Recovery interval from 0.1–3 seconds. |

See [Adaptive validation](ADAPTIVE-VALIDATION.md) for detailed scheduling and cadence behavior.

### Scaling

| Setting | Default | Meaning |
| --- | --- | --- |
| `scaling_enabled` | `false` | Enables scaling at process start. Requires restart. |
| `scaling_method` | `ls1` | `native`, `mako`, `ls1`, or `ls1-performance`. LS1 failures fall back to MAKO Scaler. |
| `scaling_factor` | `1.5` | Source-to-output ratio from 1.0–2.0. |
| `scaling_supersampling` | `false` | Allows supported variable Gamescope surfaces to render beyond the display target before downsampling. |
| `scaling_sharpness` | `0.8` | Sharpening strength from 0.0–1.0. |
| `swapchain_image_count_compatibility` | `false` | Preserves the game's requested swapchain image minimum. Use only for games that otherwise fail to start. Requires restart. |

### Performance and device selection

| Setting | Default | Meaning |
| --- | --- | --- |
| `flow_scale` | `0.8` | LSFG motion-estimation resolution from 0.25–1.0. Lower values reduce GPU cost and may reduce quality. |
| `performance_mode` | `false` | Uses the lighter LSFG model. |
| `ultra_performance` | `false` | Restart-bound lighter preset using 70% Flow Scale, the lighter model, FP16 permission, and LS1 Performance when scaling is enabled. |
| `gpu` | Automatic | Optional GPU name, vendor/device ID, or PCI bus ID. Cross-GPU Frame Generation is unsupported. |
| `pacing` | `none` | Compatibility field; `none` is the only supported value. |

## Desktop scaling and resolution

MAKO scales the image size presented by the game, which may already include the game's own upscaling. Set Steam's **Game Resolution** to the display maximum, then select a lower in-game resolution. Some games require Windowed mode because fullscreen or borderless keeps a display-sized image.

On a fixed 1920×1080 surface, a 1.5x factor advertises a 1280×720 source. On a variable desktop surface, set the game itself to 1280×720 and use 1.5x to request 1920×1080 output. Raising the factor without lowering the game resolution enlarges the output and can increase GPU and memory cost.

Gamescope is recommended because it provides a reliable output target. Outside Gamescope, the desktop compositor may scale the result again. Confirm activation by checking that diagnostics report different source and presentation sizes. See [Scaling](SCALING.md) for supported surfaces and fallback behavior.

## Applying changes

| Boundary | Common settings |
| --- | --- |
| **Live** | Frame Generation execution, multiplier within available capacity, Adaptive controls, target, caps, cadence, and recovery |
| **Private resource rebuild** | Scaling method and sharpness, Flow Scale, Lighter FG Model, and some multiplier-capacity changes |
| **May wait for swapchain recreation** | Scale Factor, Quality Supersampling, and capacity growth beyond current headroom |
| **Process restart** | Frame Generation provisioning, Scaling enablement, DLL, FP16, GPU, Ultra Performance, swapchain compatibility, layer activation, and launcher settings |

Live-safe settings still apply when the same save also contains a restart-bound change. Failed private replacements keep the previous working resources. See [Runtime transitions](RUNTIME-TRANSITIONS.md) for exact behavior.

## Format compatibility

MAKO accepts only `version = 2`. Unknown keys are ignored and removed the next time a UI saves the file. Do not add launcher, shader, or HDR options to `conf.toml`; use the relevant UI controls or supported launcher configuration instead.

## Standalone launcher

Saving a profile does not activate MAKO. Native Steam and Proton games must start through:

```text
~/.local/bin/mako-launch %command%
```

For a direct desktop command, replace `%command%` with the executable and arguments. Flatpak applications require the matching runtime extension and [Flatpak preparation](FLATPAK-GUIDE.md); a host `mako-launch` prefix does not configure the sandbox.

Select another file or profile explicitly with:

```text
MAKO_CONFIG="$HOME/.config/mako-render/conf.toml" MAKO_PROFILE="My game" ~/.local/bin/mako-launch %command%
```

`DISABLE_MAKO=1` bypasses MAKO. If no profile matches, the Renderer stays inactive.

The Qt UI's per-profile **Shaders** controls use MAKO's private bundled vkBasalt and share the same profile files as MAKO Decky. When Shaders are enabled, copy the complete launch option displayed by the UI; it adds the required activation and isolated configuration path. Advanced users can edit that displayed file, and MAKO preserves options outside its compact controls. See [Optional graphics integrations](LAYER-CHAINING.md#standalone-mako-renderer-with-vkbasalt) for manual chaining and support boundaries.

The UI stores global **Zink** and **Force ALSA** choices in `~/.config/mako-render/launcher.conf`. Steam Deck mode, Gamescope WSI, and MangoHud controls remain MAKO Decky features.

## Environment-only configuration

Set `MAKO_ENV=1` to build one profile from environment variables instead of TOML:

- global: `MAKO_DLL_PATH`, `MAKO_NO_FP16`;
- Fixed and identity: `MAKO_GPU`, `MAKO_MULTIPLIER`, `MAKO_FRAME_GENERATION_PROVISIONED`, `MAKO_FRAME_GENERATION_ENABLED`, `MAKO_FRAME_GENERATION_REFRESH_THRESHOLD`, `MAKO_BASE_FPS_CAP`;
- Adaptive: `MAKO_ADAPTIVE`, `MAKO_ADAPTIVE_AUTO_BASE_FPS_CAP`, `MAKO_ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY`, `MAKO_TARGET_FPS`, `MAKO_ADAPTIVE_MAX_MULTIPLIER`, `MAKO_ADAPTIVE_STABLE_CADENCE`, `MAKO_DYNAMIC_CADENCE_RECOVERY`, `MAKO_DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS`;
- Scaling: `MAKO_SCALING_ENABLED`, `MAKO_SCALING_METHOD`, `MAKO_SCALING_FACTOR`, `MAKO_SCALING_SUPERSAMPLING`, `MAKO_SCALING_SHARPNESS`, `MAKO_SWAPCHAIN_IMAGE_COUNT_COMPATIBILITY`; and
- resources: `MAKO_ULTRA_PERFORMANCE`, `MAKO_FLOW_SCALE`, `MAKO_PERFORMANCE_MODE`, `MAKO_PACING`.

`MAKO_DISABLE_HDR_EXPOSURE=1` and `DISABLE_GAMESCOPE_WSI=1` close the unfinished HDR path. Environment-only settings are process-start settings, not live profile controls.
