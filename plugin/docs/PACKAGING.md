# Local packaging and publishing

## Choose the right cycle

Run development and packaging commands from `plugin/`; run the hardware gate and publisher from the repository root.

| Need | Command | Contract |
| --- | --- | --- |
| Fast installed-plugin iteration | `pnpm run dev:frontend`, `dev:backend`, `dev:engine`, or another `dev:*` scope | Mutates only the local development installation and does not create a ZIP |
| Fast native tester ZIP | `pnpm run package:local-engine-fast` | Verified 64-bit native payload for focused testing; intentionally omits 32-bit and Flatpak payloads and cannot be published |
| Complete tester ZIP | `pnpm run package:local-engine` | Self-contained native 64-bit/32-bit and Flatpak package suitable for trusted testers; does not tag or publish |
| Release-candidate hardware gate | `cd .. && ./scripts/run-steamos-hardware-validation.sh --deploy-to-decky` | Requires a clean, pushed commit; rebuilds it on the SteamOS/AMD host and optionally installs the already-verified ZIP without rebuilding |
| Actual release | `cd .. && ./scripts/publish-release.sh X.Y.Z` | Publishes MAKO Renderer first, pins its immutable assets, then publishes MAKO Decky |

A local or retained CI ZIP is never the published release. After publication, download the GitHub MAKO Decky asset and run the normal **Install MAKO Renderer** flow once to validate the exact public package. See [Testing](../../TESTING.md) for what each validation cycle proves and [How to release MAKO](../../HOW_TO_RELEASE.md) for the authoritative release sequence.

## Build a local installation ZIP

Run the commands in this guide from the `plugin/` directory. Install pnpm and dependencies once; with Volta, run `volta install pnpm` first.

```bash
pnpm install --frozen-lockfile
pnpm run package:local
```

This creates a versioned local ZIP under `out/`, named `MAKO-Decky-local.<engine-and-source-identity>.zip`. The packager regenerates configuration bindings, builds the frontend and sibling MAKO Renderer source, verifies its payload, and creates a MAKO Decky ZIP. It does not tag, push, or publish anything.

Local ZIPs are self-contained. Their generated manifest stores the embedded archive under MAKO's `bundled_renderer` metadata and omits Decky's `remote_binary` field, because Decky Loader downloads every `remote_binary` entry even when installing a local ZIP. The packager rejects a local artifact that would trigger a download, lacks its embedded archive, or has a mismatched checksum. Published packages keep the established `remote_binary` contract and are rejected unless their Renderer and optional Flatpak URLs use HTTPS.

The ZIP keeps the established `Mako/` directory and installs to `~/homebrew/plugins/Mako` so a package replaces earlier versions instead of creating a second case-sensitive directory. This is an internal compatibility slug. The Decky manifest/listing name remains **MAKO - Frame Generation**, while project documentation, the frontend, and lifecycle logs identify the component as **MAKO Decky**.

Profile sidecars remain canonical input and the generated `mako-run` file remains disposable cache during packaging and installation. `tests/test_configuration_boundaries.py` characterizes the exact wrapper and sidecar bytes produced by the pure generator/storage modules and the `ConfigurationService` facade; package verification remains necessary because those unit tests do not prove ZIP layout, permissions, or installed loader activation.

Pass a path to choose the output location:

```bash
pnpm run package:local -- /path/to/MAKO-Decky.zip
```

To reuse local copies of the _already pinned_ renderer and Flatpak archives, provide both archives directly. Their filenames and checksums must match the pin in `package.json`:

```bash
scripts/package-local.sh \
  --engine-archive /path/to/MAKO-Renderer-v<version>-linux.tar.xz \
  --flatpak-archive /path/to/MAKO-Renderer-v<version>-flatpaks.tar.xz \
  /path/to/MAKO-Decky-local-test.zip
```

For an engine candidate that differs from the pin, use `pnpm run package:local-engine` or `scripts/package-local.sh --local-engine-repo /path/to/MAKO/engine`. That workflow creates and records a local payload identity in the ZIP instead of pretending it is a released archive.

### Build directly from a local engine checkout

For day-to-day development, build both engine payloads and the Decky ZIP in one command:

```bash
pnpm run package:local-engine
```

That command expects the engine checkout at `../engine`. For another location, invoke the packager directly:

```bash
scripts/package-local.sh --local-engine-repo /path/to/MAKO/engine
```

For quick native Steam-game iteration, omit the expensive Flatpak runtime matrix and the 32-bit host layer while retaining a verified 64-bit build:

```bash
pnpm run package:local-engine-fast
```

Native-only, 64-bit-only archives are labelled accordingly and must not be published. Run the complete local-engine packaging command before any release candidate or when testing Flatpak games and launchers. Local artifacts are keyed by the engine commit and dirty-worktree fingerprint, so UI-only repackaging reuses an already verified matching engine build; changing engine source produces a new fingerprint and rebuilds it.

The engine's native and Flatpak packaging scripts run first, including their tests and dual-architecture layout checks. Decky then embeds those artifacts and writes their source commit, dirty-worktree marker, and calculated checksums into the generated ZIP's `bundled_renderer` record. A local release-candidate ZIP takes its displayed and generated package version from the versioned `RELEASE_NOTES.md` heading, while the tracked Decky `package.json` remains unchanged, so this development path cannot alter the release pin. Published builds still require the release notes, package version, Renderer pin, and requested release version to match. The ZIP is named with the local engine commit and `.dirty` when applicable. It does not tag, push, or publish the monorepo.

Renderer metadata keeps native host ISA (`host_architectures`, currently `x86_64`) separate from Vulkan process bitness (`architectures`, normally `64` and `32`). Packaging and publishing reject missing or unknown host declarations, and MAKO Decky checks the native host before extracting the bundled Renderer. On an incompatible AArch64/Armada host, startup also replaces a legacy wrapper with an early passthrough and removes positively identified MAKO Flatpak activation left by older builds. The exact fail-closed contract and the gates for future native support are documented in [Armada and native AArch64 support](ARMADA.md).

## Fast direct SteamOS iteration

When the MAKO monorepo is on the SteamOS machine, do not create a ZIP for every edit. Install the plugin and use its **Install MAKO Renderer** action once first, then run these commands from `plugin/`:

```bash
pnpm run dev:frontend  # TypeScript/React change
pnpm run dev:backend   # Python/backend change
pnpm run dev:engine    # Native 64-bit layer change
pnpm run dev:all       # All three
pnpm run dev:host      # Decky plus 64-bit and genuine 32-bit host layers
pnpm run dev:flatpaks  # Decky plus Flatpak bundles for 23.08, 24.08, and 25.08
pnpm run dev:e2e       # Decky, both host layers, and all Flatpak bundles
```

Each command deploys directly to Decky's installed MAKO Decky at `~/homebrew/plugins/Mako` and tells you to reload it from Decky's Developer menu. `dev:engine` calls MAKO Renderer's persistent incremental build, then atomically replaces only the private 64-bit host layer. It skips the archive, ZIP, Flatpak runtime matrix, 32-bit layer, CLI/UI, and full test suite, so it is for native 64-bit Steam-game testing only. Quit the game before deploying the engine. Use the regular package commands before publishing or testing release packaging.

`dev:all` remains the fast native 64-bit loop. Use `dev:host` when you also need a genuine 32-bit Steam/Proton process, `dev:flatpaks` when testing the Flatpak Setup flow, and `dev:e2e` before a full local regression pass. Host development builds configure both private manifests with install-relative 64-bit or 32-bit library paths; this matters because Steam Pressure Vessel imports and rewrites those manifests before starting Proton. The Flatpak commands build and place the three verified bundles in the installed plugin; open **Flatpak Setup** and choose **Update** for the target runtime to install a bundle into the application sandbox.

The ordered Decky runtime list is owned by `shared_config.py`; backend bundle identities, frontend controls, and packaging/deployment loops derive from that contract. The Renderer build matrix remains independently owned by `engine/dist/flatpak/mako-render/runtime-versions.txt`, with a cross-component regression test requiring both boundaries to list the same versions.

On SteamOS, the host commands require `lib32-glibc`; if `/usr/include/gnu/stubs-32.h` is missing despite the package appearing installed, reinstall it without `--needed`. The Flatpak commands additionally need `flatpak-builder`. The source-build guide gives the exact SteamOS commands for the former; for the latter use `sudo pacman -S flatpak-builder` while the SteamOS filesystem is temporarily writable.

All reusable MAKO build data uses `engine/build/cache`. The Flatpak command stores downloaded dependencies in `engine/build/cache/flatpak` and stages its large disposable build under `engine/build/work/flatpak`. This avoids SteamOS's comparatively small `/tmp` filesystem and lets later builds reuse downloaded runtimes. Set `MAKO_BUILD_CACHE_ROOT` or `MAKO_BUILD_WORK_ROOT` to move all build storage together; the component-specific `MAKO_FLATPAK_CACHE_ROOT` and `MAKO_FLATPAK_TMP_ROOT` overrides remain available.

The cache has no automatic expiry and survives reboots. Inspect its current size with the safe, dry-run command:

```bash
pnpm run dev:prune-flatpak-cache
```

Only when you explicitly want to reclaim that space, remove this checkout's Flatpak cache and any interrupted staging directory with:

```bash
pnpm run dev:prune-flatpak-cache -- --confirm
```

It never removes native incremental builds, the installed MAKO Decky, its already-deployed Flatpak bundles, or your normal user Flatpak installation. The next `dev:flatpaks` or `dev:e2e` run will need to download the SDK/runtime dependencies again. For safety, the prune command targets only the default repo-local locations; manage custom cache locations yourself.

To inspect all repository-local caches, including the isolated native Qt SDK and compiler cache, use:

```bash
pnpm run dev:prune-build-cache
```

Add `-- --confirm` only when you intentionally want to remove the complete `engine/build/cache` and `engine/build/work` trees. Normal Linux builds do not write cache data into `/root` or `/usr`; paths under `/usr` in Docker or Flatpak build output belong to disposable sandboxes.

Every direct development deployment refreshes a blue status box at the top of the plugin. It records the local deployment timestamp, the monorepo commit, component-scoped local edits, the deployed frontend and backend scope, and the first 12 characters of the 64-bit layer, 32-bit layer, and Flatpak-archive SHA-256 values when those artifacts were part of the run. `dev:all`, `dev:host`, and `dev:e2e` confirm that the plugin and applicable MAKO Renderer artifacts were deployed together; narrower commands identify what remained unchanged.

Set `DECKY_PLUGIN_DIR` if MAKO Decky is installed elsewhere, or invoke `scripts/deploy-dev.sh --engine-repo /path/to/MAKO/engine --engine` when using a different engine tree. The engine build requires the SteamOS CMake, Ninja, and compiler prerequisites in the [source-build guide](../../engine/docs/BUILDING-FROM-SOURCE.md).

## Publish a GitHub release

Use the root [How to release MAKO](../../HOW_TO_RELEASE.md) guide. From a clean `main` worktree, one command versions, builds, verifies, pins, publishes, and links both packages:

```bash
./scripts/publish-release.sh 1.2.0
```

Before running it, update and commit both [`engine/RELEASE_NOTES.md`](../../engine/RELEASE_NOTES.md) and [`plugin/RELEASE_NOTES.md`](../RELEASE_NOTES.md). Their versioned “What’s new” headings and bodies are copied verbatim into the respective GitHub release notes; commit messages are never used as public change lists.

The workflow publishes MAKO Renderer first, including the host and Flatpak assets, then commits its exact URLs and checksums before publishing the matching MAKO Decky ZIP as GitHub's **Latest** release. It is resumable and refuses a dirty worktree, the wrong branch, local-only payloads, mismatched pins, or reused tags that point at different code. Run it only after the committed release candidate has passed the SteamOS hardware gate and applicable manual game matrix; the publisher does not silently substitute for either test cycle.

Release entry points:

- [Latest MAKO Decky release](https://github.com/eugeniosegala/MAKO/releases/latest)
- [All MAKO releases](https://github.com/eugeniosegala/MAKO/releases)
