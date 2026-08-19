# Troubleshooting

These instructions apply to both published MAKO Decky ZIPs and local developer builds.

## HDR is unavailable by design

HDR frame generation is unavailable in this release. **Disable HDR (Restart)** is checked, read-only, and enforced by the backend. The launcher uses MAKO's validated SDR path, so an HDR option being unavailable in a game is expected and does not mean the ZIP installed incorrectly.

Use **Disable MAKO Renderer on Next Launch** and restart the game if you need to test whether the layer is the cause of a startup or presentation problem.

## A game will not start or MAKO appears inactive

1. For a native Steam or Proton game, confirm its launch option is exactly:

    ```text
    /home/deck/.local/bin/mako-run %command%
    ```

2. Open Mako and select **Install MAKO Renderer**. Installing a Decky ZIP alone does not replace the private Renderer payload.
3. Check the selected profile's `Lossless.dll` path, GPU choice, and **Active In** rule. Start with Fixed 2x before testing Adaptive settings.
4. Test the game's V-Sync both on and off. Its own FPS limiter, VRR, or compositor configuration can affect frame pacing.

For Heroic on SteamOS, use `/home/deck/.local/bin/mako-run` as the **Wrapper**, leave **Arguments** empty, and add no `%command%`. For an EmuDeck Flatpak emulator shortcut, prepare that emulator in **Flatpak Setup**, then use the same path as the shortcut's **Target** and leave EmuDeck's existing **Launch Options** unchanged. If **Wrapper path for this device** shows a different path, use the displayed value instead. See the [EmuDeck setup](../../README.md#emudeck).

## Bazzite and multi-GPU systems

MAKO reads Decky's actual user home and generates the copyable launch command and Flatpak wrapper path for that device. Do not substitute `/home/deck` on a system whose displayed path is different.

With no **GPU** value configured, MAKO Renderer follows the Vulkan device used by the game instead of choosing the first GPU reported by the driver. A saved GPU value remains an explicit override. If a multi-GPU game still fails, clear that field first so automatic selection can follow the game, then collect the `startup` and `errors` diagnostics below.

## Collect diagnostics

Published builds keep diagnostics off by default. Local development ZIPs and direct `dev:*` deployments enable them automatically. To collect a focused Steam report, temporarily replace the game's normal launch option with:

```text
MAKO_PRESENT_DIAGNOSTICS=1 MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 /home/deck/.local/bin/mako-run %command%
```

For Heroic, keep the normal Wrapper and Arguments fields. Add these two environment variables in the game's settings instead:

```text
MAKO_PRESENT_DIAGNOSTICS=1
MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25
```

For an EmuDeck Flatpak shortcut, first save its current **Target** and **Launch Options**. Temporarily set **Target** to `/usr/bin/env`, then prepend this to the existing **Launch Options** without changing the existing emulator ID, ROM path, or flags:

```text
MAKO_PRESENT_DIAGNOSTICS=1 MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 /home/deck/.local/bin/mako-run
```

Use Mako's displayed **Wrapper path for this device** when it differs. Do not add `%command%`. After reproducing the issue, restore the shortcut's original **Target** and **Launch Options** exactly.

Reproduce the issue, quit the game, then run this in Desktop Mode:

```bash
/home/deck/.local/bin/mako-diagnostics all
```

Useful focused reports are `mako-diagnostics adaptive`, `recovery`, `performance`, `startup`, and `errors`. Add `--lines 2000` when the normal output is too short, or `--log PATH` for a saved diagnostic log. Remove the temporary variables afterwards because they can generate substantial log traffic.

For complete user-facing instructions, including how to preserve EmuDeck arguments, create `MAKO-diagnostics.txt` on the Desktop, restore normal launch settings, and submit the report, see [Collect MAKO Decky Diagnostics](COLLECT_DIAGNOSTICS.md).

## Update and Flatpak checks

Use the clean update path for every newer local ZIP:

1. Quit games using `/home/deck/.local/bin/mako-run`.
2. Uninstall **Mako** from Decky and install the newer ZIP through **Developer > Install Plugin from Zip**.
3. Restart the Steam Deck or Steam Machine.
4. Open Mako and select **Install MAKO Renderer**.
5. In **Flatpak Setup**, select **Update** for every prepared application's matching runtime extension, such as Heroic or Dolphin.

This preserves profiles and launch options while replacing the private native Renderer and refreshing any shared Flatpak extensions.

## Report an issue

Follow [Collect MAKO Decky Diagnostics](COLLECT_DIAGNOSTICS.md) to reproduce the problem, create the focused Desktop report, restore normal launch settings, and submit the file privately. The linked form asks for the report context, so do not paste the diagnostic text into a public GitHub issue.
