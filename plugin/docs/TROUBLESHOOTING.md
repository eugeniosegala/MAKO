# MAKO Decky troubleshooting

These instructions apply to published ZIPs and local development builds.

## HDR is unavailable by design

HDR frame generation and scaling are disabled in this release. **Disable HDR** is checked and read-only, and MAKO removes inherited `DXVK_HDR` activation.

Profiles without Scaling or explicit Gamescope WSI use MAKO's isolated top-role manifest. **Enable Scaling (Restart)** uses MAKO frame generation → validated Gamescope WSI → MAKO spatial scaling; Native Resolution keeps that path provisioned without a model scaler. **Gamescope WSI (Restart)** exposes the guarded MAKO → WSI path for supported 64-bit FG-only native profiles but does not enable HDR. MangoHud or vkBasalt may follow the MAKO roles without exposing the host's full implicit-layer directory.

See [WSI isolation](../../engine/docs/WSI-ISOLATION.md), [optional graphics integrations](../../engine/docs/LAYER-CHAINING.md), and the [HDR pipeline](../../engine/docs/HDR-PIPELINE.md).

## A game does not start or MAKO appears inactive

1. For native Steam or Proton, set Launch Options exactly to:

    ```text
    /home/deck/.local/bin/mako-run %command%
    ```

2. Open MAKO Decky and select **Install MAKO Renderer**. Installing the ZIP alone does not install its bundled Renderer.
3. Check the selected profile's `Lossless.dll` path, GPU override, and **Matched Processes**. Start with Fixed 2x.
4. Compare the game's V-Sync on and off; its limiter, VRR, and compositor can change pacing.
5. Use **Disable MAKO Renderer on Next Launch** and restart once to isolate whether MAKO causes the failure.

For Heroic, use the displayed MAKO wrapper as **Wrapper**, leave **Arguments** empty, and do not add `%command%`. For an EmuDeck Flatpak, prepare the emulator in **Flatpak Setup**, use the wrapper as the Steam shortcut **Target**, and preserve EmuDeck's existing Launch Options. See the [installation workflows](../../README.md#heroic-and-other-flatpak-applications).

## Bazzite and multi-GPU systems

Use the **Wrapper path for this device** shown by MAKO; do not hardcode `/home/deck` when the device uses another home path.

With no **GPU** override, MAKO follows the Vulkan device selected by the game. On a multi-GPU failure, clear the override first, then collect diagnostics. Dual-GPU frame generation is unsupported.

## Updates and Flatpak runtimes

Follow the root [clean update workflow](../../README.md#updating-mako-decky). It preserves profiles and launch options while explicitly replacing the native Renderer and each prepared Flatpak runtime.

## Diagnostics

Follow [Collect MAKO Decky Diagnostics](COLLECT_DIAGNOSTICS.md). It gives launch-type-specific logging instructions, preserves the latest three sessions, creates a Desktop report, restores normal settings, and submits the file privately. Diagnostics are opt-in because synchronous logging can distort frame pacing.

If Decky cannot install, display, or open the plugin at all, use [MAKO Decky installation failures](DECKY_INSTALLATION_FAILURES.md) instead.
