---
name: mako-publish-release
description: Prepare, publish, and verify a matched public MAKO Renderer and MAKO Decky release. Use for an authorized release, not local deployment or tester packaging.
---

# Publish MAKO

Locate the MAKO checkout and read its current `AGENTS.md` and [HOW_TO_RELEASE.md](../../../HOW_TO_RELEASE.md). The guide and owning scripts are authoritative; follow their current instructions rather than a copied procedure or a commit SHA stored in this skill.

For each new paired release, choose one new `X.Y.Z` that advances both MAKO Renderer and MAKO Decky. Reuse a version only when resuming that exact incomplete release. Prepare and commit the two release-note files with the release changes. Push the candidate `main`, confirm the worktree is clean and `HEAD` matches `origin/main`, and record that commit's SHA. Follow the guide's complete local candidate ZIP check and applicable game matrix. Run MAKO Gym only when the changed boundary calls for targeted hardware evidence, and record what was not tested.

When the user explicitly authorizes publication, run the guide's top-level `./scripts/publish-release.sh X.Y.Z`. Let it publish the versioned Renderer host archive, Flatpak bundles, and verified Arch package first, then record the immutable Renderer checksums and source commit in Decky's pin, then version and publish the Decky ZIP. Use component commands only for a documented interrupted-release resume. Keep the pinned Vulkan-Headers and vkBasalt checks, release tests, and package verification active unless the guide's explicit maintainer exception applies. Never move a published tag, replace an asset, or manually edit script-owned pins and links.

Complete the guide's public-asset installation check. Verify Renderer and Decky release assets, pinned hashes, README links, the final GitHub Pages deployment, and live website download links before reporting the release complete. Report the published version, source and release commits, tags, artifact identities, validation evidence, and any unfinished checks. If the request is only to prepare a release, stop before publication and report readiness.
