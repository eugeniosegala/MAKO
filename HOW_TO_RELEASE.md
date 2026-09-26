# How to release MAKO

The standard release ships MAKO Renderer and MAKO Decky as a matched `X.Y.Z` pair. Every new paired release advances both component versions; reuse a version only to resume that same incomplete release. Release from the current clean, pushed `main` commit and preserve the enforced order: **Renderer → checksum pin → Decky**.

## Release stages

| Stage | Command | Result |
| --- | --- | --- |
| Focused local iteration | The `dev:*` commands in [MAKO Decky packaging](plugin/docs/PACKAGING.md) | Updates the local development installation; no package or release |
| Complete tester package | `MAKO_PORTABLE_PACKAGE=1 pnpm --dir plugin run package:local-engine` | Self-contained ZIP using the release native builder for trusted testing; no tag or release |
| Release-candidate check | `./scripts/check-release-candidate.sh PATH-TO-LOCAL-DECKY-ZIP` | Checks the already-built complete ZIP against the clean pushed source commit, package contract, both native bitnesses, and Flatpak runtime list; no rebuild or installation |
| Publication | `./scripts/publish-release.sh X.Y.Z` | Publishes Renderer, records immutable checksums, then publishes Decky |
| Public-asset check | Download the released Decky ZIP and select **Install MAKO Renderer** | Verifies the actual public artifact and installation path |

Fast native-only packages and direct deployments are intentionally incomplete and cannot become release candidates.

## One-time setup

- Install the [Renderer build prerequisites](engine/docs/BUILDING-FROM-SOURCE.md).
- Authenticate GitHub CLI with `gh auth login -h github.com`.
- Confirm `origin` targets this repository and the worktree is clean.
- Prepare the SteamOS test machine and complete any affected manual game checks described in [Testing](TESTING.md). A compatible MAKO Gym checkout is needed only for hardware suites you choose to run.

Published host archives must remain compatible with Qt 6.4. Publication selects the portable native builder in `engine/scripts/package-local.sh`: Ubuntu 22.04, Clang 14, Qt 6.2, and the Vulkan headers pinned in [`engine/vulkan-headers-revision.txt`](engine/vulkan-headers-revision.txt). Use that builder for the complete local candidate too. Docker or Podman is required for native publication on Linux as well as other hosts. Flatpak extensions use the same Vulkan-Headers pin while retaining their separate runtime-specific compilers, libraries, and verification. Native and Flatpak packages also include the exact dual-architecture vkBasalt fork release pinned in [`engine/vkbasalt-release.json`](engine/vkbasalt-release.json); retain its fork workflow, artifact attestation, checksum, and source/upstream commits with release evidence.

## Keep tester and release builds aligned

Source identity alone is insufficient: Vulkan extension macros can change compiled presentation handling. In 3.2.0, older native build headers omitted `VkPresentId2KHR` support that was present in the earlier tester binary. The 3.2.1 hotfix restored the tester's native build setup; the exact cause of every reported game failure was not confirmed.

- Build complete tester ZIPs with `MAKO_PORTABLE_PACKAGE=1`. If the builder or SDK changes, move the matching cached native archive out of `engine/out/` before rebuilding; local Decky cache keys track source identity, not the host toolchain. Publication rebuilds native archives independently.
- Keep the shared package header check enabled for both 64-bit and 32-bit native and Flatpak builds. `MAKO_REQUIRE_NATIVE_PACKAGE_HEADERS=ON` requires headers at least as new as [`engine/vulkan-headers-revision.txt`](engine/vulkan-headers-revision.txt), with `VK_KHR_present_id2` and `VK_EXT_present_timing`, even when tests are skipped. It checks build-time declarations and does not raise the game's Vulkan runtime requirement. Do not turn required compatibility support into an optional compile-time path without an equivalent fallback or a package failure.
- Retain the exact tester ZIP, its SHA-256, embedded Renderer hashes and binary fingerprint, source commit and dirty state, build log, compiler/Qt/Vulkan header versions, pinned vkBasalt asset identity, and Flatpak SDK/runtime identities. Record the devices, driver, Proton version, games, and results against that artifact. After changing the header pin, regenerate the shared Flatpak module with `just generate-flatpak-headers`. After changing the vkBasalt pin, regenerate its module with `just generate-vkbasalt-release`. Package and portable gates reject stale generated modules. Do not identify tested content by filename or source commit alone.
- Treat any builder, header, compiler, build-option, or runtime SDK change as a new candidate requiring affected compatibility coverage. The container recipe fixes the baseline but still receives distribution package updates; it does not guarantee reproducible bytes. Record the resolved versions from each build.
- After publication, download and verify the actual public assets, check that Decky's embedded Renderer matches the standalone archive, and complete the public installation and affected-game checks below. A successful build or checksum check does not establish game compatibility.

## Prepare the release

Write the user-facing “What’s new” copy in:

- [MAKO Renderer release notes](engine/RELEASE_NOTES.md)
- [MAKO Decky release notes](plugin/RELEASE_NOTES.md)

For a paired release, give both files the same codename and root `assets/<lowercase-codename>.png` banner. Keep one PNG for each new release illustration and reference it directly without a duplicate WebP variant. Preserve historical image paths referenced by published release notes, including the existing Sea Rapture WebP. Update each first heading to the new version and commit both files with the release changes. The release builds validate the note headings, content, version, and shared codename. These two files are the only manual release copy; versions, pins, asset URLs, checksums, and README/website links are script-owned.

Push the candidate commit and wait for the normal portable `Tests` workflow. Reuse an existing complete portable ZIP from that exact clean commit, or build one on the SteamOS test machine with the same portable builders used by publication. Check that exact ZIP:

```bash
MAKO_PORTABLE_PACKAGE=1 pnpm --dir plugin run package:local-engine
./scripts/check-release-candidate.sh plugin/out/MAKO-Decky-local.<identity>.zip
```

Use the exact ZIP path printed by the package command in place of the example identity. The check verifies integrity, embedded Renderer checksum and source commit, both x86 bitnesses, and all supported Flatpak bundles without another build or an installation simulation. Retain its SHA-256 and build identity with your game-test notes. Run relevant MAKO Gym suites ad hoc when a change needs hardware measurements; [Testing](TESTING.md) maps boundaries to suites. Record what was run, reused, or not tested. Complete the applicable manual game matrix; portable CI and a package contract cannot establish game smoothness or compatibility.

The local candidate ZIP is not promoted byte-for-byte: publication makes its own release-version and pin commits and rebuilds the public artifacts in the enforced order. The final public-asset installation check remains a separate required stage.

## Publish

From the repository root, replace `1.2.0` with the release version:

```bash
./scripts/publish-release.sh 1.2.0
```

The publisher:

1. validates both release-note files and shared codename;
2. versions, tests, builds, and publishes the 64-bit/32-bit Renderer host archive and supported Flatpak bundles;
3. records the exact Renderer tag, source commit, URLs, and checksums in `plugin/package.json`, synchronizes the Arch `mako-renderer-bin` recipe to that immutable host archive, verifies both pins, and publishes the exact verified `.pkg.tar.zst` alongside the Renderer archives without installing it;
4. versions, tests, packages, and publishes the checksum-pinned MAKO Decky ZIP as GitHub's **Latest** release;
5. updates release links, pushes `main`, triggers the website deployment from canonical metadata, and verifies remote tags, Renderer asset checksums and pins, and Decky asset presence; and
6. removes rebuildable release output and disposable staging after verification while preserving reusable caches.

Do not manually edit generated version links or pins.

### Maintainer-directed hotfix without automated validation

Only when the maintainer explicitly requests skipping automated validation, run the publisher with `MAKO_RELEASE_SKIP_TESTS=1`. This omits Renderer CTest/launcher tests and Decky suites, and adds `[skip ci]` to release-owned commits. Build, ABI, archive-layout, package-contract, and checksum verification still run. Record the skipped validation in the release evidence; this path does not establish game or hardware validation.

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

Verify the final pushed `main` commit and its GitHub Pages deployment before calling the website updated. The website derives both component versions and its Decky ZIP, Renderer host, and Flatpak download links from `plugin/package.json`; the Arch package is available on the Renderer release page. Check those live links and assets, plus the Renderer and Decky release links in the root, Renderer, and Decky READMEs. If `[skip ci]` prevented Pages from running, dispatch `.github/workflows/pages.yml` on `main` and verify that deployment before declaring the public release complete.
