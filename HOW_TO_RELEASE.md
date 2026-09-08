# How to release MAKO

MAKO Renderer and MAKO Decky normally ship as a matched `X.Y.Z` pair. Release from a clean, pushed `main` commit and preserve the enforced order: **Renderer → checksum pin → Decky**.

## Release stages

| Stage | Command | Result |
| --- | --- | --- |
| Focused local iteration | The `dev:*` commands in [MAKO Decky packaging](plugin/docs/PACKAGING.md) | Updates the local development installation; no package or release |
| Complete tester package | `MAKO_PORTABLE_PACKAGE=1 pnpm --dir plugin run package:local-engine` | Self-contained ZIP using the release native builder for trusted testing; no tag or release |
| Release-candidate gate | `./scripts/run-steamos-hardware-validation.sh --gym-suite <affected-suite> --gym-reason '<why>' --deploy-to-decky` | Clean SteamOS/AMD rebuild, explicitly selected MAKO Gym evidence, retained validation ZIP/evidence that is not promoted to the GitHub release, and optional deployment |
| Publication | `./scripts/publish-release.sh X.Y.Z` | Publishes Renderer, records immutable checksums, then publishes Decky |
| Public-asset check | Download the released Decky ZIP and select **Install MAKO Renderer** | Verifies the actual public artifact and installation path |

Fast native-only packages and direct deployments are intentionally incomplete and cannot become release candidates.

## One-time setup

- Install the [Renderer build prerequisites](engine/docs/BUILDING-FROM-SOURCE.md).
- Authenticate GitHub CLI with `gh auth login -h github.com`.
- Confirm `origin` targets this repository and the worktree is clean.
- Prepare the dedicated SteamOS/AMD host described in [Testing](TESTING.md). The launcher creates a disposable one-job runner; do not leave a persistent public-repository runner online.
- Keep a clean private `MAKO-Gym` checkout beside MAKO. Its `./scripts/check.sh` and contract version must pass before runner registration.

Published host archives must remain compatible with Qt 6.4. Publication always selects the portable native builder in `engine/scripts/package-local.sh`, matching the SteamOS release gate: Ubuntu 22.04, Clang 14, Qt 6.2, and Vulkan headers 1.4.328. Docker or Podman is required for native publication on Linux as well as other hosts. Flatpak extensions retain their separate runtime-specific SDKs and verification.

## Keep tester and release builds aligned

Source identity alone is insufficient: Vulkan extension macros can change compiled presentation handling. In 3.2.0, older native build headers omitted `VkPresentId2KHR` support that was present in the earlier tester binary. The 3.2.1 hotfix restored the tester's native build setup; the exact cause of every reported game failure was not confirmed.

- Build complete tester ZIPs with `MAKO_PORTABLE_PACKAGE=1`. If the builder or SDK changes, move the matching cached native archive out of `engine/out/` before rebuilding; local Decky cache keys track source identity, not the host toolchain. Publication and the hardware gate already rebuild native archives independently.
- Keep the native package header check enabled for both 64-bit and 32-bit builds. `MAKO_REQUIRE_NATIVE_PACKAGE_HEADERS=ON` requires headers 1.4.328 or newer with `VK_KHR_present_id2`, even when tests are skipped. It checks build-time declarations and does not raise the game's Vulkan runtime requirement. Do not turn required compatibility support into an optional compile-time path without an equivalent fallback or a package failure.
- Retain the exact tester ZIP, its SHA-256, embedded Renderer hashes and binary fingerprint, source commit and dirty state, build log, compiler/Qt/Vulkan header versions, and Flatpak SDK/runtime identities. Record the devices, driver, Proton version, games, and results against that artifact. Do not identify tested content by filename or source commit alone.
- Treat any builder, header, compiler, build-option, or runtime SDK change as a new candidate requiring affected compatibility coverage. The container recipe fixes the baseline but still receives distribution package updates; it does not guarantee reproducible bytes. Record the resolved versions from each build.
- After publication, download and verify the actual public assets, check that Decky's embedded Renderer matches the standalone archive, and complete the public installation and affected-game checks below. A successful build or checksum check does not establish game compatibility.

## Prepare the release

Write the user-facing “What’s new” copy in:

- [MAKO Renderer release notes](engine/RELEASE_NOTES.md)
- [MAKO Decky release notes](plugin/RELEASE_NOTES.md)

For a paired release, give both files the same codename and root `assets/<lowercase-codename>.png` banner. Keep one PNG for each new release illustration and reference it directly without a duplicate WebP variant. Preserve historical image paths referenced by published release notes, including the existing Sea Rapture WebP. Update each first heading to the new version and commit both files with the release changes. The release builds validate the note headings, content, version, and shared codename. These two files are the only manual release copy; versions, pins, asset URLs, checksums, and README/website links are script-owned.

Choose MAKO Gym hardware coverage from the changed production boundaries using [Testing](TESTING.md). A release does not by itself widen the selection. Select each affected suite explicitly and record a short rationale; use `--no-gym-suites` for a change with no Renderer-facing hardware boundary or when retained evidence already matches the exact gate-built package and source commit, host/driver, Gym commit, and required selection. Name that prior run in the rationale. Use `--all-gym-suites` only for an explicit maintainer-requested broad audit or a change that genuinely crosses every Gym boundary.

Run the hardware gate for that commit, for example:

```bash
./scripts/run-steamos-hardware-validation.sh \
  --gym-suite recovery \
  --gym-suite gamescope-e2e \
  --gym-reason 'Adaptive presentation and Gamescope lifecycle changed' \
  --deploy-to-decky
```

Omit `--deploy-to-decky` unless the host is the dedicated MAKO Decky test installation. Review the retained package identity, sanitized environment evidence, selection rationale, selected MAKO Gym summaries, and explicit omissions. Complete the applicable manual game matrix as well; portable CI and synthetic hardware workloads are not substitutes for it. Reuse retained evidence only when the exact gate-built package and source commit, host/driver, Gym commit, configuration, and required rows match; otherwise rerun the affected coverage.

The gate tests the exact package it builds for the pushed candidate commit. That retained ZIP is not promoted byte-for-byte: publication makes its own release-version and pin commits and independently rebuilds the public artifacts in the enforced order. The final public-asset installation check therefore remains a separate required stage.

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
5. updates release links, pushes `main`, triggers the website deployment from canonical metadata, and verifies remote tags, Renderer asset checksums and pins, and Decky asset presence; and
6. removes rebuildable release output and disposable staging after verification while preserving reusable caches.

Do not manually edit generated version links or pins.

### Maintainer-directed hotfix without automated validation

Only when the maintainer explicitly requests skipping automated validation, run the publisher with `MAKO_RELEASE_SKIP_TESTS=1`. This omits Renderer CTest/launcher tests and Decky suites, and adds `[skip ci]` to release-owned commits. Build, ABI, archive-layout, package-contract, and checksum verification still run. The hardware workflow remains a separate action and is omitted only when the maintainer also requests that exception. Record the skipped validation in the release evidence; this path does not establish game or hardware validation.

The publisher still uses the mandatory portable native builder and header checks on this path. Match the tester evidence as described above; a new release has new version metadata and binaries, so matching runtime source and toolchain is not a claim of byte-identical artifacts. `[skip ci]` also suppresses automatic Pages deployment, which can be dispatched separately when a website refresh is required.

## Resume an interrupted release

The top-level command is resumable: it checksum-verifies complete Renderer state and skips a Decky release when its version, tag, release, and expected asset name are present. Fix the reported cause and rerun the same command.

For a deliberate component-only resume:

```bash
./engine/scripts/publish-package.sh --version 1.2.0
./plugin/scripts/publish-package.sh --version 1.2.0
```

Maintain **Renderer → pin → Decky**. Never move an existing tag or replace a published asset; publish a new version when released content must change.

## Verify the public package

After the command reports success, confirm the Decky release is **Latest**, download its published ZIP rather than the retained candidate, install it on a test SteamOS device, and select **Install MAKO Renderer**. This final check validates the public download and normal user-facing installation path.

Verify the downloaded host and Flatpak archives against the checksums pinned in `plugin/package.json`, verify the ZIP integrity and embedded payload hashes, and confirm the installed Renderer reports the expected build fingerprint. Close games before updating the Renderer. For a launch-compatibility hotfix, repeat the affected game launches and retain the diagnostics; include 64-bit/32-bit and native/Flatpak paths when affected. Record unavailable rows explicitly. Keep the prior working artifacts for comparison; never replace a published asset to correct a mismatch.
