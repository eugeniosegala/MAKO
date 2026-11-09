# MAKO Decky

<p align="center">
  <img src="assets/mako-logo.webp" width="256" alt="MAKO Decky logo" />
</p>

MAKO Decky is the Decky Loader component of MAKO. It provides per-game controls, installation, updates, Flatpak preparation, and game launch integration for MAKO Renderer on Steam Deck, Steam Machine, SteamOS, and Linux more broadly. **MAKO FG, MAKO Scaler, and Shaders work without Lossless Scaling.** Only the optional Lossless Scaling frame generation model and LS1 scaling require it.

MAKO is an independent community project bringing frame generation, spatial scaling, and bundled shader effects to Linux. MAKO Decky does not contain or distribute Lossless Scaling, `Lossless.dll`, or extracted proprietary model payloads. The optional Lossless Scaling frame generation model (LSFG) and LS1 scaling read selected resources at runtime from a lawful, user-supplied <a href="https://store.steampowered.com/app/993090/Lossless_Scaling/" target="_blank" rel="noopener noreferrer">Lossless Scaling</a> installation; MAKO FG, the open MAKO Scaler, and bundled shaders do not require it. MAKO does not alter the user's DLL file, and translated resources remain process-local. Users are responsible for complying with the terms applicable to their copy. See <a href="../THIRD_PARTY_NOTICES.md" target="_blank" rel="noopener noreferrer">Third-party notices</a>.

## Download

To use the optional Lossless Scaling frame generation model or LS1 scaling, first install the **default public version** of <a href="https://store.steampowered.com/app/993090/Lossless_Scaling/" target="_blank" rel="noopener noreferrer">Lossless Scaling</a> through Steam. Beta branches are not validated.

Open the <a href="https://github.com/eugeniosegala/MAKO/releases/latest" target="_blank" rel="noopener noreferrer">latest MAKO Decky release</a> and download the ZIP under **Assets**. Previous Decky releases are available on the <a href="https://github.com/eugeniosegala/MAKO/releases" target="_blank" rel="noopener noreferrer">MAKO releases page</a>.

For direct Vulkan-layer installation without Decky, open the <a href="https://github.com/eugeniosegala/MAKO/releases/tag/render-v4.6.0" target="_blank" rel="noopener noreferrer">latest MAKO Renderer release</a> and download the Linux archive under **Assets**.

Published MAKO Renderer packages target x86_64 Linux hosts, with 64-bit and 32-bit x86 game-process layers. MAKO Decky safely refuses incompatible native AArch64/Armada installation; see <a href="docs/ARMADA.md" target="_blank" rel="noopener noreferrer">Armada and native AArch64 support</a> for that boundary.

## What it manages

- Installs and updates the per-user MAKO Renderer Vulkan layer and common `mako-run` wrapper.
- Saves per-game and per-process profiles, then selects them automatically by Steam application ID or process name.
- Offers Frame Generation, Scaling, Shaders, performance, and compatibility controls. **Live Status** shows active features and pending changes.
- Bundles vkBasalt shader effects and supports optional host-installed MangoHud and Gamescope WSI compatibility. See [graphics integrations](../engine/docs/LAYER-CHAINING.md) for limits.
- Offers an opt-in [native Steam Remote Play override](docs/CONFIGURATION.md#native-steam-remote-play) using ordinary saved profiles and verified original-client restoration.
- Prepares matching Vulkan runtime extensions and application access for supported Flatpak workflows.
- Shares the native Renderer with the standalone installer. **Uninstall MAKO Renderer** removes it but keeps MAKO Decky and profiles. Removing or reinstalling MAKO Decky preserves the native Renderer and its self-contained launchers, native Remote Play override, and profiles. The desktop **Uninstall MAKO Renderer** entry or `~/.local/bin/mako-installer --uninstall` removes Renderer after Decky has been removed. Flatpak extensions remain installed separately.

Close games using MAKO before installing or updating the Renderer. Installation preserves valid profiles. If the saved configuration cannot be read or validated, installation resets it and its profiles to defaults. Read-only configurations stop installation. If installation fails, MAKO attempts to restore the previous installation and configuration and reports any recovery problems.

## Install and use

Follow the [installation guide](../README.md#install-and-use) to install Decky Loader and the MAKO Decky ZIP. Then open MAKO Decky and select **Install MAKO Renderer**; installing the ZIP alone does not install its bundled Renderer. For a native Steam or Proton game, add this under **Steam Properties > Launch Options**:

```text
/home/deck/.local/bin/mako-run %command%
```

Start the game normally. MAKO automatically selects a matching saved profile, or uses the Default profile when no match exists.

For Heroic, Lutris, EmuDeck, Flatpak applications, and [other non-Steam games](docs/LAUNCHERS.md#other-non-steam-games), follow the [launcher setup guide](docs/LAUNCHERS.md).

Follow the [update guide](../README.md#updating-mako-decky) to update MAKO Decky, its bundled Renderer, and installed Flatpak extensions. After replacing the ZIP, apply the Renderer update offered in Decky, even when the version number is unchanged.

If you installed Renderer with pacman, Decky's **Install MAKO Renderer** creates a separate user-local copy. Pacman keeps ownership of its package; keep Renderer versions aligned when they share profiles.

## Panel display

MAKO Decky follows Steam's interface language and falls back to English for unsupported languages.

Press **R1** or select **Hide info** to hide explanations and Experimental tags while keeping controls, status, warnings, recovery errors, and version information visible. Press **R1** again or select **Show info** to restore them. MAKO Decky remembers this preference independently of game profiles and collapsed settings sections.

See the [configuration guide](docs/CONFIGURATION.md) for settings and profiles, [troubleshooting](docs/TROUBLESHOOTING.md) for common problems, and [Collect MAKO Decky Diagnostics](docs/COLLECT_DIAGNOSTICS.md) to create a report when you need help.

## Development

To build MAKO Decky from source or create a local test ZIP, follow the [packaging guide](docs/PACKAGING.md). Contributors should also follow the [testing guide](../TESTING.md).

The [frontend code map](docs/FRONTEND-ARCHITECTURE.md) identifies the owners of profile state, configuration writes, settings views, and the shader effect selector. The [backend code map](docs/BACKEND-ARCHITECTURE.md) identifies the RPC, profile, wrapper, installation, Flatpak, and runtime-status boundaries.

## MAKO FG

MAKO FG is bundled with MAKO Renderer. Choose MAKO FG or an optional Lossless Scaling model per game, then restart the game. See [MAKO FG setup](../engine/docs/MAKO-FG.md) for model details and limitations.
