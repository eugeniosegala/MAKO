# MAKO

<p align="center">
  <img src="plugin/assets/mako-logo.png" width="256" alt="MAKO logo" />
</p>

> [!IMPORTANT]
> **Decky LSFG-VK Experimental and LSFG-VK Experimental are now MAKO.** This repository is the new home and continuation of [Decky LSFG-VK Experimental](https://github.com/eugeniosegala/decky-lsfg-vk-experimental) and [LSFG-VK Experimental](https://github.com/eugeniosegala/lsfg-vk-experimental). Future development, releases, documentation, and issue tracking happen here.

> **Independent project and Lossless Scaling requirement:** MAKO is an independent Linux graphics project comprising MAKO Decky and MAKO Renderer. It brings Lossless Scaling frame generation to Steam Deck and desktop Linux today, with scaling support planned as the project expands. MAKO requires the `Lossless.dll` supplied by a licensed [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) installation but does not bundle, copy, or modify that proprietary library. Test it per game; MAKO is not an official Lossless Scaling, Decky Loader, or lsfg-vk release.

## Downloads

| Component | Recommended for | Releases |
|-----------|-----------------|----------|
| **MAKO Decky** | Steam Deck and Decky Loader users | MAKO-branded ZIPs are not published yet · [Legacy experimental releases](https://github.com/eugeniosegala/decky-lsfg-vk-experimental/releases) |
| **MAKO Renderer** | Direct Vulkan-layer installation without Decky | MAKO-branded archives are not published yet · [Legacy renderer releases](https://github.com/eugeniosegala/lsfg-vk-experimental/releases) |

The first MAKO-branded Decky ZIP and Renderer archive have not been released yet. The legacy release tracks above remain available for the previous experimental projects, but their packages, wrapper names, and instructions still use the old LSFG-VK identities. This README describes MAKO and will link to its own release tracks once the first MAKO packages are published.

## ✨ Highlights

|    | Highlight                         | What it brings |
|:--:|-----------------------------------|----------------|
| 🖼️ | **Full-quality frame generation** | Uses the Lossless Scaling frame-generation models from the user's licensed installation, with quality and performance controls per profile. |
| 👻 | **Significantly reduced ghosting** | The full-quality v2 model with Performance Mode disabled can show noticeably less ghosting than the older layer. Results remain game-dependent. |
| 🎯 | **Adaptive Frame Generation**     | Optionally targets 30–240 FPS while MAKO Renderer varies generated frames up to a selected 2x–4x ceiling. |
| 🌈 | **HDR foundation**                | MAKO Renderer includes HDR10/PQ and linear-scRGB groundwork. MAKO Decky keeps HDR exposure disabled while activation, presentation, colour, and performance are validated across games. |
| 🧩 | **64-bit and 32-bit Vulkan**       | Ships architecture-matched host and Flatpak layers so Vulkan can select the correct library for each game process. |
| 🛡️ | **Gamescope recovery**            | Bounded presentation recovery preserves native presentation and resumes generation only after the game cadence becomes stable again. |
| ⏯️ | **Live frame-generation switch**  | Turns frame generation on or off without discarding the selected Fixed or Adaptive settings. |
| 🎮 | **Per-game Heroic support**        | Enables MAKO only for the Heroic games you choose, using the same private configuration and engine as native Steam games. |

## What MAKO is

MAKO is a next-generation, Vulkan-powered graphics project for Linux gaming, built to bring Lossless Scaling frame generation and scaling to Steam Deck and desktop Linux. Frame generation is available today, with scaling support planned as the project expands.

The project consists of two closely integrated components:

- **MAKO Decky** is the Decky Loader component, providing per-game controls, installation, updates, Flatpak preparation, and game launch integration.
- **MAKO Renderer** is the Vulkan layer that provides the graphics pipeline for frame generation and future scaling capabilities.

## 🎮 In-game considerations

> [!TIP]
> **Try the game's V-Sync setting both on and off.** It can make frame delivery feel steadier, but it may also add input lag or clash with the game's FPS cap, VRR, or compositor. Compare both options and keep the one that feels smoother and more responsive.

Every game, renderer, and display setup behaves differently. Compare Fixed and Adaptive Frame Generation one setting at a time. For most games, fullscreen is the best starting point for performance and frame pacing. Restart after major display or model changes, and keep the configuration that works best for that game.

## Install and use

1. **Install Decky Loader** if needed. Switch to Desktop Mode and follow the [official Decky Loader installation guide](https://github.com/SteamDeckHomebrew/decky-loader#-installation), then return to Game Mode.
2. **Install [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) from Steam.** MAKO needs the licensed installation's `Lossless.dll`.
3. **Wait for the first MAKO Decky ZIP.** MAKO-branded packages are not published yet; see the release-status note above before installing a legacy experimental package.
4. In Decky's settings, enable **Developer Mode**, then select **Developer > Install Plugin from Zip**.
5. Open **Mako** and select **Install MAKO Renderer (developer build)**. This required step installs the renderer bundled in the ZIP into MAKO's private location.
6. Leave the defaults in place unless a game needs adjustment. Fixed 2x is the normal starting point; Adaptive Frame Generation is optional.
7. For a native Steam or Proton game, add this under **Steam Properties > Launch Options**:

   ```text
   ~/.local/bin/mako-run %command%
   ```

8. Start the game normally.

> [!IMPORTANT]
> If Decky does not show or reload **Mako** after installing a ZIP, uninstall Mako from Decky, install the ZIP again, and restart your Steam Deck or Steam Machine. Open Mako afterwards and repeat step 5.

### Heroic and other Flatpak applications

The Steam launch wrapper cannot enter a Flatpak sandbox directly, so configure Heroic through **Flatpak Setup**:

1. Select **Flatpak Setup** in Mako.
2. Under **Flatpak Applications**, prepare **Heroic**. If the matching runtime extension is missing, Mako tells you which runtime to install, commonly **25.08**. Preparing Heroic grants access to the wrapper, configuration, and `Lossless.dll`; it does not enable frame generation for every Heroic game.
3. In every Heroic game you want to enable, open **Settings > Advanced** and set the first **Wrapper** field to:

   ```text
   /home/deck/.local/bin/mako-run
   ```

   Leave **Arguments** empty. Do not use `%command%` in Heroic.
4. Start that game normally from Heroic or its Steam shortcut.

The wrapper applies only to the selected Heroic games and enables the private MAKO Renderer Flatpak layer for that game.

> [!IMPORTANT]
> After installing a newer MAKO ZIP, return to **Flatpak Setup** and select **Update** for Heroic's matching runtime extension. This replaces Heroic's Flatpak engine with the version bundled in the new ZIP while preserving its preparation and per-game Wrapper commands.

### EmuDeck and Dolphin

EmuDeck's Dolphin is a Flatpak application, so it uses the same **Flatpak Setup** screen but does not need Heroic's per-game Wrapper field:

1. In Mako, select **Flatpak Setup** and prepare **Dolphin Emulator**. Install the matching runtime extension when prompted; current EmuDeck Dolphin builds commonly use **25.08** through the KDE runtime.
2. In Dolphin, set **Graphics > General > Backend** to **Vulkan**.
3. Launch your game normally from EmuDeck or its Steam shortcut. Do **not** add `~/.local/bin/mako-run`, `%command%`, or a Wrapper field for Dolphin.

Preparing Dolphin grants it access to MAKO's configuration and `Lossless.dll`, then enables the private Vulkan layer only inside Dolphin's Flatpak sandbox. The setting applies to Dolphin launches generally; use MAKO profiles if you need different renderer settings for different games.

> [!IMPORTANT]
> After updating MAKO, return to **Flatpak Setup** and select **Update** for Dolphin's matching runtime extension as well.

### Updating MAKO Decky

The clean update path avoids Decky retaining an older backend or bundled payload:

1. Quit games using `~/.local/bin/mako-run`.
2. Uninstall Mako from Decky, then install the newer ZIP through **Developer > Install Plugin from Zip**.
3. Restart your Steam Deck or Steam Machine.
4. Open Mako and select **Install MAKO Renderer (developer build)** to install the native renderer bundled in the ZIP.
5. If you use Heroic, open **Flatpak Setup** and select **Update** for Heroic's matching runtime extension, usually **25.08**.

> [!IMPORTANT]
> **Preferred clean update:** To prevent Decky retaining a previous plugin backend or bundled payload, especially when moving between local test ZIPs, uninstall **Mako** from Decky, install the newer ZIP, restart your Steam Deck or Steam Machine, then select **Install MAKO Renderer (developer build)** in the plugin.

Profiles and Steam launch options are retained. The private native engine and launcher are recreated in step 4; shared Flatpak extensions are retained and then refreshed in step 5.

## Documentation

- [Configuration guide](plugin/docs/CONFIGURATION.md): Fixed and Adaptive modes, quality and performance settings, profiles, and compatibility options.
- [Troubleshooting](plugin/docs/TROUBLESHOOTING.md): Gamescope recovery, HDR compatibility, and diagnostic logs.
- [Local packaging and publishing](plugin/docs/PACKAGING.md): build a ZIP for a Steam machine or publish a release.
- [MAKO Renderer documentation](engine/README.md): engine identity, source builds, configuration, and direct use.

## Use MAKO Renderer directly

Decky is optional. Linux users can build and install MAKO Renderer directly as an implicit Vulkan layer:

```bash
cmake -S engine -B engine/build \
  -DCMAKE_BUILD_TYPE=Release \
  -DMAKO_BUILD_UI=On \
  -DMAKO_INSTALL_XDG_FILES=On
cmake --build engine/build
sudo cmake --install engine/build
```

Configure the licensed `Lossless.dll` path with `mako-ui` or `~/.config/mako-render/conf.toml`, then activate the layer only for the game process. In Steam launch options, use:

```text
ENABLE_MAKO=1 %command%
```

See [Building from Source](engine/docs/BUILDING-FROM-SOURCE.md), [Configuration](engine/docs/CONFIGURATION.md), and [Troubleshooting](engine/docs/TROUBLESHOOTING.md) for the complete standalone workflow.

## Featured in

Community creators have covered and tested the project on Steam Deck hardware. See [Featured In](plugin/docs/FEATURED_IN.md) for video links, channels, and coverage details.

## Credits and project lineage

MAKO is built on the work of two open-source projects and their communities:

- **[Kurt Himebauch / xXJSONDeruloXx](https://github.com/xXJSONDeruloXx/decky-lsfg-vk)** created the original Decky LSFG-VK plugin that formed the foundation of MAKO's Decky interface, installation workflow, and per-game controls.
- **[PancakeTAS](https://github.com/PancakeTAS/lsfg-vk)** and the **lsfg-vk contributors** created the Vulkan layer and Linux integration on which MAKO Renderer is based.

MAKO also thanks the **Lossless Scaling developers** for the frame-generation and scaling technology accessed through each user's licensed installation, and the **Decky Loader team**, community contributors, testers, guide authors, and creators who helped make the project possible.

The original copyright and license notices are preserved in [LICENSE.md](LICENSE.md). MAKO is an independent community project and is not affiliated with or endorsed by Lossless Scaling, Decky Loader, or either upstream project.

## License

MAKO is distributed under [GPL-3.0-or-later](LICENSE.md). The root license also preserves the BSD-3-Clause and MIT notices required by incorporated upstream code.
