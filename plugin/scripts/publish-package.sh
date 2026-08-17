#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
script_dir="$project_dir/scripts"
repository_root="$(cd "$project_dir/.." && pwd)"
output_path=""
output_path_set=false

usage() {
  cat <<'EOF'
Usage: scripts/publish-package.sh [output-path]

Builds and verifies the Decky plugin ZIP, then creates or verifies the matching
plugin-v tag, pushes it, uploads the ZIP, and creates or updates the GitHub
release as the repository's Latest release.
EOF
}

while (($#)); do
  case "$1" in
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

if [[ -n "$(git -C "$project_dir" status --porcelain --untracked-files=normal)" ]]; then
  echo "Refusing to publish from a dirty worktree. Commit the release changes first." >&2
  exit 1
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
    if (flatpak && (!flatpak.name || !flatpak.url || !flatpak.sha256hash)) {
      process.exitCode = 1;
      throw new Error("flatpak_bundle must define name, url, and sha256hash when present");
    }
    process.stdout.write(`${binary.name}\t${binary.version}\t${manifest.version}\t${githubRepository}\t${flatpak ? "true" : "false"}\t${binary.url ?? ""}\t${binary.release_tag ?? ""}\t${flatpak?.name ?? ""}\t${flatpak?.url ?? ""}\n`);
  ' "$project_dir/package.json"
)

notes_package_version="0.13.0-experimental.25"
notes_engine_version="2.0.0-dev28-experimental.25"
notes_previous_package_version="0.13.0-experimental.24"
plugin_release_tag="plugin-v$package_version"
notes_previous_package_tag=""
notes_package_tag_pattern="plugin-v*"
if [[ "$package_version" != "$notes_package_version" || "$engine_version" != "$notes_engine_version" ]]; then
  echo "Release notes still describe plugin $notes_package_version with engine $notes_engine_version. Update them before publishing." >&2
  exit 1
fi
latest_previous_package_tag=""
while IFS= read -r candidate_tag; do
  if [[ "$candidate_tag" != "$plugin_release_tag" ]]; then
    latest_previous_package_tag="$candidate_tag"
    break
  fi
done < <(git -C "$project_dir" tag --merged HEAD --list "$notes_package_tag_pattern" --sort=-version:refname)
if [[ -n "$notes_previous_package_tag" ]]; then
  if ! git -C "$project_dir" rev-parse -q --verify "refs/tags/$notes_previous_package_tag" >/dev/null; then
    echo "Release-note baseline tag $notes_previous_package_tag is missing." >&2
    exit 1
  fi
  if ! git -C "$project_dir" merge-base --is-ancestor "$notes_previous_package_tag" HEAD; then
    echo "Release-note baseline $notes_previous_package_tag is not an ancestor of HEAD." >&2
    exit 1
  fi
  if [[ "$latest_previous_package_tag" != "$notes_previous_package_tag" ]]; then
    echo "Release notes use $notes_previous_package_tag, but the latest prior MAKO Decky tag is ${latest_previous_package_tag:-missing}. Update the baseline and change list before publishing." >&2
    exit 1
  fi
elif [[ -n "$latest_previous_package_tag" ]]; then
  echo "The MAKO Decky release track already contains $latest_previous_package_tag. Set notes_previous_package_tag before publishing another plugin-v release." >&2
  exit 1
fi
if [[ "$archive_url" == local-only://* || "$engine_release_tag" == local-only-* ]]; then
  echo "Refusing to publish a package pinned to a local-only engine payload." >&2
  exit 1
fi

expected_engine_release_tag="render-v$engine_version"
expected_archive_name="mako-render-v$engine_version-linux.tar.xz"
expected_flatpak_archive_name="mako-render-v$engine_version-flatpaks.tar.xz"
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

current_branch="$(git -C "$project_dir" branch --show-current)"
if [[ -z "$current_branch" ]]; then
  echo "Publishing requires a checked-out branch, not a detached HEAD." >&2
  exit 1
fi
if [[ "$current_branch" != "main" ]]; then
  echo "Publish from main; current branch is $current_branch." >&2
  exit 1
fi

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
  '> Looking for the standalone Vulkan layer? See the [MAKO Renderer releases](https://github.com/eugeniosegala/MAKO/releases?q=tag%3Arender-v).' \
  '' \
  "## What’s new since \`$notes_previous_package_version\`" \
  '' \
  '- **Engine update:** Bundles checksum-verified MAKO Renderer `2.0.0-dev28-experimental.25`. Complete the required in-plugin engine-update step after installing the ZIP.' \
  '- **HDR foundation — in progress:** The bundled engine contains HDR10/PQ and linear-scRGB colour-pipeline groundwork, Gamescope feedback, packed HDR10 boundary transport, and safe passthrough. HDR activation and frame generation are not exposed by this Decky release while cross-game presentation, colour, and performance validation continues.' \
  '- **64-bit and 32-bit Vulkan support:** Installs architecture-matched host and Flatpak layers. Vulkan selects the correct layer for each game process, so genuine 32-bit Vulkan games no longer need the old WoW64 option; existing wrappers are migrated away from stale `PROTON_USE_WOW64` exports.' \
  '- **Safer live reconfiguration and stall recovery:** Transient partial configuration writes are retried. Frame Generation and Adaptive Target, Maximum Multiplier, and Smooth Cadence can update in place when resources permit; resource-shape and model settings are deferred, so restart the game to guarantee those changes. A transient backend stall keeps native presentation active and warms temporal history before generation resumes.' \
  '- **Private layer discovery migration:** The `.25` wrapper regenerates older launchers and retains the uniquely named MAKO Renderer layer on the proven SDR path. Heroic Flatpak launches explicitly retain Gamescope WSI ahead of MAKO Renderer; Flatpak cleanup recognises both historical isolated and additive layouts.' \
  '- **Diagnostic log presets:** Installs `~/.local/bin/mako-diagnostics` with focused HDR, Adaptive, recovery, performance, lifecycle, startup, layer, and error filters.' \
  '- **Monorepo engine packaging:** Maintainers can build a Decky ZIP directly from the sibling MAKO Renderer checkout. The generated ZIP records the exact commit, dirty state, filenames, and checksums without changing the tracked public release pin.' \
  '- **Documentation:** Expands HDR, dual-architecture, diagnostics, Flatpak migration, local packaging, and community-coverage guidance.' \
  '' \
  '## 🎮 In-game considerations' \
  '' \
  '> [!TIP]' \
  '> **Try the game’s V-Sync setting both on and off.** It can make frame delivery feel steadier, but may also add input lag or clash with the game’s FPS cap, VRR, or compositor. Every game is different: compare both options and keep the one that feels smoother and more responsive.' \
  '' \
  'Every game, renderer, and display setup behaves differently. Compare Fixed and Adaptive Frame Generation one setting at a time. For most games, fullscreen is the best starting point for performance and frame pacing. Keep the configuration that feels best for that game.' \
  '' \
  'See the [Configuration guide](https://github.com/eugeniosegala/MAKO/blob/main/plugin/docs/CONFIGURATION.md) and [Troubleshooting guide](https://github.com/eugeniosegala/MAKO/blob/main/plugin/docs/TROUBLESHOOTING.md) for the full behaviour and per-game controls.' \
  '' \
  '> ⚠️ **Required renderer-update step:** Installing the ZIP updates MAKO Decky, but does **not** by itself replace MAKO Renderer. Open Mako and select **Install MAKO Renderer (developer build)** to install the version bundled in the new ZIP.' \
  '' \
  '> [!IMPORTANT]' \
  '> **Preferred clean update:** To prevent Decky retaining a previous plugin backend or bundled payload, especially when moving between local test ZIPs, uninstall **Mako** from Decky, install the newer ZIP, restart your Steam Deck or Steam Machine, then select **Install MAKO Renderer (developer build)** in the plugin.' \
  '' \
  > "$notes_file"

if [[ "$has_flatpak_bundle" == "true" ]]; then
  printf '%s\n' \
    '> **First-time Heroic setup:** Read the [MAKO Decky README](https://github.com/eugeniosegala/MAKO/tree/main/plugin) before preparing Heroic.' \
    '' \
    >> "$notes_file"
fi

printf '%s\n' \
  '## Installation' \
  '' \
  'New to Decky or installing this plugin for the first time? See the [MAKO Decky README](https://github.com/eugeniosegala/MAKO/tree/main/plugin) for setup and prerequisites.' \
  '' \
  "1. Download \`$(basename "$output_path")\` below." \
  "2. On the Steam OS, open Decky Loader's settings and enable **Developer Mode**." \
  '3. Choose **Developer** > **Install Plugin from Zip**, then select the downloaded ZIP.' \
  '4. In Mako, select **Install MAKO Renderer (developer build)**. For native Steam/Proton games, add `~/.local/bin/mako-run %command%` to the game’s Steam launch options.' \
  '' \
  '> [!IMPORTANT]' \
  '> If Decky does not show or reload the plugin after installing a ZIP, uninstall **Mako** from Decky, install the ZIP again, then restart your Steam Deck or Steam Machine. Open Mako afterwards and select **Install MAKO Renderer (developer build)** again.' \
  '' \
  '## Updating an existing MAKO Decky installation' \
  '' \
  '1. Quit any game currently using `~/.local/bin/mako-run`.' \
  '2. Uninstall **Mako** from Decky, then download the newer ZIP from [MAKO Decky releases](https://github.com/eugeniosegala/MAKO/releases?q=tag%3Aplugin-v).' \
  '3. In Game Mode, open Decky Loader’s settings, choose **Developer** > **Install Plugin from Zip**, then select the newer ZIP.' \
  '4. Restart your Steam Deck or Steam Machine.' \
  '5. ⚠️ **Required:** Open Mako and select **Install MAKO Renderer (developer build)** to install the version bundled in the new ZIP.' \
  '6. If you use Heroic, select **Flatpak Setup**, then select **Update** for Heroic’s matching runtime extension (usually **25.08**). This replaces its Flatpak layer with the engine bundled in the new ZIP; Heroic preparation and per-game Wrapper commands remain unchanged.' \
  '' \
  'Experimental profiles and Steam launch options are retained. The private native renderer and launcher are re-created in step 5; shared Flatpak extensions are retained, then refreshed in step 6.' \
  '' \
  "## Known limitations of MAKO Renderer $engine_version" \
  '' \
  '- **HDR is in progress and unavailable in this Decky release:** The engine foundation is included, but the plugin locks HDR exposure off and does not provide a per-game opt-in. In-game HDR controls may be unavailable by design. A later release can unlock the path after activation, presentation, colour, and performance are validated across games.' \
  '- **Adaptive targets are not hard frame limiters:** Adaptive varies generated-frame count toward an average target. It cannot reduce a native framerate already above the target, exceed the configured multiplier/GPU/compositor capacity, or guarantee an unreachable output rate.' \
  '- **Image-quality and latency trade-offs remain game-dependent:** Higher multipliers and lower real-frame rates can increase ghosting and input latency. Smooth Cadence may improve motion consistency while reducing responsiveness.' \
  '' \
  '## Before you play' \
  '' \
  '- This is experimental: test each game before relying on it.' \
  '- Confirm the detected `Lossless.dll` path before launching. Leaving it blank permits upstream discovery.' \
  '' \
  '## Engine payload' \
  '' \
  "- Bundles checksum-verified \`$archive_name\`." \
  >> "$notes_file"

echo "Publishing $plugin_release_tag to $github_repository..."
git -C "$project_dir" push origin "$current_branch"
git -C "$project_dir" push origin "$plugin_release_tag"

if gh release view "$plugin_release_tag" --repo "$github_repository" >/dev/null 2>&1; then
  gh release edit "$plugin_release_tag" --repo "$github_repository" \
    --title "MAKO Decky v$package_version" \
    --notes-file "$notes_file" \
    --prerelease=false \
    --latest
  gh release upload "$plugin_release_tag" "$output_path" --repo "$github_repository" --clobber
else
  gh release create "$plugin_release_tag" "$output_path" --repo "$github_repository" \
    --title "MAKO Decky v$package_version" \
    --notes-file "$notes_file" \
    --latest \
    --verify-tag
fi

echo "Published: https://github.com/$github_repository/releases/tag/$plugin_release_tag"
