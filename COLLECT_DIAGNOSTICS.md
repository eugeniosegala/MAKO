# Collect MAKO Diagnostics

Choose the guide that matches your installation. Each guide explains how to enable focused logging, reproduce the problem once, create `MAKO-diagnostics.txt`, restore normal launch settings, and submit the report.

Use the shared [MAKO diagnostic report form](https://docs.google.com/forms/d/e/1FAIpQLScSd9qgkYCq3Kbbc3_52k4_82iTmEqt3_FxOqGuxQ6FsjutgA/viewform) to describe the problem and upload the file. Do not paste diagnostic text into a public GitHub issue.

## MAKO Decky

If Decky Loader cannot install **MAKO Decky**, or the plugin does not appear or open, use the [installation and loading failure guide](https://github.com/eugeniosegala/MAKO/blob/main/plugin/docs/DECKY_INSTALLATION_FAILURES.md).

If **MAKO Decky** opens, use the [MAKO Decky diagnostics guide](https://github.com/eugeniosegala/MAKO/blob/main/plugin/docs/COLLECT_DIAGNOSTICS.md). It covers native Steam and Proton games, Heroic, EmuDeck, the managed Renderer, and Flatpak extensions.

## Standalone MAKO Renderer

Use the [standalone diagnostics guide](https://github.com/eugeniosegala/MAKO/blob/main/engine/docs/COLLECT_DIAGNOSTICS.md) if you installed **MAKO Renderer** directly, built it from source, open `mako-ui` yourself, or launch games with `mako-launch` without MAKO Decky. It covers Steam, Proton, terminal, Heroic, and Flatpak launches.

## If you are not sure

- If **MAKO Decky** appears in Decky Loader, use its guide.
- If you extracted `MAKO-Renderer-v<version>-linux.tar.xz` or ran `cmake --install`, use the standalone guide.
- Otherwise, explain in the form how you start the game.

Review the generated file for personal paths before uploading it.
