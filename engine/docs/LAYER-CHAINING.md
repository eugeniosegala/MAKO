# Optional graphics integrations

This guide defines the named exceptions to MAKO's private implicit-layer boundary and the separate rules for game-local integrations. [WSI isolation](WSI-ISOLATION.md) owns the default launch and Gamescope presentation contract.

Commands using `/home/deck/.local/bin/mako-run` apply to MAKO Decky. Standalone `mako-launch` does not provide a validated external-layer workflow.

## Default MAKO Decky launch

The normal Steam launch option is:

```text
/home/deck/.local/bin/mako-run %command%
```

The generated wrapper exposes only MAKO's managed manifests, removes additive implicit-layer discovery, disables Gamescope WSI and competing Frame Generation, and leaves the Gamescope compositor active. Keep this baseline unless a named per-profile exception is needed.

## Supported per-profile exceptions

### Gamescope WSI compatibility

MAKO Decky provides **Gamescope WSI (Restart)** under **Compatibility Settings** for supported 64-bit Frame Generation launches. This opt-in setting addresses artifact reports such as [#7](https://github.com/eugeniosegala/MAKO/issues/7) and [#12](https://github.com/eugeniosegala/MAKO/issues/12). Scaling automatically enables and locks its managed WSI path; HDR remains unsupported.

Inside Gamescope, MAKO preserves the frame-generation → WSI → spatial-scaling order, with selected 64-bit MangoHud or vkBasalt processing last. It loads only validated, managed layer files. Invalid session or layer evidence fails closed to top-only MAKO with scaling suppressed, while other optional layers fail independently.

MAKO Decky stages and repairs the host’s 64-bit Gamescope WSI payload during Renderer installation. It supports direct 64-bit native Vulkan and Proton launches, plus prepared Heroic and EmuDeck Flatpaks. It does not support Desktop Mode, mismatched nested Wayland sessions, unprepared Flatpaks, 32-bit WSI presentation, or HDR.

Test Fixed 2× before Adaptive and confirm generated delivery and active scaling through the final-output counter. Disable Scaling or Gamescope WSI if pacing or output regresses.

MAKO Decky validates and stages exact architecture-specific manifests and available libraries. It does not expose the complete host implicit-layer directory. Invalid optional-tool evidence suppresses that tool; invalid WSI or lower-spatial evidence falls back to top-only MAKO with scaling inactive. HDR remains disabled in every current managed chain.

Layer membership cannot change after Vulkan starts. Restart the game after changing WSI, Scaling, MangoHud, or vkBasalt.

### Enable MangoHud or vkBasalt

1. Install the tool on the SteamOS host.
2. Keep `/home/deck/.local/bin/mako-run %command%` as the Steam launch option.
3. Select the default profile or save a profile for the running game.
4. Enable exactly one tool under **External Tools**.
5. Restart the game.

The controls do not enable host external layers inside Flatpak games.

MangoHud continues to read `~/.config/MangoHud/MangoHud.conf`. To override a few values for one launch while the MangoHud profile control remains enabled, use:

```text
/home/deck/.local/bin/mako-run env MANGOHUD_CONFIG=fps,frametime,cpu_stats,gpu_stats,position=top-right %command%
```

Do not add activation or layer-path variables when the managed control is enabled. vkBasalt similarly continues to read its normal configuration below `~/.config/vkBasalt/`; MAKO Decky controls admission, not effects.

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
| vkBasalt | Managed experimental per-profile path |
| OBS Vulkan Capture | Unsupported candidate until the exact host/plugin/sandbox path and generated-frame capture are validated |
| RenderDoc | Developer diagnostic only; use its own registration and activation flow and measure the resulting chain |
| Other Frame Generation layers | Never combine with MAKO |
| Mesa device selection and anti-lag | Excluded from managed optional-tool paths |
| Other capture, overlay, post-process, validation, or vendor layers | Unsupported until a named guarded path has evidence |

Adding a supported integration requires one exact manifest path and intended order, portable launcher/wrapper/package tests, then real native Vulkan, DXVK, VKD3D-Proton, Gamescope, focus/overlay, recreation, shutdown, architecture, and sandbox evidence. A game starting successfully proves discovery, not image or synchronization compatibility.

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
