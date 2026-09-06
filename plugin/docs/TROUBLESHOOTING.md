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

Use the [Heroic setup](../../README.md#heroic) or the [launcher setup guide](LAUNCHERS.md) for Lutris, EmuDeck, and manually added Flatpak shortcuts. Heroic uses its per-game **Wrapper** field; Lutris uses **Command prefix**. Flatpak launchers also need preparation in **Flatpak Setup**.

### Ubisoft Connect closes before the game starts

Use the normal Steam/Proton launch option above. MAKO leaves `UbisoftConnect.exe`, `upc.exe`, and `UplayWebCore.exe` on its inactive native-presentation path while retaining the game's inherited launch environment. This avoids applying Frame Generation or Scaling to the launcher's own windows; it does not establish the cause of every Ubisoft or Proton crash.

If the failure persists, collect [MAKO Renderer diagnostics for the failing Steam launch](COLLECT_DIAGNOSTICS.md) and a Proton log from that same attempt. Decky's plugin lifecycle log records installation and profile edits but cannot show which executable crashed or whether MAKO created a rendering context there. Capture a game profile after gameplay loads so **Matched Processes** describes the game rather than its launcher.

## XR Gaming / Breezy lag

XR Gaming's Gamescope effect and its Vulkan-only mode use different presentation paths. MAKO's Gamescope WSI toggle does not select between them. See the [XR Gaming compatibility notes](../../engine/docs/LAYER-CHAINING.md#xr-gaming--breezy) for the current evidence limits and the short off/on/off capture needed to investigate Anchor/Follow lag. Keep the actual Breezy runtime archive; its plugin installation log cannot identify a rendering slowdown.

## Bazzite and multi-GPU systems

Use the **Wrapper path for this device** shown by MAKO; do not hardcode `/home/deck` when the device uses another home path.

With no **GPU** override, MAKO follows the Vulkan device selected by the game. On a multi-GPU failure, clear the override first, then collect diagnostics. Dual-GPU frame generation is unsupported.

If the install control reports an unsupported native AArch64 or Armada host, see [Armada and native AArch64 support](ARMADA.md). The current x86_64 Renderer payload is intentionally not activated there.

## Updates and Flatpak runtimes

Follow the root [clean update workflow](../../README.md#updating-mako-decky). It preserves valid profiles and launch options while explicitly replacing the native Renderer and each prepared Flatpak runtime.

If invalid configuration prevents profiles from loading or saving, close games using MAKO and select **Install MAKO Renderer** in MAKO Decky. Installation recreates an unreadable or invalid `conf.toml` with defaults, replacing its saved profiles. Read-only files require owner write permission before retrying.

## Diagnostics

Follow [Collect MAKO Decky Diagnostics](COLLECT_DIAGNOSTICS.md). It gives launch-type-specific logging instructions, preserves the latest five sessions, creates a Desktop report, restores normal settings, and submits the file privately. Diagnostics are opt-in because synchronous logging can distort frame pacing.

If Decky cannot install, display, or open the plugin at all, use [MAKO Decky installation failures](DECKY_INSTALLATION_FAILURES.md) instead.
