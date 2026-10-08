# Configuration

Use **MAKO Renderer Configuration** (`mako-ui`) or edit `~/.config/mako-render/conf.toml`. The UI saves automatically, and the configuration format is `version = 2`.

## Quick start

1. Open `~/.local/bin/mako-ui`.
2. Create a profile and add the game's executable under **Matched Processes**. Alternatively, start the game normally, use **Detect Running Game…**, save the match, and close the game. Detection does not activate MAKO in the already-running process.
3. Configure Frame Generation, Scaling, and/or Shaders.
4. Add the launch option shown by the UI to the game's Steam properties. For a basic native or Proton profile, use `~/.local/bin/mako-launch %command%`.
5. Restart the game. The configuration window can be closed during play.

Options marked **Restart** apply on the next launch. See [Runtime transitions](RUNTIME-TRANSITIONS.md) for the complete live-update contract.

## Lossless Scaling availability

**MAKO Scaler and Shaders work without Lossless Scaling; only Frame Generation and LS1 require it.** All profile controls remain editable and save normally when the DLL is missing. For a setup without it, turn Frame Generation off and choose **MAKO Scaler** and/or **Shaders**. The configuration app shows an availability warning. To use Frame Generation or LS1, install Lossless Scaling or correct its DLL path, then restart the game.

## Profiles

Both Qt and MAKO Decky can create named profiles without a running game. **Create New Profile** in Qt copies the selected profile's Renderer settings, process matches, both optional power sets, shader selections, and custom shader configuration, then saves and selects the independent copy. Manually created copies do not inherit a Steam AppID association. Rename and delete preserve other profiles; Default (`mako`) cannot be renamed or deleted, and deletion returns to Default when present. Empty, reserved, invalid, and duplicate names show an error. A failed save keeps the previous selection and restores the profile files; correct file access before retrying.

Qt remains a standalone editor: you can browse saved profiles during play, and **Detect Running Game…** records executable matches rather than Decky's automatic Steam session/AppID association. Selecting a profile in Qt changes the editor and the saved selection used by Remote Play; ordinary local games keep their launch-time profile identity. Both editors use the same live and restart setting lifetimes. Edit shared profiles in one UI at a time.

Profiles are selected automatically through `active_in`, which may contain Linux executables, Windows executables, process names, or executable-path suffixes. `MAKO_PROFILE` selects an exact profile name and takes priority over automatic matching. Clients explicitly launched with `MAKO_FOLLOW_CURRENT_PROFILE=1` instead follow a valid root `current_profile` selection on each existing configuration reload, falling back to `MAKO_PROFILE` if that selection is unavailable. Native Remote Play opts into this behavior; ordinary game launchers do not.

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

### AC and battery settings

In the Qt UI, enable **Separate power settings** under **Power Profiles**, then choose **Editing settings for** to edit Handheld, Docked, or Base settings. Enabling it copies the existing Renderer settings into both power sets. Turning it off removes the two sets and restores Base settings. Selecting a set in either editor changes which values you edit; the running Renderer continues to select settings from the detected power source.

The optional `handheld` and `docked` tables belong to the preceding `[[profile]]`. Both tables must exist when the feature is enabled. They inherit omitted Renderer settings from that profile and cannot override its name or process matches. For example:

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

The Renderer selects Handheld on battery and Docked on confirmed AC power, including a charger without a dock. When any saved profile has power sets, one background sampler per configuration watcher reads Linux system power supplies at startup and on requests made at most once every two seconds, without rewriting configuration or requiring either UI to remain open. Startup waits up to 50 ms for its first reading; an unavailable or slower reading uses the base profile until confirmed power becomes available through the normal live/restart transition rules. Presentation consumes only the cached source and never waits for a supply read. A stalled read keeps the last confirmed source and cannot accumulate further requests. Removing all power sets stops further requests without joining a pending read in presentation. An external display alone does not select Docked. A transient read failure during play retains the last confirmed source. Peripheral batteries are ignored. Sandboxes without readable system power supplies use the base profile at startup.

Frame Generation, Scaling, GPU selection, and Renderer performance controls can differ. Process matching, global DLL/FP16 settings, Shaders, and launcher settings stay shared. Normal live, recreation, and restart requirements apply to every power switch. `MAKO_PROFILE` still selects the profile identity and then resolves its power set; `MAKO_ENV` remains an explicit environment-only configuration. Use current matching editors and Renderer builds: older editors may discard these optional tables when saving.

### Launcher exclusions

MAKO keeps known launcher and web-helper processes inactive while allowing their child games to match normally. The shared [launcher exclusion registry](../mako-common/launcher_exclusions.json) is the source for both MAKO Renderer and MAKO Decky. Contributors changing it must update [the compatibility ledger](../../CLEANUPS.md), run `just generate-launcher-exclusions`, and verify with `just check-launcher-exclusions`.

## Global settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `dll` | Automatic | Optional absolute path to `Lossless.dll`. LSFG and LS1 require it; MAKO Scaler and Native Resolution do not. |
| `allow_fp16` | `true` | Selects FP16 LSFG and MAKO Scaler colour arithmetic; `false` selects FP32. Ordinary builds fail explicitly when native FP16 LSFG is unavailable; experimental builds may force conversion of a supported FP32 graph with unqualified image quality. LS1 Quality and LS1 Performance always use FP32 independently of this setting. Coordinates and transport retain their required precision. Requires restart; Ultra Performance respects this choice. |

MAKO Decky and `mako-ui` share this configuration. Edit a profile in one UI at a time.

### Native Steam Remote Play

Native streams automatically use sRGB/SDR output while the override is enabled. Install the current Renderer helper before enabling it; this startup policy applies equally to Decky and Qt and does not change ordinary profiles or their live settings. See [SDR output](../../plugin/docs/REMOTE-PLAY.md#sdr-output) for diagnostics and validation limits.

The Qt **Remote Play** controls and MAKO Decky manage one opt-in native Steam override on the receiving device. Each stream starts with the saved ordinary profile, including normal Shaders, Scaling, Frame Generation, and optional AC/battery settings. Both editors can copy and select a manually named profile during a stream; later edits target that saved copy and it is reusable for future streams. The Renderer follows the optional root `current_profile` selection through its existing watcher without changing normal game process matching or live/restart lifetimes. Shader and launcher settings remain shared across power modes. The override is installation state outside `conf.toml`, so editing a profile alone does not enable it. Close streams before enabling, restoring, or updating the override; Decky's existing profile dropdown stays locked during a stream. See [Remote Play](../../plugin/docs/REMOTE-PLAY.md) for the shared owner, shader cache, launcher differences, and recovery limits.

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

See [Adaptive validation](ADAPTIVE-VALIDATION.md) for detailed scheduling and cadence behavior.

**Match Display Refresh Rate** sits directly below Target FPS in both UIs and is off by default. When enabled, Adaptive follows Gamescope's current display refresh and updates Target FPS automatically during play when you switch displays or change refresh rate. No game restart or open editor is required. The saved target is shown as a disabled **Fallback Target FPS**; turn matching off to edit it. Unavailable refresh feedback uses this saved fallback. Editor cap estimates use the fallback; Decky's Live Status shows the applied target. Matching reuses the existing compositor feedback cache and adds no probe or polling loop. Matching does not change display settings or Maximum Multiplier.

#### Choosing a mode in the Qt UI

**Real FPS** counts frames rendered by the game; **output FPS** also includes frames generated by MAKO. For example, Fixed 2x can request 120 output FPS from a 60 real FPS game when the Renderer and display can keep up. A Target FPS or multiplier is a goal or ceiling for MAKO's output plan, not a promise that the game or display will reach that rate.

| Qt control | Practical effect |
| --- | --- |
| **Fixed Multiplier** | Requests the selected 2x–5x total output ratio. `0x` pauses generation live without discarding the selected ratio. Start with 2x and increase only if the game and display benefit. |
| **Adaptive Frame Generation**, **Target FPS**, **Max Adaptive Multiplier** | Varies generated work toward the target, up to the multiplier ceiling. It may use less than the ceiling or miss an unreachable target; it does not slow a game already running above the target. |
| **Steady Base Cap** | Starts by capping real frames at half the target, such as 60 real FPS for a 120 FPS target, to favour an even cadence. With Smooth Cadence, a validated higher integer cadence can change that cap. This can cost responsiveness. |
| **Fractional Adaptive** and **Real Frame Priority** | Allow a changing mix of real and generated frames to keep more game frames. Higher explicit priority raises the real-frame cap and can reduce latency or artifacts, but pacing may feel less even. At a 120 FPS target, Low, Medium, High, and Very High cap real frames at 72, 80, 90, and 96 FPS respectively; these are ceilings, not guaranteed game rates. **Automatic** preserves the normal Fractional policy. |
| **Base FPS Cap** | Manually limits real game frames; `Off` adds no MAKO real-frame cap. Steady Base Cap or an explicit Fractional Real Frame Priority takes precedence while active. A cap does not make a game reach that rate. |
| **Smooth Cadence** | Favours a validated, even delivery pattern and can let ordered Gamescope presentation pace eligible modes. It can lower real FPS or responsiveness; compare it on and off in the affected game. |
| **Gamescope VRR** | **Follow Steam** leaves live VRR alone. **On** or **Off** temporarily requests that state while this game's MAKO Frame Generation session runs on supported Gamescope launches; MAKO restores the previous state on exit unless Steam or the user changed it during play. This requires `gamescopectl`, `xprop`, a working systemd user service, confirmed VRR support, and verified live root-property readback. Gamescope's VRR state is session-wide; this control does not set the game's FPS or the display refresh rate. |

Gamescope VRR feedback can affect an eligible pacing handoff, but it does not choose a higher multiplier or make a variable Fractional plan constant. [Adaptive validation](ADAPTIVE-VALIDATION.md#vrr-and-fixed-refresh-pacing-paths) describes the qualifying conditions and limits; neither the output plan nor its timestamps prove physical display scanout.

### Scaling

| Setting | Default | Meaning |
| --- | --- | --- |
| `scaling_enabled` | `false` | Enables scaling at process start. Requires restart. |
| `scaling_method` | `ls1` | `native`, `mako`, `ls1`, or `ls1-performance`. LS1 failures fall back to MAKO Scaler. |
| `scaling_factor` | `1.5` | Target output-to-source ratio per dimension from 1.0–2.0; the surface, display target, and resource limits may reduce the effective ratio. |
| `scaling_supersampling` | `false` | Allows supported variable Gamescope surfaces to render beyond the display target before downsampling. |
| `scaling_sharpness` | `0.8` | Sharpening strength from 0.0–1.0. |
| `swapchain_image_count_compatibility` | `false` | Preserves the game's requested swapchain image minimum. Use only for games that otherwise fail to start. Requires restart. |

### Performance and device selection

| Setting | Default | Meaning |
| --- | --- | --- |
| `flow_scale` | `0.8` | LSFG motion-estimation resolution from 0.25–1.0. Lower values reduce GPU cost and may reduce quality. |
| `performance_mode` | `false` | Uses the lighter LSFG model. |
| `ultra_performance` | `false` | Restart-bound lighter preset using 70% Flow Scale, the lighter model and LS1 Performance when scaling is enabled. Respects the global precision choice. |
| `gpu` | Automatic | Optional GPU name, vendor/device ID, or PCI bus ID. Cross-GPU Frame Generation is unsupported. |
| `pacing` | `none` | Compatibility field; `none` is the only supported value. |

### Qt Shaders and compatibility controls

The **Shaders** group uses MAKO's bundled vkBasalt. Enable it before launching the game, then select **Effects** in the order you want them applied; more effects use more GPU time. **HDR Look (SDR)** changes the appearance of an SDR image and does not enable HDR output. **Sharpening** offers CAS or DLS; **Sharpness** sets its strength, and **DLS Denoise** appears only with DLS. **Anti-aliasing** offers lighter, softer FXAA or more selective SMAA. Bundled and custom shader choices can change live after Shaders was enabled at launch; rebuilding a chain may cause a brief hitch. The displayed profile file is available for advanced vkBasalt settings.

Use **Add Custom Shader** under **Effects** to choose a local vkBasalt-compatible ReShade `.fx` file. Its **Custom Shader:** entry appears for the selected profile without being enabled automatically. Custom and bundled effects share the same ordered list; unchecking or clearing selections retains custom definitions and options. **Refresh** discovers entries added manually to the displayed profile file, such as `MyTone = "/home/deck/shaders/MyTone.fx"`. Before the first Effects edit, existing custom effects in its `effects` chain appear selected when the profile loads; subsequent activation and order are controlled through the UI. The first explicit Effects edit adopts those entries into UI control; unrelated edits preserve existing advanced chains.

The red **Delete selected custom shaders** button removes the selected custom definitions from this profile, deselects them, and updates the list immediately. It leaves bundled effects, unselected custom definitions, advanced options, other profiles, and original shader files in place. It is disabled when no custom effect is selected. Deletion saves pending edits first and restores the previous shader file and selections if the sidecar write fails; errors appear beside the controls. Shader settings remain shared across Battery, AC, and Base power sets. Deletion removes the selected custom effects from the running chain when Shaders was enabled at startup.

The UI references the original shader file. Keep its includes and textures accessible and configure `reshadeIncludePath`, `reshadeTexturePath`, and shader-specific options in the profile file when needed. Aliases are case-sensitive, begin with an ASCII letter, and contain letters, digits, or underscores, up to 128 characters; bundled aliases remain reserved. A selected entry with a removed definition remains visible as missing so it can be unchecked. Custom shaders need vkBasalt-compatible ReShade features. Selection, order, deletion, and configuration-file path or option edits apply live while Shaders is active; a missing file or compilation error retains the previous chain until a corrected configuration is saved. Editing source/include/texture files alone does not trigger reload; use **Refresh** to reload edited source, includes, and textures. Flatpak games need sandbox access to their files and dependencies. These controls and files are shared with [MAKO Decky custom shaders](../../plugin/docs/CONFIGURATION.md#custom-shaders).

Under **Compatibility**, **Auto-disable Frame Generation by Refresh Rate** pauses generation only when Gamescope confirms a refresh at or below the chosen threshold; it does nothing without refresh feedback. While paused, Steady Base Cap and target-derived Fractional Real Frame Priority caps are released; a saved manual Base FPS Cap still applies, or Off leaves real frames uncapped by MAKO. Generation and Adaptive caps resume above the threshold without changing saved choices. **Dynamic Cadence Recovery** periodically checks games that change native frame rate between scenes or menus; enabling it clears real-frame caps. **Game Swapchain Images** preserves the game's requested minimum when the normal allocation prevents startup, at the cost of generated-frame headroom. Leave these controls at their defaults unless they address a specific game.

## Desktop scaling and resolution

MAKO scales the image size presented by the game, which may already include the game's own upscaling. Set Steam's **Game Resolution** to the display maximum, then select a lower in-game resolution. Test Fullscreen, Borderless Fullscreen, and Windowed because the image each mode presents depends on the game. Check the actual source and presentation sizes in diagnostics; the in-game resolution alone does not prove MAKO receives a smaller image.

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

This example is for the user-local archive. The Qt UI generates the path for its installed launcher; the Arch package uses `/usr/bin/mako-launch %command%`.

For a direct desktop command, replace `%command%` with the executable and arguments. Flatpak applications require the matching runtime extension and [Flatpak preparation](FLATPAK-GUIDE.md); a host `mako-launch` prefix does not configure the sandbox.

Select another file or profile explicitly with:

```text
MAKO_CONFIG="$HOME/.config/mako-render/conf.toml" MAKO_PROFILE="My game" ~/.local/bin/mako-launch %command%
```

`DISABLE_MAKO=1` bypasses MAKO. If no profile matches, the Renderer stays inactive.

The Qt UI's per-profile **Shaders** controls use MAKO's private bundled vkBasalt and share the same profile files as MAKO Decky. When Shaders are enabled, copy the complete launch option displayed by the UI; it adds the required activation and isolated configuration path. Advanced users can edit that displayed file, and MAKO preserves options outside its compact controls. See [Optional graphics integrations](LAYER-CHAINING.md#standalone-mako-renderer-with-vkbasalt) for manual chaining and support boundaries.

The UI stores global **Disable Steam Overlay**, **Zink** and **Force ALSA** choices in `~/.config/mako-render/launcher.conf`. **Disable Steam Overlay (Restart)** appears under Performance Settings, defaults to on, and removes Steam overlay hooks for games started through `mako-launch`. It may improve smoothness but can affect Steam Input and overlay features; turn it off and restart to restore them. **Zink** routes Mesa OpenGL through Vulkan for OpenGL games that need it. **Force ALSA** selects SDL and Wine/Proton ALSA audio instead of their usual PulseAudio path and may help an affected game's audio or Zink compatibility. Zink and Force ALSA require a game restart and should be enabled only when useful. Steam Deck mode, Gamescope WSI, and MangoHud controls remain MAKO Decky features.

## Environment-only configuration

Set `MAKO_ENV=1` to build one profile from environment variables instead of TOML:

- global: `MAKO_DLL_PATH`, `MAKO_NO_FP16`;
- Fixed and identity: `MAKO_GPU`, `MAKO_MULTIPLIER`, `MAKO_FRAME_GENERATION_PROVISIONED`, `MAKO_FRAME_GENERATION_ENABLED`, `MAKO_FRAME_GENERATION_REFRESH_THRESHOLD`, `MAKO_BASE_FPS_CAP`;
- Adaptive: `MAKO_ADAPTIVE`, `MAKO_ADAPTIVE_AUTO_BASE_FPS_CAP`, `MAKO_ADAPTIVE_FRACTIONAL_REAL_FRAME_PRIORITY`, `MAKO_TARGET_FPS`, `MAKO_ADAPTIVE_TARGET_REFRESH_RATE`, `MAKO_ADAPTIVE_MAX_MULTIPLIER`, `MAKO_ADAPTIVE_STABLE_CADENCE`, `MAKO_DYNAMIC_CADENCE_RECOVERY`, `MAKO_DYNAMIC_CADENCE_PROBE_INTERVAL_SECONDS`;
- Scaling: `MAKO_SCALING_ENABLED`, `MAKO_SCALING_METHOD`, `MAKO_SCALING_FACTOR`, `MAKO_SCALING_SUPERSAMPLING`, `MAKO_SCALING_SHARPNESS`, `MAKO_SWAPCHAIN_IMAGE_COUNT_COMPATIBILITY`; and
- resources: `MAKO_ULTRA_PERFORMANCE`, `MAKO_FLOW_SCALE`, `MAKO_PERFORMANCE_MODE`, `MAKO_PACING`.

`MAKO_DISABLE_HDR_EXPOSURE=1` and `DISABLE_GAMESCOPE_WSI=1` close the unfinished HDR path. Environment-only settings are process-start settings, not live profile controls.
