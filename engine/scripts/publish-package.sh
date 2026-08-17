#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

version="$(tr -d '[:space:]' < VERSION)"
tag="render-v$version"
archive="out/mako-render-v$version-linux.tar.xz"
flatpak_archive="out/mako-render-v$version-flatpaks.tar.xz"
release_branch="$(git branch --show-current)"
source_commit="$(git rev-parse HEAD)"
release_remote="${MAKO_RELEASE_REMOTE:-origin}"
notes_version="2.0.0-dev28-experimental.25"
notes_previous_version="2.0.0-dev28-experimental.24"
notes_previous_tag=""
notes_tag_pattern="render-v*"

if [[ "$version" != "$notes_version" ]]; then
    echo "Release notes still describe $notes_version. Update scripts/publish-package.sh for $version before publishing." >&2
    exit 1
fi

latest_previous_tag=""
while IFS= read -r candidate_tag; do
    if [[ "$candidate_tag" != "$tag" ]]; then
        latest_previous_tag="$candidate_tag"
        break
    fi
done < <(git tag --merged HEAD --list "$notes_tag_pattern" --sort=-version:refname)
if [[ -n "$notes_previous_tag" ]]; then
    if ! git rev-parse -q --verify "refs/tags/$notes_previous_tag" >/dev/null; then
        echo "Release-note baseline tag $notes_previous_tag is missing." >&2
        exit 1
    fi
    if ! git merge-base --is-ancestor "$notes_previous_tag" HEAD; then
        echo "Release-note baseline $notes_previous_tag is not an ancestor of HEAD." >&2
        exit 1
    fi
    if [[ "$latest_previous_tag" != "$notes_previous_tag" ]]; then
        echo "Release notes use $notes_previous_tag, but the latest prior MAKO Renderer tag is ${latest_previous_tag:-missing}. Update the baseline and change list before publishing." >&2
        exit 1
    fi
elif [[ -n "$latest_previous_tag" ]]; then
    echo "The MAKO Renderer release track already contains $latest_previous_tag. Set notes_previous_tag before publishing another render-v release." >&2
    exit 1
fi

if [[ "$release_branch" != "main" ]]; then
    echo "Publish from main; current branch is $release_branch." >&2
    exit 1
fi

if [[ -n "$(git status --porcelain --untracked-files=normal)" ]]; then
    echo "Working tree has uncommitted changes. Commit or stash them before publishing." >&2
    exit 1
fi

tag_exists=false
if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
    tag_commit="$(git rev-list -n 1 "$tag")"
    if [[ "$tag_commit" != "$source_commit" ]]; then
        echo "Tag $tag does not point at HEAD. Publish from its intended commit or bump VERSION." >&2
        exit 1
    fi
    tag_exists=true
fi

for command in gh git node; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "Required command not found: $command" >&2
        exit 1
    fi
done

if ! git remote get-url "$release_remote" >/dev/null 2>&1; then
    echo "Release remote '$release_remote' is not configured." >&2
    exit 1
fi

release_repository="$(git remote get-url "$release_remote")"
release_repository="${release_repository#git@github.com:}"
release_repository="${release_repository#https://github.com/}"
release_repository="${release_repository%.git}"
if [[ "$release_repository" != */* ]]; then
    echo "Could not determine a GitHub owner/repository from remote '$release_remote'." >&2
    exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
    checksum_command=(sha256sum)
elif command -v shasum >/dev/null 2>&1; then
    checksum_command=(shasum -a 256)
else
    echo "Required command not found: sha256sum or shasum" >&2
    exit 1
fi

if ! gh auth status >/dev/null 2>&1; then
    echo "GitHub CLI is not authenticated. Run: gh auth login -h github.com" >&2
    exit 1
fi

scripts/package-local.sh "$archive"
checksum="$("${checksum_command[@]}" "$archive" | awk '{print $1}')"
scripts/package-flatpaks.sh "$flatpak_archive"
flatpak_checksum="$("${checksum_command[@]}" "$flatpak_archive" | awk '{print $1}')"
notes_file="$(mktemp "${TMPDIR:-/tmp}/mako-release-notes.XXXXXX")"
cleanup() {
    rm -f "$notes_file"
}
trap cleanup EXIT

cat > "$notes_file" <<EOF
> Looking for the Decky plugin? Download the [latest MAKO Decky release](https://github.com/eugeniosegala/MAKO/releases/latest).

## Experimental Linux build

This is a development build of the MAKO Renderer 2.x line. Test it game by game and retain a known-good rollback path.

## What's new since \`$notes_previous_version\`

This \`.25\` release consolidates the tested SDR runtime, safer live configuration and recovery, and complete dual-architecture packaging. It also carries an HDR pipeline foundation for future testing; HDR frame generation is not presented as release-ready.

### Changes introduced in this release

#### SDR stability and live configuration

- Restores the established ordered SDR presentation path after introducing Gamescope-aware HDR transport. SDR and HDR now select separate colour, presentation, transition, and recovery policies so HDR experiments do not silently change ordinary SDR pacing.
- Keeps real game frames native-first when generated-output resources or private GPU work are unavailable. Expected Gamescope image pressure does not schedule inference first, hold the present thread behind a refresh-length wait, or repeatedly reset Adaptive stabilization.
- Preserves temporal-history liveness through startup, menu/focus transitions, generated-image pressure, and backend-busy frames. Recovery remains in-place and never invalidates a game-owned swapchain merely to apply a policy decision.
- Makes configuration reloads resilient to transient partial TOML writes. Frame Generation, Fixed/Adaptive mode, Fixed multiplier within reserved capacity, Adaptive target and ceiling, and Smooth Cadence can update without rebuilding the game swapchain.
- Keeps the load-aware Adaptive state machine, Smooth Cadence, menu/fast-cadence filtering, delayed-load rollback, and Fixed 2x/3x/4x paths covered by deterministic timing tests and the compatibility matrix.

#### Host and Flatpak packaging

- Ships architecture-matched ELF64 and ELF32 Vulkan layers and manifests in the host archive. The CLI and Qt UI remain 64-bit.
- Ships both layer architectures in each Freedesktop 23.08, 24.08, and 25.08 Flatpak extension and verifies the deployed bundle paths before publishing.
- Uses the uniquely named, environment-gated MAKO Renderer layer with an isolated installation path.

#### HDR foundation for future releases

- Classifies the complete Vulkan format/colour-space pair, with separate SDR 8-bit, SDR high-precision, HDR10/PQ, and linear-scRGB pipelines.
- Includes explicit BT.2020/PQ to linear scRGB conversion around the model, plus a capability-validated packed HDR10 boundary transport that leaves model and temporal images at 16-bit float.
- Resolves Gamescope application feedback away from the presentation hot path and requires application colour-space feedback or HDR metadata. Display HDR capability alone never promotes an SDR swapchain.
- Keeps unsupported or unconfirmed encodings on real-frame passthrough. The companion Decky plugin sets \`MAKO_DISABLE_HDR_EXPOSURE=1\` as the engine's hard SDR boundary and leaves DXVK at its normal SDR default; direct launchers can set the same MAKO variable.
- Treats this code as architecture and diagnostic groundwork. Cross-game HDR activation, colour validation, presentation and performance still require future hardware testing.

### Important limitations

- Adaptive Frame Generation is experimental and opt-in. This independent Vulkan-layer scheduler varies between zero and three generated frames per real frame toward the configured average target. It cannot reduce a native framerate already above the target, exceed the selected 4x maximum, guarantee an unreachable target, or provide the Windows Queue Target modes.
- Higher interpolation ratios and lower real-frame rates can increase ghosting and input latency. Smooth Cadence can improve motion consistency but may lower real-frame cadence and responsiveness, so test it per game.
- HDR frame generation is not release-ready in \`.25\`. The code is retained as disabled-by-default foundation and may fail to expose HDR, attach, generate, present, or perform acceptably in a particular game.
- HDR10 conversion adds full-resolution GPU work. Packed boundary images reduce only the private exchange-image footprint; neither change is a universal performance claim.
- HLG, Dolby Vision, and unvalidated wide-colour combinations intentionally use real-frame passthrough. A game still needs its own HDR renderer and an HDR-capable SteamOS/Gamescope session.
- Lossless Scaling and \`Lossless.dll\` must already be installed through Steam; neither release archive includes or modifies it.
- Gamescope generated-image admission is nonblocking and native-first. Recovery remains inside the existing context; the layer does not force game-owned swapchain recreation for a setting change or recovery decision.
- Flatpak extensions for 23.08, 24.08, and 25.08 are packaged separately in \`$(basename "$flatpak_archive")\` under the dedicated MAKO Renderer extension ID.

### Included files

- 64-bit Vulkan implicit layer and manifest under \`lib/\` and \`share/vulkan/implicit_layer.d/\`
- 32-bit Vulkan implicit layer and manifest under \`lib32/\` and \`share/vulkan/implicit_layer.d/\`
- 64-bit CLI and Qt configuration UI
- XDG desktop files

### Adaptive configuration

Enable Adaptive mode through the Qt UI or a profile:

\`\`\`toml
adaptive = true
target_fps = 120
adaptive_max_multiplier = 3
adaptive_stable_cadence = false
frame_generation_enabled = true
\`\`\`

Frame Generation, Fixed/Adaptive mode, Fixed multiplier, Adaptive target, maximum multiplier, and Smooth Cadence can update live when the current context already reserved sufficient capacity. Restart after changing GPU, Flow Scale, Performance Mode, pacing, DLL, FP16 policy, or when increasing beyond the resources created at startup. HDR remains a separate experimental restart-time boundary.

### Optional diagnostics

Presentation diagnostics remain disabled by default. When enabled, slow Vulkan operations, fallback/recovery state, Adaptive ramp decisions, fast-cadence bursts, 2x gameplay-hitch recovery, and swapchain lifecycle events include a process-unique \`context=<ID>\` so concurrent or replacement contexts can be separated.

With MAKO Renderer, enable diagnostics with this Steam launch option:

\`\`\`bash
MAKO_PRESENT_DIAGNOSTICS=1 MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 ~/.local/bin/mako-run %command%
\`\`\`

After reproducing the problem, extract the latest diagnostic entries with:

\`\`\`bash
grep -aF "mako: present diagnostics:" ~/.steam/steam/logs/console-linux.txt | tail -n 800
\`\`\`

See the [presentation-stall troubleshooting guide](https://github.com/eugeniosegala/MAKO/blob/main/engine/docs/TROUBLESHOOTING.md#diagnosing-presentation-stalls) for recovery variables, event meanings, and focused filters. Disable diagnostics after collecting the trace.

### Install

Download \`$(basename "$archive")\` and extract it to your local prefix:

\`\`\`bash
tar -xJf $(basename "$archive") -C ~/.local
\`\`\`

The host archive includes 64-bit and 32-bit Vulkan layers; the CLI and Qt UI are 64-bit. Flatpak extensions are provided separately below.

### Flatpak extensions

Download and extract \`$(basename "$flatpak_archive")\`. It contains one self-contained experimental extension for each supported Flatpak runtime. Install the extension matching the application runtime, for example:

\`\`\`bash
flatpak install --user org.freedesktop.Platform.VulkanLayer.makorender-24.08.flatpak
\`\`\`

- SHA-256: \`$flatpak_checksum\`

### Build details

- Source commit: \`$source_commit\`
- SHA-256: \`$checksum\`
- Renderer lineage version: \`2.0.0-dev28\`
EOF

if [[ "$tag_exists" == false ]]; then
    git tag -a "$tag" -m "MAKO Renderer v$version"
fi
git push "$release_remote" "$release_branch"
git push "$release_remote" "$tag"

if gh release view "$tag" --repo "$release_repository" >/dev/null 2>&1; then
    gh release edit "$tag" \
        --repo "$release_repository" \
        --title "MAKO Renderer v$version" \
        --notes-file "$notes_file" \
        --prerelease \
        --latest=false
    gh release upload "$tag" "$archive" "$flatpak_archive" \
        --repo "$release_repository" \
        --clobber
else
    gh release create "$tag" "$archive" "$flatpak_archive" \
        --repo "$release_repository" \
        --title "MAKO Renderer v$version" \
        --prerelease \
        --latest=false \
        --notes-file "$notes_file" \
        --verify-tag
fi

node "$repo_root/../plugin/scripts/pin-renderer-release.mjs" \
    "$repo_root/../plugin/package.json" \
    "$version" \
    "$tag" \
    "$source_commit" \
    "$release_repository" \
    "$archive" \
    "$checksum" \
    "$flatpak_archive" \
    "$flatpak_checksum"

echo "Published: https://github.com/$release_repository/releases/tag/$tag"
echo "Updated plugin/package.json with the published renderer assets. Review and commit that pin before publishing MAKO Decky."
