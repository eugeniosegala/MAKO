# MAKO Renderer

<p align="center">
  <img src="assets/mako-render-logo.webp" width="256" alt="MAKO Renderer logo" />
</p>

<!-- prettier-ignore -->
> [!NOTE]
> **<a href="https://github.com/eugeniosegala/lsfg-vk-experimental" target="_blank" rel="noopener noreferrer">LSFG-VK Experimental</a> is now MAKO Renderer.** The <a href="https://github.com/eugeniosegala/MAKO" target="_blank" rel="noopener noreferrer">MAKO repository</a> is its new home and continuation, including future development, releases, documentation, and issue tracking.

MAKO Renderer is MAKO's Vulkan layer and standalone component for Steam Deck, Steam Machine, SteamOS, and Linux. It provides LSFG Fixed or Adaptive Frame Generation and LS1 or open MAKO spatial scaling. The standalone package also includes optional bundled shader effects. Scaling can run alone or combine with Frame Generation before or after interpolation according to the presentation extent.

The layer descends directly from the GPL-3.0-or-later version 2 tree of <a href="https://github.com/PancakeTAS/lsfg-vk" target="_blank" rel="noopener noreferrer">lsfg-vk</a> at upstream commit <a href="https://github.com/PancakeTAS/lsfg-vk/commit/8b0da2661c6f3473a7fccc8ba643880050e71642" target="_blank" rel="noopener noreferrer"><code>8b0da266</code></a> and retains its open-source attribution and license obligations. MAKO Renderer does not contain or distribute Lossless Scaling, `Lossless.dll`, or extracted proprietary model payloads. LSFG frame generation and LS1 scaling read selected resources at runtime from a lawful, user-supplied <a href="https://store.steampowered.com/app/993090/Lossless_Scaling/" target="_blank" rel="noopener noreferrer">Lossless Scaling</a> installation; the open MAKO Scaler and bundled shaders do not require it. MAKO Renderer does not alter the user's DLL file, and translated resources remain process-local. Users are responsible for complying with the terms applicable to their copy. See <a href="../THIRD_PARTY_NOTICES.md" target="_blank" rel="noopener noreferrer">Third-party notices</a> and the exact <a href="../LICENSE.md#lsfg-vk-renderer-lineage" target="_blank" rel="noopener noreferrer">Renderer lineage</a>.

Frame Generation provisioning and Scaling must be selected before the game starts. In a Frame Generation-provisioned process, `0x` and active generation factors can change live; scaler method, sharpness, and compatible FG resource controls use live or private-resource transitions. Source/presentation geometry changes may wait for a game-owned swapchain recreation. See <a href="docs/RUNTIME-TRANSITIONS.md" target="_blank" rel="noopener noreferrer">runtime transitions</a> for the exact live, deferred, and restart boundaries.

## Downloads

MAKO Renderer packages are published under **Assets** on the <a href="https://github.com/eugeniosegala/MAKO/releases/tag/render-v3.3.0" target="_blank" rel="noopener noreferrer">latest MAKO Renderer release</a>:

| Release file | Choose it for | What it provides |
| --- | --- | --- |
| `MAKO-Renderer-v<version>-linux.tar.xz` | General Linux, including a direct user-local installation on SteamOS | The portable host archive with graphical install/uninstall launchers, CLI, configuration UI, desktop integration, and matching 64-bit/32-bit layers. It installs under `~/.local` by default. |
| [`mako-renderer-bin-<version>-<pkgrel>-x86_64.pkg.tar.zst`](dist/arch/README.md#install-the-release-package) | Arch Linux or a traditional writable Arch-based distribution—not SteamOS—starting with MAKO Renderer 4.0 | The verified system-wide pacman package. Follow the linked Arch installation guide; pacman owns its files under `/usr`. Do not also run the archive's user-local installer for the same user. |
| `MAKO-Renderer-v<version>-flatpaks.tar.xz` | Flatpak games, launchers, or emulators | An archive containing one MAKO Vulkan runtime extension for each supported Freedesktop runtime. It does not install the host CLI or configuration UI. |

GitHub's automatically generated **Source code** ZIP and tarball contain the repository source, not ready-to-run Renderer packages. Steam Deck and Steam Machine users who prefer a managed workflow should instead install the <a href="https://github.com/eugeniosegala/MAKO/releases/latest" target="_blank" rel="noopener noreferrer">latest MAKO Decky release</a>.

Published packages target x86_64 Linux hosts and include Vulkan layers for both 64-bit and 32-bit x86 game processes. Native AArch64/Armada packages require a separately built and validated Renderer and are not part of this release.

## Installation

For frame generation or LS1 scaling, first install the **default public version** of <a href="https://store.steampowered.com/app/993090/Lossless_Scaling/" target="_blank" rel="noopener noreferrer">Lossless Scaling</a> through Steam. MAKO can use beta branches, but they are not validated; the default public branch is recommended. The open MAKO Scaler works without `Lossless.dll`.

### Steam Deck or Steam Machine

**MAKO Decky** is the recommended SteamOS installation path. It manages the shared native MAKO Renderer installation, creates the `mako-run` launcher, and prepares supported Flatpak applications. Install the Decky ZIP, open **MAKO Decky**, select **Install MAKO Renderer**, then add this Steam launch option to a native Steam or Proton game:

```text
/home/deck/.local/bin/mako-run %command%
```

See the <a href="../README.md#install-and-use" target="_blank" rel="noopener noreferrer">main installation guide</a> for Decky, Heroic, Lutris, and EmuDeck setup.

### Direct Linux installation

Download and extract `MAKO-Renderer-v<version>-linux.tar.xz` from the <a href="https://github.com/eugeniosegala/MAKO/releases/tag/render-v3.3.0" target="_blank" rel="noopener noreferrer">latest MAKO Renderer release</a>, then run **Install MAKO Renderer**. It verifies the archive, preserves profiles, opens **MAKO Renderer Configuration**, and shows the Steam/Proton launch option. Run the installer again to update; use **Uninstall MAKO Renderer** to remove the shared native installation. The included `README.txt` contains offline instructions.

Starting with MAKO Renderer 4.0, Arch Linux users can download the `mako-renderer-bin-X.Y.Z-1-x86_64.pkg.tar.zst` asset from the matching Renderer release and follow the [Arch installation guide](dist/arch/README.md#install-the-release-package). The tracked recipe repackages the same checksum-pinned host archive as that system-wide package. It excludes the user-local installer and uninstaller, preserves profiles, and documents coexistence with MAKO Decky.

Close games using MAKO before updating. The installer validates and stages the complete payload before replacing files and restores the previous installation if a later step fails. It reports permission or storage failures without requesting root access. If restoration also fails, it retains recovery backups beside the affected files and reports their locations. Profiles and diagnostics remain untouched unless you explicitly choose to remove configuration during uninstall.

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

**A standalone native or Proton game needs a MAKO launch command as well as a matching profile.** Installing MAKO Renderer or opening its configuration window does not enable it for every game. Complete both steps below, then restart the game. You can close the configuration window after editing; it does not need to stay open during play.

| How the game runs | How to activate MAKO |
| --- | --- |
| Native Steam or Proton, with standalone MAKO Renderer | Add `~/.local/bin/mako-launch %command%` to that game's Steam Launch Options. |
| Native desktop game, emulator, or launcher | Start the game through `mako-launch`, as shown below. |
| Flatpak game, launcher, or emulator | Install the matching extension and prepare that application using the [Flatpak guide](docs/FLATPAK-GUIDE.md), then launch it normally. |
| MAKO Decky manages the game | Use Decky's `mako-run` and its [launcher setup](../plugin/docs/LAUNCHERS.md). |

### 1. Prepare a game or program profile

Open **MAKO Renderer Configuration** from the application launcher (the MAKO-logo icon), or run:

```bash
~/.local/bin/mako-ui
```

1. Start your game and reach gameplay, then select **Detect Running Game…** in MAKO Renderer Configuration. Select its executable and choose **Use Game Profile** to create a profile or open its existing match. This works with native Linux and Wine/Proton games, including non-Steam games. Use **Refresh** or **Show all applications** if it is missing.
2. To keep a profile you already configured, select that profile first and choose **Add to Selected Profile** in the picker. An existing match opens its profile instead of creating a duplicate. You can also use **Create New Profile** and enter an executable manually under **Profile Matching > Matched Processes > Edit...**, for example `Game.exe` or `dolphin-emu`, then press **+**. Match the rendering executable, rather than a launcher, Steam display title, ROM filename, or Flatpak application ID.
3. For Frame Generation or LS1, set **Lossless.dll Path (Restart)** if automatic discovery does not find your Steam installation. For open MAKO Scaler use without the DLL, turn **Frame Generation** off and select **MAKO Scaler**.
4. Select **Enable Frame-gen (Restart)**, choose Fixed or Adaptive Frame Generation, and/or select **Enable Scaling (Restart)**. For scaling, choose the method and factor and lower the game's resolution as described in [desktop scaling and resolution](docs/CONFIGURATION.md#desktop-scaling-and-resolution).
5. Changes save automatically. Close the window to flush pending edits, then follow the launch instructions below.

Selecting a profile in the UI chooses which settings you edit; **Matched Processes** chooses which program uses them. With no match, MAKO remains dormant. You can also select a profile explicitly with `MAKO_PROFILE`, as shown below. See [Configuration](docs/CONFIGURATION.md) for all settings and matching rules.

Detection saves the executable match; it does not inject MAKO into an already running game. After capture, the UI shows the launch setup. Follow the native launch instructions or Flatpak preparation below, then restart the game.

### 2. Launch the game with MAKO

For a **native Steam or Proton game**, open **Steam Library > right-click the game > Properties > General > Launch Options** and enter:

```text
~/.local/bin/mako-launch %command%
```

Keep `%command%` exactly as written: Steam replaces it with the game's normal launch command, including Proton when applicable. Set this once for each game, then use Steam's **Play** button as usual. If the installer used a custom prefix, use the launcher path from its completion message. Remove the MAKO prefix to return to the normal native launch.

To use the profile named `My game` explicitly, use this launch option instead:

```text
MAKO_PROFILE="My game" ~/.local/bin/mako-launch %command%
```

For a **direct desktop command**, pass the executable and its arguments to the same launcher. Replace the example path with your installed game:

```bash
~/.local/bin/mako-launch "/path/to/your-game"
```

`%command%` is a Steam placeholder; do not use it in a terminal. For native Heroic or Lutris, put the absolute `mako-launch` path in the game's Wrapper or Command prefix field and let the launcher supply the game command; see [third-party launchers](../plugin/docs/LAUNCHERS.md). For an emulator, select its Vulkan graphics backend before playing.

`mako-launch` enables MAKO only for that process and establishes the supported standalone Vulkan-layer boundary. Use one Frame Generation implementation per game. Gamescope WSI and MangoHud profile controls remain MAKO Decky features.

The Qt configuration window exposes the same compact, per-profile shader controls as MAKO Decky, including an Effects checkbox picker for ordered combinations, and shows the complete launch option for the selected profile. It reads and writes Decky's `~/.config/mako-render/profile-wrapper-settings.json`, so a profile already configured in Decky displays its existing shader choices in Qt. Each non-default profile uses an isolated config under `~/.config/mako-render/vkbasalt/`; the default `mako` profile uses vkBasalt's global config. Renaming or deleting a Renderer profile moves or removes only its attached sidecar entries and isolated shader file; unrelated profiles and the global config are preserved.

To opt in manually to MAKO's private bundled vkBasalt after the Renderer for a native Steam or Proton game, use:

```text
ENABLE_VKBASALT=1 ~/.local/bin/mako-launch %command%
```

For the complete vkBasalt option suite, create a normal vkBasalt configuration file and select it for that game:

```text
ENABLE_VKBASALT=1 VKBASALT_CONFIG_FILE="$HOME/.config/vkBasalt/game-name.conf" ~/.local/bin/mako-launch %command%
```

The config path is optional for manual use. The Qt UI generates `MAKO_PROFILE`, `ENABLE_VKBASALT`, and an isolated `VKBASALT_CONFIG_FILE` together so the selected Renderer and shader profiles cannot drift. The launcher admits only the private 64-bit and 32-bit vkBasalt manifests installed with MAKO, establishes the exact `MAKO Renderer -> vkBasalt` order, and ignores a system-wide vkBasalt copy. If the complete private bundle or a selected config is unreadable, it reports the problem and safely launches with MAKO alone. FXAA, SMAA, CAS, DLS, sharpening strength, DLS denoise, and compact ordered effect selections in a selected config apply live; restart after changing layer membership or manual custom/advanced effects. See [Optional graphics integrations](docs/LAYER-CHAINING.md#standalone-mako-renderer-with-vkbasalt) for the full contract; Flatpak applications use the [separate sandbox setup](docs/FLATPAK-GUIDE.md#optional-private-vkbasalt-chain).

MAKO operates on Vulkan: native Vulkan and Proton games through DXVK or VKD3D-Proton are supported, while OpenGL requires the optional Zink launcher setting. If no profile matches the game process, MAKO remains dormant and presentation stays native.

Direct desktop scaling is supported, but Gamescope is recommended and can also run nested in Desktop Mode. MAKO reads the game's source image size from Vulkan. Without Gamescope, it may lack an authoritative display target, so the desktop compositor may add another scaling step or make MAKO fall back safely to native presentation after a recreation. On a variable desktop surface, lower the resolution in the game first: raising Scale Factor enlarges MAKO's output rather than reducing the game's source size. Frame Generation remains supported. See <a href="docs/CONFIGURATION.md#desktop-scaling-and-resolution" target="_blank" rel="noopener noreferrer">desktop scaling and resolution</a> for setup and limitations.

Want to use Frame Generation or scaling with videos? See <a href="docs/VIDEOS_WITH_MAKO.md" target="_blank" rel="noopener noreferrer">Videos with MAKO</a>.

### Flatpak games, launchers, and emulators

**Flatpak preparation is a separate one-time setup for each application.** The standalone configuration window edits Renderer profiles; it does not install runtime extensions or prepare Flatpak programs. The archive's **Install MAKO Flatpak Extensions** helper installs extensions only. MAKO Decky's **Flatpak Setup** provides the managed preparation workflow.

Without Decky, follow the [standalone Flatpak guide](docs/FLATPAK-GUIDE.md) to identify the app and runtime, install its matching extension, grant configuration and DLL access, and set the sandbox environment. Then restart the application and launch it normally or with `flatpak run APP_ID`. A host `mako-launch` prefix around `flatpak run` does not prepare the sandbox. The guide also covers Heroic/Lutris games, EmuDeck emulators, verification, updates, and disabling MAKO.

### Manual configuration and validation

You can configure MAKO without the UI by editing `~/.config/mako-render/conf.toml`. This complete minimal file matches a Windows executable and enables 2x Frame Generation; replace `Game.exe` with the game's executable name:

```toml
version = 2

[global]
allow_fp16 = true

[[profile]]
name = "My game"
active_in = ["Game.exe"]
multiplier = 2
frame_generation_provisioned = true
frame_generation_enabled = true
```

When adding a profile to an existing file, append only the `[[profile]]` block and keep the existing `version` and `[global]` settings. Manual configuration still requires the native launch command or Flatpak preparation above.

Validate the configuration or run the built-in benchmark with:

```bash
~/.local/bin/mako-cli validate
~/.local/bin/mako-cli inspect-dll --dll "/path/to/Lossless.dll"
~/.local/bin/mako-cli benchmark
```

`inspect-dll` validates LSFG and LS1 resources independently without changing the user-owned file. Run `mako-cli` without a subcommand to see all commands and options. Use `mako-diagnostics` for a focused standalone report.

The Renderer and CLI's LSFG commands default to FP16 when the selected GPU supports it, with FP32 on unsupported devices. To select FP32 explicitly, set `allow_fp16 = false` under `[global]` and restart the game, or pass `--no-fp16` to a CLI benchmark, debug, or LSFG quality command. The CLI's precision options are independent of the Renderer configuration file; see [global settings](docs/CONFIGURATION.md#global-settings).

The benchmark uses a defined traffic-image pair uploaded before timing; its recipe-2 results require fresh baselines. For changing source images, output counts, timestamps, and frame history, use the [temporal quality sequence](docs/IMAGE-QUALITY-REGRESSION.md#temporal-frame-generation-sequences). That readback-based check verifies correctness and is separate from timed capacity measurements.

## In-game considerations

<!-- prettier-ignore -->
> [!TIP]
> Try the game's V-Sync setting both on and off. Neither setting is universally best: the result depends on the game, its FPS cap, VRR, and the compositor. Keep whichever option feels smoother and more responsive for that game.

Compare Fixed Frame Generation, Adaptive Frame Generation, and scaling one setting at a time. For scaling, use a display mode where the application input is smaller than the presentation output; some games require Windowed mode because fullscreen or borderless keeps a display-sized input. Enable or disable Scaling between game sessions. See <a href="docs/SCALING.md" target="_blank" rel="noopener noreferrer">spatial scaling</a>, <a href="docs/WSI-ISOLATION.md" target="_blank" rel="noopener noreferrer">WSI isolation</a>, and <a href="docs/TROUBLESHOOTING.md" target="_blank" rel="noopener noreferrer">troubleshooting</a> for compatibility limits and diagnostics.

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

### Renderer source layout

`mako-render/src/entrypoint.cpp` owns Vulkan interception and `instance.*` owns process/device state and context routing. Swapchain implementation files share one `Swapchain` class and remain compiled into both isolated layer roles through the same `LAYER_SOURCES` list:

| File under `mako-render/src/swapchain/` | Responsibility |
| --- | --- |
| `swapchain.hpp` | Context state, lifetime order, and method declarations |
| `create.cpp`, `create_policy.hpp` | Initial construction and application swapchain provisioning |
| `resources.cpp` | Private FG/scaler resources, replacement preparation and commit, and HDR reclassification |
| `profile.cpp` | Live profile application, scheduler resets, refresh feedback, and guarded recreation requests |
| `present.cpp`, `retirement.hpp` | Presentation execution and retirement proof |
| `status.cpp` | Assemble requested/applied live status from current state |

The pure scheduling, presentation, scaling, and transition policies retain their existing focused headers and portable tests. Shader algorithms and generated payloads stay with their generators. Keep new work in its existing owner; splitting a translation unit must not introduce another state store, change destruction order, or move work across present and recreation boundaries.

## More documentation

- [Native installation transactions](../INSTALLATION-TRANSACTIONS.md): atomic replacement, rollback, shared native identity, failure boundaries, and contract tests for both installers.
- <a href="docs/LIFECYCLE.md" target="_blank" rel="noopener noreferrer">Lifecycle</a>: probes, timers, setting lifetimes, swapchain ownership, pacing policies, recovery, retirement, and heuristic risk.
- <a href="docs/MEMORY-MANAGEMENT.md" target="_blank" rel="noopener noreferrer">Memory management</a>: resource ownership, image pooling, allocation accounting, replacement peaks, cleanup, and validation.
- <a href="docs/CONFIGURATION.md" target="_blank" rel="noopener noreferrer">Configuration</a>: profiles, frame-generation, scaling, and shader controls, Adaptive mode, and environment variables.
- <a href="docs/SCALING.md" target="_blank" rel="noopener noreferrer">Spatial scaling architecture</a>: pipeline order, surface support, formats, resources, private transitions, and validation.
- <a href="docs/RUNTIME-TRANSITIONS.md" target="_blank" rel="noopener noreferrer">Runtime configuration transitions</a>: live-safe updates, recreation, and restart boundaries.
- <a href="docs/ADAPTIVE-VALIDATION.md" target="_blank" rel="noopener noreferrer">Adaptive validation</a>: scheduler behavior, VRR and fixed-refresh pacing paths, frame plans, benchmarking, and game validation.
- <a href="docs/WSI-ISOLATION.md" target="_blank" rel="noopener noreferrer">WSI isolation</a>: Vulkan discovery, Gamescope presentation ownership, and diagnostics.
- <a href="docs/LAYER-CHAINING.md" target="_blank" rel="noopener noreferrer">Optional graphics integrations</a>: MAKO Decky external tools, manual chaining, and limits.
- <a href="docs/FLATPAK-GUIDE.md" target="_blank" rel="noopener noreferrer">Flatpak guide</a>: runtime extensions and direct application overrides.
- <a href="docs/VIDEOS_WITH_MAKO.md" target="_blank" rel="noopener noreferrer">Videos with MAKO</a>: use Frame Generation and spatial scaling with mpv.
- <a href="docs/TROUBLESHOOTING.md" target="_blank" rel="noopener noreferrer">Troubleshooting</a>: activation, configuration, and presentation diagnostics.
- <a href="docs/COLLECT_DIAGNOSTICS.md" target="_blank" rel="noopener noreferrer">Collect standalone diagnostics</a>: create a focused Desktop report.
