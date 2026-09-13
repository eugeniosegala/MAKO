MAKO Renderer standalone archive
================================

MAKO Renderer provides Frame Generation and spatial scaling for Vulkan games on SteamOS and Linux. The archive includes matching 64-bit and 32-bit Vulkan layers, the configuration UI, the command-line tools, and the managed installer.

Frame Generation and LS1 scaling require a user-owned installation of the default public version of Lossless Scaling from Steam, with beta participation disabled. The open MAKO Scaler does not require Lossless.dll.

Install or update
-----------------

1. Close every game currently using MAKO Renderer.
2. Extract the entire archive into a new folder. Do not run the installer from an archive preview.
3. Double-click "Install MAKO Renderer" and choose "Execute" if your file manager asks.
4. Confirm the installation location. The installer verifies every managed payload file, safely updates an existing installation, and preserves your profiles.
5. MAKO Renderer Configuration opens when installation finishes. You can reopen it from the application menu or by running ~/.local/bin/mako-ui.

Prepare and launch a game
-------------------------

Installing MAKO Renderer or opening its configuration window does not activate it for every game. A standalone native or Proton game needs both a matching profile and the launch command.

1. In MAKO Renderer Configuration, choose "Create New Profile" and name it.
2. Under Profile Matching > Matched Processes > Edit..., enter the game's executable or process name (for example Game.exe), then press +. Use the game's process, not its launcher or Steam display title.
3. Select Frame Generation and/or scaling. Set Lossless.dll Path if automatic discovery fails. To use the open MAKO Scaler without the DLL, turn Frame Generation off and select MAKO Scaler. Scaling enablement and other restart-labelled settings must be selected before launch.
4. Changes save automatically. You can close the configuration window; it does not need to stay open during play.

For a native Steam or Proton game, add this under Steam Properties > General > Launch Options:

    ~/.local/bin/mako-launch %command%

Keep %command% exactly as written. Set this once per game, then use Steam's Play button normally. If you selected a custom installation prefix, use the mako-launch path shown by the installer's completion message instead.

For a direct desktop command, replace %command% with the actual game executable and arguments:

    ~/.local/bin/mako-launch "/path/to/your-game"

Flatpak games, launchers, and emulators require the matching MAKO runtime extension and preparation for each application. The standalone configuration window edits profiles; it does not prepare Flatpaks. "Install MAKO Flatpak Extensions" installs extensions only. Follow the Flatpak setup link below for runtime selection, configuration/DLL access, and sandbox environment, then restart and launch the app normally. The host mako-launch command does not replace that preparation. MAKO Decky's Flatpak Setup provides managed preparation when using Decky.

Update
------

Download and fully extract the newer MAKO Renderer archive, close games using MAKO, and run its "Install MAKO Renderer" file. The installer replaces only the managed payload and preserves your profiles and settings.

The complete payload is verified and staged before any installed file is replaced. If a later update step fails, the installer restores the previous files and selected Renderer version. Check destination permissions and available disk space before retrying; do not run the user-local installer with sudo. If restoration also fails, the installer reports where it retained recovery backups.

Uninstall
---------

Open the application menu and select "Uninstall MAKO Renderer", or run:

    ~/.local/bin/mako-installer --uninstall

The first confirmation removes the managed native Renderer while preserving modified files and your profiles. MAKO Decky remains installed, but the default standalone installation shares its native Renderer with MAKO Decky; after removal, open MAKO Decky and select "Install Renderer" before using MAKO through Decky again. Shared Flatpak runtime extensions remain installed.

The uninstaller then separately asks whether to remove profiles, settings, and diagnostics from ~/.config/mako-render. Declining keeps them for a later installation. Accepting permanently removes that directory and its contents.

Documentation and support
-------------------------

Direct installation and usage:
https://github.com/eugeniosegala/MAKO/blob/main/engine/README.md

Flatpak setup:
https://github.com/eugeniosegala/MAKO/blob/main/engine/docs/FLATPAK-GUIDE.md

Troubleshooting:
https://github.com/eugeniosegala/MAKO/blob/main/engine/docs/TROUBLESHOOTING.md

Diagnostics:
https://github.com/eugeniosegala/MAKO/blob/main/engine/docs/COLLECT_DIAGNOSTICS.md
