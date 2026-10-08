# Third-party launcher setup

Install MAKO Renderer through MAKO Decky first. The examples use `/home/deck/.local/bin/mako-run`; use **Flatpak Setup > Wrapper path for this device** if yours differs. MAKO needs Vulkan, including DXVK/VKD3D for compatible Windows games.

- [Heroic](#heroic)
- [Lutris](#lutris)
- [EmuDeck](#emudeck)
- [Manually added Flatpak shortcuts](#manually-added-flatpak-shortcuts)
- [Other non-Steam games](#other-non-steam-games)

For standalone MAKO Renderer, native launchers use `mako-launch`; Flatpak apps follow the [standalone guide](../../engine/docs/FLATPAK-GUIDE.md).

## Heroic

1. For Flatpak Heroic, prepare **Heroic** in **Flatpak Setup** and install the matching extension. Native Heroic skips this step.
2. Open the game's **Settings > Advanced** and set the first **Wrapper** field to:

    ```text
    /home/deck/.local/bin/mako-run
    ```

    Leave **Arguments** empty; do not add `%command%`.

3. Launch from Heroic or its Steam shortcut.

Remove the game's Wrapper to disable MAKO for that game.

## Lutris

1. For Flatpak Lutris, prepare **Lutris** in **Flatpak Setup** and install the matching extension.
2. Right-click the game and open **Configure > System options**. Enable **Advanced** if needed, then set **Command prefix** to:

    ```text
    /home/deck/.local/bin/mako-run
    ```

    Quote paths containing spaces. Set this per game, not globally; do not add `%command%` or use **Pre-launch script**.

3. Save and launch from Lutris or its existing Steam shortcut.

Remove the prefix to disable MAKO. Games launched through an already-running Steam client need the [Steam launch option](../../README.md#install-and-use).

## EmuDeck

1. Prepare each Flatpak emulator in **Flatpak Setup** and install its matching extension.
2. Select **Vulkan** in the emulator's graphics settings.
3. In Desktop Mode, edit each game's Steam **Properties > Shortcut**:

    | Field | Value |
    | --- | --- |
    | **Target** | `/home/deck/.local/bin/mako-run` (or your displayed wrapper path) |
    | **Start In** | `/usr/bin` |
    | **Launch Options** | Keep the EmuDeck-generated value, including the ROM path and flags. |

Disable the emulator in **Flatpak Setup** to remove app-wide preparation. Native/AppImage emulators use `/home/deck/.local/bin/mako-run %command%` in Launch Options instead.

## Manually added Flatpak shortcuts

For shortcuts whose original **Target** is `/usr/bin/flatpak`:

1. Prepare the app and install its extension in **Flatpak Setup**.
2. Replace **Target** with the following, using your displayed wrapper path:

    ```text
    "/home/deck/.local/bin/mako-run" "/usr/bin/flatpak"
    ```

3. Keep **Start In** and **Launch Options** unchanged. Steam shortcut fields must be edited manually.

## Other non-Steam games

For native Linux or Proton shortcuts, keep **Target** and **Start In** and set **Launch Options** to:

```text
/home/deck/.local/bin/mako-run %command%
```

Launch, configure the game profile, and restart after enabling features. For OpenGL games, try **Zink**.

## Updates and help

After updating MAKO, use **Flatpak Setup > Update** for each prepared app's matching runtime extension, then restart the app. Flatpak Heroic/Lutris use per-game wrappers; emulator preparation is app-wide.

If an app update changes its runtime, reopen **Flatpak Setup**. Prepared apps show whether their current matching MAKO extension is installed. When it is missing, select **Install <version> extension** beside the app, then restart the app and its games. Preparation, profiles, and per-game wrappers remain valid when the app ID and overrides stay the same; do not toggle preparation off and on just to change runtimes. An unsupported or unavailable runtime, or an unavailable extension-status check, shows a warning without guessing an install branch. New runtime branches are installed only when selected; **Install MAKO Renderer** refreshes branches already installed.

MAKO Scaler and Shaders work without Lossless Scaling; Frame Generation and LS1 require it. For problems, [collect diagnostics](COLLECT_DIAGNOSTICS.md).
