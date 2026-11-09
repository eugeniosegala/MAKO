# Arch Linux packaging

Install `mako-renderer-bin` for a system-wide MAKO Renderer on writable x86_64 Arch Linux. SteamOS users should use [MAKO Decky or the user-local archive](../../README.md#installation).

## Contents

The package includes the CLI, launcher, Qt UI, diagnostics helper, and 64-bit/32-bit Vulkan layers under `/usr`. Use pacman to manage these files; the user-local archive installer is not included. Maintainers can find the [payload layout and checks](../../docs/BUILDING-FROM-SOURCE.md#arch-package-maintenance) in the build guide.

## Install the release package

An `x86_64` Arch Linux system with the multilib repository enabled is required because one package carries both the 64-bit and 32-bit Vulkan layers and their runtime dependencies. Download `mako-renderer-bin-X.Y.Z-1-x86_64.pkg.tar.zst` from the matching MAKO Renderer GitHub release and leave it unextracted. Open a terminal in the folder containing the downloaded file, then run the following command, replacing the example filename with the exact downloaded filename:

```bash
sudo pacman -U ./mako-renderer-bin-X.Y.Z-1-x86_64.pkg.tar.zst
```

The `./` prefix means the terminal's current folder; this command does not download the package. You can instead use the file's full path. For example, if you saved it in `Downloads`, replace the example filename below with the downloaded filename:

```bash
sudo pacman -U "$HOME/Downloads/mako-renderer-bin-X.Y.Z-1-x86_64.pkg.tar.zst"
```

If pacman reports `could not find or read package`, check that the file exists at the path you supplied and that its filename matches exactly. Download it again if it was deleted or the download did not finish. Reinstalling after removal uses the same steps.

Pacman verifies the package database transaction, installs the declared dependencies, owns every installed path, and runs the non-mutating coexistence report. The GitHub release records the package's SHA-256 in its release notes and the repository's release metadata.

### Configure and launch

Installing the package does not activate MAKO for every game. Open **MAKO Renderer Configuration** from the application menu or run:

```bash
mako-ui
```

Create or select a game profile and configure Frame Generation, Scaling, and/or Shaders. Only the optional Lossless Scaling frame generation model and LS1 scaling require a lawful user-supplied `Lossless.dll`; MAKO FG, the open MAKO Scaler, and bundled shaders do not. LS1 and DirectX-only LSFG models additionally need the optional `vkd3d` dependency, plus `lib32-vkd3d` for 32-bit games. Native SPIR-V LSFG models do not need the translator. For a native Steam or Proton game, add this under **Steam Properties > General > Launch Options**:

```text
/usr/bin/mako-launch %command%
```

Use the absolute `/usr/bin` path so a separate `~/.local/bin/mako-launch` cannot shadow the pacman-owned launcher. Flatpak games additionally require the matching MAKO Flatpak runtime extension and application preparation described in the [Flatpak guide](../../docs/FLATPAK-GUIDE.md).

Use the complete launch option shown by `/usr/bin/mako-ui` when shaders or an explicit profile are selected.

## Build from the tracked recipe

To reproduce the package from the official archive instead, build and install from this directory:

```bash
cd engine/dist/arch
makepkg -si
```

`makepkg` downloads the release archive, verifies `sha256sums`, and repackages the extracted payload. The package does not run a compiler.

## Build the complete local Renderer artifact set

From the repository root, build release-shaped host, Flatpak, and Arch artifacts from the current checkout with:

```bash
just package-renderer-local-release
```

The equivalent component command is `engine/scripts/package-local-release.sh`. It writes all three artifacts and `SHA256SUMS` under `engine/out/`, uses the same portable builders as publication, and verifies the pacman package against the newly built host archive. It synchronizes a disposable copy of this recipe, so the tracked public archive checksum, Decky Renderer pin, tags, and releases remain unchanged.

## Upgrade

This package is not currently published through an official pacman repository or the AUR, so `pacman -Syu` alone cannot discover a new MAKO version. Download the newer `.pkg.tar.zst` from the matching GitHub release and run `sudo pacman -U` again. Pacman replaces all package-owned files under `/usr`. User profiles and configuration data are not package-owned and are never touched during an upgrade. The `pre_upgrade` hook re-reports any user-local MAKO install it detects.

## Remove

If native Remote Play is enabled, [restore Steam Client](../../../plugin/docs/REMOTE-PLAY.md#restore-steam) before removing the package.

```bash
sudo pacman -R mako-renderer-bin
```

Pacman removes only the files the package owns under `/usr`. Profiles and configuration under `~/.config/mako-render` are preserved and must be removed manually if they are no longer wanted. A user-local MAKO install, including MAKO Decky's, is left untouched.

Do not run the upstream installer or extract the archive with `MAKO_INSTALL_PREFIX=/usr`. That writes the same `/usr/bin` and `/usr/share` paths this package owns: pacman then refuses the next install or upgrade with `exists in filesystem`, and the upstream uninstaller would delete files pacman still believes it owns. If pacman reports a file conflict, use `pacman -Qo` to check whether the file is unowned, remove it, and install again; do not reach for `--overwrite`.

## Coexistence with MAKO Decky and other user-local installs

The pacman package manages `/usr`; MAKO Decky and the standalone archive share a separate native installation under `~/.local`. Pacman reports user-local copies but does not change or remove them. Decky does not adopt the system package or run pacman. First-time Decky Renderer setup explains that selecting Install creates a separate user-local copy. You can keep the system package and explicitly choose that local installation; keep their versions aligned because they share profiles. An already existing user-local payload remains updateable and removable independently. Removing the Decky plugin preserves that user-local Renderer.

Use `/usr/bin/mako-launch` and `/usr/bin/mako-ui` explicitly to select this package. Do not export `ENABLE_MAKO=1` globally: use the launcher to select the matching layers. Flatpak runtime extensions are installed and updated separately.

## Profiles and configuration data

Profiles, settings, and runtime state live in `~/.config/mako-render` (`conf.toml`, `profile-metadata.json`, `profile-wrapper-settings.json`, and `runtime-state/`). They belong to the user and are never installed, replaced, or removed by this package.

Because both delivery paths share that directory, do not run two different MAKO Renderer versions against one `~/.config/mako-render`: a user-local install and this package would migrate the same profiles with different engine versions. Use explicit launchers to choose an installation and keep Renderer releases/builds aligned before editing their shared profiles.

## Updating for a new release

The publisher synchronizes the recipe with the verified Renderer archive. See [Arch package maintenance](../../docs/BUILDING-FROM-SOURCE.md#arch-package-maintenance) for pin updates and packaging-only fixes.

## AUR publication

The release workflow publishes packages on GitHub, not the AUR. AUR publication is a separate maintainer workflow; see [the build guide](../../docs/BUILDING-FROM-SOURCE.md#aur-publication).

## Verification

The package is checked against the host archive's checksums, required files, architectures, dependencies and Vulkan manifests. The native Remote Play SDR helper is the only reviewed ELF under `/usr/share`; it retains the same 64-bit and dependency checks as the host tools. The installed checksum manifest is `/usr/share/doc/mako-renderer-bin/MAKO-Renderer-install-manifest.txt`. See [maintainer verification](../../docs/BUILDING-FROM-SOURCE.md#verification) for the complete contract.

## Links

- [Upstream README](../../../README.md)
- [Engine README](../../README.md)
- [Flatpak guide](../../docs/FLATPAK-GUIDE.md)
- [Troubleshooting](../../docs/TROUBLESHOOTING.md)
- [Collect diagnostics](../../docs/COLLECT_DIAGNOSTICS.md)
- [Installation transactions](../../../INSTALLATION-TRANSACTIONS.md)
- [Cleanups](../../../CLEANUPS.md)
