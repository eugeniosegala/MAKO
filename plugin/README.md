# MAKO Decky

<p align="center">
  <img src="assets/mako-logo.webp" width="256" alt="MAKO Decky logo" />
</p>

<!-- prettier-ignore -->
> [!NOTE]
> **<a href="https://github.com/eugeniosegala/decky-lsfg-vk-experimental" target="_blank" rel="noopener noreferrer">Decky LSFG-VK Experimental</a> is now MAKO Decky.** The <a href="https://github.com/eugeniosegala/MAKO" target="_blank" rel="noopener noreferrer">MAKO repository</a> is its new home and continuation, including future development, releases, documentation, and issue tracking.

MAKO Decky is the Decky Loader component of MAKO. It provides per-game controls, installation, updates, Flatpak preparation, and game launch integration for MAKO Renderer on Steam Deck, Steam Machine, SteamOS, and Linux more broadly.

MAKO is an independent community project bringing LSFG frame generation, LS1 scaling, and the built-in open MAKO Scaler to Linux. MAKO Decky does not contain or distribute Lossless Scaling, `Lossless.dll`, or extracted proprietary model payloads. LSFG and LS1 read selected resources at runtime from a lawful, user-supplied <a href="https://store.steampowered.com/app/993090/Lossless_Scaling/" target="_blank" rel="noopener noreferrer">Lossless Scaling</a> installation; the open MAKO Scaler does not require it. MAKO does not alter the user's DLL file, and translated resources remain process-local. Users are responsible for complying with the terms applicable to their copy. See <a href="../THIRD_PARTY_NOTICES.md" target="_blank" rel="noopener noreferrer">Third-party notices</a>.

## Download

Open the <a href="https://github.com/eugeniosegala/MAKO/releases/latest" target="_blank" rel="noopener noreferrer">latest MAKO Decky release</a> and download the ZIP under **Assets**. Previous Decky releases are available on the <a href="https://github.com/eugeniosegala/MAKO/releases" target="_blank" rel="noopener noreferrer">MAKO releases page</a>.

For direct Vulkan-layer installation without Decky, open the <a href="https://github.com/eugeniosegala/MAKO/releases/tag/render-v2.2.0" target="_blank" rel="noopener noreferrer">latest MAKO Renderer release</a> and download the Linux archive under **Assets**.

Published MAKO Renderer packages target x86_64 Linux hosts, with 64-bit and 32-bit x86 game-process layers. MAKO Decky safely refuses incompatible native AArch64/Armada installation; see <a href="docs/ARMADA.md" target="_blank" rel="noopener noreferrer">Armada and native AArch64 support</a> for that boundary.

## What it manages

- Installs the private MAKO Renderer Vulkan layer for the current user.
- Creates a per-game `mako-run` launcher and profile-based configuration.
- Presents Fixed and Adaptive controls under **Frame Generation**, followed by **Spatial Settings**, shared **Performance Settings**, and **Advanced Rendering Settings**, so the two features remain easy to scan without duplicated controls. Usage, compatibility, external-tool, and manual controls follow the primary feature flow. A compact **Live Status** card reports what the running game is actually using, including input/output resolution and whether upscaling runs before generated frames. Select **Enable Scaling (Restart)** before launch; methods and sharpness remain live, while Scale Factor requests one guarded game-owned recreation on the managed Gamescope path and otherwise waits for a natural resolution change.
- Provides a per-profile Gamescope WSI compatibility toggle plus host-installed MangoHud or experimental vkBasalt under **External Tools**. Scaling requires and locks its managed WSI path; independently enabling WSI for an FG-only profile is limited to supported 64-bit host launches.
- Prepares matching Vulkan runtime extensions for selected Flatpak applications.
- Keeps MAKO activation limited to the selected game process.
- Shares one active native Renderer version with the standalone archive installer. Installing either version selects it for both launch workflows; a later MAKO Decky installation adopts a valid standalone Renderer and offers its bundled update when the versions differ.
- Removes all files supplied by either managed native Renderer installer when you select **Uninstall MAKO Renderer**, while preserving MAKO Decky and profiles so the plugin can offer installation again. Uninstalling MAKO Decky also removes the managed native Renderer; shared Flatpak runtime extensions remain installed.

## Development

MAKO Decky lives in the `plugin/` directory of the MAKO monorepo and consumes the sibling `engine/` source tree.

```bash
pnpm install --frozen-lockfile
pnpm run test
pnpm run build
pnpm run package:local-engine
```

`pnpm run package:local-engine` builds and bundles the sibling MAKO Renderer checkout. Use `pnpm run package:local-engine-fast` for a native, 64-bit development package without Flatpak extensions.

The resulting ZIP is written under `plugin/out/`; local commands never publish. Use direct `dev:*` deployment for iteration, `package:local-engine` for a tester ZIP, and the documented release workflow only for a release candidate. See <a href="docs/PACKAGING.md" target="_blank" rel="noopener noreferrer">Packaging</a> and <a href="../TESTING.md" target="_blank" rel="noopener noreferrer">Testing</a> for the exact commands and validation gates.

## Using a local build

After installing the ZIP through Decky developer settings, open MAKO Decky and install MAKO Renderer. For a native Steam or Proton game, use:

```text
/home/deck/.local/bin/mako-run %command%
```

MAKO Decky's wrapper activates `VK_LAYER_MAKO_render` only for the selected game process.

See <a href="docs/CONFIGURATION.md" target="_blank" rel="noopener noreferrer">Configuration</a>, <a href="docs/ARMADA.md" target="_blank" rel="noopener noreferrer">Armada and native AArch64 support</a>, <a href="docs/TROUBLESHOOTING.md" target="_blank" rel="noopener noreferrer">Troubleshooting</a>, <a href="docs/COLLECT_DIAGNOSTICS.md" target="_blank" rel="noopener noreferrer">Collect MAKO Decky Diagnostics</a>, and <a href="docs/PACKAGING.md" target="_blank" rel="noopener noreferrer">Packaging</a> for detailed workflows.
