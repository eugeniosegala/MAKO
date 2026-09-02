# MAKO Decky packaging

Run plugin commands from `plugin/`; run the hardware gate and publisher from the repository root.

| Need | Command | Result |
| --- | --- | --- |
| Frontend/backend/Renderer iteration | `pnpm run dev:frontend`, `dev:backend`, `dev:engine`, or another `dev:*` scope | Mutates only the local development installation; no ZIP |
| Fast native tester ZIP | `pnpm run package:local-engine-fast` | Verified native 64-bit package without 32-bit or Flatpak payloads; never publish |
| Complete tester ZIP | `pnpm run package:local-engine` | Self-contained native 64/32-bit and Flatpak package |
| Release candidate | `cd .. && ./scripts/run-steamos-hardware-validation.sh --gym-suite <affected-suite> --gym-reason '<why>' --deploy-to-decky` | Clean SteamOS/AMD rebuild, selected hardware validation, retained ZIP, optional exact-package deployment |
| Release | `cd .. && ./scripts/publish-release.sh X.Y.Z` | Renderer publication and checksum pin followed by Decky publication |

Local and retained CI ZIPs are not published releases. After publishing, download and install the public Decky asset once. [Testing](../../TESTING.md) defines the evidence for each cycle; [How to release MAKO](../../HOW_TO_RELEASE.md) owns publication.

## Build a tester ZIP

Install dependencies once, then build the sibling `../engine` and create a complete self-contained ZIP:

```bash
pnpm install --frozen-lockfile
pnpm run package:local-engine
```

`package:local` is an exact alias for `package:local-engine`. Use `pnpm run package:local-engine-fast` only for a native 64-bit focused package; it omits 32-bit and Flatpak payloads and cannot become a release candidate.

The complete command writes under `out/`, regenerates bindings, builds the frontend, creates the native host and Flatpak Renderer payloads, verifies them, and packages the result without tagging or publishing. For another Renderer checkout or output path:

```bash
scripts/package-local.sh --local-engine-repo /path/to/MAKO/engine
pnpm run package:local -- /path/to/MAKO-Decky.zip
```

To package already-downloaded assets, both filenames and checksums must match the pin in `package.json`:

```bash
scripts/package-local.sh \
  --engine-archive /path/to/MAKO-Renderer-v<version>-linux.tar.xz \
  --flatpak-archive /path/to/MAKO-Renderer-v<version>-flatpaks.tar.xz \
  /path/to/MAKO-Decky-local-test.zip
```

Local packages embed the Renderer archive under `bundled_renderer` and omit Decky's `remote_binary`, preventing a local ZIP from silently downloading another build. The verifier rejects missing payloads, checksum mismatches, unsafe URLs, inconsistent metadata, missing project or dependency notices, and a missing or incomplete frontend source map. Every ZIP carries the root license, third-party notices, asset-provenance record, applicable bundled-dependency license texts, and the bundled Decky dependency source needed to audit the generated frontend. The stable install slug remains `Mako/`, and the immutable Decky listing identity remains **MAKO - Frame Generation** so new packages replace existing installations.

Artifacts are keyed by the Renderer commit and dirty-worktree fingerprint, allowing UI-only rebuilds to reuse a verified engine. The generated package records source identity, checksums, host ISA, and Vulkan process bitness. `host_architectures` currently declares x86_64; `architectures` declares 64/32-bit game-process layers. These are different contracts. Incompatible AArch64/Armada hosts fail closed as documented in [Armada support](ARMADA.md).

Local release-candidate identity comes from the current component release notes without changing the tracked release pin. Package tests remain necessary even though unit tests characterize wrapper and sidecar output: unit tests do not prove archive layout, manifest activation, permissions, or embedded checksums.

## Direct SteamOS iteration

Install a package and select **Install MAKO Renderer** once, then use the narrowest scope:

```bash
pnpm run dev:frontend  # TypeScript/React
pnpm run dev:backend   # Python/backend
pnpm run dev:engine    # Native 64-bit Renderer
pnpm run dev:all       # Frontend, backend, native 64-bit Renderer
pnpm run dev:host      # Decky plus native 64-bit and 32-bit Renderer
pnpm run dev:flatpaks  # Decky plus all supported Flatpak bundles
pnpm run dev:e2e       # Decky, both host layers, and Flatpak bundles
```

These commands deploy to `~/homebrew/plugins/Mako` and tell you when to reload. Quit games before replacing the Renderer. `dev:engine` and `dev:all` intentionally omit package verification, CLI/UI archives, 32-bit, and Flatpak unless their scope says otherwise. Use `dev:host` for 32-bit processes, `dev:flatpaks` for sandbox work, and `dev:e2e` before a complete local regression pass.

Flatpak development commands place verified bundles in the installed plugin; use **Flatpak Setup > Update** to install one into an application. The supported runtime list is owned by `shared_config.py` and cross-checked against the Renderer matrix. SteamOS host builds require `lib32-glibc`; Flatpak builds also require `flatpak-builder`. See the [source-build guide](../../engine/docs/BUILDING-FROM-SOURCE.md).

Set `DECKY_PLUGIN_DIR` for another installed path or pass `--engine-repo <path>` to `scripts/deploy-dev.sh`. Every direct deployment updates the plugin's development status box with source identity, scopes, and available artifact hashes. If the installed Decky manifest is protected, deployment leaves it unchanged and prints a warning; reinstall a verified ZIP to apply listing-name or manifest changes.

## Build cache

Reusable compiler, SDK, dependency, and Flatpak data lives under `engine/build/cache`; staging lives under `engine/build/work`. Override both with `MAKO_BUILD_CACHE_ROOT` and `MAKO_BUILD_WORK_ROOT`.

Inspect cache use without deleting anything:

```bash
pnpm run dev:prune-flatpak-cache
pnpm run dev:prune-build-cache
```

Add `-- --confirm` only when intentionally removing the corresponding repository-local cache and work directories. This never removes the installed plugin, installed Flatpak extensions, or normal user Flatpak data.

## Publish

Use [How to release MAKO](../../HOW_TO_RELEASE.md). Update and commit both component release-note files, pass the SteamOS hardware gate and applicable game matrix, then run:

```bash
./scripts/publish-release.sh 1.2.0
```

The resumable workflow publishes Renderer first, records immutable checksums, then builds and publishes the matching Decky package. It refuses dirty state, the wrong branch, stale/local pins, mismatched versions, or reused tags pointing to different code.
