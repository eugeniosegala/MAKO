# MAKO Renderer

<p align="center">
  <img src="assets/mako-render-logo.webp" width="256" alt="MAKO Renderer logo" />
</p>

<!-- prettier-ignore -->
> [!NOTE]
> **[LSFG-VK Experimental](https://github.com/eugeniosegala/lsfg-vk-experimental) is now MAKO Renderer.** The [MAKO repository](https://github.com/eugeniosegala/MAKO) is its new home and continuation, including future development, releases, documentation, and issue tracking.

MAKO Renderer is MAKO's Vulkan layer and standalone renderer component. It brings LS1 spatial scaling, MAKO's built-in open spatial scaler, and LSFG frame generation to Steam Deck, Steam Machine, SteamOS, and Linux. Scaling can run on its own or reconstruct real frames before Fixed or Adaptive Frame Generation.

The layer is derived from [lsfg-vk](https://github.com/PancakeTAS/lsfg-vk) and retains its open-source attribution and license obligations. LS1 scaling and LSFG frame generation require a user-supplied `Lossless.dll` from a licensed [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) installation. MAKO Renderer never bundles, copies, persists, replaces, or modifies that proprietary library; the open MAKO scaler does not require it.

Scaling Engine is a game-start setting. Once it is enabled, compatible scaler and tuning changes use a game-owned swapchain recreation; a game started for scaling only must restart before it can enable Frame Generation. See [runtime transitions](docs/RUNTIME-TRANSITIONS.md) for the exact live and restart boundaries.

## Downloads

Standalone Linux and Flatpak archives are published on the [latest MAKO Renderer release](https://github.com/eugeniosegala/MAKO/releases/tag/render-v2.2.0). Steam Deck and Steam Machine users who prefer a managed workflow should install the [latest MAKO Decky release](https://github.com/eugeniosegala/MAKO/releases/latest).

Published archives target x86_64 Linux hosts and include Vulkan layers for both 64-bit and 32-bit x86 game processes. Native AArch64/Armada packages require a separately built and validated Renderer and are not part of this release.

## Installation

### Steam Deck or Steam Machine

**MAKO Decky** is the recommended SteamOS installation path. It installs MAKO Renderer privately, creates the `mako-run` launcher, and prepares supported Flatpak applications. Install the Decky ZIP, open **MAKO Decky**, select **Install MAKO Renderer**, then add this Steam launch option to a native Steam or Proton game:

```text
/home/deck/.local/bin/mako-run %command%
```

See the [main installation guide](../README.md#install-and-use) for Decky, Heroic, and EmuDeck setup.

### Direct Linux installation

For LS1 scaling or frame generation, first install [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) through Steam. The open MAKO scaler works without `Lossless.dll`.

For the normal standalone workflow, download and extract `MAKO-Renderer-v<version>-linux.tar.xz` from the [latest Renderer release](https://github.com/eugeniosegala/MAKO/releases/tag/render-v2.2.0), then double-click **Install MAKO Renderer** and choose **Execute** if prompted. It verifies and installs the archive below `~/.local`, preserves profiles, and opens **MAKO Renderer Configuration**. Repeat it after extracting a newer archive; use **Uninstall MAKO Renderer** from the application menu to remove the standalone installation.

For a manual installation, extract the archive into your user-local prefix:

```bash
mkdir -p ~/.local
tar -xJf MAKO-Renderer-v<version>-linux.tar.xz -C ~/.local
```

The archive provides `mako-ui`, `mako-cli`, `mako-launch`, `mako-diagnostics`, desktop files, Vulkan manifests, and matching 64-bit and 32-bit layers. If `~/.local/bin` is not on `PATH`, run them by full path. Only the optional graphical interface needs Qt; see [building from source](docs/BUILDING-FROM-SOURCE.md) for distribution prerequisites and manual install variants.

<!-- prettier-ignore -->
> [!IMPORTANT]
> Direct installation is separate from MAKO Decky. The installer warns before replacing paths shared with a Decky installation; if you continue, reinstall Decky's matching Renderer afterwards.

For Flatpak games or emulators, install the matching MAKO Vulkan runtime extension and grant the application access to MAKO configuration and the Steam library. MAKO Decky performs this through **Flatpak Setup**; direct installs can follow the [Flatpak guide](docs/FLATPAK-GUIDE.md).

## Usage

Open **MAKO Renderer Configuration** from the application menu, or run:

```bash
~/.local/bin/mako-ui
```

Create a profile, match it to the game's executable or process name, and select the options you want. To use spatial scaling, enable Scaling Engine before launching, choose a lower game rendering resolution, and select Native, MAKO (Open), LS1 Quality, or LS1 Performance. Fixed and Adaptive Frame Generation remain separate options. The configuration guide covers every setting, profile rule, and environment override.

Launch only the selected game through MAKO:

```text
~/.local/bin/mako-launch %command%
```

For a direct desktop command, pass the executable and arguments to the same launcher:

```bash
~/.local/bin/mako-launch your-game-command
```

`mako-launch` enables MAKO only for that process. It maintains the supported standalone Vulkan-layer boundary; use one frame-generation implementation per game. MAKO Decky's optional Gamescope WSI, MangoHud, and vkBasalt controls remain Decky features because they require per-game manifest staging.

### Manual configuration and validation

You can configure MAKO without the UI by editing `~/.config/mako-render/conf.toml`. This minimal profile matches a Windows executable and enables 2x Frame Generation:

```toml
[[profile]]
name = "My game"
active_in = ["Game.exe"]
multiplier = 2
frame_generation_enabled = true
```

Validate the configuration or run the built-in benchmark with:

```bash
~/.local/bin/mako-cli validate
~/.local/bin/mako-cli benchmark
```

Run `mako-cli` without a subcommand to see its configuration, benchmark, and language options. Use `mako-diagnostics` to collect a focused standalone report when troubleshooting.

## In-game considerations

<!-- prettier-ignore -->
> [!TIP]
> Try the game's V-Sync setting both on and off. It can make frame delivery steadier, but may also add input lag or clash with the game's FPS cap, VRR, or compositor. Keep the setting that feels best for that game.

Every game, renderer, and display setup behaves differently. Compare scaling, Fixed Frame Generation, and Adaptive Frame Generation one setting at a time. Fullscreen is usually the best starting point for performance and pacing. Change Scaling Engine between game sessions; compatible changes within an enabled engine may briefly flicker while the game recreates its swapchain. See [spatial scaling](docs/SCALING.md), [WSI isolation](docs/WSI-ISOLATION.md), and [troubleshooting](docs/TROUBLESHOOTING.md) for compatibility limits and diagnostics.

## Build from source

Build a standard development configuration with CMake and Vulkan development headers:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The optional desktop interface also requires Qt 6.2 or newer. The [building-from-source guide](docs/BUILDING-FROM-SOURCE.md) covers prerequisites, SteamOS builds, 32-bit layers, installation prefixes, and package creation. Portable CTest verifies renderer contracts and synthetic image-quality coverage; MAKO Gym owns real-hardware Vulkan, quality, performance, synchronization, and recovery evidence for release validation.

Build local host archives and Flatpak extensions with:

```bash
./scripts/package-local.sh
./scripts/package-flatpaks.sh
```

Artifacts are written under `engine/out/`. MAKO Decky packages this engine automatically through `pnpm run package:local-engine` in the sibling `plugin/` directory.

## More documentation

- [Configuration](docs/CONFIGURATION.md): profiles, scaling and frame-generation controls, Adaptive mode, and environment variables.
- [Spatial scaling architecture](docs/SCALING.md): pipeline order, surface support, formats, resource cost, live recreation, and validation.
- [Runtime configuration transitions](docs/RUNTIME-TRANSITIONS.md): live-safe updates, recreation, and restart boundaries.
- [Adaptive validation](docs/ADAPTIVE-VALIDATION.md): scheduler behavior, frame plans, benchmarking, and game validation.
- [WSI isolation](docs/WSI-ISOLATION.md): Vulkan discovery, Gamescope presentation ownership, and diagnostics.
- [Optional graphics integrations](docs/LAYER-CHAINING.md): MAKO Decky external tools, manual chaining, and limits.
- [Flatpak guide](docs/FLATPAK-GUIDE.md): runtime extensions and direct application overrides.
- [Troubleshooting](docs/TROUBLESHOOTING.md): activation, configuration, and presentation diagnostics.
- [Collect standalone diagnostics](docs/COLLECT_DIAGNOSTICS.md): create a focused Desktop report.
