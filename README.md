# MAKO - Frame Generation on SteamOS/Linux

<p align="center">
  <img src="plugin/assets/mako-logo.webp" width="256" alt="MAKO logo" />
</p>

<!-- prettier-ignore -->
> [!IMPORTANT]
> **[Decky LSFG-VK Experimental](https://github.com/eugeniosegala/decky-lsfg-vk-experimental) and [LSFG-VK Experimental](https://github.com/eugeniosegala/lsfg-vk-experimental) are now MAKO.** This repository is their new home and continuation. Future development, releases, documentation, and issue tracking happen here.

> **Independent project:** MAKO is independently developed and maintained for Steam Deck and Steam Machine. **MAKO Decky** provides per-game controls and integration, while **MAKO Renderer** supplies the Vulkan frame-generation layer. The project builds on work by **[PancakeTAS and the lsfg-vk contributors](https://github.com/PancakeTAS/lsfg-vk)** and **[xXJSONDeruloXx, the original Decky LSFG-VK developer](https://github.com/xXJSONDeruloXx/decky-lsfg-vk)**, whom MAKO gratefully thanks. Frame generation is available today, with scaling planned. MAKO requires `Lossless.dll` from a licensed [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) installation but does not bundle, copy, or modify it. Test it per game; MAKO is not an official Lossless Scaling, Decky Loader, or lsfg-vk release.

## Downloads

| Component | Recommended for | Releases |
| --- | --- | --- |
| **MAKO Decky** | Steam Deck, Steam Machine, and Decky Loader users | [Latest MAKO Decky release (ZIP under Assets)](https://github.com/eugeniosegala/MAKO/releases/latest) |
| **MAKO Renderer** | Direct Vulkan-layer installation without Decky | [Latest MAKO Renderer release (Linux archive under Assets)](https://github.com/eugeniosegala/MAKO/releases/tag/render-v2.0.0) |

<!-- prettier-ignore -->
> [!NOTE]
> Check the Downloads table regularly for updates. MAKO Decky and MAKO Renderer are published independently.

## ✨ Highlights

|  | Highlight | What it brings |
| :-: | --- | --- |
| 🖼️ | **Full-quality frame generation** | Uses the Lossless Scaling frame-generation models from the user's licensed installation, with quality and performance controls per profile. |
| 👻 | **Significantly reduced ghosting** | The full-quality v2 model with Performance Mode disabled can show noticeably less ghosting than the older layer. Results remain game-dependent. |
| 🎯 | **Adaptive Frame Generation** | Optionally targets 30–240 FPS while MAKO Renderer varies generated frames up to a selected 2x–4x ceiling. |
| 🌈 | **HDR foundation** | MAKO Renderer includes HDR10/PQ and linear-scRGB groundwork. MAKO Decky keeps HDR exposure disabled while activation, presentation, colour, and performance are validated across games. |
| 🧩 | **64-bit and 32-bit Vulkan** | Ships architecture-matched host and Flatpak layers so Vulkan can select the correct library for each game process. |
| 🛡️ | **Gamescope recovery** | Bounded presentation recovery preserves native presentation and resumes generation only after the game cadence becomes stable again. |
| ⏯️ | **Live frame-generation switch** | Turns frame generation on or off without discarding the selected Fixed or Adaptive settings. |
| 🗂️ | **Dedicated game/process profiles** | Capture a running game once and keep its renderer and compatibility settings. MAKO automatically selects it by Steam app ID or process, with isolated per-profile controls including ALSA audio. |
| 🎮 | **Per-game Heroic and EmuDeck support** | Enables MAKO only for the Heroic games and EmuDeck titles you choose, using the same private configuration and engine as native Steam games. |

## What MAKO is

MAKO is a next-generation, Vulkan-powered graphics project for Linux gaming, built to bring Lossless Scaling frame generation and scaling to Steam Deck and desktop Linux. Frame generation is available today, with scaling support planned as the project expands.

The project consists of two closely integrated components:

- **MAKO Decky** is the Decky Loader component, providing per-game controls, installation, updates, Flatpak preparation, and game launch integration.
- **MAKO Renderer** is the Vulkan layer that provides the graphics pipeline for frame generation and future scaling capabilities.

## 🎮 In-game considerations

<!-- prettier-ignore -->
> [!TIP]
> **Try the game's V-Sync setting both on and off.** It can make frame delivery feel steadier, but it may also add input lag or clash with the game's FPS cap, VRR, or compositor. Compare both options and keep the one that feels smoother and more responsive.

Every game, renderer, and display setup behaves differently. Compare Fixed and Adaptive Frame Generation one setting at a time. For most games, fullscreen is the best starting point for performance and frame pacing. Restart after major display or model changes, and keep the configuration that works best for that game.

## Install and use

1. **Install Decky Loader** if needed. Switch to Desktop Mode and follow the [official Decky Loader installation guide](https://github.com/SteamDeckHomebrew/decky-loader#-installation), then return to Game Mode.
2. **Install [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) from Steam.** MAKO needs the licensed installation's `Lossless.dll`.
3. Open the [latest MAKO Decky release](https://github.com/eugeniosegala/MAKO/releases/latest) and download the MAKO Decky ZIP under **Assets**.
4. In Decky's settings, enable **Developer Mode**, then select **Developer > Install Plugin from Zip**.
5. Open **MAKO Decky** and select **Install MAKO Renderer**. This required step installs the renderer bundled in the ZIP into MAKO Decky's private location.
6. Leave the defaults in place unless a game needs adjustment. Fixed 2x is the normal starting point; Adaptive Frame Generation is optional.
7. For a native Steam or Proton game, add this under **Steam Properties > Launch Options**:

    ```text
    /home/deck/.local/bin/mako-run %command%
    ```

8. Start the game normally.

<!-- prettier-ignore -->
> [!IMPORTANT]
> If Decky does not show or reload **MAKO Decky** after installing a ZIP, uninstall MAKO Decky, install the ZIP again, and restart your Steam Deck or Steam Machine. Open MAKO Decky afterwards and repeat step 5.

### Heroic and other Flatpak applications

The Steam launch wrapper cannot enter a Flatpak sandbox directly, so configure Heroic through **Flatpak Setup**:

1. Select **Flatpak Setup** in MAKO.
2. Under **Flatpak Applications**, prepare **Heroic**. If the matching runtime extension is missing, MAKO tells you which runtime to install, commonly **25.08**. Preparing Heroic grants access to the wrapper, configuration, and `Lossless.dll`; it does not enable frame generation for every Heroic game.
3. In every Heroic game you want to enable, open **Settings > Advanced** and set the first **Wrapper** field to the SteamOS wrapper path:

    ```text
    /home/deck/.local/bin/mako-run
    ```

    MAKO also shows the exact **Wrapper path for this device** under **Flatpak Setup**. Use that displayed path when it differs, such as on Bazzite or with a custom username. Leave **Arguments** empty and do not use `%command%` in Heroic.

4. Start that game normally from Heroic or its Steam shortcut.

The wrapper applies only to the selected Heroic games and enables the private MAKO Renderer Flatpak layer for that game.

<!-- prettier-ignore -->
> [!IMPORTANT]
> After installing a newer MAKO ZIP, return to **Flatpak Setup** and select **Update** for Heroic's matching runtime extension. This replaces Heroic's Flatpak engine with the version bundled in the new ZIP while preserving its preparation and per-game Wrapper commands.

### EmuDeck

For any EmuDeck emulator installed as a Flatpak:

1. In MAKO, select **Flatpak Setup** and prepare the emulator you use. Install its matching runtime extension when prompted.
2. Select **Vulkan** as that emulator's graphics backend when it offers one.
3. In Desktop Mode, open the Steam shortcut for each EmuDeck game you want to configure, then set these fields under **Properties > Shortcut**:

    - **Target**

        ```text
        /home/deck/.local/bin/mako-run
        ```

        This is the standard SteamOS path. If MAKO shows a different **Wrapper path for this device** under **Flatpak Setup**, use the displayed path.

    - **Start In**

        ```text
        /usr/bin
        ```

    - **Launch Options:** leave the EmuDeck-generated value unchanged. It already contains the correct emulator ID, ROM path, and flags for that shortcut.

If EmuDeck installed an emulator as a native application or AppImage instead, it is not a Flatpak workflow: use the normal Steam launch option `/home/deck/.local/bin/mako-run %command%` for that shortcut.

<!-- prettier-ignore -->
> [!IMPORTANT]
> After updating MAKO, return to **Flatpak Setup** and select **Update** for every prepared emulator's matching runtime extension.

For a step-by-step guide that creates a shareable report on the Desktop, see [Collect MAKO Decky Diagnostics](plugin/docs/COLLECT_DIAGNOSTICS.md).

### Updating MAKO Decky

The clean update path avoids Decky retaining an older backend or bundled payload:

1. Quit games using `/home/deck/.local/bin/mako-run`.
2. Uninstall MAKO from Decky, then install the newer ZIP through **Developer > Install Plugin from Zip**.
3. Restart your Steam Deck or Steam Machine.
4. Open MAKO Decky and select **Install MAKO Renderer** to install the native renderer bundled in the ZIP.
5. If you use Heroic or EmuDeck Flatpak emulators, open **Flatpak Setup** and select **Update** for each prepared application's matching runtime extension shown by MAKO.

<!-- prettier-ignore -->
> [!IMPORTANT]
> **Preferred clean update:** To prevent Decky retaining a previous plugin backend or bundled payload, especially when moving between local test ZIPs, uninstall **MAKO Decky**, install the newer ZIP, restart your Steam Deck or Steam Machine, then select **Install MAKO Renderer** in MAKO Decky.

Profiles and Steam launch options are retained. The private native engine and launcher are recreated in step 4; shared Flatpak extensions are retained and then refreshed in step 5.

## Use MAKO Renderer directly

Decky is optional. Desktop Linux users can install the published MAKO Renderer archive directly:

1. Purchase and install [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) through Steam.
2. Open the [latest MAKO Renderer release](https://github.com/eugeniosegala/MAKO/releases/tag/render-v2.0.0) and download the Linux archive under **Assets**. New releases use `MAKO-Renderer-v<version>-linux.tar.xz`; the existing v2.0.0 release retains its published legacy name, `mako-render-v2.0.0-linux.tar.xz`.
3. Extract it into your user-local prefix:

```bash
mkdir -p ~/.local
tar -xJf MAKO-Renderer-v<version>-linux.tar.xz -C ~/.local
```

For v2.0.0, substitute its legacy filename from step 2 in the extraction command.

4. Open **MAKO Renderer Configuration** from the application launcher or run `~/.local/bin/mako-ui`.
5. Add a game profile, then activate MAKO only for that game. In Steam launch options, use:

```text
ENABLE_MAKO=1 %command%
```

The archive includes both 64-bit and 32-bit Vulkan layers. Flatpak applications need the separate runtime extension. See the dedicated [MAKO Renderer installation and usage guide](engine/README.md) for Qt requirements, manual configuration, validation, benchmarking, Flatpak setup, source builds, and troubleshooting.

## Documentation

- [Configuration guide](plugin/docs/CONFIGURATION.md): Fixed and Adaptive modes, quality and performance settings, profiles, and compatibility options.
- [Troubleshooting](plugin/docs/TROUBLESHOOTING.md): Gamescope recovery, HDR compatibility, and diagnostic logs.
- [Collect MAKO Diagnostics](COLLECT_DIAGNOSTICS.md): choose the MAKO Decky or standalone Renderer collection workflow and submit one shared report.
- [Local packaging and publishing](plugin/docs/PACKAGING.md): build a ZIP for a Steam machine or publish a release.
- [Release process](HOW_TO_RELEASE.md): publish Renderer and Decky end to end with one versioned command.
- [MAKO Renderer documentation](engine/README.md): engine identity, source builds, configuration, and direct use.

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

## AI-assisted development

MAKO uses coding agents as part of an evidence-driven engineering workflow while keeping architecture, review, validation, and release decisions under human ownership. See [AI use in MAKO](AI_USE.md) for the full approach.
