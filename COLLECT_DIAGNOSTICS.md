# Collect MAKO Diagnostics

MAKO has two installation workflows. Choose the guide that matches how MAKO
was installed on the affected device. Both guides create the same
`MAKO-diagnostics.txt` file and submit it through the same
[MAKO diagnostic report form][diagnostic-form].

## MAKO Decky

Use the [MAKO Decky diagnostics guide](plugin/docs/COLLECT_DIAGNOSTICS.md) when
you installed **Mako** through Decky Loader, use
`/home/deck/.local/bin/mako-run`, or manage the Renderer and Flatpak extensions
from the Decky plugin.

The Decky guide covers native Steam and Proton games, Heroic, and EmuDeck
shortcuts without changing their normal arguments.

## Standalone MAKO Renderer

Use the [standalone MAKO Renderer diagnostics guide](engine/docs/COLLECT_DIAGNOSTICS.md)
when you installed the Renderer archive directly, built it from source, open
`mako-ui` yourself, or activate games with `ENABLE_MAKO=1` without the Decky
plugin.

The Renderer guide covers native Steam and Proton games, direct terminal
launches, and existing Heroic or Flatpak configurations.

## If you are not sure

- If **Mako** appears in Decky Loader, use the **MAKO Decky** guide.
- If you extracted `mako-render-v<version>-linux.tar.xz` or ran
  `cmake --install`, use the **standalone MAKO Renderer** guide.
- If neither description is clear, select **Not sure** in the shared form and
  explain how you normally start the game.

Do not paste the diagnostic text into a public GitHub issue. Follow the
selected guide, review the generated file for personal paths, and upload it
through the shared form.

[diagnostic-form]: https://docs.google.com/forms/d/e/1FAIpQLScSd9qgkYCq3Kbbc3_52k4_82iTmEqt3_FxOqGuxQ6FsjutgA/viewform
