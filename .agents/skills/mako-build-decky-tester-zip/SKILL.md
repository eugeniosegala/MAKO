---
name: mako-build-decky-tester-zip
description: Build and verify a complete portable MAKO Decky tester ZIP from the current MAKO checkout, including locally built 64-bit, 32-bit, and Flatpak Renderer payloads. Use for a full tester ZIP, not local deployment or publication.
---

# Build a MAKO Decky tester ZIP

Use MAKO's owning packager so tester archives retain the same portable native and Flatpak builders used by publication.

## Workflow

1. Locate the MAKO repository root and confirm it contains `AGENTS.md`, `plugin/package.json`, and `engine/`. Read the current `AGENTS.md` and `plugin/docs/PACKAGING.md`; they remain authoritative if this skill becomes stale.
2. Inspect the branch and worktree. Preserve all user changes. A dirty checkout is allowed for a tester build and is represented in the local artifact identity.
3. Tell the user the portable complete build is starting and may take several minutes. From the repository root run exactly:

   ```bash
   MAKO_PORTABLE_PACKAGE=1 pnpm --dir plugin run package:local-engine
   ```

4. Do not set `MAKO_RELEASE_SKIP_TESTS` unless the user explicitly requests the documented maintainer exception. Let the command select Docker or Podman and request execution approval if the environment requires it.
5. Treat the packager's `Created and verified:` path as the output. Confirm that ZIP with `unzip -t`, record its size and SHA-256, and return a clickable absolute file link.

The result normally lives under `plugin/out/` and embeds the current locally built Renderer host and Flatpak payloads. Do not substitute the Decky-only `--local-plugin` path or the 64-bit-only fast package for this workflow.

This skill builds an artifact only. It does not install or reload Decky, mutate the installed Renderer, commit, push, tag, publish, or change release pins.
