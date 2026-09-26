---
name: mako-deploy-local
description: Build, deploy, and reload MAKO Decky plus the local dual-bitness host Renderer in the installed Decky test environment. Use for live local testing, not ZIP creation or publication.
---

# Deploy MAKO locally

This workflow mutates the installed MAKO Decky development instance and replaces its host Renderer. Announce that local deployment is starting. Games using MAKO must be closed before the Renderer is replaced; stop if there is evidence that one is still running.

## Workflow

1. Locate the MAKO repository root and confirm it contains `AGENTS.md`, `plugin/package.json`, and `plugin/scripts/deploy-dev.sh`. Read the current `AGENTS.md` and the **Direct SteamOS iteration** section of `plugin/docs/PACKAGING.md`; they remain authoritative if this skill becomes stale.
2. Inspect the branch and worktree and preserve all user changes. The plugin must already be installed in Decky; let the deployment script fail closed if the configured installation cannot be found.
3. For the normal Decky plus host Renderer request, run this from the repository root:

   ```bash
   pnpm --dir plugin run dev:host -- --reload
   ```

   This deploys the frontend, Python backend, and native 64-bit and 32-bit Renderer layers, refreshes development identity, and reloads MAKO Decky. Do not use `dev:all` as a substitute because it intentionally omits the 32-bit Renderer.
4. Use the heavier Flatpak path only when the user explicitly requests Flatpak deployment or the tested change affects the runtime extensions:

   ```bash
   pnpm --dir plugin run dev:e2e -- --reload
   ```

   This stages all supported Flatpak bundles in the installed plugin. It does not install them into each target application; tell the user to use **Flatpak Setup > Update** when applicable.
5. Check the command's final deployment and reload status. Report exactly which scopes were deployed and whether reload succeeded. If the script says a protected manifest was retained, surface that warning rather than replacing it manually.

This workflow creates no ZIP and performs no publication. Do not commit, push, tag, publish, or run a full release build unless separately requested.
