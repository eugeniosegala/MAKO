# MAKO Decky troubleshooting

These instructions apply to published ZIPs and local development builds.

## HDR is unavailable by design

HDR frame generation and scaling are disabled in this release. **Disable HDR** is checked and read-only, and MAKO removes inherited `DXVK_HDR` activation.

Inside a supported Gamescope session, **Enable Scaling (Restart)** uses the managed Frame Generation → Gamescope WSI → Spatial Scaling order. **Gamescope WSI (Restart)** provides the same guarded presentation path for affected 64-bit Frame Generation-only profiles. MangoHud or vkBasalt can follow MAKO's roles without exposing the host's complete implicit-layer directory.

See [WSI isolation](../../engine/docs/WSI-ISOLATION.md), [optional graphics integrations](../../engine/docs/LAYER-CHAINING.md), and the [HDR pipeline](../../engine/docs/HDR-PIPELINE.md).

## A game does not start or MAKO appears inactive

1. For native Steam or Proton, set Launch Options exactly to:

    ```text
    /home/deck/.local/bin/mako-run %command%
    ```

2. Open MAKO Decky and select **Install MAKO Renderer**. Installing the ZIP alone does not install its bundled Renderer.
3. If using Frame Generation or LS1, check the selected profile's `Lossless.dll` path. Clear an unnecessary **GPU** override and verify **Matched Processes**. Start with Fixed 2x.
4. Compare the game's V-Sync on and off; its limiter, VRR, and compositor can change pacing.
5. Select **Disable MAKO Renderer on Next Launch**, restart the game, and compare once. Turn the option off after the test.

For Heroic, use the displayed MAKO wrapper as **Wrapper**, leave **Arguments** empty, and do not add `%command%`. For an EmuDeck Flatpak, prepare the emulator in **Flatpak Setup**, use the wrapper as the Steam shortcut **Target**, and preserve EmuDeck's existing Launch Options. See the [installation workflows](../../README.md#heroic-and-other-flatpak-applications).

## Bazzite and multi-GPU systems

Use the **Wrapper path for this device** shown by MAKO; do not hardcode `/home/deck` when the device uses another home path.

With no **GPU** override, MAKO follows the Vulkan device selected by the game. On a multi-GPU failure, clear the override first, then collect diagnostics. Dual-GPU frame generation is unsupported.

If the install control reports an unsupported native AArch64 or Armada host, see [Armada and native AArch64 support](ARMADA.md). The current x86_64 Renderer payload is intentionally not activated there.

## Updates and Flatpak runtimes

Follow the root [clean update workflow](../../README.md#updating-mako-decky). It preserves profiles and launch options while explicitly replacing the native Renderer and each prepared Flatpak runtime.

## Diagnostics

Follow [Collect MAKO Decky Diagnostics](COLLECT_DIAGNOSTICS.md). It gives launch-type-specific logging instructions, preserves the latest five sessions, creates a Desktop report, restores normal settings, and submits the file privately. Diagnostics are opt-in because synchronous logging can distort frame pacing.

If Decky cannot install, display, or open the plugin at all, use [MAKO Decky installation failures](DECKY_INSTALLATION_FAILURES.md) instead.
