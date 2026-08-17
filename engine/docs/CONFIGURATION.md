# Configuration Options

Configuring MAKO Renderer is done either through **mako-ui** (graphical interface) or by manually editing the configuration file located at `~/.config/mako-render/conf.toml`.

Regardless of the method you choose, the concept of profiles remains the same.
- **Profiles**: Profiles allow you to create different sets of configurations for different applications or use cases. A profile can automatically be selected through the "active_in" property.
- **Profile Settings**: Settings related to a profile are stored under the "Profile Settings" or `[[profile]]` section.
- **Global Settings**: Any settings in the "Global Settings" or `[global]` section apply to all profiles.

### All Configuration Options

Below is a list of all available **global** configuration options:
- **Path to Lossless Scaling / `dll`**: By default, MAKO Renderer will search certain directories for Lossless Scaling. If you have Lossless Scaling installed in a custom location, you can specify the full path to the "Lossless.dll" file inside of Lossless Scaling here.
- **Allow half-precision / `allow_fp16`**: If enabled, this will allow MAKO Renderer to take advantage of half-precision shader operations if supported by the GPU. This has a giant performance uplift on AMD GPUs, but does not affect NVIDIA GPUs (GTX 1000-series or older cards will actually see a big performance **decrease**). This option **does not** influence quality. (Default: `true`)

Next is a list of all available **profile** configuration options:
- **Profile Name / `name`**: The name of the profile, displayed in **mako-ui**. Additionally, this is used when selecting a profile through the `MAKO_PROFILE` environment variable.
- **Active In / `active_in`**: A list of 1) linux binary names, such as `mpv`, 2) windows executables, such as `GenshinImpact.exe` and 3) process names, such as `GameThread`. It is also possible to specify the last part of a path (e.g. `Ghostrunner2/Binaries/Win64/Ghostrunner2-Win64-Shipping.exe`). When a process matching one of these rules is detected, this profile will be activated.
- **Multiplier / `multiplier`**: The frame generation multiplier. A value of 3 means that for every frame rendered by the application, MAKO Renderer will generate 2 additional frames. (Default: `2`)
- **Frame Generation / `frame_generation_enabled`**: Live synthesis switch. Set this to `false` to present the game's
  real frames directly without model scheduling or per-swapchain interpolation resources. The Vulkan layer and shared
  backend remain loaded so the selected Fixed or Adaptive mode can resume when this returns to `true`. (Default:
  `true`)
- **Adaptive Frame Generation / `adaptive`**: Experimental opt-in mode that varies between zero and three generated
  frames per real frame to approach `target_fps` as an average. Fixed `multiplier` is ignored while this is enabled.
  This independent Vulkan-layer scheduler does not include Lossless Scaling's Windows Queue Target modes. It cannot
  reduce a game already rendering above the target and cannot exceed the configured maximum multiplier or 4x the
  base framerate. (Default: `false`)
- **Adaptive Target / `target_fps`**: Desired displayed framerate for Adaptive mode. Cap the game separately if its
  real framerate can exceed this value. Targets above the configured multiplier limit cannot be reached. (Default: `120`)
- **Maximum Adaptive Multiplier / `adaptive_max_multiplier`**: Limits Adaptive mode to 2x, 3x, or 4x total output.
  Every real frame is still presented. If the target would require a higher ratio, output remains below the target
  instead of adding the more artifact-prone generated frames. Adaptive also ramps toward this limit after startup or
  recovery and can temporarily reduce it when added generation load harms real-frame throughput. If the compositor's
  cadence divisor makes the first step misleading, it can make one bounded bridge test at the next step. Rejected
  first-step probes wait 15 seconds; interrupted probes can rearm after two seconds of stable cadence. Repeated
  failures at a higher multiplier back off progressively from 5 to 15, 30, and then 60 seconds, unless the measured
  base rate improves by at least 15%. After a generated-image recovery, Adaptive preserves the last validated level
  through the safety warm-up and waits five seconds before probing a higher level. Multiplier policy is frozen while
  generated output is bypassed, so recovery cannot falsely accept a level that was not actually running. When Smooth
  Cadence is enabled, a modest fractional target such as 60 -> 90 can validate a constant cadence to avoid alternating
  real-only and generated frames; it returns to strict target scheduling if the higher constant workload is not
  sustainable. An abrupt menu, focus, or display transition preserves the previous real-rate baseline and proven
  generation level. Adaptive restores that level only after one second at least 90% of the earlier base rate; after
  five seconds without recovery it discards the stale baseline and ramps cleanly from zero. After restoration, the
  recovered real-only rate remains the delayed-load baseline; if the restored level then collapses throughput,
  Adaptive measures one second without generated work and returns to the lower proven level. A validated level that
  reaches at least 95% of the target is treated as sufficient, and a remaining deficit must persist for one second
  before a higher multiplier is tested. (Default: `3`)
- **Smooth Cadence / `adaptive_stable_cadence`**: When enabled, Adaptive may use the validated constant-cadence policy
  described above instead of alternating generated-frame counts to match a fractional target exactly. Strict scheduling
  settles first, and constant cadence is considered only when it already needs at least 95% of the corresponding integer
  output count. A severe sustained collapse starts one second of real-only measurement; Adaptive then resumes fractional
  scheduling or tests one higher level when the configured maximum permits it. Rescue has a 15-second cooldown and never
  exceeds the selected maximum. Constant cadence can lower the real-frame presentation rate and feel less responsive,
  even when displayed motion is smoother. Leave Smooth Cadence disabled to use strict target scheduling while retaining
  the remaining Adaptive recovery, load shedding, multiplier limits, and retry backoff. (Default: `false`)
- **Flow Scale / `flow_scale`**: The resolution scale at which the motion vectors are calculated. A lower value means better performance, but worse quality. (Default: `1.0`)
- **Performance Mode / `performance_mode`**: When enabled, a significantly lighter frame generation model is used. This has a minor quality impact, but greatly improves performance.
(Default: `false`)
- **Pacing Mode / `pacing`**: This option is explained in greater detail below. Supported values are **None / `none`**.
- **GPU / `gpu`**: The GPU to use for frame generation. This MUST be the **same GPU** as the one being used by the application. **Dual GPU is NOT supported**. You can identify a GPU through its name (e.g. `NVIDIA GeForce RTX 3080`), uppercase-only ID (e.g. `0x10DE:0x2C02`) or PCI bus ID (e.g. `3:0.0`). If not specified, the primary GPU will be used, which may lead to issues.

"Frame Generation", Fixed/Adaptive mode, "Multiplier", "Adaptive Target", "Maximum Adaptive Multiplier", and "Smooth Cadence" can be **hot-reloaded** when the active context has the required reserved capacity. Fixed and Adaptive share one private output set, so these ordinary controls do not invalidate or recreate the game-owned swapchain. Flow Scale, Performance Mode, GPU selection, a capacity increase beyond the reserved set, and an HDR encoding change remain pending until the game naturally recreates its swapchain; restart the game when an immediate deterministic change is required. The layer never returns an out-of-date result merely because a Decky setting changed. Global DLL and FP16 changes still require a process restart because they alter the shared backend instance.

HDR10 transport packing is automatic and has no setting. When both the application and backend Vulkan devices report
the exact external-image and packed-storage capabilities required by the engine, the private source/output exchange
images use 32-bit packed HDR10 instead of 64-bit float. PQ decoding, the LSFG model, and all temporal working images
remain linear 16-bit float. If either capability check fails, the validated float HDR10 transport remains in use.

### Pacing Modes

**Pacing modes** determine how MAKO Renderer synchronizes frame generation with the application's frame rate.

V-Sync can help when it gives MAKO Renderer more evenly spaced real frames to work with, which can make generated output feel
smoother. It can also add input lag or work poorly with a game's FPS cap, VRR, or compositor, so test it enabled and
disabled for each game and keep the setting that feels best.

For the normal SDR path, `none` uses MAKO Renderer's private FIFO output transport. The layer reserves swapchain capacity for
generated images and presents the generated/real sequence in order. That output ordering is separate from the game's
V-Sync setting. HDR-capable Gamescope swapchains preserve their separate WSI presentation contract, so this SDR detail
does not describe the experimental HDR path.

Here are all available pacing modes:
- `none`: Uses the established ordered SDR FIFO transport. It may require workarounds on some compositors.
- *... there are no other pacing modes yet ...*

### Environment Variables

The following environment variables affect MAKO Renderer:
- `ENABLE_MAKO`: Set to `1` in the MAKO launch wrapper to scope its uniquely named
  implicit Vulkan layer to that game.
- `DISABLE_MAKO`: If set to `1`, MAKO Renderer will be completely disabled.
- `DISABLE_LSFGVK` and `DISABLE_LSFG`: Disable the two public LSFG layer identities. The MAKO Decky wrapper
  sets both so only its private engine is active for the wrapped game.
- `MAKO_CONFIG`: Path to the configuration file.
- `MAKO_PROFILE`: Name of the profile to use. If set, this will override automatic profile detection.
- `MAKO_DISABLE_HDR_EXPOSURE`: Set to `1` by the companion Decky plugin's default restart-time SDR launch, or by a
  direct launcher that wants the same hard boundary. It overrides Gamescope/DXVK HDR capability and keeps every HDR
  evidence path disabled for that process. HDR-capable launches do not
  force the engine into HDR: application colour-space feedback or HDR metadata must still confirm application intent.
- `MAKO_PRESENT_ACQUIRE_TIMEOUT_MS`: Optional timeout for generated-image acquisition. A timeout enters the
  Gamescope presentation fallback; unset or `0` keeps the normal unbounded acquisition path.
- `MAKO_PRESENT_DIAGNOSTICS`: Set to `1` to log slow presentation operations.
- `MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS`: Minimum duration in milliseconds reported by presentation diagnostics.

If you do not wish to use a configuration file, you can also set configuration options through environment variables. To do this, set `MAKO_ENV=1` and then any of the following variables:
- `MAKO_DLL_PATH`: Path to Lossless Scaling DLL.
- `MAKO_NO_FP16`: If set to `1`, half-precision will be disabled.
- `MAKO_MULTIPLIER`: Frame generation multiplier.
- `MAKO_FRAME_GENERATION_ENABLED`: Set to `0` for live real-frame passthrough. Unlike
  `DISABLE_MAKO`, the layer remains loaded.
- `MAKO_ADAPTIVE`: Set to `1` to enable Adaptive Frame Generation.
- `MAKO_TARGET_FPS`: Adaptive displayed-framerate target.
- `MAKO_ADAPTIVE_MAX_MULTIPLIER`: Maximum Adaptive multiplier from `2` to `4`.
- `MAKO_ADAPTIVE_STABLE_CADENCE`: Set to `1` to enable Smooth Cadence in Adaptive mode. It defaults to disabled.
- `MAKO_FLOW_SCALE`: Flow scale value.
- `MAKO_PERFORMANCE_MODE`: If set to `1`, performance mode will be enabled.
- `MAKO_PACING`: Pacing mode to use.
- `MAKO_GPU`: GPU to use for frame generation.
