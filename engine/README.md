# MAKO Renderer

<p align="center">
  <img src="assets/mako-render-logo.webp" width="256" alt="MAKO Renderer logo" />
</p>

<!-- prettier-ignore -->
> [!NOTE]
> **[LSFG-VK Experimental](https://github.com/eugeniosegala/lsfg-vk-experimental) is now MAKO Renderer.** The [MAKO repository](https://github.com/eugeniosegala/MAKO) is its new home and continuation, including future development, releases, documentation, and issue tracking.

MAKO Renderer is the Vulkan renderer and layer component of MAKO. It brings Lossless Scaling frame generation to Steam Deck, Steam Machine, SteamOS, and Linux more broadly, with scaling coming soon.

The layer is derived from [lsfg-vk](https://github.com/PancakeTAS/lsfg-vk) and retains its open-source attribution and license obligations. It requires a user-supplied `Lossless.dll` from a licensed [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) installation. MAKO Renderer does not bundle or replace that proprietary library.

## Downloads

Standalone Linux and Flatpak archives are published on the [latest MAKO Renderer release](https://github.com/eugeniosegala/MAKO/releases/tag/render-v2.2.0). Download the required archive under **Assets**. Steam Deck users who want the managed workflow should install the [latest MAKO Decky release](https://github.com/eugeniosegala/MAKO/releases/latest).

Published archives currently target x86_64 Linux hosts and contain Vulkan layers for both 64-bit and 32-bit x86 game processes. Native AArch64/Armada packages require a separately built and validated Renderer and are not part of this release.

## Installation

### Steam Deck or Steam Machine: use MAKO Decky

For SteamOS, **MAKO Decky** is the recommended installation path. It installs MAKO Renderer in a private location, creates the `mako-run` launcher, and prepares supported Flatpak applications. Install the Decky ZIP, open **MAKO Decky**, and select **Install MAKO Renderer**. For a native Steam or Proton game, set its Steam launch option to:

```text
/home/deck/.local/bin/mako-run %command%
```

See the [main MAKO installation guide](../README.md#install-and-use) for the complete Decky, Heroic, and EmuDeck setup.

### Direct Linux installation

1. Purchase and install [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) through Steam. MAKO requires its licensed `Lossless.dll` but does not bundle, copy, or modify it.
2. Open the [latest MAKO Renderer release](https://github.com/eugeniosegala/MAKO/releases/tag/render-v2.2.0) and download `MAKO-Renderer-v<version>-linux.tar.xz` under **Assets**.
3. Optionally save the archive's file list so you know exactly what was installed, then extract it into your user-local prefix:

```bash
mkdir -p ~/.local
tar -tJf MAKO-Renderer-v<version>-linux.tar.xz > MAKO-Renderer-v<version>-files.txt
tar -xJf MAKO-Renderer-v<version>-linux.tar.xz -C ~/.local
```

The archive installs `mako-ui`, `mako-cli`, `mako-launch`, `mako-diagnostics`, XDG desktop files, Vulkan manifests, and matching 64-bit and 32-bit layers. The Vulkan loader selects the correct layer for each game; the UI, CLI, launcher, and diagnostics helper remain 64-bit applications or scripts. If `~/.local/bin` is not on your `PATH`, run tools with their full paths, such as `~/.local/bin/mako-ui`.

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

The Vulkan layer and `mako-cli` do not use Qt. Only the optional `mako-ui` graphical interface needs Qt, Qt Quick, and Qt Quick Controls. If those runtime components are not already installed, run only the command for your distribution:

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
2. Under **Profile Matching**, add the game's Linux binary, Windows executable, or process name to **Matched Processes**. The selected profile's current matches remain visible in the main window. Start with fixed **2x** frame generation and adjust one setting at a time.
3. Launch only the selected game through MAKO. For a Steam game, use this launch option:

    ```text
    ~/.local/bin/mako-launch %command%
    ```

    For a direct desktop command, pass the executable and its arguments to the same launcher:

    ```bash
    ~/.local/bin/mako-launch your-game-command
    ```

4. Start the game normally. `mako-launch` enables MAKO only for that process and selects its installed private manifest directory and supported SDR presentation boundary. Steam's Vulkan Fossilize/overlay hooks, system-wide implicit presentation layers, and installed LSFG-VK frame-generation layers cannot bypass MAKO's swapchain interception. Gamescope and the Steam/Game Mode interface remain active outside the application layer chain. The launcher keeps the unfinished HDR path disabled because the isolated Gamescope WSI layer cannot be reintroduced after Vulkan starts. Game-local integrations such as OptiScaler are unchanged; use only one frame-generation implementation per game. See [WSI isolation](docs/WSI-ISOLATION.md) and the [HDR pipeline architecture](docs/HDR-PIPELINE.md) for the complete contract.

Set an advanced launch variable before the helper and it is passed to the game unchanged. For example, this selects a named profile without changing the saved default:

```text
MAKO_PROFILE="My game" ~/.local/bin/mako-launch %command%
```

The configuration UI writes MAKO's normal configuration and does not launch games, so open `mako-ui` directly. Likewise, run `mako-cli` directly for validation, benchmarks, and quality tests; use `mako-launch` only for a Vulkan application that should load the frame-generation layer.

The UI also exposes global standalone launch compatibility for Zink and ALSA. These off-by-default switches are stored separately in `~/.config/mako-render/launcher.conf`, apply only to the next game process started through `mako-launch`, and require a game restart. They do not alter the selected Renderer profile. Steam Deck mode and the guarded Gamescope WSI, MangoHud, and vkBasalt layer-chain controls remain MAKO Decky-only because they need per-game identity or validated manifest staging and ordering.

The desktop UI supports English, Brazilian Portuguese, European Portuguese, Spanish, Korean, Japanese, Ukrainian, and Simplified Chinese, matching MAKO Decky's supported language inventory. It automatically selects a matching system language on first run and remembers later choices. CLI output remains available in English, Brazilian Portuguese, European Portuguese, and Spanish with a global option before the command, such as `mako-cli --lang pt-BR validate`; the language choice does not alter Renderer profiles.

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
~/.local/bin/mako-launch vulkaninfo | grep -i VK_LAYER_MAKO_render
```

### Benchmarking

Run the built-in frame-generation benchmark with:

```bash
~/.local/bin/mako-cli benchmark
```

The default duration is 10 seconds. Run `~/.local/bin/mako-cli` without a subcommand to see benchmark options for the DLL path, resolution, Flow Scale, multiplier, Performance Mode, GPU, and duration.

## In-game considerations

<!-- prettier-ignore -->
> [!TIP]
> Try the game's V-Sync setting both on and off. It can make frame delivery feel steadier, but may also add input lag or clash with the game's FPS cap, VRR, or compositor. Keep whichever setting feels smoother and more responsive for that game.

Every game, renderer, and display setup behaves differently. Compare Fixed and Adaptive Frame Generation one setting at a time. Fullscreen is usually the best starting point for performance and frame pacing. Restart the game after major display, DLL, GPU, Flow Scale, Performance Mode, or model changes.

## Identity

- Product: **MAKO Renderer**
- Vulkan layer: `VK_LAYER_MAKO_render`
- Library: `libmako-render.so`
- Configuration UI: `mako-ui`
- CLI: `mako-cli`
- Standalone game launcher: `mako-launch`
- Diagnostics helper: `mako-diagnostics`
- Configuration: `~/.config/mako-render/conf.toml`
- Activation: `~/.local/bin/mako-launch <command>`
- Deactivation: `DISABLE_MAKO=1`

## Build packages

MAKO Renderer uses CMake and requires Vulkan development headers. The optional desktop interface also requires Qt 6.2 or newer.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

On a detected SteamOS or Steam Machine host, CTest makes the FP32 and FP16 image-quality regression mandatory by default; a missing AMD GPU or `Lossless.dll` fails the run. Macs and other unsupported hosts report it as skipped. Configure hardware CI with `-DMAKO_GPU_QUALITY_TEST=REQUIRED` to record the requirement explicitly.

Local Linux archives and Flatpak runtime extensions can be built with:

```bash
./scripts/package-local.sh
./scripts/package-flatpaks.sh
```

Artifacts are written under `engine/out/`. The Decky package in the sibling `plugin/` directory builds and bundles this engine automatically through `pnpm run package:local-engine`.

## More documentation

- [Configuration](docs/CONFIGURATION.md): profiles, frame-generation controls, Adaptive mode, and environment variables.
- [Adaptive validation](docs/ADAPTIVE-VALIDATION.md): deterministic scheduler stages, generated-frame-plan ownership, characterization tests, benchmarking, and the real-game matrix.
- [WSI isolation](docs/WSI-ISOLATION.md): private Vulkan discovery, Gamescope presentation ownership, tradeoffs, diagnostics, and future HDR constraints.
- [Optional graphics integrations](docs/LAYER-CHAINING.md): MAKO Decky's per-profile MangoHud and experimental vkBasalt controls, manual layer chaining, capture and DLL-injector test lanes, verification, evidence, and limits.
- [HDR pipeline architecture](docs/HDR-PIPELINE.md): evidence, colour classification, split transport/model formats, fallbacks, live transitions, and validation requirements.
- [Flatpak guide](docs/FLATPAK-GUIDE.md): runtime extensions and direct Flatpak application overrides.
- [Building from Source](docs/BUILDING-FROM-SOURCE.md): prerequisites, SteamOS builds, and packaging.
- [Troubleshooting](docs/TROUBLESHOOTING.md): activation, configuration, and presentation diagnostics.
- [AMD image-quality regression](docs/IMAGE-QUALITY-REGRESSION.md): deterministic Flow Scale 1.0 GPU validation and comparison artifacts.
- [Collect standalone MAKO Renderer diagnostics](docs/COLLECT_DIAGNOSTICS.md): create a focused Desktop report and submit it through the shared form.
