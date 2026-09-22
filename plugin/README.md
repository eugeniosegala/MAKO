# MAKO Decky

<p align="center">
  <img src="assets/mako-logo.webp" width="256" alt="MAKO Decky logo" />
</p>

<!-- prettier-ignore -->
> [!NOTE]
> **MAKO Decky succeeds <a href="https://github.com/eugeniosegala/decky-lsfg-vk-experimental" target="_blank" rel="noopener noreferrer">Decky LSFG-VK Experimental</a> under a separate product and package identity.** The <a href="https://github.com/eugeniosegala/MAKO" target="_blank" rel="noopener noreferrer">MAKO repository</a> continues its development lineage, but MAKO Decky imports state only from public MAKO 2.0.0 or newer; install it separately from the differently named predecessor.

MAKO Decky is the Decky Loader component of MAKO. It provides per-game controls, installation, updates, Flatpak preparation, and game launch integration for MAKO Renderer on Steam Deck, Steam Machine, SteamOS, and Linux more broadly.

MAKO is an independent community project bringing LSFG frame generation, spatial scaling, and bundled shader effects to Linux. MAKO Decky does not contain or distribute Lossless Scaling, `Lossless.dll`, or extracted proprietary model payloads. LSFG and LS1 read selected resources at runtime from a lawful, user-supplied <a href="https://store.steampowered.com/app/993090/Lossless_Scaling/" target="_blank" rel="noopener noreferrer">Lossless Scaling</a> installation; the open MAKO Scaler and bundled shaders do not require it. MAKO does not alter the user's DLL file, and translated resources remain process-local. Users are responsible for complying with the terms applicable to their copy. See <a href="../THIRD_PARTY_NOTICES.md" target="_blank" rel="noopener noreferrer">Third-party notices</a>.

## Download

For frame generation or LS1 scaling, first install the **default public version** of <a href="https://store.steampowered.com/app/993090/Lossless_Scaling/" target="_blank" rel="noopener noreferrer">Lossless Scaling</a> through Steam. MAKO can use beta branches, but they are not validated; the default public branch is recommended. The open MAKO Scaler works without `Lossless.dll`.

Open the <a href="https://github.com/eugeniosegala/MAKO/releases/latest" target="_blank" rel="noopener noreferrer">latest MAKO Decky release</a> and download the ZIP under **Assets**. Previous Decky releases are available on the <a href="https://github.com/eugeniosegala/MAKO/releases" target="_blank" rel="noopener noreferrer">MAKO releases page</a>.

For direct Vulkan-layer installation without Decky, open the <a href="https://github.com/eugeniosegala/MAKO/releases/tag/render-v3.3.0" target="_blank" rel="noopener noreferrer">latest MAKO Renderer release</a> and download the Linux archive under **Assets**.

Published MAKO Renderer packages target x86_64 Linux hosts, with 64-bit and 32-bit x86 game-process layers. MAKO Decky safely refuses incompatible native AArch64/Armada installation; see <a href="docs/ARMADA.md" target="_blank" rel="noopener noreferrer">Armada and native AArch64 support</a> for that boundary.

## What it manages

- Installs and updates the per-user MAKO Renderer Vulkan layer and common `mako-run` wrapper.
- Saves per-game and per-process profiles, then selects them automatically by Steam application ID or process name.
- Groups Fixed and Adaptive Frame Generation, Spatial Scaling, Shaders, performance, compatibility, external-tool, and manual controls. **Live Status** reports the active mode, scaler, resolutions, limits, fallbacks, and pending changes for the running game.
- Provides HDR Brightness Boost with a per-profile 203–1000-nit target for supported Gamescope HDR displays, keeping the game and MAKO in SDR while Gamescope maps the final image through the display's HDR output.
- Provides a per-profile Gamescope WSI compatibility option, host-installed MangoHud, and MAKO's private pinned 64-bit/32-bit vkBasalt build, including live per-game sharpening, anti-aliasing, and lightweight shader presets. Scaling uses the combined Renderer by default; the independent WSI option selects the managed compatibility path inside a supported Gamescope session.
- Prepares matching Vulkan runtime extensions and application access for supported Flatpak workflows.
- Shares one active native Renderer version with the standalone archive installer. Installing either version selects it for both launch workflows; a later MAKO Decky installation adopts a valid standalone Renderer and offers its bundled update when the versions differ.
- Removes files supplied by either managed native Renderer installer when you select **Uninstall MAKO Renderer**, while preserving MAKO Decky and its profiles. Uninstalling MAKO Decky also removes the managed native Renderer; shared Flatpak runtime extensions remain installed.

Close games using MAKO before installing or updating the Renderer. Installation preserves valid profiles. If the saved configuration cannot be read or validated, installation resets it and its profiles to defaults. Read-only configurations stop installation. If installation fails, MAKO attempts to restore the previous installation and configuration and reports any recovery problems.

## Install and use

Follow the [installation guide](../README.md#install-and-use) to install Decky Loader and the MAKO Decky ZIP. Then open MAKO Decky and select **Install MAKO Renderer**; installing the ZIP alone does not install its bundled Renderer. For a native Steam or Proton game, add this under **Steam Properties > Launch Options**:

```text
/home/deck/.local/bin/mako-run %command%
```

Start the game normally. MAKO automatically selects a matching saved profile, or uses the Default profile when no match exists.

For Heroic, Lutris, EmuDeck, and other Flatpak applications, follow the [launcher setup guide](docs/LAUNCHERS.md).

When updating, follow the [update guide](../README.md#updating-mako-decky) to replace MAKO Decky, its bundled Renderer, and any prepared Flatpak extensions.

## Panel display

Press **R1** or select **Hide info** to hide explanations and optional information while keeping settings and actions available. The Lossless Scaling and MAKO Renderer installation status card stays visible, as does **Live Status** while a game runs, along with any Lossless Scaling model warning and its update action. The version number and release codename also stay visible. Press **R1** again or select **Show info** to restore the information. MAKO Decky remembers your display preference without changing game profiles or which settings sections you have collapsed.

See the [configuration guide](docs/CONFIGURATION.md) for settings and profiles, [troubleshooting](docs/TROUBLESHOOTING.md) for common problems, and [Collect MAKO Decky Diagnostics](docs/COLLECT_DIAGNOSTICS.md) to create a report when you need help.

## Development

To build MAKO Decky from source or create a local test ZIP, follow the [packaging guide](docs/PACKAGING.md). Contributors should also follow the [testing guide](../TESTING.md).
