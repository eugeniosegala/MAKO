# Optional graphics integrations

Use MAKO's managed integration controls. Restart the game after changing layer activation.

## Default MAKO Decky launch

Keep the normal Steam launch option:

```text
/home/deck/.local/bin/mako-run %command%
```

**Disable Steam Overlay (Restart)** is on by default and may affect Steam Input. Turn it off to restore Steam's in-game overlay and FPS counter. The standalone Qt UI has the same option globally for `mako-launch`.

## Standalone MAKO Renderer with vkBasalt

vkBasalt is bundled; no separate installation is needed.

1. Open **MAKO Renderer Configuration** and select the game's profile.
2. Enable **Shaders**.
3. Copy the complete launch option shown by the UI into Steam.
4. Restart the game.

For manual native or Proton activation:

```text
ENABLE_VKBASALT=1 ~/.local/bin/mako-launch %command%
```

To choose an advanced vkBasalt configuration:

```text
ENABLE_VKBASALT=1 VKBASALT_CONFIG_FILE="$HOME/.config/vkBasalt/game-name.conf" ~/.local/bin/mako-launch %command%
```

Flatpak apps require [Flatpak shader setup](FLATPAK-GUIDE.md#optional-private-vkbasalt-chain). To disable Shaders, turn them off in the UI and use its updated launch option:

```text
~/.local/bin/mako-launch %command%
```

## Standalone MAKO Renderer with MangoHud

Select a profile in the Qt UI and enable **External Tools → Enable MangoHud (Restart)**. Install host MangoHud and Python 3 first; **Refresh MangoHud** checks availability without blocking the UI. Copy the updated launch option into Steam and restart the game. Disabling MangoHud also requires copying the updated option. The profile selection is stored in the same `profile-wrapper-settings.json` field used by Decky.

For manual native or Proton activation:

```text
MANGOHUD=1 ~/.local/bin/mako-launch %command%
```

MangoHud counts every output frame, including generated ones. To count only the game's own frames, enable **Count Real Frames Only (Restart)** or add `MAKO_MANGOHUD_REAL_FRAMES=1` to a manual command; see [MangoHud frame counting](#mangohud-frame-counting).

MangoHud and Shaders are mutually exclusive. The UI switches the selected integration without deleting shader settings; a manual command requesting both is rejected. Host MangoHud is not enabled inside Flatpak or for a direct `flatpak run` command.

`mako-launch --check-mangohud` reports availability as JSON without writing files. Native launch checks only the named `MangoHud.x86_64.json` and `MangoHud.x86.json` manifests in the standard XDG configuration/data locations. It verifies each layer's identity, architecture, library path and activation gates, resolves relative library paths, and stages only validated manifests under `$XDG_CACHE_HOME/mako-render/mangohud` (normally `~/.cache/mako-render/mangohud`). The directory is private to the user; concurrent launches are serialized during staging and unchanged files are not rewritten. Missing or invalid installations leave the game running without MangoHud and produce a diagnostic. The launcher never adds the host's full implicit-layer directory.

## MAKO Decky integrations

### Gamescope WSI compatibility

Try **Gamescope WSI (Restart)** for motion artifacts or presentation problems in supported 64-bit Gamescope games. Close the game before changing it, then check **Live Status** after relaunching.

Unsupported: Desktop Mode, 32-bit WSI presentation, unprepared Flatpaks, mismatched nested Wayland sessions, and HDR.

### MangoHud and Shaders

Save or select the game's profile, enable **Shaders** in its tab or **MangoHud** under **External Tools**, then restart.

MangoHud and Shaders cannot run together. MangoHud requires a host installation and is unavailable inside Flatpak games. Shaders use bundled vkBasalt; prepare Flatpak apps in **Flatpak Setup** first.

Shader selections apply live after activation. More effects cost more GPU time; **HDR Look (SDR)** does not enable HDR. Use **Refresh** after editing shader files. See [MAKO Decky configuration](../../plugin/docs/CONFIGURATION.md#shaders) for controls and custom shaders.

## Shared MangoHud layout

Both Qt and MAKO Decky show shared-layout instructions under **External Tools → Enable MangoHud (Restart)**. In Decky, select **Show info** if descriptions are hidden. Create the configuration file manually; the toggle does not create it.

To use one custom MangoHud layout across your games, create `~/.config/mako-render/mangohud.conf` with ordinary MangoHud options, for example:

```ini
fps
frametime
position=top-right
```

In Decky, keep the usual `mako-run %command%` launch option and enable **MangoHud** in each profile that should use it. In Qt, use the complete copied launch option, including `MANGOHUD=1`. Both launchers respect `XDG_CONFIG_HOME` for the shared file location. On each supported host launch, MAKO selects this file if it is a readable regular file. Creating or removing it takes effect on the next game launch without resaving profiles. Missing, unreadable, or non-file paths leave the inherited configuration unchanged. MAKO does not create, edit, or execute the file.

The shared file replaces any inherited `MANGOHUD_CONFIGFILE`, including Gaming Mode's Gamescope preset file. This changes only the launched game's MangoHud configuration; Steam's separate performance overlay remains under Steam's control. Existing `MANGOHUD_CONFIG` values retain MangoHud's precedence: without `read_cfg`, that variable can bypass file loading; with `read_cfg`, its options override file values. For a per-game file exception, apply the override after MAKO's wrapper:

```text
/home/deck/.local/bin/mako-run env MANGOHUD_CONFIGFILE=/absolute/path/to/game.conf %command%
```

For standalone launches, use `MANGOHUD=1 ~/.local/bin/mako-launch env MANGOHUD_CONFIGFILE=/absolute/path/to/game.conf %command%`. Quote paths containing spaces. The shared file does not enable MangoHud by itself or add Flatpak or simultaneous Shader support. Choosing a layout adds no MAKO per-frame work. MangoHud's own cost depends on its metrics.

## MangoHud frame counting

MangoHud's position relative to MAKO Renderer decides what it counts. **Count Real Frames Only (Restart)**, shown under MangoHud in both MAKO Decky and the Qt UI, selects it per profile:

| Setting | MangoHud position | Counter | HUD |
| --- | --- | --- | --- |
| Off (default) | Below MAKO | Every output frame, generated ones included | Drawn on each output frame at output resolution |
| On | Above MAKO | The game's own frames; pair it with Steam's performance overlay or top bar, which report MAKO's output rate | Drawn into the game's frame, so it passes through Frame Generation and Scaling. Changing numbers and graphs can show Frame Generation artifacts, and Scaling upscales the HUD from the game's rendering resolution |

A MangoHud `fps_limit` follows the same side: below MAKO it caps MAKO's output, above MAKO it caps the game's frames. The Qt UI adds `MAKO_MANGOHUD_REAL_FRAMES=1` to the copied launch option when the setting is on.

Whenever the Renderer is active and a 64-bit MangoHud manifest is available, both launchers list MangoHud and MAKO explicitly in `VK_INSTANCE_LAYERS`, including the guarded 32-bit identity when installed. When MangoHud is the list's only reason, they also keep `ENABLE_MAKO=1` and `MANGOHUD=1` and put MangoHud's guarded directory on the matching side of MAKO's in `VK_IMPLICIT_LAYER_PATH`. Older Vulkan loaders, such as 1.3.239 in Steam's older runtimes or a game's bundled copy, never enable an implicit layer from the list alone; 1.4.309 and newer do. With the gates set, the Steam Linux Runtime's loader orders these layers by discovery instead of by the list, which the directory order matches. A loader that ignores `VK_IMPLICIT_LAYER_PATH`, as 1.3.239 does, keeps MangoHud below MAKO, so **Count Real Frames Only** counts every output frame there. Gamescope WSI and kept Desktop Steam overlay chains keep both gates off and need a loader that enables implicit layers from the list. A 32-bit-only MangoHud installation, Flatpak games, and launches without an active Renderer keep implicit activation. MAKO Decky Remote Play launches use the setting saved in the generated `mako-run`. Shaders (vkBasalt) stay below MAKO so effects apply to every output frame.

## Support boundaries

| Integration | Support |
| --- | --- |
| Steam Vulkan FPS overlay | Desktop Mode native/Proton launches with **Disable Steam Overlay** off |
| MangoHud | Host games; cannot run with MAKO Shaders |
| vkBasalt | Bundled for native/Proton and prepared Flatpak apps |
| XR Gaming / Breezy | Compatibility unvalidated |
| ReShade / OptiScaler through Proton | Game-specific; disable their Frame Generation |
| RenderDoc | Developer diagnostics only |
| OBS Vulkan Capture / other Vulkan layers | Unsupported |
| Other Frame Generation layers | Do not combine with MAKO |

## XR Gaming / Breezy

Gamescope effects and Vulkan-only mode use different paths; MAKO's WSI toggle does not select between them. Compatibility with glasses is unvalidated.

For Anchor/Follow lag, compare MAKO alone, XR alone, and both. Record the XR effect off for 30 seconds, on for 30 seconds, then off again, keeping the scene and settings unchanged. Send the actual Breezy runtime archive with [MAKO diagnostics](../../COLLECT_DIAGNOSTICS.md).

## Game-local Proton integrations

For Windows ReShade or OptiScaler, keep the integration's documented DLL overrides and use your normal MAKO launch command. Disable its Frame Generation while MAKO handles generation. Compatibility varies by game.

## Troubleshooting and rollback

Disable the optional integration and restart if startup, effects, or pacing worsen. If the problem remains, follow [Decky troubleshooting](../../plugin/docs/TROUBLESHOOTING.md) or [standalone troubleshooting](TROUBLESHOOTING.md).

For implementation details, see [WSI isolation](WSI-ISOLATION.md).

## Shader parameter editor contract

The Qt and Decky editors use the matching native `mako-shader-tool` bundled with the maintained vkBasalt dependency. It reflects ReShade uniform annotations through the same compiler used for rendering, in a separate process without Vulkan initialization. Native Deband controls share their defaults with its runtime owner. The helper emits versioned JSON; the UIs keep their own native bindings and write the ordinary per-profile vkBasalt file. Qt keeps process discovery asynchronous and uses the common atomic file writer for updates and rollback.

Schema 1 optionally includes a `presentation` object for each parameter: the declared `widget`, stable `group` key, unnumbered `label`, `component`, and `count`. The compiler preserves original dimensions before scalar specialization. Only complete non-array numeric vectors group as X/Y/Z/W; only explicit `ui_type = "color"` on float3/float4 gets RGBA labels and a preview. Arrays, matrices, missing hints, and malformed groups retain scalar controls. Scalar integer combo/radio/list labels preserve empty slots so values never shift. Boolean `ui_items` label checkbox components, not enum values. Group labels must agree with each underlying scalar label. Authored bounds and `input` hints remain authoritative; display-only colour clamping never changes exact numeric values. Decky and Qt use independent bindings with a shared contract test.

Both editors compact consecutive declared colours in the same category into a four-column swatch grid with one colour's controls visible at a time. Focusing a swatch does not select or save it; activating another colour commits any focused numeric draft before replacing its controls. Reset remains effect-wide and preserves protected values. Category boundaries and intervening parameters keep their authored order. This presentation adds no effect-name rules or new metadata fields.

Numeric presentation shortens exact FP32 values and the helper's nine-significant-digit readbacks only when the shorter decimal has the same FP32 representation. This applies to scalar, vector, array, colour, and unbounded fields; independently typed decimals retain their precision. Opening, focusing, blurring, and closing untouched fields never writes the shortened presentation. Authored slider bounds and steps retain their FP32 meaning. Missing float steps use the largest power of ten no greater than one hundredth of the range (Gamma's 0.5–2 range uses 0.01); integer steps still default to 1. Slider edits snap relative to the lower bound, preserve endpoints, and remove arithmetic tails; exact fields bypass snapping. Decky explicitly configures controller granularity and stepped values, and Qt uses `Slider.SnapAlways`. No persisted schema, metadata protocol, or shader arithmetic changes.

Overrides use `makoShader_<hex-effect-alias>_<hex-uniform-name>_<component-index>` assignments. Components are numbered in compiler order within a uniform; scalar controls use zero. This isolates effects with identical uniform names and supports vectors, matrices, and arrays. Legacy bare-name overrides remain readable, with an effect-local value taking precedence. Reset writes the reserved `default` marker, resolved by the runtime from its compiled shader, so resolution-dependent defaults stay correct and older bare-name overrides cannot defeat the reset. Runtime source uniforms, hidden/read-only (`noedit`) controls, and momentary boolean buttons are omitted. The optional boolean `resettable` flag preserves `noreset` annotations in both UI actions and backend validation. Compile-time macros and unsupported ReShade features retain their existing limitations.

Both metadata bindings reject duplicate identities, cross-effect keys, unsupported types, and malformed values before enabling edits. Invalid optional presentation hints fall back to scalar controls. Integer sliders require integral bounds and positive integral steps; float bounds are compared at the shader’s FP32 precision. Discovery occurs only on editor opening. A new Decky discovery invalidates the previous cache, and only the newest successful result may replace it. The UIs coalesce edits; the existing transactional graph replacement applies saved values live, preserving the previous graph if compilation fails. There is no extra GPU pass, rendering buffer, or per-frame option processing. Applying parameter changes can briefly interrupt rendering while the graph rebuilds, and settings that request more shader work can increase GPU cost. For a local dependency candidate, build the maintained fork’s release-shaped archive and pass it through the existing `MAKO_VKBASALT_LOCAL_ARCHIVE` package/deployment override; never hand-edit the public dependency pin.

Metadata discovery uses a nominal 1920×1080 SDR compilation context; source controls gated by resolution or colour-format preprocessor conditions can differ from an active game.

The bundled `CropStretch.fx` clamps each edge to 0–49% even for manual configuration edits, keeps bilinear taps inside the cropped image, and preserves colour and alpha with one texture read in one pass. Unselected effects add no pass.
