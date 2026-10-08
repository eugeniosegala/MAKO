# MAKO Decky packaging

Run plugin commands from `plugin/`; run the release-candidate check and publisher from the repository root.

| Need | Command | Result |
| --- | --- | --- |
| Frontend/backend/Renderer iteration | `pnpm run dev:frontend`, `dev:backend`, `dev:engine`, or another `dev:*` scope | Mutates only the local development installation; no ZIP |
| Decky-only tester ZIP | `scripts/package-local.sh --local-plugin` | Self-contained ZIP using the pinned released Renderer; no native rebuild |
| Fast native tester ZIP | `pnpm run package:local-engine-fast` | Verified native 64-bit package without 32-bit or Flatpak payloads; never publish |
| Complete tester ZIP | `pnpm run package:local-engine` | Self-contained native 64/32-bit and Flatpak package |
| Release candidate | `MAKO_PORTABLE_PACKAGE=1 pnpm run package:local-engine`, then `cd .. && ./scripts/check-release-candidate.sh PATH-TO-ZIP` | One complete portable ZIP from the clean pushed commit, checked without a second build |
| Release | `cd .. && ./scripts/publish-release.sh X.Y.Z` | Renderer publication and checksum pin followed by Decky publication |

The local candidate ZIP is not a publication input; the publisher applies release-owned metadata commits and rebuilds. MAKO Gym suites and local deployment are selected ad hoc for affected changes. After publishing, download and install the public Decky asset once. [Testing](../../TESTING.md) defines the evidence for each cycle; [How to release MAKO](../../HOW_TO_RELEASE.md) owns publication.

## Build a tester ZIP

Install dependencies once, then build the sibling `../engine` and create a complete self-contained ZIP:

```bash
pnpm install --frozen-lockfile
pnpm run package:local-engine
```

For testing intended to qualify a public release, use `MAKO_PORTABLE_PACKAGE=1 pnpm run package:local-engine`. This uses the publication container builders for both the native Renderer and Flatpak bundles, selecting Docker or Podman automatically. Without that flag, Flatpak packaging uses host `flatpak-builder` when available and falls back to the same container path when it is absent. Portable and default builds use separate local archive names; retain build identity and artifact hashes, and invalidate an archive if its external SDK or container toolchain changes without a source change. See [tester/release build alignment](../../HOW_TO_RELEASE.md#keep-tester-and-release-builds-aligned).

The [Renderer source-build guide](../../engine/docs/BUILDING-FROM-SOURCE.md#package-dependency-pins) owns the shared Vulkan-Headers/vkBasalt pins and required presentation-header checks. SteamOS and portable native builders use the shared header revision; default host builds use installed headers with the same minimum check. Flatpak builds retain their runtime SDK compilers and libraries. Keep build identities distinct from release-owned archive pins.

Native Remote Play is owned by [`engine/scripts/mako_remote_play/`](../../engine/scripts/mako_remote_play/), bundled byte-identically into Decky and checked by `check:generated-config`. Components have no runtime import between them. Qt controls and `mako-remote-play` require a current-source Renderer; a Decky-only ZIP using the older published payload cannot supply them.

`package:local` is an exact alias for `package:local-engine`. Use `pnpm run package:local-engine-fast` only for a native 64-bit focused package; it omits 32-bit and Flatpak payloads and cannot become a release candidate.

For a frontend, backend, or wrapper change that does not need a new Renderer build, use the pinned released payloads:

```bash
scripts/package-local.sh --local-plugin
```

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

Local packages embed the Renderer archive under `bundled_renderer` and omit Decky's download metadata, so Decky cannot silently fetch another build. Package validation checks payloads, checksums, metadata, legal notices, dependency licences, and the frontend source map. The stable install slug remains `Mako/`, and the immutable Decky listing identity remains **MAKO - Frame Generation** so new packages replace existing installations.

Artifacts are keyed by the Renderer commit, dirty-worktree fingerprint, and portable/default builder mode, allowing UI-only rebuilds to reuse a verified engine without crossing build paths. Local packages record the complete repository commit and dirty state as well as Renderer source identity, checksums, host ISA, and Vulkan process bitness; the release-candidate check uses that repository commit to reject an older ZIP. `host_architectures` currently declares x86_64; `architectures` declares 64/32-bit game-process layers. These are different contracts. Incompatible AArch64/Armada hosts fail closed as documented in [Armada support](ARMADA.md).

Local package identity comes from the current component release notes without changing the tracked release pin. Package tests remain necessary because unit tests do not prove archive layout, manifest activation, permissions, or embedded checksums.

`MAKO_RELEASE_SKIP_TESTS=1` omits automated Decky and native Renderer tests only for an explicitly requested maintainer exception. It retains build freshness and package verification. See [the hotfix exception](../../HOW_TO_RELEASE.md#maintainer-directed-hotfix-without-automated-validation) for publication and evidence requirements.

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
pnpm run dev:reload    # Reload only MAKO Decky
```

The deployment commands write to `~/homebrew/plugins/Mako` and tell you when to reload; `dev:reload` only reloads the existing installation. Quit games before replacing the Renderer. Host deployments also validate and stage MAKO's pinned private vkBasalt build for the selected architectures. Deployments that include the backend or native Renderer refresh existing standalone and profile-managed vkBasalt shader catalogs, so saved game profiles use the current shaders. `dev:engine` and `dev:all` deploy the current 64-bit `mako-cli` alongside the native layers so Decky model checks use the same backend as games. They intentionally omit package verification, UI archives, 32-bit, and Flatpak unless their scope says otherwise. A 32-bit-only deployment preserves the installed host CLI. Use `dev:host` for 32-bit processes, `dev:flatpaks` for sandbox work, and `dev:e2e` before a complete local regression pass.

For each requested native architecture, `deploy-dev.sh` resolves the active Frame Generation manifest through `dev-renderer-selection.py` and the installation service's shared manifest resolver. It updates that owner's libraries: Decky's `~/.local/share/mako-render/lib{,32}/` or the default standalone `~/.local/lib{,32}/`. Spatial scaling and private vkBasalt manifests are refreshed to select the same owner. An unknown selection, missing active library, or disagreement between private and registered Frame Generation manifests stops deployment before installed files are changed. Custom standalone prefixes require their own installation workflow.

Before reporting native success, deployment resolves the installed manifests again and prints verified SHA-256 hashes for the selected Frame Generation, spatial scaling, and vkBasalt libraries, plus the installed CLI when the 64-bit Renderer is selected. Require those checks and a successful reload before testing; a matching inactive copy is insufficient. Direct deployment leaves release identity and standalone ownership records as the installed package baseline; the development status box records the local build. This remains an incremental development overlay, not a package installation or complete rollback transaction.

Run native development builds on the SteamOS host with CMake, Ninja, a C++ compiler, and X11/XCB development headers available there. A SteamOS system-image update can remove locally installed build packages and headers while leaving Pacman's package records intact; use the [SteamOS build-tool installer](../../engine/docs/BUILDING-FROM-SOURCE.md#building-mako-renderer-from-source) to check and restore them. `dev:all` fetches MAKO's shared Vulkan-Headers pin into a repository-local cache and enforces the same presentation header check as packages; it does not use the portable release container.

To deploy the separately selected [forced-FP16 native experiment](../../engine/docs/BUILDING-FROM-SOURCE.md#experimental-forced-lsfg-fp16), use `MAKO_BUILD_DIR=build/lsfg-fp16-experimental pnpm run dev:all --reload --experimental-lsfg-fp16`. The deployment script forwards that build option to the Renderer owner; reusing an experimental build directory alone does not enable conversion. This retains the normal 64-bit scope and existing FP16 toggle. Use the same flag with `dev:host` only when both architectures are requested. The option rejects frontend/backend-only and Flatpak scopes, and image quality remains unqualified.

Flatpak development commands place verified bundles in the installed plugin; use **Flatpak Setup > Update** to install one into an application. The [shared Flatpak catalogue](../../engine/docs/BUILDING-FROM-SOURCE.md#flatpak-extension-builds) generates the Renderer matrix and Decky's `shared_flatpak_runtimes.py`, which is shipped beside `shared_config.py` in ZIPs and backend deployments. Regenerate with `just generate-flatpak-runtimes` after adding or retiring a branch; the same list drives UI rows, RPC status fields, bundle validation, and installed-extension refresh. Complete direct SteamOS host builds require `lib32-glibc`. Flatpak builds use host `flatpak-builder` when available or an automatically selected Docker/Podman container otherwise. See the [source-build guide](../../engine/docs/BUILDING-FROM-SOURCE.md).

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

Use [How to release MAKO](../../HOW_TO_RELEASE.md). Update and commit both component release-note files, check the complete local candidate ZIP and applicable game matrix, then run:

```bash
cd .. && ./scripts/publish-release.sh 1.2.0
```

The resumable workflow publishes Renderer first, records immutable checksums, then builds and publishes the matching Decky package. It refuses dirty state, the wrong branch, stale/local pins, mismatched versions, or reused tags pointing to different code.
