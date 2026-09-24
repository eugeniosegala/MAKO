# Optional graphics integrations

This guide defines the named exceptions to MAKO's private implicit-layer boundary and the separate rules for game-local integrations. [WSI isolation](WSI-ISOLATION.md) owns the default launch and Gamescope presentation contract.

Commands using `/home/deck/.local/bin/mako-run` apply to MAKO Decky. Standalone `mako-launch` supports the private bundled vkBasalt path documented below, but it does not provide a general external-layer workflow.

## Default MAKO Decky launch

The normal Steam launch option is:

```text
/home/deck/.local/bin/mako-run %command%
```

The generated wrapper exposes only MAKO's managed manifests, removes additive implicit-layer discovery, disables Gamescope WSI and competing Frame Generation, and leaves the Gamescope compositor active. Keep this baseline unless a named per-profile exception is needed.

## Standalone MAKO Renderer with vkBasalt

The standalone native Renderer archive includes MAKO's pinned 64-bit and 32-bit vkBasalt libraries, manifests, and curated shader sources. The Qt UI exposes the same compact per-profile shader controls and sidecar format as MAKO Decky. A profile already configured in Decky therefore appears with its current choices in Qt; Qt generates a launch option containing the exact `MAKO_PROFILE`, `ENABLE_VKBASALT`, and `VKBASALT_CONFIG_FILE` values for that selection.

A native Steam or Proton game can also opt into that private bundle manually with:

```text
ENABLE_VKBASALT=1 ~/.local/bin/mako-launch %command%
```

To use any vkBasalt option without expanding the MAKO UI, create a standard vkBasalt configuration and select it for the game:

```text
ENABLE_VKBASALT=1 VKBASALT_CONFIG_FILE="$HOME/.config/vkBasalt/game-name.conf" ~/.local/bin/mako-launch %command%
```

`VKBASALT_CONFIG_FILE` is optional for manual use. When it is omitted, vkBasalt uses its normal configuration search. Qt and Decky instead map every selected main profile to one shader config: the default `mako` profile uses the global file, Steam profiles use their app ID, and other profiles use the same short hashed identity. Their compact controls merge only MAKO-owned effects and values, preserving advanced settings and custom chains. Renaming or deleting a main profile moves or removes only its attached sidecar entries and isolated file, never another profile or the global file. When a file is selected, MAKO's bundled fork watches it and applies FXAA, SMAA, CAS, DLS, sharpening strength, DLS denoise, and controlled effect changes live. FXAA and SMAA default to medium quality. A controlled effect selection can also change around an unchanged custom chain; the transition waits once for vkBasalt's graphics queue, retires the old graph, and may cause a brief hitch while the new graph is built. This bounds effect-owned GPU memory instead of retaining every visited combination. Adding, removing, or reordering custom effects manually still requires a restart. `mako-launch` consumes the implicit activation request, selects only the manifests relative to the MAKO installation prefix, and creates the explicit `VK_LAYER_MAKO_render:VK_LAYER_VKBASALT_post_processing` prefix. An inherited system vkBasalt manifest or duplicate instance-layer request cannot join that managed path. Caller-requested non-vkBasalt layers remain after the supported prefix but are not thereby supported integrations.

Both private architecture manifests and libraries must be present, and a selected config must be readable before launch. If either check fails, the launcher prints a warning, keeps private vkBasalt disabled, and continues with MAKO Renderer alone. Layer membership is fixed when Vulkan starts, so activation requires a restart; custom effect-chain changes retain the running graph until restart. This launcher flow covers native Vulkan and Proton games; use the [standalone Flatpak vkBasalt setup](FLATPAK-GUIDE.md#optional-private-vkbasalt-chain) for a sandboxed application.

To return to MAKO Renderer alone, restore the normal launch option:

```text
~/.local/bin/mako-launch %command%
```

## Supported per-profile exceptions

### Gamescope WSI compatibility

MAKO Decky provides **Gamescope WSI (Restart)** under **Compatibility Settings** for supported 64-bit launches with Frame Generation, Scaling, or both. This opt-in setting addresses artifact reports such as [#7](https://github.com/eugeniosegala/MAKO/issues/7) and [#12](https://github.com/eugeniosegala/MAKO/issues/12). Scaling leaves this option independent and defaults to the combined Renderer with WSI isolated; HDR remains unsupported.

Inside Gamescope, selecting both Scaling and WSI preserves the Renderer → WSI → spatial-scaling order, with selected 64-bit MangoHud or vkBasalt processing last. WSI-only profiles omit the lower spatial role. MAKO loads only validated, managed layer files. An ineligible session or unavailable manifest leaves WSI disabled and uses the combined Renderer, subject to its normal scaling extent checks. Other optional layers fail independently.

MAKO Decky stages and repairs the host’s 64-bit Gamescope WSI payload during Renderer installation. It supports direct 64-bit native Vulkan and Proton launches, plus prepared Heroic and EmuDeck Flatpaks. It does not support Desktop Mode, mismatched nested Wayland sessions, unprepared Flatpaks, 32-bit WSI presentation, or HDR.

Test Fixed 2× before Adaptive and confirm generated delivery and active scaling through the final-output counter. The WSI toggle can be changed independently when pacing or output regresses. Restart and verify active scaling plus identical source/output sizes before comparing performance.

MAKO Decky validates and stages exact architecture-specific manifests and available libraries. It does not expose the complete host implicit-layer directory. Invalid optional-tool evidence suppresses that tool. Missing staged WSI or spatial manifests keep the launch on the combined Renderer; missing surface or create evidence after a split chain starts keeps scaling native or rejects the invalid create. HDR remains disabled in every current managed chain.

Layer membership cannot change after Vulkan starts. Restart the game after changing WSI, Scaling, MangoHud, or vkBasalt.

### Enable MangoHud or vkBasalt

1. Install MangoHud on the SteamOS host if that is the selected tool. vkBasalt is already included with MAKO Renderer.
2. Keep `/home/deck/.local/bin/mako-run %command%` as the Steam launch option.
3. Select the default profile or save a profile for the running game.
4. Enable exactly one tool under **External Tools**.
5. Restart the game.

Host MangoHud is not enabled inside Flatpak games. Bundled vkBasalt is available to native games and prepared MAKO Flatpak runtimes.

MangoHud continues to read `~/.config/MangoHud/MangoHud.conf`. To override a few values for one launch while the MangoHud profile control remains enabled, use:

```text
/home/deck/.local/bin/mako-run env MANGOHUD_CONFIG=fps,frametime,cpu_stats,gpu_stats,position=top-right %command%
```

Do not add activation or layer-path variables when the managed control is enabled.

#### vkBasalt with MAKO Decky

**Shaders (Restart)** loads only MAKO Renderer's private bundled vkBasalt. MAKO ignores system-wide vkBasalt manifests and libraries, and a missing private bundle leaves vkBasalt disabled instead of falling back to another copy. No separate vkBasalt installation or launch option is needed.

The compact controls set CAS or DLS sharpening, sharpening strength, DLS denoise, optional medium-quality FXAA or SMAA, and one **Effects** choice. The list is ordered by general usefulness: Off, HDR Look (SDR), Vibrance, Colourfulness, Curves, Deband, Technicolor 2, DPX / Cineon, Bleach Bypass, Noir, Technicolor, Monochrome, Sepia, Film Grain, Vignette, Cartoon, Nostalgia, and Chromatic Aberration. MAKO Decky automatically uses vkBasalt's global file for the Default profile and an isolated file for every saved game or process profile; game files use their Steam app ID, including Steam-assigned non-Steam shortcut IDs, while profiles without one use a short fallback identity. The UI shows the exact active path. Advanced edits are supported in that file because Decky merges only the compact controls and preserves other vkBasalt options. Every compact control applies live while vkBasalt is active; activation and manual advanced edits apply after restarting the game. Selecting a different effect graph waits once for vkBasalt's graphics queue, retires the old graph, and may cause a brief transition hitch while the new graph is built. Decky installs its pinned SweetFX-derived sources in the shared profile directory; Deband uses vkBasalt's built-in implementation.

For Flatpak games, install the matching MAKO extension and prepare the application in **Flatpak Setup**. If the effect does not appear, reinstall MAKO Renderer, update the Flatpak extension when applicable, and collect a [MAKO Decky diagnostics report](../../plugin/docs/COLLECT_DIAGNOSTICS.md).

## Manual MangoHud diagnostic path

The managed profile control is preferred. This manual path exists for a focused 64-bit native Vulkan or Proton experiment when **Enable MangoHud**, **Enable vkBasalt**, Gamescope WSI compatibility, and Scaling are all off.

In Steam Desktop Mode, set the complete launch option to:

```text
/home/deck/.local/bin/mako-run env MANGOHUD=1 NODEVICE_SELECT=1 DISABLE_LAYER_MESA_ANTI_LAG=1 VK_IMPLICIT_LAYER_PATH=/home/deck/.local/share/mako-render/vulkan/implicit_layer.d:/usr/share/vulkan/implicit_layer.d %command%
```

Use the wrapper path displayed by MAKO Decky if the home directory is not `/home/deck`. `%command%` is a Steam placeholder and should not be pasted into a terminal.

This exposes the system implicit-manifest directory, so the two Mesa guards are part of the command. The intended order is `Application -> MAKO Renderer -> MangoHud -> Vulkan driver`, allowing MangoHud to observe generated and real presents. Verify the actual loader chain; directory order alone is not a complete Vulkan ordering proof.

Do not wrap `mako-run` with the `mangohud` launcher. MAKO resets implicit discovery inside its wrapper, and the preload path can affect Steam Runtime helpers without admitting the Vulkan overlay correctly.

## Other Vulkan layers

Do not generalize the manual MangoHud command to a support claim. Every Vulkan layer has its own manifest, activation, architecture, ordering, synchronization, and sandbox requirements.

| Integration | MAKO status |
| --- | --- |
| MangoHud | Managed per-profile path; bounded manual diagnostic path above |
| vkBasalt | Bundled pinned 64-bit/32-bit layer; managed MAKO Decky profiles plus the named standalone native/Proton and Flatpak paths |
| OBS Vulkan Capture | Unsupported candidate until the exact host/plugin/sandbox path and generated-frame capture are validated |
| RenderDoc | Developer diagnostic only; use its own registration and activation flow and measure the resulting chain |
| Other Frame Generation layers | Never combine with MAKO |
| Mesa device selection and anti-lag | Excluded from managed optional-tool paths |
| Other capture, overlay, post-process, validation, or vendor layers | Unsupported until a named guarded path has evidence |

Adding a supported integration requires one exact manifest path and intended order, portable launcher/wrapper/package tests, then real native Vulkan, DXVK, VKD3D-Proton, Gamescope, focus/overlay, recreation, shutdown, architecture, and sandbox evidence. A game starting successfully proves discovery, not image or synchronization compatibility.

## XR Gaming / Breezy

XR Gaming has two distinct rendering paths. Its [Gamescope integration](https://github.com/wheaney/XRLinuxDriver/blob/3e0132f67bba17709e16286a1f8dce88bcf65adc/src/plugins/gamescope_reshade_wayland.c) loads a ReShade effect in the compositor through Wayland. This runs outside the game's Vulkan layer chain, so MAKO's private manifest isolation does not disable it. MAKO Decky's **Gamescope WSI (Restart)** setting controls a different, application-side layer; enabling it is not an established XR fix.

XR Gaming's [**Disable gamescope integration** control](https://github.com/wheaney/decky-XRGaming/blob/main/src/index.tsx) instead selects its Vulkan-only path, which uses Breezy's vkBasalt fork. That layer is excluded by MAKO's default isolation. Selecting MAKO's vkBasalt control is not proof of compatibility with Breezy's fork, its transforms, or head-tracking timing. Neither XR path currently has validated MAKO compatibility on glasses.

[Issue #24](https://github.com/eugeniosegala/MAKO/issues/24) reports severe lag only when Frame Generation and Anchor/Follow run together. The supplied MAKO 2.1.0 logs show successful Renderer startup and Gamescope compiling a `Transform` effect, but contain neither presentation-timing diagnostics nor Vulkan loader order. They do not establish whether the slowdown comes from game cadence, generated-image waits, shared GPU load, or compositor/head-tracking timing. Missing-texture warnings alone do not establish a shader failure, and the recorded Gamescope abort follows session shutdown.

For a useful comparison, keep resolution, refresh rate, game scene, and MAKO settings fixed. First compare MAKO alone, XR alone, and both without diagnostics. Then follow the owning [Decky](../../plugin/docs/COLLECT_DIAGNOSTICS.md) or [standalone](COLLECT_DIAGNOSTICS.md) diagnostic guide for one short run: start with XR effects off, enable the affected mode after 30 seconds, disable it again after 30 seconds, and note both times. Include the actual Renderer build, glasses model, selected refresh rate, and whether XR Gaming's Gamescope integration is enabled. Provide the actual archive produced by `breezy_vulkan_logs` alongside the MAKO report; the Decky plugin's installation log does not contain XR runtime evidence. Review and send these files privately. Enable `VK_LOADER_DEBUG=layer` for a separate short capture only when investigating the Vulkan-only path, and retain its unfiltered log because the MAKO collector intentionally filters unrelated layer records.

## Game-local Proton integrations

ReShade and OptiScaler are usually injected through Windows proxy DLLs and Wine/Proton DLL overrides rather than `VK_IMPLICIT_LAYER_PATH`. MAKO's host layer isolation does not remove those files or overrides.

Coexistence is title-specific. Preserve the integration's documented DLL override, start from the normal `mako-run` launch, and disable every other Frame Generation implementation. OptiScaler upscaling may remain active, but its Frame Generation must be off while MAKO owns generation.

## Verify and roll back

For one short run, add loader and presentation diagnostics to the otherwise working command. For the manual MangoHud path:

```text
VK_LOADER_DEBUG=layer MAKO_PRESENT_DIAGNOSTICS=1 MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 /home/deck/.local/bin/mako-run env MANGOHUD=1 NODEVICE_SELECT=1 DISABLE_LAYER_MESA_ANTI_LAG=1 VK_IMPLICIT_LAYER_PATH=/home/deck/.local/share/mako-render/vulkan/implicit_layer.d:/usr/share/vulkan/implicit_layer.d %command%
```

After quitting the game, collect:

```bash
/home/deck/.local/bin/mako-diagnostics layers --lines 2000
```

Confirm the architecture-correct instance and device call stacks, the intended order, generated and real delivery, and absence of unrelated or competing layers. Test Fixed before Adaptive and include focus, overlays, recreation, and shutdown. Remove loader and presentation diagnostics afterward because synchronous logging can disturb pacing.

If the game fails, the tool is absent, pacing regresses, or the chain is unexpected, restore:

```text
/home/deck/.local/bin/mako-run %command%
```

Reproduce the native MAKO baseline before assigning the fault to the scheduler or presentation path. Record untested 32-bit, Flatpak, non-SteamOS, non-RADV, and other hardware/driver boundaries explicitly.
