#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
script_dir="$project_dir/scripts"
repository_root="$(cd "$project_dir/.." && pwd)"
output_path=""
output_path_set=false
requested_version=""

usage() {
  cat <<'EOF'
Usage: scripts/publish-package.sh [--version X.Y.Z] [output-path]

Builds and verifies the Decky plugin ZIP, then creates or verifies the matching
plugin-v tag, pushes it, uploads the ZIP, and creates or updates the GitHub
release as the repository's Latest release. When --version is supplied, the
script updates and commits plugin/package.json first. Its renderer pin must
already match that version, and plugin/RELEASE_NOTES.md must contain the
matching manually curated heading.
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
      continue
      ;;
    --version=*)
      requested_version="${1#*=}"
      shift
      continue
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    --)
      shift
      if (($# > 1)); then
        echo "Only one output path may be specified" >&2
        usage >&2
        exit 2
      fi
      if (($# == 1)); then
        output_path="$1"
        output_path_set=true
        shift
      fi
      break
      ;;
    --*)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      if [[ "$output_path_set" == true ]]; then
        echo "Only one output path may be specified" >&2
        usage >&2
        exit 2
      fi
      output_path="$1"
      output_path_set=true
      ;;
  esac
  shift
done

if (($#)); then
  echo "Only one output path may be specified" >&2
  usage >&2
  exit 2
fi

if [[ -n "$requested_version" && ! "$requested_version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "Release version must use X.Y.Z format: $requested_version" >&2
  exit 2
fi

for command in git gh node; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Publishing requires command: $command" >&2
    exit 1
  fi
done

if ! gh auth status >/dev/null 2>&1; then
  echo "Publishing requires an authenticated GitHub CLI session. Run: gh auth login -h github.com" >&2
  exit 1
fi

current_branch="$(git -C "$project_dir" branch --show-current)"
if [[ -z "$current_branch" ]]; then
  echo "Publishing requires a checked-out branch, not a detached HEAD." >&2
  exit 1
fi
if [[ "$current_branch" != "main" ]]; then
  echo "Publish from main; current branch is $current_branch." >&2
  exit 1
fi

if ! git -C "$project_dir" remote get-url origin >/dev/null 2>&1; then
  echo "Publishing requires the origin remote." >&2
  exit 1
fi

if [[ -n "$(git -C "$project_dir" status --porcelain --untracked-files=normal)" ]]; then
  echo "Refusing to publish from a dirty worktree. Commit the release changes first." >&2
  exit 1
fi

notes_version="${requested_version:-$(node -p 'require(process.argv[1]).version' "$project_dir/package.json")}"
manual_release_notes="$(
  node "$repository_root/scripts/read-release-notes.mjs" \
    "$project_dir/RELEASE_NOTES.md" "MAKO Decky" "$notes_version"
)"

git -C "$project_dir" fetch origin \
  refs/heads/main:refs/remotes/origin/main --tags --quiet
remote_main_commit="$(git -C "$project_dir" rev-parse refs/remotes/origin/main)"
if [[ "$(git -C "$project_dir" rev-parse HEAD)" != "$remote_main_commit" ]]; then
  echo "Local main must exactly match origin/main before publishing. Push or synchronize the release commit first." >&2
  exit 1
fi

if [[ -n "$requested_version" ]]; then
  read -r current_package_version pinned_engine_version < <(
    node -e '
      const manifest = require(process.argv[1]);
      process.stdout.write(`${manifest.version ?? ""}\t${manifest.remote_binary?.[0]?.version ?? ""}\n`);
    ' "$project_dir/package.json"
  )
  if [[ "$pinned_engine_version" != "$requested_version" ]]; then
    echo "MAKO Decky v$requested_version requires a MAKO Renderer v$requested_version pin." >&2
    echo "Publish MAKO Renderer first: ./engine/scripts/publish-package.sh --version $requested_version" >&2
    exit 1
  fi
  if [[ "$current_package_version" != "$requested_version" ]]; then
    node -e '
      const fs = require("node:fs");
      const path = process.argv[1];
      const version = process.argv[2];
      const manifest = JSON.parse(fs.readFileSync(path, "utf8"));
      manifest.version = version;
      fs.writeFileSync(path, `${JSON.stringify(manifest, null, 2)}\n`);
    ' "$project_dir/package.json" "$requested_version"
    git -C "$repository_root" add plugin/package.json
    git -C "$repository_root" commit -m "Release MAKO Decky v$requested_version"
    git -C "$repository_root" push origin "$current_branch"
  fi
fi

read -r archive_name engine_version package_version github_repository has_flatpak_bundle archive_url engine_release_tag flatpak_archive_name flatpak_archive_url < <(
  node -e '
    const manifest = require(process.argv[1]);
    const [binary] = manifest.remote_binary ?? [];
    const repositoryUrl = manifest.repository?.url;
    const githubRepository = repositoryUrl
      ?.replace(/^git\+https:\/\/github\.com\//, "")
      .replace(/\.git$/, "");
    const flatpak = binary?.flatpak_bundle;
    if (!binary?.name || !binary?.version || !manifest.version || !githubRepository) {
      process.exitCode = 1;
      throw new Error("package.json must define version, GitHub repository, and one versioned remote_binary entry");
    }
    if (!Array.isArray(binary.host_architectures) ||
        binary.host_architectures.length === 0 ||
        binary.host_architectures.some((architecture) =>
          !["x86_64", "aarch64"].includes(architecture))) {
      process.exitCode = 1;
      throw new Error("remote_binary must declare supported native host architectures");
    }
    if (flatpak && (!flatpak.name || !flatpak.url || !flatpak.sha256hash)) {
      process.exitCode = 1;
      throw new Error("flatpak_bundle must define name, url, and sha256hash when present");
    }
    process.stdout.write(`${binary.name}\t${binary.version}\t${manifest.version}\t${githubRepository}\t${flatpak ? "true" : "false"}\t${binary.url ?? ""}\t${binary.release_tag ?? ""}\t${flatpak?.name ?? ""}\t${flatpak?.url ?? ""}\n`);
  ' "$project_dir/package.json"
)

plugin_release_tag="plugin-v$package_version"
if [[ "$archive_url" == local-only://* || "$engine_release_tag" == local-only-* ]]; then
  echo "Refusing to publish a package pinned to a local-only engine payload." >&2
  exit 1
fi

expected_engine_release_tag="render-v$engine_version"
expected_archive_name="MAKO-Renderer-v$engine_version-linux.tar.xz"
expected_flatpak_archive_name="MAKO-Renderer-v$engine_version-flatpaks.tar.xz"
expected_archive_url="https://github.com/$github_repository/releases/download/$expected_engine_release_tag/$expected_archive_name"
expected_flatpak_archive_url="https://github.com/$github_repository/releases/download/$expected_engine_release_tag/$expected_flatpak_archive_name"
if [[ "$engine_release_tag" != "$expected_engine_release_tag" ||
      "$archive_name" != "$expected_archive_name" ||
      "$archive_url" != "$expected_archive_url" ]]; then
  echo "Publishing MAKO Decky requires a pinned MAKO Renderer release from this repository." >&2
  echo "Expected: $expected_archive_url (tag $expected_engine_release_tag)" >&2
  echo "Update plugin/package.json after publishing the renderer track." >&2
  exit 1
fi
if [[ "$has_flatpak_bundle" == "true" &&
      ( "$flatpak_archive_name" != "$expected_flatpak_archive_name" ||
        "$flatpak_archive_url" != "$expected_flatpak_archive_url" ) ]]; then
  echo "Publishing MAKO Decky requires the matching MAKO Renderer Flatpak asset: $expected_flatpak_archive_url" >&2
  exit 1
fi

if [[ "$output_path_set" == false ]]; then
  output_path="$project_dir/out/MAKO-Decky-v$package_version.zip"
elif [[ "$output_path" != /* ]]; then
  output_path="$project_dir/$output_path"
fi

package_args=()
local_engine_archive="$repository_root/engine/out/$archive_name"
local_flatpak_archive="$repository_root/engine/out/$flatpak_archive_name"
if [[ -f "$local_engine_archive" ]]; then
  echo "Using the checksum-pinned MAKO Renderer archive from the monorepo build output."
  package_args+=(--engine-archive "$local_engine_archive")
  if [[ "$has_flatpak_bundle" == "true" ]]; then
    if [[ ! -f "$local_flatpak_archive" ]]; then
      echo "The native MAKO Renderer archive is local, but its matching Flatpak archive is missing: $local_flatpak_archive" >&2
      exit 1
    fi
    package_args+=(--flatpak-archive "$local_flatpak_archive")
  fi
fi
"$script_dir/package-local.sh" "${package_args[@]}" "$output_path"

current_commit="$(git -C "$project_dir" rev-parse HEAD)"
if git -C "$project_dir" rev-parse -q --verify "refs/tags/$plugin_release_tag" >/dev/null; then
  tag_commit="$(git -C "$project_dir" rev-list -n 1 "$plugin_release_tag")"
  if [[ "$tag_commit" != "$current_commit" ]]; then
    echo "Tag $plugin_release_tag does not point at HEAD. Create the release from its intended commit." >&2
    exit 1
  fi
else
  git -C "$project_dir" tag -a "$plugin_release_tag" -m "MAKO Decky v$package_version"
fi

notes_dir="$(mktemp -d "${TMPDIR:-/tmp}/mako-plugin-release-notes.XXXXXX")"
cleanup() {
  rm -rf "$notes_dir"
}
trap cleanup EXIT
notes_file="$notes_dir/release-notes.md"

printf '%s\n' \
  "> Looking for the standalone Vulkan layer? See [MAKO Renderer v$engine_version](https://github.com/$github_repository/releases/tag/$engine_release_tag)." \
  '' \
  "$manual_release_notes" \
  '' \
  '## 🎮 In-game considerations' \
  '' \
  '> [!TIP]' \
  '> **Try the game’s V-Sync setting both on and off.** It can make frame delivery feel steadier, but may also add input lag or clash with the game’s FPS cap, VRR, or compositor. Every game is different: compare both options and keep the one that feels smoother and more responsive.' \
  '' \
  'Every game, renderer, and display setup behaves differently. Compare Fixed Frame Generation, Adaptive Frame Generation, and scaling-only operation one setting at a time. For most games, fullscreen is the best starting point for performance and frame pacing. Keep the configuration that feels best for that game.' \
  '' \
  '- **Adaptive target behaviour:** Adaptive varies the generated-frame count toward an average target. It cannot reduce a native frame rate already above that target, and the result still depends on the selected multiplier plus available GPU and compositor capacity.' \
  '- **Quality and latency tuning:** Higher multipliers and lower real-frame rates can increase ghosting and input latency. Smooth Cadence may improve motion consistency while reducing responsiveness, so compare the available choices per game.' \
  '' \
  'See the [Configuration guide](https://github.com/eugeniosegala/MAKO/blob/main/plugin/docs/CONFIGURATION.md) and [Troubleshooting guide](https://github.com/eugeniosegala/MAKO/blob/main/plugin/docs/TROUBLESHOOTING.md) for complete behaviour and per-game controls.' \
  '' \
  '> [!IMPORTANT]' \
  '> MAKO does not contain or distribute Lossless Scaling, `Lossless.dll`, or extracted proprietary model payloads. Frame Generation and LS1 scaling read selected resources at runtime from a lawful, user-supplied Lossless Scaling installation. The open MAKO Scaler does not use it. MAKO does not alter the user’s DLL file, and translated resources remain process-local. Users are responsible for complying with the terms applicable to their copy.' \
  '' \
  '> ⚠️ **Required MAKO Renderer update:** Installing the ZIP updates MAKO Decky, but does **not** replace the shared native Renderer. Open the plugin and select **Install MAKO Renderer** afterwards.' \
  '' \
  > "$notes_file"

printf '%s\n' \
  '## Installation' \
  '' \
  'New to Decky or installing MAKO Decky for the first time? See the [full install and use guide](https://github.com/eugeniosegala/MAKO#install-and-use) for Decky Loader setup and prerequisites.' \
  '' \
  "1. Download \`$(basename "$output_path")\` below." \
  "2. On SteamOS, open Decky Loader's settings and enable **Developer Mode**." \
  '3. Choose **Developer** > **Install Plugin from Zip**, then select the downloaded ZIP.' \
  '4. In MAKO Decky, select **Install MAKO Renderer**. For native Steam/Proton games, add `/home/deck/.local/bin/mako-run %command%` to the game’s Steam launch options.' \
  '' \
  '> [!IMPORTANT]' \
  '> **Preferred clean update:** To prevent Decky retaining an older backend or bundled payload, uninstall **MAKO Decky**, install the newer ZIP, restart your Steam Deck or Steam Machine, then open the plugin and select **Install MAKO Renderer**. This also resolves cases where Decky does not show or reload the plugin after installation.' \
  '' \
  >> "$notes_file"

if [[ "$has_flatpak_bundle" == "true" ]]; then
  printf '%s\n' \
    '**First-time Heroic or EmuDeck setup:** Read the [Heroic and other Flatpak applications guide](https://github.com/eugeniosegala/MAKO#heroic-and-other-flatpak-applications) before preparing either integration.' \
    '' \
    >> "$notes_file"
fi

printf '%s\n' \
  '## Updating an existing MAKO Decky installation' \
  '' \
  '1. Quit any game currently using `/home/deck/.local/bin/mako-run`.' \
  "2. Uninstall **MAKO Decky**, then download the newer ZIP from [version $package_version](https://github.com/$github_repository/releases/tag/$plugin_release_tag)." \
  '3. In Game Mode, open Decky Loader’s settings, choose **Developer** > **Install Plugin from Zip**, then select the newer ZIP.' \
  '4. Restart your Steam Deck or Steam Machine.' \
  '5. ⚠️ **Required:** Open MAKO Decky and select **Install MAKO Renderer** to install the version bundled in the new ZIP.' \
  '6. In **Flatpak Setup**, select **Update** for every prepared application’s matching runtime extension. This replaces its Flatpak layer with the engine bundled in the new ZIP while preserving its preparation and per-game Wrapper commands.' \
  '' \
  'Existing profiles and Steam launch options are retained. The shared native Renderer and launcher are re-created in step 5; shared Flatpak extensions are retained, then refreshed in step 6.' \
  '' \
  '## Known limitation' \
  '' \
  '- **HDR frame generation and scaling are unavailable in this Decky release:** The engine foundation is included, but the plugin locks HDR exposure off and does not provide a per-game opt-in. In-game HDR controls may be unavailable by design. A later release can unlock the path after activation, presentation, colour, and performance are validated across games.' \
  '' \
  '## Before you play' \
  '' \
  '- Test MAKO per game. Start with Frame Generation and Scaling disabled, then enable one path at a time before combining them.' \
  '- If you use Frame Generation or LS1 scaling, confirm the detected `Lossless.dll` path before launching. Leaving it blank permits normal discovery. The open MAKO Scaler does not need it.' \
  '- Do not combine `mako-run` with another frame-generation or scaling Vulkan wrapper for the same game.' \
  '' \
  "## Bundled MAKO Renderer \`$engine_version\`" \
  '' \
  "- Includes checksum-verified \`$archive_name\`." \
  "- Corresponding MAKO Decky source: [\`$plugin_release_tag\`](https://github.com/$github_repository/tree/$plugin_release_tag)." \
  "- Corresponding MAKO Renderer source: [\`$engine_release_tag\`](https://github.com/$github_repository/tree/$engine_release_tag), pinned by commit and checksum in the ZIP metadata." \
  '' \
  >> "$notes_file"

echo "Publishing $plugin_release_tag to $github_repository..."
git -C "$project_dir" push origin "$current_branch"
git -C "$project_dir" push origin "$plugin_release_tag"

if gh release view "$plugin_release_tag" --repo "$github_repository" >/dev/null 2>&1; then
  node "$repository_root/scripts/upload-release-assets.mjs" \
    "$github_repository" "$plugin_release_tag" "$output_path"
  gh release edit "$plugin_release_tag" --repo "$github_repository" \
    --title "MAKO Decky v$package_version" \
    --notes-file "$notes_file" \
    --prerelease=false \
    --latest
else
  gh release create "$plugin_release_tag" "$output_path" --repo "$github_repository" \
    --title "MAKO Decky v$package_version" \
    --notes-file "$notes_file" \
    --latest \
    --verify-tag
fi

node "$repository_root/scripts/update-release-links.mjs" \
  decky "$package_version" "$github_repository"
release_link_readmes=(README.md plugin/README.md engine/README.md)
if ! git -C "$repository_root" diff --quiet -- "${release_link_readmes[@]}"; then
  git -C "$repository_root" add "${release_link_readmes[@]}"
  git -C "$repository_root" commit -m "docs: link MAKO Decky v$package_version"
  git -C "$repository_root" push origin "$current_branch"
fi

echo "Published: https://github.com/$github_repository/releases/tag/$plugin_release_tag"
