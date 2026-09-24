# Arch Linux packaging

This directory contains the Arch Linux packaging for MAKO Renderer. `PKGBUILD` repackages the official prebuilt Linux host archive (`MAKO-Renderer-vX.Y.Z-linux.tar.xz`) from the upstream GitHub release into a system-wide `mako-renderer-bin` package, and starting with MAKO Renderer 4.0 the verified `.pkg.tar.zst` is published alongside that archive. No MAKO source is built. Every included payload file is installed with the same content hash, so the installed layers, CLI, configuration UI, and release-owned private integrations stay auditable against the archive's `MAKO-Renderer-install-manifest.txt`.

The `-bin` suffix follows the convention for a package that repackages prebuilt deliverables instead of building them from source. A future source-built package would be named `mako-renderer`; this package provides and conflicts with that common package identity so pacman treats the two implementations as alternatives.

## Contents

The package installs:

- `mako-cli`, `mako-launch`, `mako-ui`, and `mako-diagnostics` into `/usr/bin`.
- the 64-bit Vulkan layers into `/usr/lib` and the 32-bit layers into `/usr/lib32`.
- the public implicit-layer manifests into `/usr/share/vulkan/implicit_layer.d`, gated by `ENABLE_MAKO` and `ENABLE_MAKO_SPATIAL_SCALING`.
- the private manifests used by `mako-launch` into `/usr/share/mako-render/vulkan/implicit_layer.d` and `/usr/share/mako-render/vulkan/spatial_scaling.d`.
- any checksum-manifested private integration shipped by the pinned archive, including MAKO's private 64-bit and 32-bit vkBasalt libraries, manifests, licences, and provenance when present in that Renderer release.
- the configuration desktop entry and its seven hicolor icons.
- license texts into `/usr/share/licenses/mako-renderer-bin`.
- `ASSET_PROVENANCE.md`, the version file, and the upstream checksum manifest into `/usr/share/doc/mako-renderer-bin`.

The package deliberately excludes:

- `bin/mako-installer` and the `Install MAKO Renderer` launcher, which form the user-local installer and target `~/.local`.
- `share/applications/io.github.eugeniosegala.mako.uninstaller.desktop`, whose `Exec` calls `mako-installer --uninstall` and would target the user-local install.
- the archive's `README.txt`, which documents the user-local installer flow that this package does not provide.

Two details differ from the archive layout: the archived `share/doc/mako-render/*` files are installed under `/usr/share/licenses/mako-renderer-bin/` and `/usr/share/doc/mako-renderer-bin/`, and shared libraries are installed as mode `0755` instead of the archive's `0644`, following Arch convention. Content hashes are unaffected, so the checksum manifest still matches.

## Install the release package

An `x86_64` Arch Linux system with the multilib repository enabled is required because one package carries both the 64-bit and 32-bit Vulkan layers and their runtime dependencies. Download `mako-renderer-bin-X.Y.Z-1-x86_64.pkg.tar.zst` from the matching MAKO Renderer GitHub release and install it with:

```bash
sudo pacman -U ./mako-renderer-bin-X.Y.Z-1-x86_64.pkg.tar.zst
```

Pacman verifies the package database transaction, installs the declared dependencies, owns every installed path, and runs the non-mutating coexistence report. The GitHub release records the package's SHA-256 in its release notes and the repository's release metadata.

### Configure and launch

Installing the package does not activate MAKO for every game. Open **MAKO Renderer Configuration** from the application menu or run:

```bash
mako-ui
```

Create or select a game profile and configure Frame Generation, Scaling, and/or Shaders. Frame Generation and LS1 scaling require a lawful user-supplied `Lossless.dll`; the open MAKO Scaler and bundled shaders do not. For a native Steam or Proton game, add this under **Steam Properties > General > Launch Options**:

```text
/usr/bin/mako-launch %command%
```

Use the absolute `/usr/bin` path so a separate `~/.local/bin/mako-launch` cannot shadow the pacman-owned launcher. Flatpak games additionally require the matching MAKO Flatpak runtime extension and application preparation described in the [Flatpak guide](../../docs/FLATPAK-GUIDE.md).

## Build from the tracked recipe

To reproduce the package from the official archive instead, build and install from this directory:

```bash
cd engine/dist/arch
makepkg -si
```

`makepkg` downloads the release archive, verifies `sha256sums`, and repackages the extracted payload. The package does not run a compiler.

## Upgrade

This package is not currently published through an official pacman repository or the AUR, so `pacman -Syu` alone cannot discover a new MAKO version. Download the newer `.pkg.tar.zst` from the matching GitHub release and run `sudo pacman -U` again. Pacman replaces all package-owned files under `/usr`. User profiles and configuration data are not package-owned and are never touched during an upgrade. The `pre_upgrade` hook re-reports any user-local MAKO install it detects.

## Remove

```bash
pacman -R mako-renderer-bin
```

Pacman removes only the files the package owns under `/usr`. Profiles and configuration under `~/.config/mako-render` are preserved and must be removed manually if they are no longer wanted. A user-local MAKO install, including MAKO Decky's, is left untouched.

Do not run the upstream installer or extract the archive with `MAKO_INSTALL_PREFIX=/usr`. That writes the same `/usr/bin` and `/usr/share` paths this package owns: pacman then refuses the next install or upgrade with `exists in filesystem`, and the upstream uninstaller would delete files pacman still believes it owns. If pacman reports a file conflict, use `pacman -Qo` to check whether the file is unowned, remove it, and install again; do not reach for `--overwrite`.

## Coexistence with MAKO Decky and other user-local installs

MAKO Decky manages its user-local Renderer under `~/.local/share/mako-render`, including `lib`, `lib32`, private manifests, and `~/.local/bin/mako-run`. The standalone archive installs its prefix under `~/.local`, including `lib`, `lib32`, private manifests, and `~/.local/bin/mako-launch`. Both record ownership in `~/.local/share/mako-render/active-renderer.json`; the Decky plugin itself lives at `~/homebrew/plugins/Mako`. Neither user-local delivery path nor this package manages the other's files. This package never touches `~/.local`, and the user-local installers never touch `/usr`. The install hooks recognize both layouts, report any user-local copy they detect, and delete nothing.

Managed launches are safe to mix. `mako-launch` sets `VK_IMPLICIT_LAYER_PATH` to its own private manifest directory (`/usr/share/mako-render/vulkan/implicit_layer.d` when installed system-wide), and Decky's generated wrapper does the same for the user-local directory. The loader uses that variable instead of the standard implicit-layer search paths (`$XDG_CONFIG_HOME`, `$XDG_CONFIG_DIRS`, `/etc`, `$XDG_DATA_HOME`, and `$XDG_DATA_DIRS`, each with the `vulkan/implicit_layer.d` suffix), and ignores `VK_ADD_IMPLICIT_LAYER_PATH` while it is set; both launchers also unset that variable. A managed launch therefore discovers implicit-layer manifests only in the active launcher's directory, and the loader drops duplicate layer names by keeping the first manifest in search order, so one process never loads both layer copies. MAKO ships no explicit-layer manifests, and explicit-layer paths such as `VK_LAYER_PATH` are unaffected. An enabled override layer that declares `override_paths` bypasses these variables as well, but MAKO does not use one.

Residual caveats:

- A user who exports `ENABLE_MAKO=1` globally, outside any MAKO launcher, makes the same layer name discoverable from `/usr/share/vulkan/implicit_layer.d` (this package) and `~/.local/share/vulkan/implicit_layer.d` (registered by the user-local installer). The loader keeps the first manifest in search order, and `$XDG_DATA_HOME` is searched before `$XDG_DATA_DIRS`, so the user-local copy wins and this package's layer does not load in that process. Use a launcher rather than a global `ENABLE_MAKO`.
- `~/.local/bin` is not part of the default Arch `PATH`; `/etc/profile` appends only `/usr/local/sbin`, `/usr/local/bin`, and `/usr/bin`. If you prepend `~/.local/bin` yourself, a user-local `mako-launch`, `mako-ui`, `mako-cli`, or `mako-diagnostics` shadows the packaged one. Use `/usr/bin/...` explicitly when you want this package's copy.
- MAKO Decky's Flatpak integration owns `/usr/lib/extensions/vulkan/makorender` inside a prepared Flatpak runtime. This package never writes there. If a host exposes that path from another source, Decky's wrapper may select those extension manifests for a Decky-managed launch; remove or refresh the owning Flatpak extension rather than making this pacman package claim its files.
- Both delivery paths share one configuration directory. See the next section.

Pick one owner per user: either keep this system package and stop using the user-local install, or remove this package and keep the user-local install.

## Profiles and configuration data

Profiles, settings, and runtime state live in `~/.config/mako-render` (`conf.toml`, `profile-metadata.json`, `profile-wrapper-settings.json`, and `runtime-state/`). They belong to the user and are never installed, replaced, or removed by this package.

Because both delivery paths share that directory, do not run two different MAKO Renderer versions against one `~/.config/mako-render`: a user-local install and this package would migrate the same profiles with different engine versions. Keep one owner per user.

## Updating for a new release

The MAKO Renderer publisher first builds and verifies the host archive from the release commit, then records that exact artifact's version, URL, and checksum in `plugin/package.json`, runs `sync-release-pin.py`, resets `pkgrel` to 1 for a new upstream version, and verifies the pin with `check-release-pin.sh`. `verify-release-package.sh` builds the synchronized recipe against that exact local archive, validates its contents, and retains that exact `.pkg.tar.zst` for checksum recording and GitHub release publication. The archive's own install manifest remains the package payload source of truth, so the recipe neither selects files from an older archive nor maintains a second release payload list.

For a packaging-only fix, leave `pkgver` and `sha256sums` unchanged and increment `pkgrel`. Run `just check-arch-package` from the repository root before committing. `check-release-pin.sh` also runs in portable Renderer CTest and fails when `pkgver`, the source URL, or `sha256sums` drifts from `plugin/package.json`.

## AUR publication

This repository owns the reviewed `PKGBUILD` and publishes its verified binary package on GitHub, but the release publisher does not push to the AUR. An AUR package needs its own package repository, generated `.SRCINFO`, and maintainer credentials; those are separate publication and trust boundaries. If it is published later, generate `.SRCINFO` from this exact recipe after each synchronized Renderer release instead of maintaining a second independent pin.

## Verification

- `makepkg` verifies the release archive against `sha256sums` before packaging.
- Renderer publication runs `verify-release-package.sh` after synchronizing the recipe, uploads that exact verified package, records its checksum in release metadata, and refuses to consider the release complete if the asset is missing or differs from the recorded checksum.
- The build wrapper rejects a stale or renamed package unless its filename, `.PKGINFO` name/version/architecture, embedded `MAKO-Renderer-version.txt`, synchronized recipe, and source archive checksum all agree.
- Every payload file this package installs has the same SHA-256 as its entry in `MAKO-Renderer-install-manifest.txt`. The recipe verifies the complete archive manifest, packages every supported entry instead of maintaining a second payload allowlist, and fails on unsafe or unsupported paths. The manifest is installed unchanged at `/usr/share/doc/mako-renderer-bin/MAKO-Renderer-install-manifest.txt` so the claim can be re-checked. The only manifest entries without a matching installed file are `bin/mako-installer` and `share/applications/io.github.eugeniosegala.mako.uninstaller.desktop`, both excluded by design.
- `desktop-file-validate` accepts the installed desktop entry.
- A Vulkan loader run with `VK_IMPLICIT_LAYER_PATH` pointed at each packaged manifest directory discovers and activates `VK_LAYER_MAKO_render` and `VK_LAYER_MAKO_spatial_scaling` for the public set, and the matching layer for each private directory, with the relative `library_path` values resolving to `/usr/lib` and `/usr/lib32`.
- `bash -n PKGBUILD`, `sh -n mako-renderer-bin.install`, and `./check-release-pin.sh` all pass.

## Links

- [Upstream README](../../../README.md)
- [Engine README](../../README.md)
- [Flatpak guide](../../docs/FLATPAK-GUIDE.md)
- [Troubleshooting](../../docs/TROUBLESHOOTING.md)
- [Collect diagnostics](../../docs/COLLECT_DIAGNOSTICS.md)
- [Installation transactions](../../../INSTALLATION-TRANSACTIONS.md)
- [Cleanups](../../../CLEANUPS.md)
