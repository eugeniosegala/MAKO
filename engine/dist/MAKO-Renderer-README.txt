MAKO Renderer standalone archive
================================

MAKO Renderer provides Frame Generation and spatial scaling for Vulkan games on SteamOS and Linux. The archive includes matching 64-bit and 32-bit Vulkan layers, the configuration UI, the command-line tools, and the managed installer.

Frame Generation and LS1 scaling require a user-owned installation of Lossless Scaling from Steam. The open MAKO Scaler does not require Lossless.dll.

Install or update
-----------------

1. Close every game currently using MAKO Renderer.
2. Extract the entire archive into a new folder. Do not run the installer from an archive preview.
3. Double-click "Install MAKO Renderer" and choose "Execute" if your file manager asks.
4. Confirm the installation location. The installer verifies every managed payload file, safely updates an existing installation, and preserves your profiles.
5. MAKO Renderer Configuration opens when installation finishes. You can reopen it from the application menu or by running ~/.local/bin/mako-ui.

For a native Steam or Proton game, add this under Steam Properties > Launch Options:

    ~/.local/bin/mako-launch %command%

If you selected a custom installation prefix, use the mako-launch path shown by the installer's completion message instead.

Create or select a matching profile in MAKO Renderer Configuration before starting the game. Scaling enablement and other restart-labelled settings must be selected before launch. Flatpak games require the matching MAKO runtime extension and sandbox preparation described in the online Flatpak guide.

Update
------

Download and fully extract the newer MAKO Renderer archive, close games using MAKO, and run its "Install MAKO Renderer" file. The installer replaces only the managed payload and preserves your profiles and settings.

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
