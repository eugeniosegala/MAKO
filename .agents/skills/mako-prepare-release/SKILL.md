---
name: mako-prepare-release
description: Prepare MAKO Renderer and MAKO Decky release notes from the current branch, with a capitalized release codename, web-optimized shared artwork, and distinct flavor quotes. Use for local release preparation, not publication, deployment, or release-candidate packaging.
---

# Prepare Release

Prepare reviewable release materials for both MAKO components. Read the checkout's current `AGENTS.md`, component READMEs, [HOW_TO_RELEASE.md](../../../HOW_TO_RELEASE.md#notes-identity-and-artwork), and [ASSET_PROVENANCE.md](../../../ASSET_PROVENANCE.md). The release guide owns the format and publication boundary; follow its current rules instead of hardcoding a past release into this skill.

The deliverables are `engine/RELEASE_NOTES.md`, `plugin/RELEASE_NOTES.md`, one shared release PNG under root `assets/`, and an accurate asset-provenance entry. This workflow ends with local files and a preparation summary. Commit, push, branch changes, tags, live GitHub release edits, deployment, and publication require a separate explicit request; invoking this skill does not authorize them.

## Establish the candidate

- Inspect the selected branch and worktree, preserving unrelated changes. Identify each component's latest applicable released baseline from its `render-v*` or `plugin-v*` tags and release metadata. Prefer a released tag reachable from the branch; investigate mismatched or missing history instead of choosing a newer unrelated tag. Record the actual comparison refs. A comparison only against current `main` can miss unreleased changes already merged there.
- Review the log and final diff from each baseline to the candidate, including relevant pending source changes when preparing the current worktree. Distinguish those pending changes in the handoff. Check final code and available test evidence so reverted experiments, plans, and earlier-release features do not become new-feature claims.
- Resolve the intended paired `X.Y.Z` and display codename from the user's request or an established unreleased plan. A clear release branch such as `maelstrom` can suggest **Maelstrom**; a generic branch name cannot. Current notes can still describe the previous published release, so do not reuse their identity blindly. Ask only for missing identity information while continuing the history and style review; do not invent a final release version.

## Draft the two notes

Read the current notes and a couple of preceding release-note versions with `git show <ref>:engine/RELEASE_NOTES.md` and `git show <ref>:plugin/RELEASE_NOTES.md`, without checking out old branches. Infer structure, writing style, feature density, image placement, and quote/attribution format from those examples. Prefer the latest maintainer-edited concise copy when it improves on an older verbose release.

- Preserve the canonical heading format, `## What's new in MAKO Renderer vX.Y.Z` / `## What's new in MAKO Decky vX.Y.Z`, and use the same intended version in both.
- Use `### Release codename: <Display Name>` with at least the first letter capitalized and natural title capitalization for multiple words. Keep the lowercase branch and `assets/<lowercase-hyphenated-name>.png` slug separate. Decky already derives its lowercase label from the canonical notes through `read-release-info.mjs` and `MakoReleaseIdentity`; do not change that behavior or add a second codename owner.
- Keep the centered Ko-fi button and total-downloads badge between the shared banner and release codename in both notes for every release. Follow the release guide and reuse the README row with an absolute raw `main` URL for its local image.
- Lead with user benefits and concise, concrete feature/fix bullets. Explain Renderer runtime and standalone changes in the Renderer notes; explain controls, setup, and relevant bundled Renderer benefits in Decky. Preserve meaningful contributor credit. Avoid a commit-log dump, repeated features, unsupported performance percentages, and claims that portable tests prove game or device compatibility.
- Keep internal CI results and QA-only exceptions in the handoff unless users need to act on them. Include material user-facing limits accurately. The publishers append installation and standard in-game guidance; do not duplicate those sections, checksum lists, or installation boilerplate in these manually curated notes.

## Create the artwork and flavor text

1. Inspect recent shipped banners visually and measure their actual dimensions and byte sizes. Follow the established wide, roughly 3:1 Renaissance pixel-art and nautical MAKO style, with a scene and palette inspired by the new codename. Choose and state a size budget from comparable optimized banners; existing 1536×512 examples are useful references, not a permanent requirement.
2. Use the available built-in image-generation tool for original artwork and substantive edits, following its current skill/tool instructions. Inspect any reference images before using them. Save the selected result in the repository. If a suitable draft for this same unreleased codename already exists, inspect and reuse it instead of regenerating on every invocation. Preserve previously published artwork and paths.
3. Resize and compress the selected image for web use with available image utilities, preserving its aspect ratio and visual quality. Palette reduction can suit pixel art, but inspect gradients and detail afterward. Compare the final dimensions and bytes with the chosen shipped examples; do not claim optimization merely because the file is PNG. Keep one final PNG at `assets/<slug>.png`, with no duplicate WebP. Store prompts and intermediate originals under ignored `engine/out/<slug>-artwork/`, and record actual generation, source references, resizing, compression, and final dimensions in `ASSET_PROVENANCE.md`.
4. Reference that same banner in both notes using the repository's existing HTML image format, raw `main` URL, and meaningful alt text. Preview the local image: a new asset's public URL may not exist until it reaches `main`, which is not a reason to push during preparation.
5. Write **two distinct original flavor quotes**, one for Renderer and one for Decky, in the fantasy-card storytelling spirit of Magic: The Gathering. Tie both to the codename and illustration, and match MAKO's recent italic blockquote and fictional attribution convention. They should be different lines, not a duplicated quote or minor word swap. Use original MAKO fiction rather than copying card text or attributing invented words to a real source.

If image generation is unavailable, finish the notes and art brief, report the missing image explicitly, and leave preparation incomplete. Do not silently substitute an unrelated old banner or claim a new illustration was generated.

## Validate and hand off

Replace `X.Y.Z` below with the intended version, then run the existing read-only validators from the repository root:

```bash
node scripts/read-release-notes.mjs engine/RELEASE_NOTES.md "MAKO Renderer" X.Y.Z >/dev/null
node scripts/read-release-notes.mjs plugin/RELEASE_NOTES.md "MAKO Decky" X.Y.Z >/dev/null
node plugin/scripts/read-release-info.mjs engine/RELEASE_NOTES.md "MAKO Renderer"
node plugin/scripts/read-release-info.mjs plugin/RELEASE_NOTES.md "MAKO Decky"
pnpm --dir plugin run format:markdown:check
```

Compare the returned versions and codenames; confirm capitalization, shared local artwork, distinct quotes, the centered Ko-fi and total-downloads row between each banner and release codename, valid links, and the final image's size and appearance. Review the complete diff for unintended edits and intermediate assets. Actual package versions, pins, downloadable asset URLs, checksums, and README/website download links remain owned by the release scripts. A future-version note heading may differ from current package metadata during preparation; do not hand-edit package versions or weaken build checks to hide it.

Report the branch and comparison baselines, intended version and capitalized codename, both note paths, both quotes, an image preview/path with dimensions and bytes versus prior examples, validation results, and any missing inputs. State the Git state and that publication has not occurred. This is release-material preparation, not completed hardware qualification; do not run the publisher as a validator or start packaging, deployment, or the full Gym matrix solely to prepare copy and artwork.
