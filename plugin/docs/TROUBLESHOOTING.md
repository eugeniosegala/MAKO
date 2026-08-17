# Troubleshooting

These instructions apply to both published MAKO Decky ZIPs and local developer
builds.

## HDR is unavailable by design

HDR frame generation is still under development. **Disable Experimental HDR
(Restart)** is checked, read-only, and enforced by the backend. The launcher
uses MAKO's validated SDR path, so an HDR option being unavailable in a game is
expected and does not mean the ZIP installed incorrectly.

Use **Disable MAKO Renderer on Next Launch** and restart the game if you need
to test whether the layer is the cause of a startup or presentation problem.

## A game will not start or MAKO appears inactive

1. For a native Steam or Proton game, confirm its launch option is exactly:

   ```text
   ~/.local/bin/mako-run %command%
   ```

2. Open Mako and select **Install MAKO Renderer (developer build)**. Installing
   a Decky ZIP alone does not replace the private Renderer payload.
3. Check the selected profile's `Lossless.dll` path, GPU choice, and **Active
   In** rule. Start with Fixed 2x before testing Adaptive settings.
4. Test the game's V-Sync both on and off. Its own FPS limiter, VRR, or
   compositor configuration can affect frame pacing.

For Heroic, keep the **Wrapper** as `/home/deck/.local/bin/mako-run`, leave
**Arguments** empty, and add no `%command%`. For an EmuDeck Flatpak emulator
shortcut, prepare that emulator in **Flatpak Setup**, then put the wrapper in
the shortcut's **Target** and retain its `flatpak run ...` command in **Launch
Options**. Add `MAKO_PROFILE=...` there only to override Mako's selected
profile. See the [EmuDeck setup](../../README.md#emudeck).

## Collect diagnostics

Published builds keep diagnostics off by default. Local development ZIPs and
direct `dev:*` deployments enable them automatically. To collect a focused
Steam report, temporarily replace the game's normal launch option with:

```text
MAKO_PRESENT_DIAGNOSTICS=1 MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 ~/.local/bin/mako-run %command%
```

For Heroic, keep the normal Wrapper and Arguments fields. Add these two
environment variables in the game's settings instead:

```text
MAKO_PRESENT_DIAGNOSTICS=1
MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25
```

Reproduce the issue, quit the game, then run this in Desktop Mode:

```bash
~/.local/bin/mako-diagnostics all
```

Useful focused reports are `mako-diagnostics adaptive`, `recovery`,
`performance`, `startup`, and `errors`. Add `--lines 2000` when the normal
output is too short, or `--log PATH` for a saved diagnostic log. Remove the
temporary variables afterwards because they can generate substantial log
traffic.

## Update and Flatpak checks

Use the clean update path for every newer local ZIP:

1. Quit games using `mako-run`.
2. Uninstall **Mako** from Decky and install the newer ZIP through
   **Developer > Install Plugin from Zip**.
3. Restart the Steam Deck or Steam Machine.
4. Open Mako and select **Install MAKO Renderer (developer build)**.
5. In **Flatpak Setup**, select **Update** for every prepared application's
   matching runtime extension, such as Heroic or Dolphin.

This preserves profiles and launch options while replacing the private native
Renderer and refreshing any shared Flatpak extensions.

## Report an issue

Include the MAKO ZIP/commit, SteamOS or Linux version, GPU and driver, game and
Proton version, selected profile, exact launch method, and the relevant
diagnostic report. Remove personal paths before sharing configuration or logs.
