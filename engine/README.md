# MAKO Renderer

<p align="center">
  <img src="assets/mako-render-logo.webp" width="256" alt="MAKO Renderer logo" />
</p>

> [!NOTE]
> **[LSFG-VK Experimental](https://github.com/eugeniosegala/lsfg-vk-experimental) is now MAKO Renderer.** The [MAKO repository](https://github.com/eugeniosegala/MAKO) is its new home and continuation, including future development, releases, documentation, and issue tracking.

MAKO Renderer is the Vulkan renderer and layer component of MAKO. It brings Lossless Scaling frame generation—and, as the project expands, scaling—to Steam Deck and desktop Linux.

The layer is derived from [lsfg-vk](https://github.com/PancakeTAS/lsfg-vk) and retains its open-source attribution and license obligations. It requires a user-supplied `Lossless.dll` from a licensed [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) installation. MAKO Renderer does not bundle or replace that proprietary library.

## Downloads

Standalone Linux and Flatpak archives are published on the
[latest MAKO Renderer release](https://github.com/eugeniosegala/MAKO/releases/tag/render-v2.0.0).
Download the required archive under **Assets**.
Steam Deck users who want the managed workflow should install the
[latest MAKO Decky release](https://github.com/eugeniosegala/MAKO/releases/latest).

## Installation

### Steam Deck or Steam Machine: use MAKO Decky

For SteamOS, the companion **MAKO Decky** plugin is the recommended installation path. It installs the renderer in a private location, creates the `mako-run` launcher, and prepares supported Flatpak applications. Install the Decky ZIP, open **Mako**, and select **Install MAKO Renderer**. For a native Steam or Proton game, set its Steam launch option to:

```text
~/.local/bin/mako-run %command%
```

See the [main MAKO installation guide](../README.md#install-and-use) for the complete Decky, Heroic, and EmuDeck setup.

### Direct Linux installation

1. Purchase and install [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) through Steam. MAKO requires its licensed `Lossless.dll` but does not bundle, copy, or modify it.
2. Open the [latest MAKO Renderer release](https://github.com/eugeniosegala/MAKO/releases/tag/render-v2.0.0) and download its versioned Linux archive under **Assets**: `mako-render-v<version>-linux.tar.xz`.
3. Optionally save the archive's file list so you know exactly what was installed, then extract it into your user-local prefix:

```bash
mkdir -p ~/.local
tar -tJf mako-render-v<version>-linux.tar.xz > mako-render-v<version>-files.txt
tar -xJf mako-render-v<version>-linux.tar.xz -C ~/.local
```

The archive installs `mako-ui`, `mako-cli`, XDG desktop files, Vulkan manifests, and matching 64-bit and 32-bit layers. The Vulkan loader selects the correct layer for each game; the UI and CLI remain 64-bit applications. If `~/.local/bin` is not on your `PATH`, run tools with their full paths, such as `~/.local/bin/mako-ui`.

#### Start the configuration UI

After extracting the host archive, open the MAKO Renderer configuration UI using either method:

- **Application menu:** On Steam Deck or Steam Machine, switch to Desktop Mode. Open the application launcher, search for **MAKO Renderer Configuration**, and select it.
- **Terminal:** Open Konsole or another terminal and run:

  ```bash
  ~/.local/bin/mako-ui
  ```

Do not run the UI with `sudo`. It reads and writes your per-user configuration under `~/.config/mako-render/`. If the command reports a missing Qt component, install the graphical-interface requirements below and try again.

MAKO normally finds `Lossless.dll` in common Steam locations. Choose it explicitly in the UI or configuration file if your Steam library is elsewhere.

#### Qt requirements for the graphical interface

The Vulkan layer and `mako-cli` do not require Qt. The `mako-ui` graphical interface requires Qt 6, Qt Quick, and Qt Quick Controls. If those runtime components are not already installed, run only the command for your distribution:

```bash
# Debian or Ubuntu
sudo apt install qt6-qpa-plugins libqt6quick6 \
  qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-layouts qml6-module-qtquick-window \
  qml6-module-qtquick-dialogs qml6-module-qtqml-workerscript \
  qml6-module-qtquick-templates

# Arch Linux or SteamOS
sudo pacman -S qt6-base qt6-declarative

# Fedora
sudo dnf install qt6-qtbase qt6-qtdeclarative
```

Steam Deck and Steam Machine users following the MAKO Decky workflow do not need to perform this direct UI setup.

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

## Usage

### Graphical configuration

On SteamOS, switch to Desktop Mode and open **MAKO Renderer Configuration** from the application launcher. On any supported Linux desktop, you can instead start it from a terminal:

```bash
~/.local/bin/mako-ui
```

1. Choose the licensed Lossless Scaling DLL if MAKO did not find it automatically, then create a profile for the game.
2. Set **Active In** to the game's Linux binary, Windows executable, or process name. Start with fixed **2x** frame generation and adjust one setting at a time.
3. Launch only the selected game through MAKO. For a Steam game, use this launch option:

   ```text
   ENABLE_MAKO=1 %command%
   ```

   For a direct desktop command, prefix the game's command in the same way:

   ```bash
   ENABLE_MAKO=1 your-game-command
   ```

4. Start the game normally. MAKO's implicit Vulkan layer remains off for every other process. Do not combine MAKO with another Lossless Scaling Vulkan wrapper for the same game.

### Manual configuration

You can configure MAKO without the UI by editing `~/.config/mako-render/conf.toml`. This minimal profile selects a Windows game executable and enables 2x frame generation:

```toml
[[profile]]
name = "My game"
active_in = ["Game.exe"]
multiplier = 2
frame_generation_enabled = true
```

See [Configuration](docs/CONFIGURATION.md) for every setting, profile matching, Adaptive Frame Generation, and environment-variable overrides.

### Validation

After installing the command-line tool, validate the default configuration with:

```bash
~/.local/bin/mako-cli validate
```

Use an explicit file when needed:

```bash
~/.local/bin/mako-cli validate --config ~/.config/mako-render/conf.toml
```

For a layer-activation check on a Vulkan-capable system with `vulkaninfo` installed:

```bash
ENABLE_MAKO=1 vulkaninfo | grep -i VK_LAYER_MAKO_render
```

### Benchmarking

Run the built-in frame-generation benchmark with:

```bash
~/.local/bin/mako-cli benchmark
```

The default duration is 10 seconds. Run `~/.local/bin/mako-cli` without a subcommand to see benchmark options for the DLL path, resolution, Flow Scale, multiplier, Performance Mode, GPU, and duration.

## In-game considerations

> [!TIP]
> Try the game's V-Sync setting both on and off. It can make frame delivery feel steadier, but may also add input lag or clash with the game's FPS cap, VRR, or compositor. Keep whichever setting feels smoother and more responsive for that game.

Every game, renderer, and display setup behaves differently. Compare Fixed and Adaptive Frame Generation one setting at a time. Fullscreen is usually the best starting point for performance and frame pacing. Restart the game after major display, DLL, GPU, Flow Scale, Performance Mode, or model changes.

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
