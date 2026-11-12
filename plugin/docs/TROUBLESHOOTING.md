# MAKO Decky troubleshooting

## A game does not start or MAKO appears inactive

1. Open MAKO Decky and select **Install MAKO Renderer**; installing the ZIP alone is not enough.
2. For native Steam/Proton games, use this Launch Option, replacing the path if MAKO shows a different **Wrapper path for this device**:

    ```text
    /home/deck/.local/bin/mako-run %command%
    ```

3. Use Vulkan, including DXVK/VKD3D for Windows games. For other launchers and Flatpaks, follow [launcher setup](LAUNCHERS.md).
4. Check **Matched Processes** and the feature's **Restart** requirements. For Frame Generation, start with Fixed 2x.
5. Try **Disable MAKO Renderer on Next Launch** and restart to compare. Turn it off after testing.

MAKO FG, MAKO Scaler, and Shaders work without Lossless Scaling. Only the optional Lossless Scaling frame generation model and LS1 need its default Steam version and a valid DLL path; without it, switch to MAKO FG or MAKO Scaler.

## Stutter or visual problems

Try V-Sync on/off and different display modes. Test Frame Generation, Scaling, and Shaders separately. For overlay or Gamescope WSI issues, see [graphics integrations](../../engine/docs/LAYER-CHAINING.md).

## Ubisoft Connect closes before the game starts

Keep the normal launch option and capture the game profile after gameplay loads. If it still fails, collect [MAKO diagnostics](COLLECT_DIAGNOSTICS.md) and a Proton log from the same attempt.

## XR Gaming / Breezy lag

Compatibility is unvalidated. Follow the [short off/on/off capture](../../engine/docs/LAYER-CHAINING.md#xr-gaming--breezy) and include the actual Breezy runtime archive.

## Bazzite and multi-GPU systems

Use MAKO's displayed wrapper path. Clear an unnecessary **GPU** override; multi-GPU Frame Generation is unsupported. For an unsupported AArch64/Armada host, see [Armada support](ARMADA.md).

## Updates and Flatpak runtimes

Follow the [update steps](../../README.md#updating-mako-decky). Installing or updating MAKO Renderer also refreshes installed Flatpak extensions; retry a failed extension update through **Flatpak Setup > Update**.

If profiles cannot load or save, close games and select **Install MAKO Renderer**. An invalid configuration is replaced with defaults, including its profiles. Read-only files need owner write permission first.

## HDR is unavailable in the game

HDR is experimental and off by default. On a compatible HDR display, keep **Gamescope WSI** off, turn on **Enable HDR (Restart)**, restart the game, then enable HDR in the game's settings. See [HDR setup](CONFIGURATION.md#hdr) for display requirements and supported combinations.

## Diagnostics

If the problem remains, [collect diagnostics](COLLECT_DIAGNOSTICS.md). If the plugin will not install or open, use [installation help](DECKY_INSTALLATION_FAILURES.md).
