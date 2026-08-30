# MAKO - Scaling and Frame Generation on SteamOS/Linux

<p align="center">
  <img src="plugin/assets/mako-logo.webp" width="256" alt="MAKO logo" />
</p>

<p align="center">
  <a href="https://trendshift.io/repositories/171701?utm_source=trendshift-badge&amp;utm_medium=badge&amp;utm_campaign=badge-trendshift-171701" target="_blank" rel="noopener noreferrer"><img src="https://trendshift.io/api/badge/trendshift/repositories/171701/daily?language=C%2B%2B" alt="eugeniosegala/MAKO | Trendshift" width="250" height="55" /></a>
</p>

<p align="center">
  <a href="https://discord.gg/NAVkyCq7Rc" target="_blank" rel="noopener noreferrer"><img src="https://img.shields.io/badge/Discord-join-5865F2?style=flat-square&amp;logo=discord&amp;logoColor=white" alt="Join the MAKO Discord community" /></a>
  <a href="https://github.com/eugeniosegala/MAKO/actions/workflows/tests.yml" target="_blank" rel="noopener noreferrer"><img src="https://img.shields.io/github/actions/workflow/status/eugeniosegala/MAKO/tests.yml?branch=main&amp;style=flat-square&amp;label=tests" alt="Tests status" /></a>
  <a href="LICENSE.md" target="_blank" rel="noopener noreferrer"><img src="https://img.shields.io/badge/license-GPL--3.0--or--later-0f766e?style=flat-square" alt="GPL-3.0-or-later license" /></a>
  <br />
  <a href="https://github.com/eugeniosegala/MAKO/releases/latest" target="_blank" rel="noopener noreferrer"><img src="https://img.shields.io/github/v/release/eugeniosegala/MAKO?filter=plugin-%2A&amp;display_name=tag&amp;sort=semver&amp;style=flat-square&amp;label=Decky&amp;color=1d4ed8" alt="Latest MAKO Decky release" /></a>
  <a href="https://github.com/eugeniosegala/MAKO/releases?q=render-v" target="_blank" rel="noopener noreferrer"><img src="https://img.shields.io/github/v/release/eugeniosegala/MAKO?filter=render-%2A&amp;display_name=tag&amp;sort=semver&amp;style=flat-square&amp;label=Renderer&amp;color=1d4ed8" alt="Latest MAKO Renderer release" /></a>
  <img src="https://img.shields.io/badge/platform-SteamOS%20%7C%20Linux-6b8e23?style=flat-square" alt="SteamOS and Linux" />
</p>

<!-- prettier-ignore -->
> [!IMPORTANT]
> **<a href="https://github.com/eugeniosegala/decky-lsfg-vk-experimental" target="_blank" rel="noopener noreferrer">Decky LSFG-VK Experimental</a> and <a href="https://github.com/eugeniosegala/lsfg-vk-experimental" target="_blank" rel="noopener noreferrer">LSFG-VK Experimental</a> are now MAKO.** This repository is their new home and continuation. Future development, releases, documentation, and issue tracking happen here.

> **Independent project:** MAKO is not an official Lossless Scaling, Decky Loader, or lsfg-vk release. MAKO does not contain or distribute Lossless Scaling, `Lossless.dll`, or extracted proprietary model payloads. LS1 scaling and LSFG frame generation read selected resources at runtime from a lawful, user-supplied <a href="https://store.steampowered.com/app/993090/Lossless_Scaling/" target="_blank" rel="noopener noreferrer">Lossless Scaling</a> installation; the open MAKO scaler does not require it. MAKO does not alter the user's DLL file, and translated resources remain process-local. Users are responsible for complying with the terms applicable to their copy. See <a href="THIRD_PARTY_NOTICES.md" target="_blank" rel="noopener noreferrer">Third-party notices</a>.

## Downloads

| Component | Recommended for | Releases |
| --- | --- | --- |
| **MAKO Decky** | Steam Deck, Steam Machine, and Decky Loader users (comes with MAKO Renderer pre-installed) | <a href="https://github.com/eugeniosegala/MAKO/releases/latest" target="_blank" rel="noopener noreferrer">Latest MAKO Decky release (ZIP under Assets)</a> |
| **MAKO Renderer** | Direct Vulkan-layer installation without Decky | <a href="https://github.com/eugeniosegala/MAKO/releases/tag/render-v2.2.0" target="_blank" rel="noopener noreferrer">Latest MAKO Renderer release (Linux archive under Assets)</a> |

## Community

Join the official <a href="https://discord.gg/NAVkyCq7Rc" target="_blank" rel="noopener noreferrer">MAKO Discord</a> for discussion, testing, development, showcases, and live troubleshooting. GitHub remains the source of truth for <a href="https://github.com/eugeniosegala/MAKO/issues/new/choose" target="_blank" rel="noopener noreferrer">bug reports and feature requests</a>.

<!-- prettier-ignore -->
> [!TIP]
> **Want update alerts?** MAKO Decky and MAKO Renderer are published independently. At the top-right of the <a href="https://github.com/eugeniosegala/MAKO" target="_blank" rel="noopener noreferrer">MAKO GitHub repository</a> page, click **Watch** > **Custom**, select **Releases**, then click **Apply**. GitHub will notify you when a new release is published, subject to your GitHub notification settings.

Published Renderer packages currently target x86_64 Linux hosts and include layers for both 64-bit and 32-bit x86 game processes. Native AArch64/Armada packages are not included in this release.

## ✨ Highlights

|  | Highlight | What it brings |
| :-: | --- | --- |
| 🖼️ | **Full-quality frame generation** | Uses the Lossless Scaling frame-generation models from the user's licensed installation, with quality and performance controls per profile. |
| 👻 | **Significantly reduced ghosting** | The full-quality v2 model with Lighter FG Model off can show noticeably less ghosting than the older layer. Supported AMD GPUs also gain extra protection against ghosting and corrupted moving edges. Results remain game-dependent. |
| 🔍 | **LS1 + open spatial scaling** | Reconstructs a lower-resolution game frame with LS1 Quality, LS1 Performance, or the open single-pass MAKO method, independently or before Fixed or Adaptive generation. |
| 🎯 | **Adaptive Frame Generation** | Optionally targets 30–240 FPS while MAKO Renderer varies generated frames up to a selected 2x–5x ceiling. |
| 🌈 | **HDR foundation** | MAKO Renderer includes HDR10/PQ and linear-scRGB groundwork. MAKO Decky keeps HDR exposure disabled while activation, presentation, colour, and performance are validated across games. |
| 🧩 | **64-bit and 32-bit x86 Vulkan** | Ships architecture-matched host and Flatpak layers so Vulkan can select the correct library for each game process. |
| 🛡️ | **Gamescope recovery** | Bounded presentation recovery preserves native presentation and resumes generation only after the game cadence becomes stable again. |
| ⏯️ | **Live frame-generation switch** | Turns frame generation on or off without discarding the selected Fixed or Adaptive settings. |
| 🗂️ | **Dedicated game/process profiles** | Capture a running game once and keep its renderer and compatibility settings. MAKO automatically selects it by Steam app ID or process, with isolated per-profile controls including ALSA audio. |
| 🎮 | **Heroic and EmuDeck integration** | Gives Heroic games a per-game wrapper and prepares Flatpak emulators for Steam-shortcut profile selection, using the same private configuration and engine as native Steam games. |

## What MAKO is

MAKO (**Motion-Adaptive Kernel Orchestration**) brings LS1 spatial scaling, MAKO's open spatial scaler, and LSFG frame generation to Linux gaming. Scaling can run alone or reconstruct real frames before Fixed or Adaptive Frame Generation.

The project consists of two closely integrated components:

- **MAKO Decky** is the Decky Loader component, providing per-game controls, installation, updates, Flatpak preparation, and game launch integration.
- **MAKO Renderer** is the Vulkan layer that provides the graphics pipeline for spatial scaling and frame generation.

## 🎮 In-game considerations

<!-- prettier-ignore -->
> [!TIP]
> **Try the game's V-Sync setting both on and off.** It can make frame delivery feel steadier, but it may also add input lag or clash with the game's FPS cap, VRR, or compositor. Compare both options and keep the one that feels smoother and more responsive.

Every game, renderer, and display setup behaves differently. Compare Fixed and Adaptive Frame Generation one setting at a time. For most games, fullscreen is the best starting point for performance and frame pacing. Restart after major display or model changes, and keep the configuration that works best for that game.

## Install and use

1. **Install Decky Loader** if needed. Switch to Desktop Mode and follow the <a href="https://github.com/SteamDeckHomebrew/decky-loader#-installation" target="_blank" rel="noopener noreferrer">official Decky Loader installation guide</a>, then return to Game Mode.
2. **Install <a href="https://store.steampowered.com/app/993090/Lossless_Scaling/" target="_blank" rel="noopener noreferrer">Lossless Scaling</a> from Steam if you will use LS1 scaling or frame generation.** MAKO reads its licensed `Lossless.dll`; the open MAKO scaler works without it.
3. Open the <a href="https://github.com/eugeniosegala/MAKO/releases/latest" target="_blank" rel="noopener noreferrer">latest MAKO Decky release</a> and download its ZIP under **Assets**.
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

MAKO Decky provides the per-profile **Gamescope WSI** option and host-installed MangoHud or vkBasalt integrations. Scaling requires and locks its validated managed WSI path; independently enabling WSI for an FG-only profile remains limited to supported 64-bit host launches, while the vkBasalt path remains experimental. See <a href="engine/docs/LAYER-CHAINING.md" target="_blank" rel="noopener noreferrer">optional graphics integrations</a> for ordering, limits, and verification.

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

Steam shortcuts choose the game profile, but Flatpak preparation is app-wide. Disable it in **Flatpak Setup** to keep MAKO unavailable to an emulator.

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

For a step-by-step guide that creates a shareable report on the Desktop, see <a href="plugin/docs/COLLECT_DIAGNOSTICS.md" target="_blank" rel="noopener noreferrer">Collect MAKO Decky Diagnostics</a>.

### Updating MAKO Decky

The clean update path avoids Decky retaining an older backend or bundled payload:

1. Quit games using `/home/deck/.local/bin/mako-run`.
2. Uninstall MAKO Decky, then install the newer ZIP through **Developer > Install Plugin from Zip**.
3. Restart your Steam Deck or Steam Machine.
4. Open MAKO Decky and select **Install MAKO Renderer** to install the native renderer bundled in the ZIP.
5. If you use Heroic or EmuDeck Flatpak emulators, open **Flatpak Setup** and select **Update** for each prepared application's matching runtime extension shown by MAKO.

Profiles and Steam launch options are retained. The private native engine and launcher are recreated in step 4; shared Flatpak extensions are retained and then refreshed in step 5.

<!-- prettier-ignore -->
> [!IMPORTANT]
> **Preferred clean update:** To prevent Decky retaining a previous plugin backend or bundled payload, especially when moving between local test ZIPs, uninstall **MAKO Decky**, install the newer ZIP, restart your Steam Deck or Steam Machine, then open it and select **Install MAKO Renderer**.

## Use MAKO Renderer directly

Decky is optional. Desktop Linux users can install the published MAKO Renderer archive directly:

1. To use LS1 scaling or frame generation, purchase and install <a href="https://store.steampowered.com/app/993090/Lossless_Scaling/" target="_blank" rel="noopener noreferrer">Lossless Scaling</a> through Steam. The open MAKO scaler does not use `Lossless.dll`.
2. Download and extract `MAKO-Renderer-v<version>-linux.tar.xz` from the <a href="https://github.com/eugeniosegala/MAKO/releases/tag/render-v2.2.0" target="_blank" rel="noopener noreferrer">latest MAKO Renderer release</a>, then double-click **Install MAKO Renderer** and choose **Execute** if your file manager asks. The installer opens **MAKO Renderer Configuration** when it finishes. To reopen it later, switch to Desktop Mode, open the bottom-left Application Launcher, search for **MAKO Renderer Configuration**, and click the MAKO-logo app icon. You can also run `~/.local/bin/mako-ui`.
3. Run the installer again after extracting a newer archive. For manual installation, configuration, Flatpak setup, and troubleshooting, see the <a href="engine/README.md#direct-linux-installation" target="_blank" rel="noopener noreferrer">MAKO Renderer guide</a>.

<!-- prettier-ignore -->
> [!IMPORTANT]
> This installer is for the standalone MAKO Renderer only. It warns and asks for confirmation when MAKO Decky is installed because the current Decky and standalone flows share Renderer, Vulkan-manifest, and configuration paths. Continuing may leave MAKO Decky incompatible until you reinstall its matching Renderer.

## Documentation

- <a href="plugin/docs/CONFIGURATION.md" target="_blank" rel="noopener noreferrer">Configuration guide</a>: Scaling, Fixed and Adaptive modes, quality and performance settings, profiles, and compatibility options.
- <a href="engine/docs/SCALING.md" target="_blank" rel="noopener noreferrer">Spatial scaling architecture</a>: pipeline order, surface ownership, formats, performance model, controls, and validation matrix.
- <a href="plugin/docs/TROUBLESHOOTING.md" target="_blank" rel="noopener noreferrer">Troubleshooting</a>: Gamescope recovery, HDR compatibility, and diagnostic logs.
- <a href="COLLECT_DIAGNOSTICS.md" target="_blank" rel="noopener noreferrer">Collect MAKO Diagnostics</a>: choose the MAKO Decky or standalone renderer collection workflow and submit one shared report.
- <a href="plugin/docs/PACKAGING.md" target="_blank" rel="noopener noreferrer">Local packaging and publishing</a>: build a ZIP for a Steam machine or publish a release.
- <a href="HOW_TO_RELEASE.md" target="_blank" rel="noopener noreferrer">Release process</a>: publish both components end to end with one versioned command.
- <a href="TESTING.md" target="_blank" rel="noopener noreferrer">Testing</a>: pull-request gates, sanitizer coverage, SteamOS hardware validation, and the runtime compatibility boundary.
- <a href="https://github.com/eugeniosegala/MAKO-Gym" target="_blank" rel="noopener noreferrer">MAKO Gym</a>: private real-hardware Vulkan, Gamescope, scaling, and frame-generation QA scenarios used by the SteamOS release gate.
- <a href="engine/README.md" target="_blank" rel="noopener noreferrer">MAKO Renderer documentation</a>: engine identity, source builds, configuration, and direct use.

## Featured in

Community creators have covered and tested the project on Steam Deck hardware. See <a href="plugin/docs/FEATURED_IN.md" target="_blank" rel="noopener noreferrer">Featured In</a> for video links, channels, and coverage details.

## Credits and project lineage

MAKO is built on the work of two open-source projects and their communities:

- **<a href="https://github.com/xXJSONDeruloXx/decky-lsfg-vk" target="_blank" rel="noopener noreferrer">Kurt Himebauch / xXJSONDeruloXx</a>** created the original Decky LSFG-VK plugin that formed the foundation of MAKO's Decky interface, installation workflow, and per-game controls.
- **<a href="https://github.com/PancakeTAS/lsfg-vk" target="_blank" rel="noopener noreferrer">PancakeTAS</a>** and the **lsfg-vk contributors** created the Vulkan layer and Linux integration from which MAKO Renderer descends. MAKO's lineage is based on an earlier MIT-licensed revision, before upstream adopted GPLv3 for later development.

MAKO also thanks the **Lossless Scaling developers** for the LS1 and LSFG models accessed through each user's licensed installation, the **Wine/vkd3d developers** whose shader translator enables the Vulkan LS1 path, and the **Decky Loader team**, community contributors, testers, guide authors, and creators who helped make the project possible. MAKO's open spatial scaler is independently implemented in this repository.

The original copyright and license notices are preserved in <a href="LICENSE.md" target="_blank" rel="noopener noreferrer">LICENSE.md</a>. MAKO is an independent community project and is not affiliated with or endorsed by Lossless Scaling, Decky Loader, or either upstream project.

## License

MAKO is distributed under <a href="LICENSE.md" target="_blank" rel="noopener noreferrer">GPL-3.0-or-later</a>. Required upstream notices and the user-supplied proprietary-component boundary are recorded in <a href="THIRD_PARTY_NOTICES.md" target="_blank" rel="noopener noreferrer">Third-party notices</a>, and visual sources are recorded in <a href="ASSET_PROVENANCE.md" target="_blank" rel="noopener noreferrer">Asset provenance</a>. Contributions are accepted under the policy in <a href="CONTRIBUTING.md" target="_blank" rel="noopener noreferrer">CONTRIBUTING.md</a>.

## AI-assisted development

MAKO uses coding agents as part of an evidence-driven engineering workflow while keeping architecture, review, validation, and release decisions under human ownership. See <a href="AI_USE.md" target="_blank" rel="noopener noreferrer">AI use in MAKO</a> for the full approach.
