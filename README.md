# MAKO - Scaling and Frame Generation on SteamOS/Linux

<p align="center">
  <img src="plugin/assets/mako-logo.webp" width="256" alt="MAKO logo" />
</p>

<p align="center">
  <a href="https://trendshift.io/repositories/171701?utm_source=trendshift-badge&amp;utm_medium=badge&amp;utm_campaign=badge-trendshift-171701" target="_blank" rel="noopener noreferrer"><img src="https://trendshift.io/api/badge/trendshift/repositories/171701/daily?language=C%2B%2B" alt="eugeniosegala/MAKO | Trendshift" width="250" height="55" /></a>
</p>

<p align="center">
  <a href="https://discord.gg/NAVkyCq7Rc"><img src="https://img.shields.io/badge/Discord-join-5865F2?style=flat-square&amp;logo=discord&amp;logoColor=white" alt="Join the MAKO Discord community" /></a>
  <a href="https://github.com/eugeniosegala/MAKO/actions/workflows/tests.yml"><img src="https://img.shields.io/github/actions/workflow/status/eugeniosegala/MAKO/tests.yml?branch=main&amp;style=flat-square&amp;label=tests" alt="Tests status" /></a>
  <a href="LICENSE.md"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-0f766e?style=flat-square" alt="GPL-3.0-or-later license" /></a>
  <br />
  <a href="https://github.com/eugeniosegala/MAKO/releases/latest"><img src="https://img.shields.io/github/v/release/eugeniosegala/MAKO?filter=plugin-%2A&amp;display_name=tag&amp;sort=semver&amp;style=flat-square&amp;label=Decky&amp;color=1d4ed8" alt="Latest MAKO Decky release" /></a>
  <a href="https://github.com/eugeniosegala/MAKO/releases?q=render-v"><img src="https://img.shields.io/github/v/release/eugeniosegala/MAKO?filter=render-%2A&amp;display_name=tag&amp;sort=semver&amp;style=flat-square&amp;label=Renderer&amp;color=1d4ed8" alt="Latest MAKO Renderer release" /></a>
  <img src="https://img.shields.io/badge/platform-SteamOS%20%7C%20Linux-6b8e23?style=flat-square" alt="SteamOS and Linux" />
</p>

<!-- prettier-ignore -->
> [!IMPORTANT]
> **[Decky LSFG-VK Experimental](https://github.com/eugeniosegala/decky-lsfg-vk-experimental) and [LSFG-VK Experimental](https://github.com/eugeniosegala/lsfg-vk-experimental) are now MAKO.** This repository is their new home and continuation. Future development, releases, documentation, and issue tracking happen here.

> **Independent project:** MAKO is independently developed and maintained for Steam Deck and Steam Machine. **MAKO Decky** provides per-game controls and integration, while **MAKO Renderer** supplies Vulkan spatial scaling and frame generation. The project builds on work by **[PancakeTAS and the lsfg-vk contributors](https://github.com/PancakeTAS/lsfg-vk)** and **[xXJSONDeruloXx, the original Decky LSFG-VK developer](https://github.com/xXJSONDeruloXx/decky-lsfg-vk)**, whom MAKO gratefully thanks. LS1 scaling and LSFG frame generation require `Lossless.dll` from a licensed [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) installation; MAKO does not bundle, copy, persist, or modify it, and its built-in open spatial scaler does not require it. Test features per game; MAKO is not an official Lossless Scaling, Decky Loader, or lsfg-vk release.

## Downloads

| Component | Recommended for | Releases |
| --- | --- | --- |
| **MAKO Decky** | Steam Deck, Steam Machine, and Decky Loader users | [Latest MAKO Decky release (ZIP under Assets)](https://github.com/eugeniosegala/MAKO/releases/latest) |
| **MAKO Renderer** | Direct Vulkan-layer installation without Decky | [Latest MAKO Renderer release (Linux archive under Assets)](https://github.com/eugeniosegala/MAKO/releases/tag/render-v2.2.0) |

## Community

Join the official [MAKO Discord](https://discord.gg/NAVkyCq7Rc) for discussion, testing, development, showcases, and live troubleshooting. GitHub remains the source of truth for [bug reports and feature requests](https://github.com/eugeniosegala/MAKO/issues/new/choose).

<!-- prettier-ignore -->
> [!TIP]
> **Want update alerts?** MAKO Decky and MAKO Renderer are published independently. At the top-right of the [MAKO GitHub repository](https://github.com/eugeniosegala/MAKO) page, click **Watch** > **Custom**, select **Releases**, then click **Apply**. GitHub will notify you when a new release is published, subject to your GitHub notification settings.

Published Renderer packages currently target x86_64 Linux hosts and include layers for both 64-bit and 32-bit x86 game processes. Native AArch64/Armada packages are not included in this release.

## ✨ Highlights

|  | Highlight | What it brings |
| :-: | --- | --- |
| 🖼️ | **Full-quality frame generation** | Uses the Lossless Scaling frame-generation models from the user's licensed installation, with quality and performance controls per profile. |
| 🔍 | **LS1 + open spatial scaling** | Reconstructs a lower-resolution game frame with LS1 Quality, LS1 Performance, or the open single-pass MAKO method, independently or before Fixed or Adaptive generation. |
| 👻 | **Significantly reduced ghosting** | The full-quality v2 model with Performance Mode disabled can show noticeably less ghosting than the older layer. Supported AMD GPUs also gain extra protection against ghosting and corrupted moving edges. Results remain game-dependent. |
| 🎯 | **Adaptive Frame Generation** | Optionally targets 30–240 FPS while MAKO Renderer varies generated frames up to a selected 2x–4x ceiling. |
| 🌈 | **HDR foundation** | MAKO Renderer includes HDR10/PQ and linear-scRGB groundwork. MAKO Decky keeps HDR exposure disabled while activation, presentation, colour, and performance are validated across games. |
| 🧩 | **64-bit and 32-bit x86 Vulkan** | Ships architecture-matched host and Flatpak layers so Vulkan can select the correct library for each game process. |
| 🛡️ | **Gamescope recovery** | Bounded presentation recovery preserves native presentation and resumes generation only after the game cadence becomes stable again. |
| ⏯️ | **Live frame-generation switch** | Turns frame generation on or off without discarding the selected Fixed or Adaptive settings. |
| 🗂️ | **Dedicated game/process profiles** | Capture a running game once and keep its renderer and compatibility settings. MAKO automatically selects it by Steam app ID or process, with isolated per-profile controls including ALSA audio. |
| 🎮 | **Heroic and EmuDeck integration** | Gives Heroic games a per-game wrapper and prepares Flatpak emulators for Steam-shortcut profile selection, using the same private configuration and engine as native Steam games. |

## What MAKO is

MAKO (**Motion-Adaptive Kernel Orchestration**) is a Vulkan-powered graphics project for Linux gaming that brings LS1 spatial scaling, MAKO's built-in open spatial scaler, and LSFG frame generation to Steam Deck, Steam Machine, SteamOS, and Linux more broadly. Scaling can run alone or reconstruct real frames before Fixed or Adaptive Frame Generation.

The project consists of two closely integrated components:

- **MAKO Decky** is the Decky Loader component, providing per-game controls, installation, updates, Flatpak preparation, and game launch integration.
- **MAKO Renderer** is the Vulkan layer that provides the graphics pipeline for spatial scaling and frame generation.

Choose one launch workflow for each installation: MAKO Decky uses `mako-run`, while a directly installed MAKO Renderer uses `mako-launch`. Do not stack the two launchers or combine MAKO with another frame-generation wrapper for the same game.

## 🎮 In-game considerations

<!-- prettier-ignore -->
> [!TIP]
> **Try the game's V-Sync setting both on and off.** It can make frame delivery feel steadier, but it may also add input lag or clash with the game's FPS cap, VRR, or compositor. Compare both options and keep the one that feels smoother and more responsive.

Every game, renderer, and display setup behaves differently. Compare scaling, Fixed Frame Generation, and Adaptive Frame Generation one setting at a time. To use spatial scaling in MAKO Decky, enable Scaling Engine before launching the game, choose a lower game rendering resolution, then switch between Native Resolution, MAKO Scaler, LS1 Quality, and LS1 Performance while playing; model changes rebuild MAKO's private scaler without recreating the game's swapchain. Scale Factor applies after the game's next natural resolution change or restart. Change Scaling Engine itself only between game sessions. Frame Generation also turns on and off live when its startup resources are available, including in a session that began with generation disabled. For most games, fullscreen is the best starting point for performance and frame pacing. See the [spatial scaling architecture](engine/docs/SCALING.md) for compatibility requirements and the full pipeline contract.

## Install and use

1. **Install Decky Loader** if needed. Switch to Desktop Mode and follow the [official Decky Loader installation guide](https://github.com/SteamDeckHomebrew/decky-loader#-installation), then return to Game Mode.
2. **Install [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) from Steam if you will use LS1 scaling or frame generation.** MAKO reads its licensed `Lossless.dll`; the open MAKO scaler works without it.
3. Open the [latest MAKO Decky release](https://github.com/eugeniosegala/MAKO/releases/latest) and download its ZIP under **Assets**.
4. In Decky's settings, enable **Developer Mode**, then select **Developer > Install Plugin from Zip**.
5. Open **MAKO Decky** and select **Install MAKO Renderer**. This required step installs the bundled renderer into the plugin's private location.
6. Leave the defaults in place unless a game needs adjustment. Fixed 2x is the normal frame-generation starting point; scaling and Adaptive Frame Generation are independent options.
7. For a native Steam or Proton game, add this under **Steam Properties > Launch Options**:

    ```text
    /home/deck/.local/bin/mako-run %command%
    ```

8. Start the game normally.

<!-- prettier-ignore -->
> [!IMPORTANT]
> If Decky does not show or reload **MAKO Decky** after installing a ZIP, uninstall it, install the ZIP again, and restart your Steam Deck or Steam Machine. Then open the plugin and repeat step 5.

### Optional graphics integrations

MAKO Decky provides an experimental per-profile Gamescope WSI compatibility toggle and supports host-installed MangoHud and experimental vkBasalt under **External Tools**. Gamescope WSI is independent; MangoHud and vkBasalt remain mutually exclusive. See [optional graphics integrations](engine/docs/LAYER-CHAINING.md) for limits, verification, manual activation, and advanced integrations.

### Heroic and other Flatpak applications

The Steam launch wrapper cannot enter a Flatpak sandbox directly, so configure Heroic through **Flatpak Setup**:

1. Select **Flatpak Setup** in MAKO Decky.
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

1. In MAKO Decky, select **Flatpak Setup** and prepare the emulator you use. Install its matching runtime extension when prompted. Preparation applies to the entire emulator Flatpak rather than one ROM because Flatpak must receive MAKO's layer and configuration inside its sandbox.
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

The edited Steam shortcut supplies the per-game launch and profile identity, but the Flatpak preparation itself remains app-wide. A prepared emulator can therefore load the MAKO layer when launched outside an edited game shortcut; turn off that application's preparation in **Flatpak Setup** when you do not want MAKO available to the emulator.

If EmuDeck installed an emulator as a native application or AppImage instead, it is not a Flatpak workflow: use the normal Steam launch option `/home/deck/.local/bin/mako-run %command%` for that shortcut.

<!-- prettier-ignore -->
> [!IMPORTANT]
> After updating MAKO, return to **Flatpak Setup** and select **Update** for every prepared emulator's matching runtime extension.

### Manually added Flatpak shortcuts

Use this workflow only when a non-Steam shortcut's original **Target** is `/usr/bin/flatpak`. It is not the Heroic workflow, and EmuDeck-generated shortcuts should use the dedicated instructions above.

1. In **Flatpak Setup**, install the matching runtime extension and prepare the Flatpak application.
2. In the shortcut's **Properties > Shortcut**, replace **Target** with:

    ```text
    "/home/deck/.local/bin/mako-run" "/usr/bin/flatpak"
    ```

    Use MAKO's displayed **Wrapper path for this device** when it differs from `/home/deck/.local/bin/mako-run`.

3. Leave **Start In** and **Launch Options** unchanged so the original Flatpak application ID, command, and flags are preserved.

The reference shown in **Flatpak Setup** does not modify Steam automatically; it only builds the correct Target from this device's installed wrapper path.

For a step-by-step guide that creates a shareable report on the Desktop, see [Collect MAKO Decky Diagnostics](plugin/docs/COLLECT_DIAGNOSTICS.md).

### Updating MAKO Decky

The clean update path avoids Decky retaining an older backend or bundled payload:

1. Quit games using `/home/deck/.local/bin/mako-run`.
2. Uninstall MAKO Decky, then install the newer ZIP through **Developer > Install Plugin from Zip**.
3. Restart your Steam Deck or Steam Machine.
4. Open MAKO Decky and select **Install MAKO Renderer** to install the native renderer bundled in the ZIP.
5. If you use Heroic or EmuDeck Flatpak emulators, open **Flatpak Setup** and select **Update** for each prepared application's matching runtime extension shown by MAKO.

<!-- prettier-ignore -->
> [!IMPORTANT]
> **Preferred clean update:** To prevent Decky retaining a previous plugin backend or bundled payload, especially when moving between local test ZIPs, uninstall **MAKO Decky**, install the newer ZIP, restart your Steam Deck or Steam Machine, then open it and select **Install MAKO Renderer**.

Profiles and Steam launch options are retained. The private native engine and launcher are recreated in step 4; shared Flatpak extensions are retained and then refreshed in step 5.

## Use MAKO Renderer directly

Decky is optional. Desktop Linux users can install the published MAKO Renderer archive directly:

1. To use LS1 scaling or frame generation, purchase and install [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) through Steam. The open MAKO scaler does not use `Lossless.dll`.
2. Download and extract `MAKO-Renderer-v<version>-linux.tar.xz` from the [latest MAKO Renderer release](https://github.com/eugeniosegala/MAKO/releases/tag/render-v2.2.0), then double-click **Install MAKO Renderer** and choose **Execute** if your file manager asks.
3. Run the installer again after extracting a newer archive. For manual installation, configuration, Flatpak setup, and troubleshooting, see the [MAKO Renderer guide](engine/README.md#direct-linux-installation).

<!-- prettier-ignore -->
> [!IMPORTANT]
> This installer is for the standalone MAKO Renderer only. It warns and asks for confirmation when MAKO Decky is installed because the current Decky and standalone flows share Renderer, Vulkan-manifest, and configuration paths. Continuing may leave MAKO Decky incompatible until you reinstall its matching Renderer.

## Documentation

- [Configuration guide](plugin/docs/CONFIGURATION.md): Scaling, Fixed and Adaptive modes, quality and performance settings, profiles, and compatibility options.
- [Spatial scaling architecture](engine/docs/SCALING.md): pipeline order, surface ownership, formats, performance model, controls, and validation matrix.
- [Troubleshooting](plugin/docs/TROUBLESHOOTING.md): Gamescope recovery, HDR compatibility, and diagnostic logs.
- [Collect MAKO Diagnostics](COLLECT_DIAGNOSTICS.md): choose the MAKO Decky or standalone renderer collection workflow and submit one shared report.
- [Local packaging and publishing](plugin/docs/PACKAGING.md): build a ZIP for a Steam machine or publish a release.
- [Release process](HOW_TO_RELEASE.md): publish both components end to end with one versioned command.
- [Testing](TESTING.md): pull-request gates, sanitizer coverage, SteamOS hardware validation, and the runtime compatibility boundary.
- [MAKO Gym](https://github.com/eugeniosegala/MAKO-Gym): private real-hardware Vulkan, Gamescope, scaling, and frame-generation QA scenarios used by the SteamOS release gate.
- [MAKO Renderer documentation](engine/README.md): engine identity, source builds, configuration, and direct use.

## Featured in

Community creators have covered and tested the project on Steam Deck hardware. See [Featured In](plugin/docs/FEATURED_IN.md) for video links, channels, and coverage details.

## Credits and project lineage

MAKO is built on the work of two open-source projects and their communities:

- **[Kurt Himebauch / xXJSONDeruloXx](https://github.com/xXJSONDeruloXx/decky-lsfg-vk)** created the original Decky LSFG-VK plugin that formed the foundation of MAKO's Decky interface, installation workflow, and per-game controls.
- **[PancakeTAS](https://github.com/PancakeTAS/lsfg-vk)** and the **lsfg-vk contributors** created the Vulkan layer and Linux integration on which MAKO Renderer is based.

MAKO also thanks the **Lossless Scaling developers** for the LS1 and LSFG models accessed through each user's licensed installation, the **Wine/vkd3d developers** whose shader translator enables the Vulkan LS1 path, and the **Decky Loader team**, community contributors, testers, guide authors, and creators who helped make the project possible. MAKO's open spatial scaler is independently implemented in this repository.

The original copyright and license notices are preserved in [LICENSE.md](LICENSE.md). MAKO is an independent community project and is not affiliated with or endorsed by Lossless Scaling, Decky Loader, or either upstream project.

## License

MAKO is distributed under [GPL-3.0-or-later](LICENSE.md). The root license also preserves the BSD-3-Clause and MIT notices required by incorporated upstream code.

## AI-assisted development

MAKO uses coding agents as part of an evidence-driven engineering workflow while keeping architecture, review, validation, and release decisions under human ownership. See [AI use in MAKO](AI_USE.md) for the full approach.
