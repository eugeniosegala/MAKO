# Arch Linux packaging

This directory contains the Arch Linux packaging for MAKO Renderer. `PKGBUILD` repackages the official prebuilt Linux host archive (`MAKO-Renderer-vX.Y.Z-linux.tar.xz`) from the upstream GitHub release into a system-wide `mako-renderer-bin` package. No MAKO source is built. Every payload file that upstream's checksum manifest covers is installed with the same content hash, so the installed layers, CLI, and configuration UI stay auditable against the archive's `MAKO-Renderer-install-manifest.txt`.

The `-bin` suffix follows the convention for a package that repackages prebuilt deliverables instead of building them from source. A future source-built package would be named `mako-renderer`; this package provides and conflicts with that common package identity so pacman treats the two implementations as alternatives.

## Contents

The package installs:

- `mako-cli`, `mako-launch`, `mako-ui`, and `mako-diagnostics` into `/usr/bin`.
- the 64-bit Vulkan layers into `/usr/lib` and the 32-bit layers into `/usr/lib32`.
- the public implicit-layer manifests into `/usr/share/vulkan/implicit_layer.d`, gated by `ENABLE_MAKO` and `ENABLE_MAKO_SPATIAL_SCALING`.
- the private manifests used by `mako-launch` into `/usr/share/mako-render/vulkan/implicit_layer.d` and `/usr/share/mako-render/vulkan/spatial_scaling.d`.
- the configuration desktop entry and its seven hicolor icons.
- license texts into `/usr/share/licenses/mako-renderer-bin`.
- `ASSET_PROVENANCE.md`, the version file, and the upstream checksum manifest into `/usr/share/doc/mako-renderer-bin`.

The package deliberately excludes:

- `bin/mako-installer` and the `Install MAKO Renderer` launcher, which form the user-local installer and target `~/.local`.
- `share/applications/io.github.eugeniosegala.mako.uninstaller.desktop`, whose `Exec` calls `mako-installer --uninstall` and would target the user-local install.
- the archive's `README.txt`, which documents the user-local installer flow that this package does not provide.

Two details differ from the archive layout: the archived `share/doc/mako-render/*` files are installed under `/usr/share/licenses/mako-renderer-bin/` and `/usr/share/doc/mako-renderer-bin/`, and the four shared libraries are installed as mode `0755` instead of the archive's `0644`, following Arch convention. Content hashes are unaffected, so the checksum manifest still matches.

## Build and install

An `x86_64` Arch Linux system is required. Build and install from this directory:

```bash
cd engine/dist/arch
makepkg -si
```

`makepkg` downloads the release archive, verifies `sha256sums`, and repackages the extracted payload. The source is the official prebuilt archive; the package does not run a compiler.

## Upgrade

Upgrade with `pacman -Syu`, or rebuild and reinstall with `makepkg -si` when a new `pkgrel` is published. Pacman replaces all package-owned files under `/usr`. User profiles and configuration data are not package-owned and are never touched during an upgrade. The `pre_upgrade` hook re-reports any user-local MAKO install it detects.

## Remove

```bash
pacman -R mako-renderer-bin
```

Pacman removes only the files the package owns under `/usr`. Profiles and configuration under `~/.config/mako-render` are preserved and must be removed manually if they are no longer wanted. A user-local MAKO install, including MAKO Decky's, is left untouched.

Do not run the upstream installer or extract the archive with `MAKO_INSTALL_PREFIX=/usr`. That writes the same `/usr/bin` and `/usr/share` paths this package owns: pacman then refuses the next install or upgrade with `exists in filesystem`, and the upstream uninstaller would delete files pacman still believes it owns. If pacman reports a file conflict, use `pacman -Qo` to check whether the file is unowned, remove it, and install again; do not reach for `--overwrite`.

## Coexistence with MAKO Decky and other user-local installs

MAKO Decky and the standalone archive manage a user-local renderer under `~/.local`, using `~/.local/share/mako-render`, `~/.local/bin/mako-run`, and `~/.local/share/vulkan/implicit_layer.d`. Ownership of that copy is recorded in `~/.local/share/mako-render/active-renderer.json`, and the Decky plugin lives at `~/homebrew/plugins/Mako`. Neither the user-local installers nor this package know about each other, and neither manages the other's files. This package never touches `~/.local`, and the user-local installers never touch `/usr`. The install hooks report any user-local copy they detect and delete nothing.

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

The MAKO Renderer publisher updates `plugin/package.json`, runs `sync-release-pin.py`, resets `pkgrel` to 1 for a new upstream version, and verifies the result with `check-release-pin.sh`. This keeps the recipe aligned with the immutable archive only after the Renderer release gates have passed and the asset checksum is known.

For a packaging-only fix, leave `pkgver` and `sha256sums` unchanged and increment `pkgrel`. Run `just check-arch-package` from the repository root before committing. `check-release-pin.sh` also runs in portable Renderer CTest and fails when `pkgver`, the source URL, or `sha256sums` drifts from `plugin/package.json`.

## AUR publication

This repository owns the reviewed `PKGBUILD`, but the release publisher does not push to the AUR. An AUR package needs its own package repository, generated `.SRCINFO`, and maintainer credentials; those are separate publication and trust boundaries. Until an official AUR repository is established, build the recipe from this source tree. If it is published later, generate `.SRCINFO` from this exact recipe after each synchronized Renderer release instead of maintaining a second independent pin.

## Verification

- `makepkg` verifies the release archive against `sha256sums` before packaging.
- Every payload file this package installs has the same SHA-256 as its entry in `MAKO-Renderer-install-manifest.txt`. The manifest is installed unchanged at `/usr/share/doc/mako-renderer-bin/MAKO-Renderer-install-manifest.txt` so the claim can be re-checked. The only manifest entries without a matching installed file are `bin/mako-installer` and `share/applications/io.github.eugeniosegala.mako.uninstaller.desktop`, both excluded by design.
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
