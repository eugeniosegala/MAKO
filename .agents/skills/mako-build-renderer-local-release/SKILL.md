---
name: mako-build-renderer-local-release
description: "Build and verify the complete release-shaped MAKO Renderer artifact set locally: host archive, Flatpak bundle, Arch package, and checksums. Use to simulate Renderer packaging without publishing."
---

# Build a local MAKO Renderer release set

Use the canonical aggregate command rather than assembling release formats independently.

Execution boundary: the host runs the aggregate script and checksums. Docker or Podman compiles the native Renderer in Ubuntu 22.04 and builds Flatpak bundles in Ubuntu 24.04. The Arch package is assembled on the host when `makepkg` and `fakeroot` exist, or in an Arch container otherwise. Container builds write artifacts to the checkout; the Flatpak cache uses a persistent container volume.

## Workflow

1. Locate the MAKO repository root and confirm it contains `AGENTS.md`, `justfile`, and `engine/scripts/package-local-release.sh`. Read the current `AGENTS.md` and `engine/docs/BUILDING-FROM-SOURCE.md`; they remain authoritative if this skill becomes stale.
2. Inspect the branch and worktree and preserve all user changes. Tell the user the portable build is starting and may take several minutes.
3. Run the build and container-runtime checks on the actual host (`exec_command` with `sandbox_permissions: "require_escalated"` in Codex); the default isolated shell may not access host Podman/Docker or `just`. From the repository root run:

   ```bash
   just package-renderer-local-release
   ```

   If `just` is missing on the host, run its direct recipe command, `./engine/scripts/package-local-release.sh`, instead. Both invoke the same canonical aggregate script, which owns the portable host build, every declared Flatpak runtime, the disposable locally synchronized Arch recipe, and the checksum manifest. Its builders require MAKO FG and verify the exact release selected by `engine/models/mako-fg/versions.json` → `active` for both native architectures and both architectures of every Flatpak runtime. Report the verified release from the build log; do not hardcode a model version. Keep the generated `.mako-fg.json` receipts with cached native and Flatpak archives.
4. Do not set `MAKO_RELEASE_SKIP_TESTS` unless the user explicitly requests the documented maintainer exception. Request execution approval when container or network access requires it.
5. On success, run `sha256sum --check engine/out/SHA256SUMS`. Inspect the Arch package's `.PKGINFO` with `bsdtar`, confirm the expected dual-bitness host contents and declared Flatpak bundles, and report sizes plus clickable absolute links for all artifacts and `SHA256SUMS`.

If the canonical command fails, preserve completed output and diagnose the owning script. Do not silently manufacture an ad hoc package that bypasses its checks. Repair the canonical workflow only when the request includes getting the build operational, then rerun or resume through that workflow and disclose the repair.

This is release-shaped local packaging, not a release. Do not commit, push, change tracked release pins, tag, upload, or publish.
