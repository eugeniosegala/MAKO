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

Frame Generation and LS1 need Lossless Scaling's default Steam version and a valid DLL path. MAKO Scaler and Shaders work without it.

## Stutter or visual problems

Try V-Sync on/off and different display modes. Test Frame Generation, Scaling, and Shaders separately. For overlay or Gamescope WSI issues, see [graphics integrations](../../engine/docs/LAYER-CHAINING.md).

## Ubisoft Connect closes before the game starts

Keep the normal launch option and capture the game profile after gameplay loads. If it still fails, collect [MAKO diagnostics](COLLECT_DIAGNOSTICS.md) and a Proton log from the same attempt.

## XR Gaming / Breezy lag

Compatibility is unvalidated. Follow the [short off/on/off capture](../../engine/docs/LAYER-CHAINING.md#xr-gaming--breezy) and include the actual Breezy runtime archive.

## Bazzite and multi-GPU systems

Use MAKO's displayed wrapper path. Clear an unnecessary **GPU** override; multi-GPU Frame Generation is unsupported. For an unsupported AArch64/Armada host, see [Armada support](ARMADA.md).

## Updates and Flatpak runtimes

Follow the [update steps](../../README.md#updating-mako-decky), including **Flatpak Setup > Update** for prepared apps.

If profiles cannot load or save, close games and select **Install MAKO Renderer**. An invalid configuration is replaced with defaults, including its profiles. Read-only files need owner write permission first.

## HDR is unavailable by design

Frame Generation and Scaling are SDR-only. **Disable HDR** stays checked and read-only.

## Diagnostics

If the problem remains, [collect diagnostics](COLLECT_DIAGNOSTICS.md). If the plugin will not install or open, use [installation help](DECKY_INSTALLATION_FAILURES.md).
