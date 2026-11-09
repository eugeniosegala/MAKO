---
name: mako-build-decky-tester-zip
description: Build and verify a complete portable MAKO Decky tester ZIP from the current MAKO checkout, including locally built 64-bit, 32-bit, and Flatpak Renderer payloads. Use for a full tester ZIP, not local deployment or publication.
---

# Build a MAKO Decky tester ZIP

Use MAKO's owning packager so tester archives retain the same portable native and Flatpak builders used by publication.

Execution boundary: the host orchestrates and verifies the ZIP. Docker or Podman compiles the native Renderer in the Ubuntu 22.04 portable builder and builds Flatpak bundles in the Ubuntu 24.04 builder; the containers do more than cache downloads. The Flatpak cache persists in a container volume, while outputs return to the checkout.

## Workflow

1. Locate the MAKO repository root and confirm it contains `AGENTS.md`, `plugin/package.json`, and `engine/`. Read the current `AGENTS.md` and `plugin/docs/PACKAGING.md`; they remain authoritative if this skill becomes stale.
2. Inspect the branch and worktree. Preserve all user changes. A dirty checkout is allowed for a tester build and is represented in the local artifact identity.
3. Tell the user the portable complete build is starting and may take several minutes. Run the command and container-runtime checks on the actual host (`exec_command` with `sandbox_permissions: "require_escalated"` in Codex); the default isolated shell may not access the host Podman/Docker state. From the repository root run exactly:

   ```bash
   MAKO_PORTABLE_PACKAGE=1 pnpm --dir plugin run package:local-engine
   ```

4. Do not set `MAKO_RELEASE_SKIP_TESTS` unless the user explicitly requests the documented maintainer exception. Let the command select Docker or Podman and request execution approval if the environment requires it.
5. Treat the packager's `Created and verified:` path as the output. Confirm that ZIP with `unzip -t`, record its size and SHA-256, and return a clickable absolute file link.

The result normally lives under `plugin/out/` and embeds the current locally built Renderer host and Flatpak payloads. All payloads must carry, beside the Renderer, the exact MAKO FG release selected by `engine/models/mako-fg/versions.json` → `active`; do not hardcode its version. The builders install the active release from `engine/models/mako-fg` as a separate program beside the 64-bit Renderer and verify that no Frame Generation layer contains it, while archive reuse requires matching `.mako-fg.json` receipts. Missing or stale receipts trigger a rebuild. Report the verified release from the build log. Testers need only the ZIP; its payloads already carry the program and model. Do not substitute the Decky-only `--local-plugin` path or the 64-bit-only fast package for this workflow.

This skill builds an artifact only. It does not install or reload Decky, mutate the installed Renderer, commit, push, tag, publish, or change release pins.
