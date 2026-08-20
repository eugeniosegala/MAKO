# Collect MAKO Diagnostics

To help me understand what happened, please follow one of the two diagnostic guides below. You only need the guide that matches how MAKO was installed on your device. It will show you how to enable focused logging, reproduce the problem once, create `MAKO-diagnostics.txt`, restore your normal launch settings, and send the report to me.

The shared [MAKO diagnostic report form](https://docs.google.com/forms/d/e/1FAIpQLScSd9qgkYCq3Kbbc3_52k4_82iTmEqt3_FxOqGuxQ6FsjutgA/viewform) will guide you through describing the problem and uploading the file, so you do not need to write a separate diagnostic report in the GitHub issue.

## MAKO Decky

If Decky Loader cannot install **MAKO Decky**, or it never appears or opens after installation, use the [installation and loading failure guide](https://github.com/eugeniosegala/MAKO/blob/main/plugin/docs/DECKY_INSTALLATION_FAILURES.md). That guide does not require either MAKO component to be running.

If **MAKO Decky** is installed and opens in Decky Loader, use its [diagnostics guide](https://github.com/eugeniosegala/MAKO/blob/main/plugin/docs/COLLECT_DIAGNOSTICS.md). It covers games launched through `/home/deck/.local/bin/mako-run`, the managed renderer, and Flatpak extensions.

The Decky diagnostics guide covers native Steam and Proton games, Heroic, and EmuDeck shortcuts without changing their normal arguments.

## Standalone MAKO Renderer

Use the [standalone diagnostics guide](https://github.com/eugeniosegala/MAKO/blob/main/engine/docs/COLLECT_DIAGNOSTICS.md) when you installed **MAKO Renderer** directly, built it from source, open `mako-ui` yourself, or launch games with `mako-launch` without MAKO Decky.

The standalone guide covers native Steam and Proton games, direct terminal launches, and existing Heroic or Flatpak configurations.

## If you are not sure

- If **MAKO Decky** appears in Decky Loader, use its guide.
- If you extracted `MAKO-Renderer-v<version>-linux.tar.xz` or ran `cmake --install`, use the **standalone MAKO Renderer** guide.
- If neither description is clear, select **Not sure** in the shared form and explain how you normally start the game.

Do not paste the diagnostic text into a public GitHub issue. Follow the selected guide, review the generated file for personal paths, and upload it through the shared form.
