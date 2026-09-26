# MAKO Renderer

<p align="center">
  <img src="assets/mako-render-logo.webp" width="256" alt="MAKO Renderer logo" />
</p>

<!-- prettier-ignore -->
> [!NOTE]
> **<a href="https://github.com/eugeniosegala/lsfg-vk-experimental" target="_blank" rel="noopener noreferrer">LSFG-VK Experimental</a> is now MAKO Renderer.** Development, releases, documentation, and issue tracking continue in the <a href="https://github.com/eugeniosegala/MAKO" target="_blank" rel="noopener noreferrer">MAKO repository</a>.

MAKO Renderer is a Vulkan layer for Frame Generation, spatial scaling, and optional shader effects on SteamOS and Linux. It supports LSFG Fixed and Adaptive Frame Generation, LS1 scaling, and the open MAKO Scaler.

LSFG and LS1 require a lawful, user-supplied <a href="https://store.steampowered.com/app/993090/Lossless_Scaling/" target="_blank" rel="noopener noreferrer">Lossless Scaling</a> installation. MAKO does not distribute or modify `Lossless.dll`; the open MAKO Scaler and bundled shaders do not require it. MAKO Renderer descends from the GPL-3.0-or-later lsfg-vk version 2 tree. See the <a href="../THIRD_PARTY_NOTICES.md" target="_blank" rel="noopener noreferrer">third-party notices</a> and <a href="../LICENSE.md#lsfg-vk-renderer-lineage" target="_blank" rel="noopener noreferrer">Renderer lineage</a>.

## Downloads

Download packages from the <a href="https://github.com/eugeniosegala/MAKO/releases/tag/render-v3.3.0" target="_blank" rel="noopener noreferrer">latest MAKO Renderer release</a>:

| Release file | Use it for |
| --- | --- |
| `MAKO-Renderer-v<version>-linux.tar.xz` | Direct Linux or SteamOS installation, with the Qt configuration UI and 64-bit/32-bit layers |
| `mako-renderer-bin-<version>-<pkgrel>-x86_64.pkg.tar.zst` | A system-wide installation on writable Arch Linux systems; see the [Arch guide](dist/arch/README.md#install-the-release-package) |
| `MAKO-Renderer-v<version>-flatpaks.tar.xz` | Vulkan runtime extensions for Flatpak games, launchers, and emulators |

GitHub's automatic **Source code** archives are not installable Renderer packages. Steam Deck and Steam Machine users who want a managed setup should use <a href="https://github.com/eugeniosegala/MAKO/releases/latest" target="_blank" rel="noopener noreferrer">MAKO Decky</a>.

## Installation

For Frame Generation or LS1, install the default public version of Lossless Scaling through Steam first. Skip this when using only MAKO Scaler or shader effects.

### Steam Deck or Steam Machine

MAKO Decky is the recommended SteamOS path. Install its ZIP, open **MAKO Decky**, and select **Install MAKO Renderer**. Follow the <a href="../README.md#install-and-use" target="_blank" rel="noopener noreferrer">main installation guide</a> for game and launcher setup.

### Direct Linux installation

Extract `MAKO-Renderer-v<version>-linux.tar.xz` and run **Install MAKO Renderer**. The installer places MAKO under `~/.local`, preserves existing profiles, opens **MAKO Renderer Configuration**, and shows the required launch option. Run it again to update, or use **Uninstall MAKO Renderer** to remove the native installation.

For a manual user-local installation:

```bash
mkdir -p ~/.local
tar -xJf MAKO-Renderer-v<version>-linux.tar.xz -C ~/.local
```

Close games using MAKO before installing or updating. MAKO Decky and the standalone archive share the same native Renderer installation; installing either one selects the active version without deleting profiles.

Flatpak applications require a matching runtime extension and per-application sandbox preparation. Follow the [Flatpak guide](docs/FLATPAK-GUIDE.md); a host `mako-launch` command cannot replace that setup.

## Usage

**A profile does not activate MAKO. Every standalone native Steam or Proton game must start through `mako-launch`.** Set the launch option once for each game, then use Steam normally.

### Native Steam or Proton game

1. Open **MAKO Renderer Configuration** from the application launcher, or run `~/.local/bin/mako-ui`.
2. Create or select a profile for the game. Add its executable under **Profile Matching > Matched Processes**, or start the game normally, select **Detect Running Game…**, save the match, and close the game. This first run is only for detection; MAKO cannot attach to a game that is already running.
3. Enable Frame Generation, scaling, and/or shader effects. Changes save automatically. Options labelled **Restart** take effect the next time the game starts.
4. Copy the launch option shown by the configuration window into **Steam Properties > General > Launch Options**. Without shaders or an explicit profile override, it is:

    ```text
    ~/.local/bin/mako-launch %command%
    ```

5. Start the game with Steam's **Play** button. The configuration window can be closed during play.

When shaders are enabled, use the complete launch option shown by the UI; it includes the selected profile and its isolated shader configuration. Selecting a profile in the UI chooses what you edit, while **Matched Processes** determines which program receives those settings. If no profile matches, MAKO stays inactive.

### Other launch types

| Game or application | How to start it |
| --- | --- |
| Direct desktop command | `~/.local/bin/mako-launch "/path/to/program"` |
| Native Heroic or Lutris game | Use the absolute `mako-launch` path as its Wrapper or Command prefix; see [launcher setup](../plugin/docs/LAUNCHERS.md) |
| Flatpak game, launcher, or emulator | Complete the [Flatpak setup](docs/FLATPAK-GUIDE.md), then launch it normally |
| Game managed by MAKO Decky | Use Decky's generated `mako-run` integration |

`%command%` is a Steam placeholder and must not be typed into a terminal. MAKO operates on Vulkan; Proton games work through DXVK or VKD3D-Proton, while OpenGL applications require the optional Zink setting.

For manual profile editing and the full setting reference, see [Configuration](docs/CONFIGURATION.md). To validate a configuration or collect a focused report, run:

```bash
~/.local/bin/mako-cli validate
~/.local/bin/mako-diagnostics
```

## In-game considerations

- Try the game's V-Sync setting both on and off and keep the smoother option for that game.
- For scaling, lower the game's input resolution before increasing MAKO's scale factor. Windowed mode may be required when fullscreen or borderless keeps a display-sized input.
- Change Frame Generation, scaling, and shader effects one at a time when checking image quality or performance.

See [Scaling](docs/SCALING.md) for resolution and presentation details, or [Troubleshooting](docs/TROUBLESHOOTING.md) when MAKO does not activate or present correctly.

## Build from source

Build a standard development configuration with CMake and Vulkan development headers:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The optional Qt interface requires Qt 6.2 or newer. See [Building from source](docs/BUILDING-FROM-SOURCE.md) for prerequisites, SteamOS builds, 32-bit layers, and installation prefixes.

Build individual local packages with:

```bash
./scripts/package-local.sh
./scripts/package-flatpaks.sh
```

To create the complete release-shaped local artifact set, including the Arch package and checksums, use:

```bash
./scripts/package-local-release.sh
```

Artifacts are written under `engine/out/`. These commands build locally; they do not tag or publish a release.

## More documentation

- [Configuration](docs/CONFIGURATION.md): profiles, settings, environment variables, and manual configuration.
- [Runtime transitions](docs/RUNTIME-TRANSITIONS.md): live, deferred, and restart-required changes.
- [Scaling](docs/SCALING.md): spatial scaling setup, behavior, and limitations.
- [Adaptive validation](docs/ADAPTIVE-VALIDATION.md): Adaptive scheduling and validation.
- [Optional graphics integrations](docs/LAYER-CHAINING.md): bundled vkBasalt chaining and isolation.
- [Flatpak guide](docs/FLATPAK-GUIDE.md): runtime extensions and application preparation.
- [Videos with MAKO](docs/VIDEOS_WITH_MAKO.md): Frame Generation and scaling with mpv.
- [Troubleshooting](docs/TROUBLESHOOTING.md): activation, presentation, and diagnostics.
- [Collect diagnostics](docs/COLLECT_DIAGNOSTICS.md): create a standalone support report.
- [Building from source](docs/BUILDING-FROM-SOURCE.md): development and packaging requirements.
