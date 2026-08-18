# MAKO Decky

<p align="center">
  <img src="assets/mako-logo.webp" width="256" alt="MAKO Decky logo" />
</p>

> [!NOTE]
> **[Decky LSFG-VK Experimental](https://github.com/eugeniosegala/decky-lsfg-vk-experimental) is now MAKO Decky.** The [MAKO repository](https://github.com/eugeniosegala/MAKO) is its new home and continuation, including future development, releases, documentation, and issue tracking.

MAKO Decky is the Decky Loader component of MAKO. It provides per-game controls, installation, updates, Flatpak preparation, and game launch integration for MAKO Renderer on Steam Deck and compatible Linux systems.

MAKO is an independent community project bringing Lossless Scaling frame generation to Linux today, with scaling support planned. It requires a user-supplied `Lossless.dll` from a licensed [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) installation. MAKO Decky does not bundle, copy, or modify that proprietary library.

## Download

Open the [latest MAKO Decky release](https://github.com/eugeniosegala/MAKO/releases/latest) and download the ZIP under **Assets**.
Previous Decky releases are available on the
[MAKO releases page](https://github.com/eugeniosegala/MAKO/releases).

For direct Vulkan-layer installation without Decky, open the
[latest MAKO Renderer release](https://github.com/eugeniosegala/MAKO/releases/tag/render-v2.0.0)
and download the Linux archive under **Assets**.

## What it manages

- Installs the private MAKO Renderer Vulkan layer for the current user.
- Generates the `/home/deck/.local/bin/mako-run` per-game launcher.
- Stores renderer settings in `~/.config/mako-render/conf.toml` and versioned game/process identity separately for automatic per-game selection.
- Supports fixed and adaptive frame generation with live per-game controls.
- Prepares matching Vulkan runtime extensions for selected Flatpak applications.
- Launches selected games through MAKO's private renderer and configuration.

Scaling controls are part of MAKO's product direction; frame-generation support is the currently integrated path.

## Development

MAKO Decky lives in the `plugin/` directory of the MAKO monorepo and consumes the sibling `engine/` source tree.

```bash
pnpm install --frozen-lockfile
pnpm run test
pnpm run build
pnpm run package:local-engine
```

`pnpm run package:local-engine` builds and bundles the sibling MAKO Renderer checkout. Use `pnpm run package:local-engine-fast` for a native, 64-bit development package without Flatpak extensions.

The resulting Decky ZIP is written under `plugin/out/`. Nothing is published by the local packaging commands.

## Using a local build

After installing the ZIP through Decky developer settings, open Mako and install MAKO Renderer. For a native Steam or Proton game, use:

```text
/home/deck/.local/bin/mako-run %command%
```

MAKO's wrapper activates `VK_LAYER_MAKO_render` only for the selected game process.

See [Configuration](docs/CONFIGURATION.md), [Troubleshooting](docs/TROUBLESHOOTING.md), [Collect MAKO Diagnostics](docs/COLLECT_DIAGNOSTICS.md), and [Packaging](docs/PACKAGING.md) for detailed workflows.
