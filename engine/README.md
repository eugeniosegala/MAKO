# MAKO Renderer

<p align="center">
  <img src="assets/mako-render-logo.png" width="256" alt="MAKO Renderer logo" />
</p>

> [!NOTE]
> **LSFG-VK Experimental is now MAKO Renderer.** The [MAKO repository](https://github.com/eugeniosegala/MAKO) is its new home and continuation, including future development, releases, documentation, and issue tracking.

MAKO Renderer is the Vulkan renderer and layer component of MAKO. It brings Lossless Scaling frame generation—and, as the project expands, scaling—to Steam Deck and desktop Linux.

The layer is derived from [lsfg-vk](https://github.com/PancakeTAS/lsfg-vk) and retains its open-source attribution and license obligations. It requires a user-supplied `Lossless.dll` from a licensed [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) installation. MAKO Renderer does not bundle or replace that proprietary library.

## Release status

MAKO-branded Renderer archives are not published yet. The previous [LSFG-VK Experimental releases](https://github.com/eugeniosegala/lsfg-vk-experimental/releases) remain available, but their archives, wrapper names, and instructions use the old project identity. Build MAKO Renderer from this repository for the current standalone workflow; this page will link to MAKO archives once they are released.

## Install

### Steam Deck or Steam Machine: use MAKO Decky

For SteamOS, the companion **MAKO Decky** plugin is the recommended installation path. It installs the renderer in a private location, creates the `mako-run` launcher, and prepares supported Flatpak applications. Install the Decky ZIP, open **Mako**, and select **Install MAKO Renderer (developer build)**. For a native Steam or Proton game, set its Steam launch option to:

```text
~/.local/bin/mako-run %command%
```

See the [main MAKO installation guide](../README.md#install-and-use) for the complete Decky, Heroic, EmuDeck, and Dolphin setup. The first MAKO-branded Decky ZIP has not been published yet; do not mistake an empty MAKO release page for an installer.

### Desktop Linux: install a Renderer archive

Once MAKO Renderer archives are published, download the Linux archive and extract it into your user-local prefix:

```bash
mkdir -p ~/.local
tar -xJf mako-render-<version>-linux.tar.xz -C ~/.local
```

The archive installs the `mako-ui` configuration application, `mako-cli`, Vulkan manifests, and the 64-bit layer. Release archives also include the matching 32-bit layer for 32-bit games. If `~/.local/bin` is not on your `PATH`, run the tools with their full paths, for example `~/.local/bin/mako-ui`.

Install a licensed copy of [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) through Steam before launching a game. MAKO normally finds `Lossless.dll` in common Steam locations; choose its location explicitly in the UI or configuration file if your Steam library is elsewhere.

### Build and install from source

For the current standalone MAKO workflow, build the Renderer from this repository. Install the prerequisites for your distribution first, then run:

```bash
git clone https://github.com/eugeniosegala/MAKO.git
cd MAKO/engine

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DMAKO_BUILD_UI=On \
  -DMAKO_INSTALL_XDG_FILES=On
cmake --build build
sudo cmake --install build
```

This installs a native layer for the architecture being built. The full [building-from-source guide](docs/BUILDING-FROM-SOURCE.md) covers distribution packages, SteamOS prerequisites, 32-bit builds, and non-system installation prefixes.

### Flatpak applications

For a Flatpak game or emulator, install the matching MAKO Vulkan runtime extension and grant that application access to MAKO's configuration and the Steam library. MAKO Decky performs this setup through **Flatpak Setup**; direct installs can follow the [Flatpak guide](docs/FLATPAK-GUIDE.md).

## Configure and use

1. Start `mako-ui` (or `~/.local/bin/mako-ui` from an extracted archive), choose the Lossless Scaling DLL if MAKO did not find it automatically, and add a profile for the game.
2. Set the profile's **Active In** entry to the game's Linux binary or Windows executable name. Start with fixed **2x** frame generation and adjust from there.
3. Launch only the game through MAKO. For a Steam game, use this launch option:

   ```text
   ENABLE_MAKO=1 %command%
   ```

   For a direct desktop command, prefix the game's command in the same way:

   ```bash
   ENABLE_MAKO=1 your-game-command
   ```

4. Start the game normally. MAKO's implicit Vulkan layer remains off for every other process.

You can configure MAKO without the UI by editing `~/.config/mako-render/conf.toml`. This minimal profile selects a Windows game executable and enables 2x frame generation:

```toml
[[profile]]
name = "My game"
active_in = ["Game.exe"]
multiplier = 2
frame_generation_enabled = true
```

Use the game's own frame limiter and test V-Sync both on and off; the smoother result is game- and compositor-dependent. See [Configuration](docs/CONFIGURATION.md) for every setting, profile matching, adaptive frame generation, and environment-variable overrides.

### Validate the configuration

After installing the command-line tool, validate the default configuration with:

```bash
mako-cli validate
```

Use an explicit file when needed:

```bash
mako-cli validate --config ~/.config/mako-render/conf.toml
```

For a layer-activation check on a Vulkan-capable system with `vulkaninfo` installed:

```bash
ENABLE_MAKO=1 vulkaninfo | grep -i VK_LAYER_MAKO_render
```

## Identity

- Product: **MAKO Renderer**
- Vulkan layer: `VK_LAYER_MAKO_render`
- Library: `libmako-render.so`
- Configuration UI: `mako-ui`
- CLI: `mako-cli`
- Configuration: `~/.config/mako-render/conf.toml`
- Activation: `ENABLE_MAKO=1`
- Deactivation: `DISABLE_MAKO=1`

## Build packages

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

## More documentation

- [Configuration](docs/CONFIGURATION.md): profiles, frame-generation controls, Adaptive mode, and environment variables.
- [Flatpak guide](docs/FLATPAK-GUIDE.md): runtime extensions and direct Flatpak application overrides.
- [Building from Source](docs/BUILDING-FROM-SOURCE.md): prerequisites, SteamOS builds, and packaging.
- [Troubleshooting](docs/TROUBLESHOOTING.md): activation, configuration, and presentation diagnostics.
