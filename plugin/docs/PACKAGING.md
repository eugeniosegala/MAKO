# Local packaging and publishing

## Build a local installation ZIP

Run the commands in this guide from the `plugin/` directory. Install pnpm and
dependencies once; with Volta, run `volta install pnpm` first.

```bash
pnpm install --frozen-lockfile
pnpm run package:local
```

This creates a versioned local ZIP under `out/`, named
`Mako-local.<engine-and-source-identity>.zip`. The packager regenerates
configuration bindings, builds the frontend and sibling MAKO Renderer engine,
verifies its payload, and creates a Decky ZIP. It does not tag, push, or
publish anything.

The ZIP's `Mako/` directory and the installed
`~/homebrew/plugins/Mako` directory match the **Mako** name displayed inside
Decky. The component is officially named **MAKO Decky** within the wider MAKO
project.

Pass a path to choose the output location:

```bash
pnpm run package:local -- /path/to/Mako.zip
```

To reuse local copies of the *already pinned* Renderer and Flatpak archives,
provide both archives directly. Their filenames and checksums must match the
pin in `package.json`:

```bash
scripts/package-local.sh \
  --engine-archive /path/to/mako-render-<version>-linux.tar.xz \
  --flatpak-archive /path/to/mako-render-<version>-flatpaks.tar.xz \
  /path/to/Mako-local-test.zip
```

For an engine candidate that differs from the pin, use
`pnpm run package:local-engine` or `scripts/package-local.sh --local-engine-repo
/path/to/MAKO/engine`. That workflow creates and records a local payload
identity in the ZIP instead of pretending it is a released archive.

### Build directly from a local engine checkout

For day-to-day development, build both engine payloads and the Decky ZIP in one command:

```bash
pnpm run package:local-engine
```

That command expects the engine checkout at `../engine`. For another location, invoke the packager directly:

```bash
scripts/package-local.sh --local-engine-repo /path/to/MAKO/engine
```

For quick native Steam-game iteration, omit the expensive Flatpak runtime
matrix and the 32-bit host layer while retaining a verified 64-bit build:

```bash
pnpm run package:local-engine-fast
```

Native-only, 64-bit-only archives are labelled accordingly and must not be published. Run
the complete local-engine packaging command before any release candidate or
when testing Flatpak games and launchers. Local artifacts are keyed by the
engine commit and dirty-worktree fingerprint, so UI-only repackaging reuses an
already verified matching engine build; changing engine source produces a new
fingerprint and rebuilds it.

The engine's native and Flatpak packaging scripts run first, including their tests and dual-architecture layout
checks. Decky then embeds those artifacts and writes their source commit, dirty-worktree marker, and calculated
checksums into the generated ZIP's copy of `package.json`. The tracked Decky `package.json` remains unchanged, so this
development path cannot alter the release pin. The ZIP is named with the local engine commit and `.dirty` when
applicable. It does not tag, push, or publish the monorepo.

## Fast direct SteamOS iteration

When the MAKO monorepo is on the SteamOS machine, do not create a ZIP for every
edit. Install the plugin and use its **Install MAKO Renderer (developer build)**
action once first, then run these commands from `plugin/`:

```bash
pnpm run dev:frontend  # TypeScript/React change
pnpm run dev:backend   # Python/backend change
pnpm run dev:engine    # Native 64-bit layer change
pnpm run dev:all       # All three
pnpm run dev:host      # Decky plus 64-bit and genuine 32-bit host layers
pnpm run dev:flatpaks  # Decky plus Flatpak bundles for 23.08, 24.08, and 25.08
pnpm run dev:e2e       # Decky, both host layers, and all Flatpak bundles
```

Each command deploys directly to Decky's installed MAKO Decky at
`~/homebrew/plugins/Mako` and tells you to reload it from Decky's Developer
menu. `dev:engine` calls MAKO Renderer's persistent incremental build, then
atomically replaces only the private 64-bit host layer. It skips the archive,
ZIP, Flatpak runtime matrix, 32-bit layer, CLI/UI, and full test suite, so it is
for native 64-bit Steam-game testing only. Quit the game before deploying the
engine. Use the regular package commands before publishing or testing release
packaging.

`dev:all` remains the fast native 64-bit loop. Use `dev:host` when you also
need a genuine 32-bit Steam/Proton process, `dev:flatpaks` when testing the
Flatpak Setup flow, and `dev:e2e` before a full local regression pass. The
Flatpak commands build and place the three verified bundles in the installed
plugin; open **Flatpak Setup** and choose **Update** for the target runtime to
install a bundle into the application sandbox.

On SteamOS, the host commands require `lib32-glibc`; if
`/usr/include/gnu/stubs-32.h` is missing despite the package appearing
installed, reinstall it without `--needed`. The Flatpak commands additionally
need `flatpak-builder`. The source-build guide gives the exact SteamOS commands
for the former; for the latter use `sudo pacman -S flatpak-builder` while the
SteamOS filesystem is temporarily writable.

The Flatpak command retains its downloaded dependency cache in
`engine/build/steamos-flatpak-cache` and stages builds under
`engine/build/steamos-flatpak-tmp`. This avoids SteamOS's comparatively small
`/tmp` filesystem and lets later builds reuse downloaded runtimes. Set
`MAKO_FLATPAK_CACHE_ROOT` or `MAKO_FLATPAK_TMP_ROOT` to move either location.

The cache has no automatic expiry and survives reboots. Inspect its current
size with the safe, dry-run command:

```bash
pnpm run dev:prune-flatpak-cache
```

Only when you explicitly want to reclaim that space, remove this checkout's
Flatpak cache and any interrupted staging directory with:

```bash
pnpm run dev:prune-flatpak-cache -- --confirm
```

It never removes native incremental builds, the installed MAKO Decky, its
already-deployed Flatpak bundles, or your normal user Flatpak installation.
The next `dev:flatpaks` or `dev:e2e` run will need to download the SDK/runtime
dependencies again. For safety, the prune command targets only the default
repo-local locations; manage custom cache locations yourself.

Every direct development deployment refreshes a blue status box at the top of
the plugin. It records the local deployment timestamp, the monorepo commit,
component-scoped local edits, the deployed frontend and backend scope, and the
first 12 characters of the 64-bit layer, 32-bit layer, and Flatpak-archive
SHA-256 values when those artifacts were part of the run. `dev:all`,
`dev:host`, and `dev:e2e` confirm that the plugin and applicable MAKO Renderer
artifacts were deployed together; narrower commands identify what remained
unchanged.

Set `DECKY_PLUGIN_DIR` if MAKO Decky is installed elsewhere, or invoke
`scripts/deploy-dev.sh --engine-repo /path/to/MAKO/engine --engine` when using a
different engine tree. The engine build requires the SteamOS CMake, Ninja, and
compiler prerequisites in the [source-build guide](../../engine/docs/BUILDING-FROM-SOURCE.md).

## Publish a GitHub release

> [!IMPORTANT]
> No MAKO Renderer or MAKO Decky release has been published yet. This is the
> future release procedure; do not run it for a local test ZIP.

Publish from a clean `main` worktree and authenticate once with
`gh auth login -h github.com`. Both publishers intentionally refuse another
branch or uncommitted changes.

1. Update the Renderer version and its version-specific release-note values in
   `engine/scripts/publish-package.sh`, then commit the intended Renderer
   release on `main`.
2. From `engine/`, run:

   ```bash
   ./scripts/publish-package.sh
   ```

   This creates the `render-v<version>` prerelease, uploads the native and
   Flatpak archives, and updates `plugin/package.json` with their verified
   checksums.
3. Review and commit that generated Renderer pin. Update the Decky version and
   its version-specific release-note values in `plugin/scripts/publish-package.sh`.
4. From `plugin/`, run:

```bash
pnpm run package:publish
```

The script verifies and builds `MAKO-Decky-v<package-version>.zip`, creates or
verifies the matching `plugin-v<package-version>` tag, pushes the branch and
tag, generates release notes, and creates or updates the GitHub release. A
MAKO Decky release is explicitly marked as the repository's **Latest** release.
It never moves an existing published tag to newer code.

GitHub does not allow prereleases to own the Latest pointer, so MAKO Decky is
published as a normal GitHub release even when its component version contains
an `experimental` suffix. MAKO Renderer releases remain GitHub prereleases and
are explicitly published with `--latest=false`.

The Decky publisher requires `plugin/package.json` to pin the matching
checksum-verified Renderer assets. It refuses a local-only payload or an
unpublished/mismatched Renderer tag.

After the first release, the release entry points will be:

- [Latest MAKO Decky release](https://github.com/eugeniosegala/MAKO/releases/latest)
- [All MAKO Decky releases](https://github.com/eugeniosegala/MAKO/releases?q=tag%3Aplugin-v)
- [All MAKO Renderer releases](https://github.com/eugeniosegala/MAKO/releases?q=tag%3Arender-v)
