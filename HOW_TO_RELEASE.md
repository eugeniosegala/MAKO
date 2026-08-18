# How to release MAKO

MAKO Renderer and MAKO Decky use the same `X.Y.Z` release number. Run the
release from a clean `main` checkout after committing the changes you want to
ship.

## One-time setup

- Install the normal [Renderer build prerequisites](engine/docs/BUILDING-FROM-SOURCE.md).
- Install and authenticate GitHub CLI with `gh auth login -h github.com`.
- Confirm `git status` is clean and the `origin` remote points to this repository.

## Publish both packages

First, manually write the user-facing “What’s new” copy in both files:

- [MAKO Renderer release notes](engine/RELEASE_NOTES.md)
- [MAKO Decky release notes](plugin/RELEASE_NOTES.md)

Change the version in each file’s first heading and edit the Markdown beneath
it in the tone you want for that release. Commit both files with the changes
being released. The publisher rejects a missing, empty, or stale heading before
it changes a version or starts a build.

Then, from the repository root, replace `1.2.0` with the new version:

```bash
./scripts/publish-release.sh 1.2.0
```

That one command:

1. Validates both manually curated “What’s new” files for `1.2.0`.
2. Updates and commits `engine/VERSION`.
3. Tests and builds the 64-bit and 32-bit host Renderer archive plus the 23.08,
   24.08, and 25.08 Flatpak bundles.
4. Publishes `render-v1.2.0`, calculates its checksums, and commits the exact
   archive URLs, checksums, tag, and source commit to `plugin/package.json`.
5. Updates and commits the Decky version, tests and verifies its bundled
   Renderer payload, and builds the Decky ZIP.
6. Publishes `plugin-v1.2.0` as GitHub's **Latest** release.
7. Updates all versioned README release links, pushes `main`, and verifies that
   the remote release-asset checksums, pins, tags, and worktree agree.

No version, checksum, binary URL, Flatpak pin, or README release link needs to
be edited manually. The two “What’s new” files are intentionally the only
manual release content; the stable installation, update, in-game, limitations,
and payload sections are assembled by the component publishers.

## Resume or publish one component

The top-level command is resumable. If a complete Renderer or Decky release
already exists, it verifies and skips that component. Fix the reported cause
and run the same command again after an interrupted release.

The component publishers are also available when deliberately resuming one
stage:

```bash
./engine/scripts/publish-package.sh --version 1.2.0
./plugin/scripts/publish-package.sh --version 1.2.0
```

Publish Renderer first. Decky refuses to publish until its checksum-verified
Renderer pin matches the requested version.

Do not move an existing release tag or replace an asset by hand. If released
content must change, publish a new version.

## Final check

The command prints both release pages when verification succeeds. Confirm the
Decky page is marked **Latest**, then install its ZIP on a test SteamOS device
and select **Install MAKO Renderer** in the plugin.
