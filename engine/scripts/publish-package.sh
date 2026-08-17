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
notes_tag_pattern="render-v*"

if ! git remote get-url "$release_remote" >/dev/null 2>&1; then
    echo "Release remote '$release_remote' is not configured." >&2
    exit 1
fi

git fetch "$release_remote" --tags --quiet

latest_previous_tag=""
while IFS= read -r candidate_tag; do
    if [[ "$candidate_tag" != "$tag" ]]; then
        latest_previous_tag="$candidate_tag"
        break
    fi
done < <(git tag --merged HEAD --list "$notes_tag_pattern" --sort=-version:refname)
if [[ -n "$latest_previous_tag" ]]; then
    release_notes_heading="What's new since \`$latest_previous_tag\`"
    release_changes="$(git log --no-merges --format='- %s (%h)' "$latest_previous_tag"..HEAD -- engine README.md)"
else
    release_notes_heading="What's new in MAKO Renderer \`$version\`"
    release_changes="$(git log --no-merges --format='- %s (%h)' HEAD -- engine README.md)"
fi
if [[ -z "$release_changes" ]]; then
    release_changes='- No component-scoped changes were recorded after the previous tag.'
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

## MAKO Renderer Linux build

This is a MAKO Renderer prerelease. Test it game by game and retain a known-good rollback path.

## $release_notes_heading

$release_changes

The list above is generated from every non-merge commit that changed MAKO
Renderer or the shared README after the previous Renderer tag.

### Before you install

- Lossless Scaling and \`Lossless.dll\` must already be installed through Steam;
  neither archive includes or modifies them.
- Higher interpolation ratios can increase artifacts and input latency. Test each
  game before relying on a new setting or release.
- The host archive contains 64-bit and 32-bit Vulkan layers. The separate
  Flatpak archive contains extensions for the supported runtimes.

### Included files

- 64-bit Vulkan implicit layer and manifest under \`lib/\` and \`share/vulkan/implicit_layer.d/\`
- 32-bit Vulkan implicit layer and manifest under \`lib32/\` and \`share/vulkan/implicit_layer.d/\`
- 64-bit CLI and Qt configuration UI
- XDG desktop files

### Install

Download \`$(basename "$archive")\` and extract it to your local prefix:

\`\`\`bash
mkdir -p ~/.local
tar -xJf $(basename "$archive") -C ~/.local
\`\`\`

The host archive includes 64-bit and 32-bit Vulkan layers; the CLI and Qt UI are 64-bit. Flatpak extensions are provided separately below.

### Flatpak extensions

Download and extract \`$(basename "$flatpak_archive")\`. It contains one self-contained MAKO extension for each supported Flatpak runtime. Install the extension matching the application runtime, for example:

\`\`\`bash
flatpak install --user org.freedesktop.Platform.VulkanLayer.makorender-24.08.flatpak
\`\`\`

- SHA-256: \`$flatpak_checksum\`

### Build details

- Source commit: \`$source_commit\`
- SHA-256: \`$checksum\`
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

repository_root="$repo_root/.."
node "$repository_root/scripts/update-release-links.mjs" \
    renderer "$version" "$release_repository"
if ! git -C "$repository_root" diff --quiet -- README.md; then
    git -C "$repository_root" add README.md
    git -C "$repository_root" commit -m "docs: link MAKO Renderer v$version"
    git -C "$repository_root" push "$release_remote" "$release_branch"
fi

echo "Published: https://github.com/$release_repository/releases/tag/$tag"
echo "Updated plugin/package.json with the published renderer assets. Review and commit that pin before publishing MAKO Decky."
