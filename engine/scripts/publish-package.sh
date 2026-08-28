#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
repository_root="$(cd "$repo_root/.." && pwd)"
cd "$repo_root"

release_remote="${MAKO_RELEASE_REMOTE:-origin}"
requested_version=""

usage() {
    cat <<'EOF'
Usage: scripts/publish-package.sh [--version X.Y.Z]

Builds, verifies, and publishes MAKO Renderer. When --version is supplied, the
script updates and commits engine/VERSION first. After publishing, it commits
the generated MAKO Decky binary pins and MAKO Renderer release links automatically.
engine/RELEASE_NOTES.md must contain the matching manually curated heading.
EOF
}

while (($#)); do
    case "$1" in
        --version)
            if (($# < 2)); then
                echo "--version requires a value." >&2
                usage >&2
                exit 2
            fi
            requested_version="$2"
            shift 2
            ;;
        --version=*)
            requested_version="${1#*=}"
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ -n "$requested_version" && ! "$requested_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Release version must use X.Y.Z format: $requested_version" >&2
    exit 2
fi

for command in gh git node; do
    if ! command -v "$command" >/dev/null 2>&1; then
        echo "Required command not found: $command" >&2
        exit 1
    fi
done

release_branch="$(git branch --show-current)"
if [[ "$release_branch" != "main" ]]; then
    echo "Publish from main; current branch is $release_branch." >&2
    exit 1
fi

if [[ -n "$(git status --porcelain --untracked-files=normal)" ]]; then
    echo "Working tree has uncommitted changes. Commit or stash them before publishing." >&2
    exit 1
fi

notes_version="${requested_version:-$(tr -d '[:space:]' < VERSION)}"
manual_release_notes="$(
    node "$repository_root/scripts/read-release-notes.mjs" \
        "$repo_root/RELEASE_NOTES.md" "MAKO Renderer" "$notes_version"
)"

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

git fetch "$release_remote" --tags --quiet

if [[ -n "$requested_version" ]]; then
    current_version="$(tr -d '[:space:]' < VERSION)"
    if [[ "$current_version" != "$requested_version" ]]; then
        printf '%s\n' "$requested_version" > VERSION
        git add VERSION
        git commit -m "Release MAKO Renderer v$requested_version"
    fi
fi

version="$(tr -d '[:space:]' < VERSION)"
if [[ ! "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "engine/VERSION must use X.Y.Z format: $version" >&2
    exit 1
fi

tag="render-v$version"
archive="out/MAKO-Renderer-v$version-linux.tar.xz"
flatpak_archive="out/MAKO-Renderer-v$version-flatpaks.tar.xz"
source_commit="$(git rev-parse HEAD)"

tag_exists=false
if git rev-parse -q --verify "refs/tags/$tag" >/dev/null; then
    tag_commit="$(git rev-list -n 1 "$tag")"
    if [[ "$tag_commit" != "$source_commit" ]]; then
        echo "Tag $tag does not point at HEAD. Publish from its intended commit or bump VERSION." >&2
        exit 1
    fi
    tag_exists=true
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
> Looking for Decky integration? Download the [latest MAKO Decky release](https://github.com/eugeniosegala/MAKO/releases/latest).

$manual_release_notes

## 🎮 In-game considerations

> [!TIP]
> **Try the game’s V-Sync setting both on and off.** It can make frame delivery feel steadier, but may also add input lag or clash with the game’s FPS cap, VRR, or compositor. Every game is different: compare both options and keep the one that feels smoother and more responsive.

Every game, renderer, and display setup behaves differently. Compare scaling-only, Fixed Frame Generation, and Adaptive Frame Generation one setting at a time. For scaling, select the intended lower rendering resolution in the game. Fixed/Adaptive mode, target, and multiplier changes apply live; capacity growth, scaling controls, Flow Scale, and Lighter FG Model use one game-owned swapchain recreation when needed. Fullscreen is usually the best starting point for performance and frame pacing. Restart if a title ignores that signal, after major display, DLL, GPU, or process/device-policy changes, and before enabling Frame Generation in a process that started scaling-only.

- **Scaling quality and cost:** Higher scaling factors render fewer source pixels but require more reconstruction. Compare text, UI edges, thin geometry, motion, GPU time, and frame pacing against native resolution, then tune sharpening conservatively.
- **Adaptive target behaviour:** Adaptive varies the generated-frame count toward an average target. It cannot reduce a native frame rate already above that target, and the result still depends on the selected multiplier plus available GPU and compositor capacity.
- **Quality and latency tuning:** Higher multipliers and lower real-frame rates can increase ghosting and input latency. Smooth Cadence may improve motion consistency while reducing responsiveness, so compare the available choices per game.

> [!IMPORTANT]
> LS1 scaling and Fixed or Adaptive Frame Generation require \`Lossless.dll\` from a licensed Lossless Scaling installation. The open MAKO scaling method does not use it. Neither release archive bundles, copies, or modifies that proprietary library.

## Installation

### Host archive

Download \`$(basename "$archive")\` and extract it to your local prefix:

\`\`\`bash
mkdir -p ~/.local
tar -xJf $(basename "$archive") -C ~/.local
\`\`\`

The host archive includes 64-bit and 32-bit Vulkan layers; the CLI and Qt UI are 64-bit.

Start the configuration UI after extraction:

- **Application menu:** On Steam Deck or Steam Machine, switch to Desktop Mode, open the bottom-left Application Launcher, then select **Settings > MAKO Renderer Configuration**.
- **Terminal:** Run \`~/.local/bin/mako-ui\` from Konsole or another terminal. Do not run it with \`sudo\`.

Configure the profile in the UI or \`~/.config/mako-render/conf.toml\`. Select the licensed DLL path when using LS1 scaling or Frame Generation, then use \`~/.local/bin/mako-launch %command%\` for a direct Steam launch. The helper activates MAKO through its private Vulkan layer directory, prevents Steam's Vulkan Fossilize/overlay hooks and competing presentation layers from bypassing its swapchain interception, and keeps Gamescope and the Steam/Game Mode interface active outside that application chain.

### Flatpak runtime extensions

Download and extract \`$(basename "$flatpak_archive")\`. It contains one self-contained MAKO extension for each supported Flatpak runtime. Install the extension matching the application runtime, for example:

\`\`\`bash
flatpak install --user org.freedesktop.Platform.VulkanLayer.makorender-24.08.flatpak
\`\`\`

- SHA-256: \`$flatpak_checksum\`

## Updating an existing MAKO Renderer installation

1. Quit every game or application currently using MAKO Renderer.
2. Download the newer host archive and extract it into the same local prefix. Existing files are replaced; your configuration remains under \`~/.config/mako-render/\`.
3. If you use Flatpak applications, download the matching Flatpak archive and reinstall the extension for each runtime you use.
4. Restart the game. Revalidate the configuration with \`~/.local/bin/mako-cli validate\` if you changed the DLL path or profiles.

Keep the previous archives until the new version has been tested with your games.

## Known limitation

- **HDR scaling and frame generation are not currently supported:** HDR pipeline groundwork remains in the renderer, but MAKO does not present either feature as an enabled HDR release path yet.

## Before you play

- If you use LS1 scaling or Frame Generation, confirm that MAKO can discover \`Lossless.dll\` or select its path explicitly. The open MAKO scaling method does not need it.
- Do not combine MAKO with another frame-generation or swapchain-scaling Vulkan wrapper for the same game.

## MAKO Renderer release assets \`$version\`

- Includes checksum-verified host archive \`$(basename "$archive")\` (SHA-256: \`$checksum\`).
- Includes checksum-verified Flatpak runtime archive \`$(basename "$flatpak_archive")\` (SHA-256: \`$flatpak_checksum\`).
- The host archive contains the 64-bit and 32-bit Vulkan layers, CLI, Qt configuration UI, standalone \`mako-launch\` launcher, and desktop integration.
- Built from source commit \`$source_commit\`.
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
        --latest=false
    gh release upload "$tag" "$archive" "$flatpak_archive" \
        --repo "$release_repository" \
        --clobber
else
    gh release create "$tag" "$archive" "$flatpak_archive" \
        --repo "$release_repository" \
        --title "MAKO Renderer v$version" \
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

node "$repository_root/scripts/update-release-links.mjs" \
    renderer "$version" "$release_repository"
release_metadata_paths=(plugin/package.json README.md plugin/README.md engine/README.md)
if ! git -C "$repository_root" diff --quiet -- "${release_metadata_paths[@]}"; then
    git -C "$repository_root" add "${release_metadata_paths[@]}"
    git -C "$repository_root" commit -m "Pin MAKO Renderer v$version"
    git -C "$repository_root" push "$release_remote" "$release_branch"
fi

echo "Published: https://github.com/$release_repository/releases/tag/$tag"
echo "Committed the matching MAKO Decky binary pins and MAKO Renderer release links."
