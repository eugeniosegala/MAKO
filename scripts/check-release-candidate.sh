#!/usr/bin/env bash
# Verify an already-built complete local MAKO Decky ZIP before publication.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
  cat <<'EOF'
Usage: scripts/check-release-candidate.sh PATH-TO-LOCAL-DECKY-ZIP

Checks a complete, portable local Decky ZIP from the current clean, pushed
commit. This does not build, install, deploy, publish, or run MAKO Gym.
EOF
}

if (($# == 1)) && [[ "$1" == --help || "$1" == -h ]]; then
  usage
  exit 0
fi
if (($# != 1)); then
  usage >&2
  exit 2
fi

for command in git node python3 realpath sha256sum tar unzip; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Required command not found: $command" >&2
    exit 1
  fi
done

archive="$(realpath -e -- "$1")"
if [[ ! -f "$archive" ]]; then
  echo "MAKO Decky candidate is not a file: $archive" >&2
  exit 1
fi

cd "$repo_root"
if [[ -n "$(git status --porcelain --untracked-files=normal)" ]]; then
  echo "Release-candidate checks require a clean worktree." >&2
  exit 1
fi
branch="$(git branch --show-current)"
if [[ -z "$branch" ]] || ! git check-ref-format --branch "$branch" >/dev/null 2>&1; then
  echo "Release-candidate checks require a named branch." >&2
  exit 1
fi
git fetch --quiet origin "refs/heads/$branch:refs/remotes/origin/$branch"
source_commit="$(git rev-parse HEAD)"
engine_commit="$(git -C "$repo_root/engine" log -1 --format=%H -- .)"
if [[ "$source_commit" != "$(git rev-parse "refs/remotes/origin/$branch")" ]]; then
  echo "The candidate branch must match origin/$branch." >&2
  exit 1
fi

unzip -t "$archive" >/dev/null
work_root="$(mktemp -d "${TMPDIR:-/tmp}/mako-release-candidate.XXXXXX")"
trap 'rm -rf -- "$work_root"' EXIT
unzip -q "$archive" -d "$work_root"
package_root="$work_root/Mako"
node "$repo_root/plugin/scripts/validate-package-contract.mjs" "$package_root" local

renderer_name="$(node - "$package_root/package.json" "$source_commit" "$engine_commit" <<'JS'
const fs = require('node:fs');
const [manifestPath, sourceCommit, engineCommit] = process.argv.slice(2);
const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
const renderer = manifest.bundled_renderer;
if (manifest.local_package_source_commit !== sourceCommit ||
    manifest.local_package_worktree_dirty !== false) {
  throw new Error('The Decky ZIP must be built from the current clean commit');
}
if (renderer?.source_commit !== engineCommit || renderer?.local_worktree_dirty !== false) {
  throw new Error('The bundled Renderer must come from the current clean engine source');
}
if (!renderer.architectures?.includes('64') || !renderer.architectures?.includes('32') ||
    !renderer.host_architectures?.includes('x86_64')) {
  throw new Error('The candidate must contain 64-bit and 32-bit x86 Renderer layers');
}
process.stdout.write(renderer.name);
JS
)"

flatpak_bundle_output="$(python3 "$repo_root/plugin/scripts/read_flatpak_runtime_contract.py" bundles)"
if [[ -z "$flatpak_bundle_output" ]]; then
  echo "The Flatpak runtime contract contains no bundles." >&2
  exit 1
fi
mapfile -t flatpak_bundles <<< "$flatpak_bundle_output"
for flatpak_bundle in "${flatpak_bundles[@]}"; do
  if [[ ! -s "$package_root/bin/$flatpak_bundle" ]]; then
    echo "The complete candidate is missing Flatpak bundle $flatpak_bundle." >&2
    exit 1
  fi
done

tar -tJf "$package_root/bin/$renderer_name" > "$work_root/renderer-files.txt"
for renderer_file in ./lib/libmako-render.so ./lib32/libmako-render.so \
  ./share/vulkan/implicit_layer.d/VkLayer_MAKO_render.json; do
  if ! grep -Fxq "$renderer_file" "$work_root/renderer-files.txt"; then
    echo "The native Renderer archive is missing $renderer_file." >&2
    exit 1
  fi
done

echo "Verified complete MAKO Decky release candidate: $archive"
echo "Source commit: $source_commit"
sha256sum "$archive"
