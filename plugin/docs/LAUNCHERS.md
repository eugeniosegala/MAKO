# Third-party launcher setup

Install MAKO Renderer through MAKO Decky first, then follow the guide for your launcher. **Flatpak Setup** shows the exact **Wrapper path for this device**; the examples below use the standard SteamOS path `/home/deck/.local/bin/mako-run`.

- [Heroic](#heroic)
- [Lutris](#lutris)
- [EmuDeck](#emudeck)
- [Manually added Flatpak shortcuts](#manually-added-flatpak-shortcuts)

Flatpak Heroic and Lutris run MAKO from the selected game's wrapper inside their sandbox. Flatpak emulators instead need app-wide preparation. After updating MAKO Decky, use **Flatpak Setup > Update** for every prepared application's matching runtime extension, then restart that application.

**Using MAKO Renderer without Decky:** native Heroic and Lutris can use the installed `mako-launch` path, usually `/home/deck/.local/bin/mako-launch`, in the same per-game field described below. Configure MAKO through `mako-ui` or its configuration file. For Flatpak installations, follow the [standalone Flatpak guide](../../engine/docs/FLATPAK-GUIDE.md); the host `mako-launch` path is not a sandbox wrapper.

## Heroic

Configure Flatpak Heroic through **Flatpak Setup**. Native Heroic skips the first step.

1. In MAKO Decky's **Flatpak Setup**, prepare **Heroic** and install the matching runtime extension when prompted.
2. In each game you want to use with MAKO, open **Settings > Advanced** and set the first **Wrapper** field to the path shown by MAKO. On standard SteamOS it is:

    ```text
    /home/deck/.local/bin/mako-run
    ```

    Leave **Arguments** empty and do not use `%command%`.

3. Start the game normally from Heroic or its Steam shortcut.

Preparing Heroic makes MAKO available inside its sandbox; the per-game Wrapper decides which games use it. Remove the game's Wrapper to stop using MAKO for that game.

<!-- prettier-ignore -->
> [!IMPORTANT]
> After installing a newer MAKO ZIP, return to **Flatpak Setup** and select **Update** for Heroic's matching runtime extension, then restart Heroic. Updating MAKO Decky or the shared native Renderer does not update Flatpak extensions.

## Lutris

For **Flatpak Lutris**, first prepare **Lutris** (`net.lutris.Lutris`) in MAKO Decky's **Flatpak Setup**. Install the matching runtime extension when prompted. Native Lutris skips Flatpak Setup.

1. Right-click the chosen game in Lutris and open **Configure > System options**. Enable **Advanced** options if **Command prefix** is hidden.
2. Set **Command prefix** to your MAKO wrapper path:

    ```text
    /home/deck/.local/bin/mako-run
    ```

    Use the path displayed by MAKO for your device. If it contains spaces, enclose the whole path in double quotes. Lutris supplies the executable and arguments; do not add `%command%` or put the wrapper in **Pre-launch script**. Apply this to the selected game's configuration; a global prefix would affect every game.

3. Save and start the game from Lutris or its existing Lutris-generated Steam shortcut. Keep the shortcut's original fields intact.
4. Use Vulkan for native games or emulators, or DXVK/VKD3D for compatible Direct3D games running through Wine/Proton. MAKO does not process OpenGL output.

Remove MAKO from that game's Command prefix to disable it. Games delegated to an already-running Steam client need the [normal Steam launch option](../../README.md#install-and-use) in Steam itself.

Lutris's [system option definition](https://github.com/lutris/lutris/blob/master/lutris/sysoptions.py) and [launch command builder](https://github.com/lutris/lutris/blob/master/lutris/runner_interpreter.py) own the Command prefix behavior. Individual games and runner versions still need compatibility testing.

## EmuDeck

For any EmuDeck emulator installed as a Flatpak:

1. In MAKO Decky, select **Flatpak Setup** and prepare the emulator you use. Install its matching runtime extension when prompted. Preparation applies to the entire emulator Flatpak rather than one ROM because Flatpak must receive MAKO's layer and configuration inside its sandbox.
2. Select **Vulkan** as that emulator's graphics backend when it offers one.
3. In Desktop Mode, open the Steam shortcut for each EmuDeck game you want to configure, then set these fields under **Properties > Shortcut**:

    - **Target**

        ```text
        /home/deck/.local/bin/mako-run
        ```

        This is the standard SteamOS path. If MAKO shows a different **Wrapper path for this device** under **Flatpak Setup**, use the displayed path.

    - **Start In**

        ```text
        /usr/bin
        ```

    - **Launch Options:** leave the EmuDeck-generated value unchanged. It already contains the correct emulator ID, ROM path, and flags for that shortcut.

Steam shortcuts choose the game profile, but Flatpak preparation is app-wide. Disable it in **Flatpak Setup** to keep MAKO unavailable to an emulator.

If EmuDeck installed an emulator as a native application or AppImage instead, it is not a Flatpak workflow: use the normal Steam launch option `/home/deck/.local/bin/mako-run %command%` for that shortcut.

<!-- prettier-ignore -->
> [!IMPORTANT]
> After updating MAKO, return to **Flatpak Setup** and select **Update** for every prepared emulator's matching runtime extension.

## Manually added Flatpak shortcuts

Use this workflow only when a non-Steam shortcut's original **Target** is `/usr/bin/flatpak`. For Heroic, Lutris, and EmuDeck, use their dedicated instructions above.

1. In **Flatpak Setup**, install the matching runtime extension and prepare the Flatpak application.
2. In the shortcut's **Properties > Shortcut**, replace **Target** with:

    ```text
    "/home/deck/.local/bin/mako-run" "/usr/bin/flatpak"
    ```

    Use MAKO's displayed **Wrapper path for this device** when it differs from `/home/deck/.local/bin/mako-run`.

3. Leave **Start In** and **Launch Options** unchanged so the original Flatpak application ID, command, and flags are preserved.

The reference shown in **Flatpak Setup** does not modify Steam automatically; it only builds the correct Target from this device's installed wrapper path.

For diagnostic logging, follow [Collect MAKO Decky Diagnostics](COLLECT_DIAGNOSTICS.md).
