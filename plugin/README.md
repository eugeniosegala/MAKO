# MAKO Decky

<p align="center">
  <img src="assets/mako-logo.webp" width="256" alt="MAKO Decky logo" />
</p>

<!-- prettier-ignore -->
> [!NOTE]
> **[Decky LSFG-VK Experimental](https://github.com/eugeniosegala/decky-lsfg-vk-experimental) is now MAKO Decky.** The [MAKO repository](https://github.com/eugeniosegala/MAKO) is its new home and continuation, including future development, releases, documentation, and issue tracking.

MAKO Decky is the Decky Loader component of MAKO. It provides per-game controls, installation, updates, Flatpak preparation, and game launch integration for MAKO Renderer on Steam Deck, Steam Machine, SteamOS, and Linux more broadly.

MAKO is an independent community project bringing LS1 scaling, LSFG frame generation, and MAKO's built-in open spatial scaler to Linux. LS1 and LSFG require a user-supplied `Lossless.dll` from a licensed [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) installation; the open MAKO scaler does not. MAKO Decky never bundles, copies, persists, or modifies that proprietary library.

## Download

Open the [latest MAKO Decky release](https://github.com/eugeniosegala/MAKO/releases/latest) and download the ZIP under **Assets**. Previous Decky releases are available on the [MAKO releases page](https://github.com/eugeniosegala/MAKO/releases).

For direct Vulkan-layer installation without Decky, open the [latest MAKO Renderer release](https://github.com/eugeniosegala/MAKO/releases/tag/render-v2.2.0) and download the Linux archive under **Assets**.

Published MAKO Renderer packages target x86_64 Linux hosts, with 64-bit and 32-bit x86 game-process layers. MAKO Decky safely refuses incompatible native AArch64/Armada installation; see [Armada and native AArch64 support](docs/ARMADA.md) for that boundary.

## What it manages

- Installs the private MAKO Renderer Vulkan layer for the current user.
- Creates a per-game `mako-run` launcher and profile-based configuration.
- Provides spatial scaling plus Fixed and Adaptive Frame Generation, separately or together. Select **Enable Scaling (Restart)** before launch; methods and sharpness remain live, while Scale Factor requests one guarded game-owned recreation on the managed Gamescope path and otherwise waits for a natural resolution change.
- Provides a per-profile Gamescope WSI compatibility toggle plus host-installed MangoHud or experimental vkBasalt under **External Tools**. Scaling requires and locks its managed WSI path; independently enabling WSI for an FG-only profile is limited to supported 64-bit host launches.
- Prepares matching Vulkan runtime extensions for selected Flatpak applications.
- Keeps MAKO activation limited to the selected game process.

## Development

MAKO Decky lives in the `plugin/` directory of the MAKO monorepo and consumes the sibling `engine/` source tree.

```bash
pnpm install --frozen-lockfile
pnpm run test
pnpm run build
pnpm run package:local-engine
```

`pnpm run package:local-engine` builds and bundles the sibling MAKO Renderer checkout. Use `pnpm run package:local-engine-fast` for a native, 64-bit development package without Flatpak extensions.

The resulting ZIP is written under `plugin/out/`; local commands never publish. Use direct `dev:*` deployment for iteration, `package:local-engine` for a tester ZIP, and the documented release workflow only for a release candidate. See [Packaging](docs/PACKAGING.md) and [Testing](../TESTING.md) for the exact commands and validation gates.

## Using a local build

After installing the ZIP through Decky developer settings, open MAKO Decky and install MAKO Renderer. For a native Steam or Proton game, use:

```text
/home/deck/.local/bin/mako-run %command%
```

MAKO Decky's wrapper activates `VK_LAYER_MAKO_render` only for the selected game process.

See [Configuration](docs/CONFIGURATION.md), [Armada and native AArch64 support](docs/ARMADA.md), [Troubleshooting](docs/TROUBLESHOOTING.md), [Collect MAKO Decky Diagnostics](docs/COLLECT_DIAGNOSTICS.md), and [Packaging](docs/PACKAGING.md) for detailed workflows.
