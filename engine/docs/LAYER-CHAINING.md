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

## MAKO Decky integrations

### Gamescope WSI compatibility

Try **Gamescope WSI (Restart)** for motion artifacts or presentation problems in supported 64-bit Gamescope games. Close the game before changing it, then check **Live Status** after relaunching.

Unsupported: Desktop Mode, 32-bit WSI presentation, unprepared Flatpaks, mismatched nested Wayland sessions, and HDR.

### MangoHud and Shaders

Save or select the game's profile, enable **Shaders** under **Image Processing** or **MangoHud** under **External Tools**, then restart.

MangoHud and Shaders cannot run together. MangoHud requires a host installation and is unavailable inside Flatpak games. Shaders use bundled vkBasalt; prepare Flatpak apps in **Flatpak Setup** first.

Shader selections apply live after activation. More effects cost more GPU time; **HDR Look (SDR)** does not enable HDR. Use **Refresh** after editing shader files. See [configuration](CONFIGURATION.md#qt-shaders-and-compatibility-controls) for controls and custom shaders.

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
