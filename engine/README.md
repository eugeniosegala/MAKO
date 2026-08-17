# MAKO Renderer

<p align="center">
  <img src="assets/mako-render-logo.png" width="256" alt="MAKO Renderer logo" />
</p>

> [!NOTE]
> **LSFG-VK Experimental is now MAKO Renderer.** The [MAKO repository](https://github.com/eugeniosegala/MAKO) is its new home and continuation, including future development, releases, documentation, and issue tracking.

MAKO Renderer is the Vulkan renderer and layer component of MAKO. It brings Lossless Scaling frame generation—and, as the project expands, scaling—to Steam Deck and desktop Linux.

The layer is derived from [lsfg-vk](https://github.com/PancakeTAS/lsfg-vk) and retains its open-source attribution and license obligations. It requires a user-supplied `Lossless.dll` from a licensed [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) installation. MAKO Renderer does not bundle or replace that proprietary library.

## Download

Standalone Linux and Flatpak archives are published on the [`render-v…` MAKO Renderer release track](https://github.com/eugeniosegala/MAKO/releases?q=tag%3Arender-v). Steam Deck users who want the managed Decky experience should install the [latest MAKO Decky release](https://github.com/eugeniosegala/MAKO/releases/latest) instead.

## Identity

- Product: **MAKO Renderer**
- Vulkan layer: `VK_LAYER_MAKO_render`
- Library: `libmako-render.so`
- Configuration UI: `mako-ui`
- CLI: `mako-cli`
- Configuration: `~/.config/mako-render/conf.toml`
- Activation: `ENABLE_MAKO=1`
- Deactivation: `DISABLE_MAKO=1`

## Build

MAKO Renderer uses CMake and requires Vulkan development headers. The optional desktop interface also requires Qt 6.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Local Linux archives and Flatpak runtime extensions can be built with:

```bash
./scripts/package-local.sh
./scripts/package-flatpaks.sh
```

Artifacts are written under `engine/out/`. The Decky package in the sibling `plugin/` directory builds and bundles this engine automatically through `pnpm run package:local-engine`.

## Direct activation

For development outside Decky, activate only the MAKO layer for the launched process:

```bash
ENABLE_MAKO=1 your-game-command
```

Most Steam Deck users should use MAKO Decky and its `~/.local/bin/mako-run` wrapper instead of managing layer paths manually.

See [Configuration](docs/CONFIGURATION.md), [Building from Source](docs/BUILDING-FROM-SOURCE.md), and [Troubleshooting](docs/TROUBLESHOOTING.md) for detailed workflows.
