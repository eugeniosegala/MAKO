# How to release MAKO

MAKO Renderer and MAKO Decky normally ship as a matched pair with the same `X.Y.Z` release number. Run the release from a clean `main` checkout after committing the changes you want to ship.

## Release lifecycle at a glance

MAKO deliberately separates development, tester packaging, release-candidate validation, and publication:

| Cycle | Purpose | Entry point | Result |
| --- | --- | --- | --- |
| Local iteration | Exercise a focused frontend, backend, native Renderer, host, or Flatpak change on the development machine | The `dev:*` commands in [MAKO Decky packaging](plugin/docs/PACKAGING.md) | Directly updates the installed development plugin; creates no release |
| Tester package | Validate installation and upgrades with a self-contained package | `pnpm --dir plugin run package:local-engine` | Produces a complete local ZIP that can be sent to trusted testers; creates no tag or release |
| Release candidate | Rebuild the committed, pushed source in a clean checkout on the dedicated SteamOS/AMD host and exercise it through MAKO-Gym | `./scripts/run-steamos-hardware-validation.sh --deploy-to-decky` | Retains the verified ZIP and evidence for 14 days and optionally installs that exact ZIP; publishes nothing |
| Published release | Publish immutable matched artifacts after the release candidate and manual game matrix pass | `./scripts/publish-release.sh X.Y.Z` | Publishes MAKO Renderer, pins it by checksum, then publishes MAKO Decky |
| Published-package check | Prove the public asset installs through the user-facing path | Download the new MAKO Decky ZIP and use **Install MAKO Renderer** | Confirms the released asset, not a local or CI copy |

The fast native-only package and direct deployment paths are intentionally incomplete and must not be promoted as release candidates. The hardware gate validates and may deploy a candidate, but only the publisher creates tags, GitHub releases, immutable assets, and the final Renderer pin.

## Recommended release order

For a normal paired release, always use this order:

1. Publish **MAKO Renderer** first so its immutable archives, Flatpak bundles, source commit, and checksums exist.
2. Pin that exact Renderer release in `plugin/package.json` using the release tooling.
3. Build and publish **MAKO Decky** from the checksum-verified pin.

Treat **Renderer → pin → Decky** as the strongly recommended release strategy. A deliberate component-only release can be appropriate, so the pairing itself is not an absolute policy requirement. However, never publish MAKO Decky against an unverified, local-only, or stale Renderer payload; the Decky publisher enforces that safety boundary. The top-level publisher follows the recommended paired order automatically and should be preferred for routine releases.

## One-time setup

- Install the normal [renderer build prerequisites](engine/docs/BUILDING-FROM-SOURCE.md).
- Install and authenticate GitHub CLI with `gh auth login -h github.com`.
- Confirm `git status` is clean and the `origin` remote points to this repository.
- Prepare the dedicated SteamOS/AMD test machine described in [Testing](TESTING.md). The release gate launcher creates and removes its `steamos` + `amd-gpu` runner for each job; do not leave a persistent repository runner online.
- Keep a clean, initialized private `MAKO-Gym` checkout beside MAKO, run its `./scripts/check.sh`, and ensure its contract version matches MAKO before registering the hardware runner. The release launcher enforces these boundaries.

The renderer packager rejects a `mako-ui` binary that requires a Qt ABI newer than 6.4. A Linux host with Qt 6.2–6.4 needs no container. Non-Linux packaging requires Docker or Podman. When a rolling Linux distribution only provides a newer Qt, either runtime is an optional compatibility fallback: prefix the release command with `MAKO_PORTABLE_PACKAGE=1` to build the UI against Ubuntu 22.04's Qt 6.2 baseline.

## Publish both packages

First, manually write the user-facing “What’s new” copy in both files:

- [MAKO Renderer release notes](engine/RELEASE_NOTES.md)
- [MAKO Decky release notes](plugin/RELEASE_NOTES.md)

Give each normal paired release one shared codename without changing its semantic version, tags, or archive names. Store its shared banner in the root `assets/` directory as the lowercase, hyphenated codename alone—for example, `assets/leviathan-rising.png`—and reference the same codename and image from both component release-note files. MAKO Decky reads its version from `plugin/package.json` and its codename from the Decky release notes at build time for the subtle current-release identity at the top of the plugin; the frontend build rejects missing or mismatched release metadata.

Change the version in each file’s first heading and edit the Markdown beneath it in the tone you want for that release. Commit both files with the changes being released. The publisher rejects a missing, empty, or stale heading before it changes a version or starts a build.

Run the **SteamOS hardware validation** workflow for that commit and require it to pass before publishing:

```bash
./scripts/run-steamos-hardware-validation.sh --deploy-to-decky
```

Omit `--deploy-to-decky` when the machine is not the dedicated MAKO Decky test installation. Review the retained GPU comparisons, MAKO-Gym 47-case feature summary and logs, 66-case quality summary, 20-row default recovery summary, recorded Gym commit, and sanitized environment evidence; a green CPU-only pull-request workflow is not a substitute for this gate. The launcher preserves only scoped reusable caches and removes its runner, checkout, credentials, staging, and generated outputs when the job ends.

Then, from the repository root, replace `1.2.0` with the new version:

```bash
./scripts/publish-release.sh 1.2.0
```

That one command:

1. Validates both manually curated “What’s new” files for `1.2.0`.
2. Updates and commits `engine/VERSION`.
3. Tests and builds the 64-bit and 32-bit host renderer archive plus the 23.08, 24.08, and 25.08 Flatpak bundles.
4. Publishes `render-v1.2.0`, calculates its checksums, and commits the exact archive URLs, checksums, tag, and source commit to `plugin/package.json`.
5. Updates and commits the plugin version, tests and verifies its bundled renderer payload, and builds the MAKO Decky ZIP.
6. Publishes `plugin-v1.2.0` as GitHub's **Latest** release.
7. Updates all versioned README release links, pushes `main`, and verifies that the remote release-asset checksums, pins, tags, and worktree agree.
8. After complete verification only, removes the reproducible local release archives, generated frontend/coverage output, and disposable build staging while preserving the reusable compiler, SDK, Flatpak, dependency, and container caches.

No version, checksum, binary URL, Flatpak pin, or README release link needs to be edited manually. The two “What’s new” files are intentionally the only manual release content; the stable installation, update, in-game, limitations, and payload sections are assembled by the component publishers.

## Resume or publish one component

The top-level command is resumable. If a complete component release already exists, it verifies and skips that component. Fix the reported cause and run the same command again after an interrupted release.

The component publishers are also available when deliberately resuming one stage:

```bash
./engine/scripts/publish-package.sh --version 1.2.0
./plugin/scripts/publish-package.sh --version 1.2.0
```

When resuming a paired release manually, preserve the same **Renderer → pin → Decky** order. The Renderer publisher writes the pin after its assets exist, and the plugin publisher refuses to continue until that checksum-verified Renderer pin matches the requested version.

Do not move an existing release tag or replace an asset by hand. If released content must change, publish a new version.

## Final check

The command prints both release pages when verification succeeds. Confirm the MAKO Decky page is marked **Latest**, download its published ZIP rather than reusing the retained release-candidate artifact, then install it on a test SteamOS device and select **Install MAKO Renderer** in the plugin. This last pass validates the actual public download and user-facing installation path.
