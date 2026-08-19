# Configuration

Configure MAKO Renderer with `mako-ui` or by editing `~/.config/mako-render/conf.toml`. The UI writes the same TOML file. MAKO creates profiles so different games can use different settings.

## Profiles

Each `[[profile]]` section is a game profile. `active_in` can match a Linux binary name, Windows executable, process name, or the end of an executable path. Use `MAKO_PROFILE` when you need to select a profile explicitly instead of matching it automatically.

```toml
[[profile]]
name = "My game"
active_in = ["Game.exe"]
multiplier = 2
frame_generation_enabled = true
```

## Global settings

- **`dll`**: Optional full path to `Lossless.dll`. Leave it unset to use MAKO's normal Steam-library discovery.
- **`allow_fp16`**: Enables half-precision acceleration where it is useful. It is normally beneficial on AMD hardware; disable it for older NVIDIA GPUs if performance is worse.

## Profile settings

- **`name`**: Display name and value accepted by `MAKO_PROFILE`.
- **`active_in`**: Executables or process names that select this profile.
- **`multiplier`**: Fixed frame-generation multiplier, from 2x upward. The direct Renderer default is `2`.
- **`frame_generation_enabled`**: Live on/off switch. `false` presents real frames while keeping the layer loaded. Default: `true`.
- **`base_fps_cap`**: Caps the game's real frame rate before generation. `0` disables the cap; direct configuration accepts 1–1000 FPS.
- **`adaptive`**: Enables Adaptive Frame Generation. It varies generated frames toward `target_fps` and ignores the fixed `multiplier`. Default: `false`.
- **`adaptive_auto_base_fps_cap`**: In Adaptive mode, caps the real frame rate to half of `target_fps` for an even 2x baseline. It can trade real-frame headroom and responsiveness for steadier output. Default: `false`.
- **`target_fps`**: Adaptive displayed-frame-rate target. It is not a frame limiter; MAKO cannot reduce a game already rendering above the target or exceed the selected ceiling. Direct configuration accepts 10–1000. Default: `120`.
- **`adaptive_max_multiplier`**: Adaptive ceiling of 2x, 3x, or 4x. Start at 2x for image quality; use a higher ceiling only when the game benefits. Default: `3`.
- **`adaptive_stable_cadence`**: Prefers a constant interpolation cadence when it is sustainable. It can look smoother but may increase input lag. Default: `false`.
- **`flow_scale`**: Motion-vector resolution from 0.25 to 1.0. Lower is faster; higher favours image quality. Default: `1.0`.
- **`performance_mode`**: Uses a lighter model for lower GPU cost and more artifacts. Default: `false`.
- **`pacing`**: Presentation policy. `none` is the only supported value.
- **`gpu`**: Optional GPU name, vendor/device ID, or PCI bus ID. It must name the GPU used by the game; multi-GPU frame generation is not supported.

MAKO Decky uses its own safer UI defaults: 90 FPS Adaptive target, 0.90 Flow Scale, and Smooth Cadence enabled when it creates a profile. Those defaults do not change the direct Renderer defaults above.

## Applying changes

Frame Generation, Fixed/Adaptive mode, multiplier within existing capacity, Adaptive target/ceiling, and Smooth Cadence can usually apply while the game is running. Restart the game after changing the DLL path, FP16 policy, GPU, Flow Scale, Performance Mode, HDR-related settings, or a setting that requires more private GPU resources.

Test V-Sync both on and off for each game. It can steady the real-frame cadence, but can also add latency or conflict with an FPS cap, VRR, or the compositor.

## Environment variables

`ENABLE_MAKO=1` activates MAKO's implicit Vulkan layer only for the launched process. `DISABLE_MAKO=1` disables it. `MAKO_CONFIG` chooses a TOML file and `MAKO_PROFILE` chooses a named profile.

For a configuration that comes entirely from environment variables, set `MAKO_ENV=1` and use any of the following:

- `MAKO_DLL_PATH`, `MAKO_NO_FP16`, `MAKO_GPU`
- `MAKO_MULTIPLIER`, `MAKO_FRAME_GENERATION_ENABLED`, `MAKO_BASE_FPS_CAP`
- `MAKO_ADAPTIVE`, `MAKO_ADAPTIVE_AUTO_BASE_FPS_CAP`, `MAKO_TARGET_FPS`, `MAKO_ADAPTIVE_MAX_MULTIPLIER`, `MAKO_ADAPTIVE_STABLE_CADENCE`
- `MAKO_FLOW_SCALE`, `MAKO_PERFORMANCE_MODE`, `MAKO_PACING`

`MAKO_DISABLE_HDR_EXPOSURE=1` keeps MAKO's unfinished HDR path disabled. It is the normal boundary used by the current Decky workflow.
