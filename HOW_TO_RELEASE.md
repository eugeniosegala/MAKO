# How to release MAKO

MAKO Renderer and MAKO Decky normally ship as a matched `X.Y.Z` pair. Release from a clean, pushed `main` commit and preserve the enforced order: **Renderer → checksum pin → Decky**.

## Release stages

| Stage | Command | Result |
| --- | --- | --- |
| Focused local iteration | The `dev:*` commands in [MAKO Decky packaging](plugin/docs/PACKAGING.md) | Updates the local development installation; no package or release |
| Complete tester package | `pnpm --dir plugin run package:local-engine` | Self-contained ZIP for trusted testing; no tag or release |
| Release-candidate gate | `./scripts/run-steamos-hardware-validation.sh --deploy-to-decky` | Clean SteamOS/AMD rebuild, complete MAKO Gym validation, retained ZIP/evidence, and optional deployment |
| Publication | `./scripts/publish-release.sh X.Y.Z` | Publishes Renderer, records immutable checksums, then publishes Decky |
| Public-asset check | Download the released Decky ZIP and select **Install MAKO Renderer** | Verifies the actual public artifact and installation path |

Fast native-only packages and direct deployments are intentionally incomplete and cannot become release candidates.

## One-time setup

- Install the [Renderer build prerequisites](engine/docs/BUILDING-FROM-SOURCE.md).
- Authenticate GitHub CLI with `gh auth login -h github.com`.
- Confirm `origin` targets this repository and the worktree is clean.
- Prepare the dedicated SteamOS/AMD host described in [Testing](TESTING.md). The launcher creates a disposable one-job runner; do not leave a persistent public-repository runner online.
- Keep a clean private `MAKO-Gym` checkout beside MAKO. Its `./scripts/check.sh` and contract version must pass before runner registration.

Published host archives must remain compatible with Qt 6.4. Hosts with Qt 6.2–6.4 need no container; non-Linux hosts need Docker or Podman. On a rolling Linux host with newer Qt, set `MAKO_PORTABLE_PACKAGE=1` to build against the Ubuntu 22.04 Qt 6.2 baseline.

## Prepare the release

Write the user-facing “What’s new” copy in:

- [MAKO Renderer release notes](engine/RELEASE_NOTES.md)
- [MAKO Decky release notes](plugin/RELEASE_NOTES.md)

For a paired release, give both files the same codename and root `assets/<lowercase-codename>.png` banner. Update each first heading to the new version and commit both files with the release changes. The publisher rejects missing, empty, stale, or mismatched metadata. These two files are the only manual release copy; versions, pins, asset URLs, checksums, and README/website links are script-owned.

Run the hardware gate for that commit:

```bash
./scripts/run-steamos-hardware-validation.sh --deploy-to-decky
```

Omit `--deploy-to-decky` unless the host is the dedicated MAKO Decky test installation. Review the retained package identity, sanitized environment evidence, and every MAKO Gym summary: 48 feature, 74 quality, 18 LSFG-performance, 36 spatial-performance, 14 runtime-overhead, eight synchronization-validation, nine repeatability, 31 recovery, twelve native Gamescope E2E, twelve Proton E2E, and ten Proton-compatibility cases across at least four runtime families. Complete the applicable manual game matrix as well; portable CI and synthetic hardware workloads are not substitutes for it.

## Publish

From the repository root, replace `1.2.0` with the release version:

```bash
./scripts/publish-release.sh 1.2.0
```

The publisher:

1. validates both release-note files and shared codename;
2. versions, tests, builds, and publishes the 64-bit/32-bit Renderer host archive and supported Flatpak bundles;
3. records the exact Renderer tag, source commit, URLs, and checksums in `plugin/package.json`;
4. versions, tests, packages, and publishes the checksum-pinned MAKO Decky ZIP as GitHub's **Latest** release;
5. updates release links, pushes `main`, triggers the website deployment from canonical metadata, and verifies remote tags, assets, checksums, and pins; and
6. removes reproducible release output and disposable staging after verification while preserving reusable caches.

Do not manually edit generated version links or pins.

## Resume an interrupted release

The top-level command is resumable: it verifies and skips any already-complete component. Fix the reported cause and rerun the same command.

For a deliberate component-only resume:

```bash
./engine/scripts/publish-package.sh --version 1.2.0
./plugin/scripts/publish-package.sh --version 1.2.0
```

Maintain **Renderer → pin → Decky**. Never move an existing tag or replace a published asset; publish a new version when released content must change.

## Verify the public package

After the command reports success, confirm the Decky release is **Latest**, download its published ZIP rather than the retained candidate, install it on a test SteamOS device, and select **Install MAKO Renderer**. This final check validates the public download and normal user-facing installation path.
