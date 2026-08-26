# MAKO Decky

<p align="center">
  <img src="assets/mako-logo.webp" width="256" alt="MAKO Decky logo" />
</p>

<!-- prettier-ignore -->
> [!NOTE]
> **[Decky LSFG-VK Experimental](https://github.com/eugeniosegala/decky-lsfg-vk-experimental) is now MAKO Decky.** The [MAKO repository](https://github.com/eugeniosegala/MAKO) is its new home and continuation, including future development, releases, documentation, and issue tracking.

MAKO Decky is the Decky Loader component of MAKO. It provides per-game controls, installation, updates, Flatpak preparation, and game launch integration for MAKO Renderer on Steam Deck, Steam Machine, SteamOS, and Linux more broadly.

MAKO is an independent community project bringing Lossless Scaling LS1 scaling and LSFG frame generation plus MAKO's built-in open spatial scaler to Linux. LS1 and LSFG require a user-supplied `Lossless.dll` from a licensed [Lossless Scaling](https://store.steampowered.com/app/993090/Lossless_Scaling/) installation; the open MAKO scaling method does not use it. MAKO Decky does not bundle, copy, persist, or modify that proprietary library.

## Download

Open the [latest MAKO Decky release](https://github.com/eugeniosegala/MAKO/releases/latest) and download the ZIP under **Assets**. Previous Decky releases are available on the [MAKO releases page](https://github.com/eugeniosegala/MAKO/releases).

For direct Vulkan-layer installation without Decky, open the [latest MAKO Renderer release](https://github.com/eugeniosegala/MAKO/releases/tag/render-v2.2.0) and download the Linux archive under **Assets**.

Published MAKO Renderer packages currently target x86_64 Linux hosts, with 64-bit and 32-bit x86 game-process layers. MAKO Decky detects native AArch64/Armada hosts, refuses incompatible installation, and converts older activation state to a safe game-launch passthrough. See [Armada and native AArch64 support](docs/ARMADA.md) for the exact boundary and the hardware gates required before enabling it.

## What it manages

- Installs the private MAKO Renderer Vulkan layer for the current user.
- Generates the `/home/deck/.local/bin/mako-run` per-game launcher.
- Stores renderer settings in `~/.config/mako-render/conf.toml` and versioned game/process identity separately for automatic per-game selection.
- Supports independent spatial scaling plus fixed and adaptive frame generation with per-game controls. Scaling and frame generation can be used separately or together.
- Provides a per-profile experimental Gamescope WSI compatibility toggle plus **External Tools** controls for host-installed MangoHud and experimental vkBasalt; only one optional Vulkan layer can be selected.
- Prepares matching Vulkan runtime extensions for selected Flatpak applications.
- Launches selected games through MAKO's private renderer and configuration.

## Development

MAKO Decky lives in the `plugin/` directory of the MAKO monorepo and consumes the sibling `engine/` source tree.

```bash
pnpm install --frozen-lockfile
pnpm run test
pnpm run build
pnpm run package:local-engine
```

`pnpm run package:local-engine` builds and bundles the sibling MAKO Renderer checkout. Use `pnpm run package:local-engine-fast` for a native, 64-bit development package without Flatpak extensions.

The resulting Decky ZIP is written under `plugin/out/`. Nothing is published by the local packaging commands.

### Code boundaries

| Responsibility | Source of truth |
| --- | --- |
| Cross-language configuration fields, defaults, limits, profile identities and kinds, install-relative wrapper path, supported Flatpak runtimes, and per-game-wrapper Flatpak app IDs | `shared_config.py`, emitted to TypeScript by `scripts/generate_ts_schema.py` |
| Pre-RPC launcher fallback derived from the generated install-relative path | `src/config/runtimePaths.ts` |
| Import-safe installed package root, payload identities, layer/environment identifiers, and Flatpak bundle descriptors | `py_modules/mako_plugin/package_paths.py`, `constants.py`, guarded across components by `tests/test_decky_loader_import.py`, `test_path_package_contract.py`, and focused contract tests |
| RPC orchestration, migrations, persistence, and atomic wrapper regeneration | `py_modules/mako_plugin/configuration.py` |
| Canonical profile metadata, Decky-only wrapper-setting sidecars, and merged profile views | `py_modules/mako_plugin/profile_storage.py` |
| Pure generated-wrapper text, compatibility guards, profile selection, and launch environment | `py_modules/mako_plugin/wrapper_generation.py` |
| Running-game/editor session synchronisation and profile transactions | `src/hooks/useProfileSession.ts`, `src/hooks/useProfileEditorModel.ts` |
| Reusable UI state for deferred Target FPS writes and collapsed sections | `src/hooks/useDeferredTargetFps.ts`, `src/hooks/usePersistentCollapseState.ts` |
| View composition | `src/components/Content.tsx`, `ContentNotices.tsx`, `ConfigurationSection.tsx`, `ConfigurationSectionGroups.tsx`, `ProfileManagement.tsx`, `ScalingControl.tsx`, `FpsMultiplierControl.tsx` |
| English translation keys, fallbacks, and dictionary order | `defaults/i18n/template.json` |
| Advertised languages, Steam aliases, translated dictionaries, static call-site validation, and generated frontend bundle | `defaults/i18n/language_metadata.json`, `steam_language_map.json`, language JSON files, `scripts/i18n-contract.mjs`, and generated `src/i18n/languages.json` |

The generated wrapper is disposable cache, not another configuration store. Backend characterization tests compare pure generator output with the service facade and lock exact wrapper and sidecar bytes; focused hook tests lock deferred writes, runtime profile transitions, offline editor selection, and local collapse-state recovery without snapshotting static layout.

MAKO Decky supports English, Brazilian Portuguese, European Portuguese, Spanish, Korean, Japanese, Ukrainian, and Simplified Chinese, matching the MAKO Renderer desktop UI's ordered supported-language inventory and native display names through `tests/test_localization_language_contract.py`. Every translated dictionary has the same ordered key set as `template.json`, contains only strings, and preserves each named placeholder exactly. Every frontend `t()` call uses a static key and English fallback matching that template plus the exact replacement fields. Run `pnpm run check:i18n` for the read-only dictionary, call-site, and generated-file gate; after an intentional source-dictionary change, run `pnpm run generate:i18n` to regenerate `src/i18n/languages.json` rather than editing it directly. The normal one-shot build requires the tracked bundle to be current before bundling, and watch mode intentionally regenerates it before Rollup starts.

Use direct `dev:*` deployment for iteration, `package:local-engine` for a complete tester ZIP, the dedicated SteamOS/AMD workflow for a release candidate, and the root publisher only for the actual release. The exact boundaries are documented in [Packaging](docs/PACKAGING.md) and [Testing](../TESTING.md).

## Using a local build

After installing the ZIP through Decky developer settings, open MAKO Decky and install MAKO Renderer. For a native Steam or Proton game, use:

```text
/home/deck/.local/bin/mako-run %command%
```

MAKO Decky's wrapper activates `VK_LAYER_MAKO_render` only for the selected game process.

See [Configuration](docs/CONFIGURATION.md), [Armada and native AArch64 support](docs/ARMADA.md), [Troubleshooting](docs/TROUBLESHOOTING.md), [Collect MAKO Decky Diagnostics](docs/COLLECT_DIAGNOSTICS.md), and [Packaging](docs/PACKAGING.md) for detailed workflows.
