# MAKO Renderer

<p align="center">
  <img src="assets/mako-render-logo.webp" width="256" alt="MAKO Renderer logo" />
</p>

<!-- prettier-ignore -->
> [!NOTE]
> **<a href="https://github.com/eugeniosegala/lsfg-vk-experimental" target="_blank" rel="noopener noreferrer">LSFG-VK Experimental</a> is now MAKO Renderer.** The <a href="https://github.com/eugeniosegala/MAKO" target="_blank" rel="noopener noreferrer">MAKO repository</a> is its new home and continuation, including future development, releases, documentation, and issue tracking.

MAKO Renderer is MAKO's Vulkan layer and standalone component for Steam Deck, Steam Machine, SteamOS, and Linux. It provides LSFG Fixed or Adaptive Frame Generation, LS1 spatial scaling, and the open MAKO Scaler. Scaling can run alone or combine with Frame Generation before or after interpolation according to the presentation extent.

The layer descends directly from the GPL-3.0-or-later version 2 tree of <a href="https://github.com/PancakeTAS/lsfg-vk" target="_blank" rel="noopener noreferrer">lsfg-vk</a> at upstream commit <a href="https://github.com/PancakeTAS/lsfg-vk/commit/8b0da2661c6f3473a7fccc8ba643880050e71642" target="_blank" rel="noopener noreferrer"><code>8b0da266</code></a> and retains its open-source attribution and license obligations. MAKO Renderer does not contain or distribute Lossless Scaling, `Lossless.dll`, or extracted proprietary model payloads. LSFG frame generation and LS1 scaling read selected resources at runtime from a lawful, user-supplied <a href="https://store.steampowered.com/app/993090/Lossless_Scaling/" target="_blank" rel="noopener noreferrer">Lossless Scaling</a> installation; the open MAKO Scaler does not require it. MAKO Renderer does not alter the user's DLL file, and translated resources remain process-local. Users are responsible for complying with the terms applicable to their copy. See <a href="../THIRD_PARTY_NOTICES.md" target="_blank" rel="noopener noreferrer">Third-party notices</a> and the exact <a href="../LICENSE.md#lsfg-vk-renderer-lineage" target="_blank" rel="noopener noreferrer">Renderer lineage</a>.

Scaling must be enabled before the game starts. In a provisioned process, Frame Generation, scaler method, and sharpness can change through live or private-resource transitions; source/presentation geometry changes may wait for a game-owned swapchain recreation. See <a href="docs/RUNTIME-TRANSITIONS.md" target="_blank" rel="noopener noreferrer">runtime transitions</a> for the exact live, deferred, and restart boundaries.

## Downloads

Standalone Linux and Flatpak archives are published on the <a href="https://github.com/eugeniosegala/MAKO/releases/tag/render-v3.1.0" target="_blank" rel="noopener noreferrer">latest MAKO Renderer release</a>. Steam Deck and Steam Machine users who prefer a managed workflow should install the <a href="https://github.com/eugeniosegala/MAKO/releases/latest" target="_blank" rel="noopener noreferrer">latest MAKO Decky release</a>.

Published archives target x86_64 Linux hosts and include Vulkan layers for both 64-bit and 32-bit x86 game processes. Native AArch64/Armada packages require a separately built and validated Renderer and are not part of this release.

## Installation

### Steam Deck or Steam Machine

**MAKO Decky** is the recommended SteamOS installation path. It manages the shared native MAKO Renderer installation, creates the `mako-run` launcher, and prepares supported Flatpak applications. Install the Decky ZIP, open **MAKO Decky**, select **Install MAKO Renderer**, then add this Steam launch option to a native Steam or Proton game:

```text
/home/deck/.local/bin/mako-run %command%
```

See the <a href="../README.md#install-and-use" target="_blank" rel="noopener noreferrer">main installation guide</a> for Decky, Heroic, and EmuDeck setup.

### Direct Linux installation

For frame generation or LS1 scaling, first install <a href="https://store.steampowered.com/app/993090/Lossless_Scaling/" target="_blank" rel="noopener noreferrer">Lossless Scaling</a> through Steam. The open MAKO Scaler works without `Lossless.dll`.

Download and extract `MAKO-Renderer-v<version>-linux.tar.xz` from the <a href="https://github.com/eugeniosegala/MAKO/releases/tag/render-v3.1.0" target="_blank" rel="noopener noreferrer">latest MAKO Renderer release</a>, then run **Install MAKO Renderer**. It verifies the archive, preserves profiles, opens **MAKO Renderer Configuration**, and shows the Steam/Proton launch option. Run the installer again to update; use **Uninstall MAKO Renderer** to remove the shared native installation. The included `README.txt` contains offline instructions.

For a manual installation, extract the archive into your user-local prefix:

```bash
mkdir -p ~/.local
tar -xJf MAKO-Renderer-v<version>-linux.tar.xz -C ~/.local
```

The archive provides `mako-ui`, `mako-cli`, `mako-launch`, `mako-diagnostics`, desktop files, Vulkan manifests, and matching 64-bit and 32-bit layers. If `~/.local/bin` is not on `PATH`, run them by full path. Only the optional graphical interface needs Qt; see <a href="docs/BUILDING-FROM-SOURCE.md" target="_blank" rel="noopener noreferrer">building from source</a> for distribution prerequisites and manual install variants.

<!-- prettier-ignore -->
> [!IMPORTANT]
> MAKO Decky and the standalone archive share one native Renderer. Installing either selects its version: the standalone installer warns before replacing Decky's version, while Decky adopts a valid standalone installation or offers its bundled update when versions differ. **Uninstall MAKO Renderer** removes the shared native files but keeps MAKO Decky and profiles; uninstalling MAKO Decky removes both the plugin and managed Renderer. Shared Flatpak extensions remain installed.

For Flatpak games or emulators, install the matching MAKO Vulkan runtime extension and grant the application access to MAKO configuration and the Steam library. MAKO Decky performs this through **Flatpak Setup**; direct installs can follow the <a href="docs/FLATPAK-GUIDE.md" target="_blank" rel="noopener noreferrer">Flatpak guide</a>.

## Usage

Open **MAKO Renderer Configuration** from the application launcher (the MAKO-logo icon), or run:

```bash
~/.local/bin/mako-ui
```

Create a profile, match it to the game's executable or process name, and select its settings. For spatial scaling, select **Enable Scaling (Restart)** before launch, choose a lower game resolution, and select Native Resolution, MAKO Scaler, LS1 Quality, or LS1 Performance. Fixed and Adaptive Frame Generation remain independent. See <a href="docs/CONFIGURATION.md" target="_blank" rel="noopener noreferrer">Configuration</a> for settings, profile matching, and environment overrides.

Launch only the selected game through MAKO:

```text
~/.local/bin/mako-launch %command%
```

For a direct desktop command, pass the executable and arguments to the same launcher:

```bash
~/.local/bin/mako-launch your-game-command
```

`mako-launch` enables MAKO only for that process and establishes the supported standalone Vulkan-layer boundary. Use one Frame Generation implementation per game. Gamescope WSI, MangoHud, and vkBasalt profile controls remain MAKO Decky features because they require managed manifest staging.

MAKO operates on Vulkan: native Vulkan and Proton games through DXVK or VKD3D-Proton are supported, while OpenGL requires the optional Zink launcher setting. If no profile matches the game process, MAKO remains dormant and presentation stays native.

Direct desktop scaling is supported, but it has no authoritative Gamescope output target. MAKO applies the configured factor within the surface limits; the desktop compositor may add another scaling step, and an unsupported recreation falls back to native presentation.

Want to use Frame Generation or scaling with videos? See <a href="docs/VIDEOS_WITH_MAKO.md" target="_blank" rel="noopener noreferrer">Videos with MAKO</a>.

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
~/.local/bin/mako-cli inspect-dll --dll "/path/to/Lossless.dll"
~/.local/bin/mako-cli benchmark
```

`inspect-dll` validates LSFG and LS1 resources independently without changing the user-owned file. Run `mako-cli` without a subcommand to see all commands and options. Use `mako-diagnostics` for a focused standalone report.

## In-game considerations

<!-- prettier-ignore -->
> [!TIP]
> Try the game's V-Sync setting both on and off. It can make frame delivery steadier, but may also add input lag or clash with the game's FPS cap, VRR, or compositor. Keep the setting that feels best for that game.

Compare Fixed Frame Generation, Adaptive Frame Generation, and scaling one setting at a time; fullscreen is a useful starting point. Enable or disable Scaling between game sessions. See <a href="docs/SCALING.md" target="_blank" rel="noopener noreferrer">spatial scaling</a>, <a href="docs/WSI-ISOLATION.md" target="_blank" rel="noopener noreferrer">WSI isolation</a>, and <a href="docs/TROUBLESHOOTING.md" target="_blank" rel="noopener noreferrer">troubleshooting</a> for compatibility limits and diagnostics.

## Build from source

Build a standard development configuration with CMake and Vulkan development headers:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The optional desktop interface also requires Qt 6.2 or newer. The <a href="docs/BUILDING-FROM-SOURCE.md" target="_blank" rel="noopener noreferrer">building-from-source guide</a> covers prerequisites, SteamOS builds, 32-bit layers, installation prefixes, and package creation. Portable CTest verifies renderer contracts and synthetic image-quality coverage; MAKO Gym owns real-hardware Vulkan, quality, performance, synchronization, and recovery evidence for release validation.

Build local host archives and Flatpak extensions with:

```bash
./scripts/package-local.sh
./scripts/package-flatpaks.sh
```

Artifacts are written under `engine/out/`. MAKO Decky packages this engine automatically through `pnpm run package:local-engine` in the sibling `plugin/` directory.

## More documentation

- <a href="docs/LIFECYCLE.md" target="_blank" rel="noopener noreferrer">Lifecycle</a>: probes, timers, setting lifetimes, swapchain ownership, pacing policies, recovery, retirement, and heuristic risk.
- <a href="docs/CONFIGURATION.md" target="_blank" rel="noopener noreferrer">Configuration</a>: profiles, frame-generation and scaling controls, Adaptive mode, and environment variables.
- <a href="docs/SCALING.md" target="_blank" rel="noopener noreferrer">Spatial scaling architecture</a>: pipeline order, surface support, formats, resources, private transitions, and validation.
- <a href="docs/RUNTIME-TRANSITIONS.md" target="_blank" rel="noopener noreferrer">Runtime configuration transitions</a>: live-safe updates, recreation, and restart boundaries.
- <a href="docs/ADAPTIVE-VALIDATION.md" target="_blank" rel="noopener noreferrer">Adaptive validation</a>: scheduler behavior, frame plans, benchmarking, and game validation.
- <a href="docs/WSI-ISOLATION.md" target="_blank" rel="noopener noreferrer">WSI isolation</a>: Vulkan discovery, Gamescope presentation ownership, and diagnostics.
- <a href="docs/LAYER-CHAINING.md" target="_blank" rel="noopener noreferrer">Optional graphics integrations</a>: MAKO Decky external tools, manual chaining, and limits.
- <a href="docs/FLATPAK-GUIDE.md" target="_blank" rel="noopener noreferrer">Flatpak guide</a>: runtime extensions and direct application overrides.
- <a href="docs/VIDEOS_WITH_MAKO.md" target="_blank" rel="noopener noreferrer">Videos with MAKO</a>: use Frame Generation and spatial scaling with mpv.
- <a href="docs/TROUBLESHOOTING.md" target="_blank" rel="noopener noreferrer">Troubleshooting</a>: activation, configuration, and presentation diagnostics.
- <a href="docs/COLLECT_DIAGNOSTICS.md" target="_blank" rel="noopener noreferrer">Collect standalone diagnostics</a>: create a focused Desktop report.
