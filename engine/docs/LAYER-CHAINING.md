# Optional graphics integrations

MAKO uses a private Vulkan-layer boundary so system-wide overlays and competing Frame Generation layers do not join a game accidentally. Use the managed options below rather than adding Vulkan layer paths manually. [WSI isolation](WSI-ISOLATION.md) documents the implementation contract.

## Default MAKO Decky launch

Keep the normal Steam launch option:

```text
/home/deck/.local/bin/mako-run %command%
```

MAKO Decky adds only the layers selected for the active profile. In Steam Desktop Mode it also preserves Steam's requested Vulkan FPS overlay when available; Gaming Mode and Flatpak presentation remain unchanged.

## Standalone MAKO Renderer with vkBasalt

The standalone Renderer package already includes MAKO's pinned 64-bit and 32-bit vkBasalt build and curated effects. No separate vkBasalt installation is needed.

The recommended setup is:

1. Open **MAKO Renderer Configuration**.
2. Select or create the game's profile and enable **Shaders**.
3. Copy the complete launch option shown by the UI into the game's Steam launch options.
4. Restart the game.

The UI-generated command selects the profile and its isolated shader configuration. For a manual native Steam or Proton launch, the equivalent activation is:

```text
ENABLE_VKBASALT=1 ~/.local/bin/mako-launch %command%
```

Advanced users can select any standard vkBasalt configuration:

```text
ENABLE_VKBASALT=1 VKBASALT_CONFIG_FILE="$HOME/.config/vkBasalt/game-name.conf" ~/.local/bin/mako-launch %command%
```

MAKO's controls update only the options they own and preserve other entries in the selected file. Managed sharpening, anti-aliasing, denoise, and effect selections apply live while the layer is active. Enabling the layer or manually changing a custom effect chain requires a game restart.

`mako-launch` always selects MAKO's bundled vkBasalt rather than a system installation. If the private payload or selected configuration is unavailable, it warns and continues with MAKO Renderer alone. Native Vulkan and Proton games use this launcher; sandboxed applications require the [Flatpak vkBasalt setup](FLATPAK-GUIDE.md#optional-private-vkbasalt-chain).

To disable shaders for a standalone game, turn them off in the UI and use the launch option it displays. The basic Renderer-only option is:

```text
~/.local/bin/mako-launch %command%
```

## MAKO Decky integrations

### Gamescope WSI compatibility

**Gamescope WSI (Restart)** is an optional compatibility setting for supported 64-bit native, Proton, and prepared Flatpak launches. It can be tested when Frame Generation or Scaling has artifacts or presentation problems; Scaling otherwise uses the combined Renderer with Gamescope WSI isolated.

Change the setting with the game closed, then verify Frame Generation and Scaling through **Live Status**. It is not supported for Desktop Mode, 32-bit WSI presentation, unprepared Flatpaks, mismatched nested Wayland sessions, or HDR. See [WSI isolation](WSI-ISOLATION.md) for architecture and presentation details.

### MangoHud and Shaders

1. Keep `/home/deck/.local/bin/mako-run %command%` as the launch option.
2. Select the default profile or save a profile for the game.
3. Enable **Shaders** under **Image Processing**, or **MangoHud** under **External Tools**.
4. Restart the game.

MangoHud and Shaders are mutually exclusive. MangoHud must be installed on the host and continues to read `~/.config/MangoHud/MangoHud.conf`; host MangoHud is not enabled inside Flatpak games.

Shaders use only MAKO's bundled vkBasalt. The compact controls provide sharpening, denoise, medium-quality FXAA or SMAA, and an ordered multi-effect picker. Combining effects increases GPU cost, and **HDR Look (SDR)** remains an SDR effect rather than HDR output. The default profile uses vkBasalt's global file, while saved profiles use isolated files shown in the UI. Advanced edits are preserved, but manual custom-chain changes require a restart.

For Flatpak games, install the matching MAKO extension and prepare the application in **Flatpak Setup**. If an integration does not activate, reinstall MAKO Renderer, update the applicable Flatpak extension, and collect a [MAKO Decky diagnostics report](../../plugin/docs/COLLECT_DIAGNOSTICS.md).

## Support boundaries

| Integration | Status |
| --- | --- |
| Steam Vulkan FPS overlay | Preserved automatically for native and Proton Steam launches in Desktop Mode |
| MangoHud | Managed per profile on the host; unavailable for Flatpak games |
| vkBasalt | Bundled and managed for Decky, standalone native/Proton, and prepared Flatpak applications |
| XR Gaming / Breezy | Not yet validated with MAKO; see the notes below |
| ReShade or OptiScaler through Proton | Title-specific; disable every other Frame Generation implementation |
| RenderDoc | Developer diagnostic only |
| OBS Vulkan Capture and other Vulkan layers | Unsupported until a specific guarded path is validated |
| Other Frame Generation layers | Do not combine with MAKO |

Installing a Vulkan layer does not make it compatible with MAKO. Architecture, ordering, synchronization, and sandbox behavior must be validated for each integration.

## XR Gaming / Breezy

XR Gaming has two different paths. Its Gamescope integration runs a ReShade effect in the compositor, outside the game's Vulkan chain. Its Vulkan-only mode uses Breezy's vkBasalt fork, which MAKO's normal isolation does not admit. MAKO's **Gamescope WSI** setting does not select between these paths, and MAKO's bundled vkBasalt is not a replacement for Breezy's transforms or head-tracking integration.

Neither path currently has validated MAKO compatibility on glasses. To investigate Anchor or Follow lag, compare MAKO alone, XR alone, and both with the same game scene, resolution, and refresh rate. For one short diagnostic run, begin with the XR effect off, enable it after 30 seconds, disable it after another 30 seconds, and note both times. Submit the resulting Breezy runtime archive with the applicable [Decky](../../plugin/docs/COLLECT_DIAGNOSTICS.md) or [standalone](COLLECT_DIAGNOSTICS.md) MAKO report.

## Game-local Proton integrations

Windows ReShade and OptiScaler installations normally use proxy DLLs and Proton DLL overrides rather than host Vulkan-layer discovery, so MAKO does not remove them. Compatibility remains title-specific: preserve the integration's documented DLL override, use the normal MAKO launch command, and disable its Frame Generation while MAKO owns generation.

## Troubleshooting and rollback

Layer membership is fixed when Vulkan starts. Restart the game after changing Shaders, MangoHud, Gamescope WSI, or another integration.

If a game fails to start, an effect is missing, or pacing regresses, disable the optional integration and reproduce the normal MAKO baseline. Follow [MAKO Decky troubleshooting](../../plugin/docs/TROUBLESHOOTING.md) or [standalone troubleshooting](TROUBLESHOOTING.md), then collect the matching diagnostics report if the problem remains.
