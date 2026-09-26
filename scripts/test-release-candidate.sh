#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/mako-release-candidate-test.XXXXXX")"
trap 'rm -rf -- "$test_root"' EXIT
fixture_repo="$test_root/repo"
mkdir -p "$fixture_repo/scripts" "$fixture_repo/plugin/scripts" "$fixture_repo/engine" "$test_root/payload/lib" \
  "$test_root/payload/lib32" "$test_root/payload/share/vulkan/implicit_layer.d"
cp "$repo_root/scripts/check-release-candidate.sh" "$fixture_repo/scripts/"
printf '0.0.0\n' > "$fixture_repo/engine/VERSION"
cat > "$fixture_repo/plugin/scripts/validate-package-contract.mjs" <<'JS'
import { existsSync } from 'node:fs';
if (!existsSync(`${process.argv[2]}/package.json`) || process.argv[3] !== 'local') process.exit(1);
JS
cat > "$fixture_repo/plugin/scripts/read_flatpak_runtime_contract.py" <<'PY'
print('runtime-a.flatpak')
print('runtime-b.flatpak')
PY
touch "$test_root/payload/lib/libmako-render.so" \
  "$test_root/payload/lib32/libmako-render.so" \
  "$test_root/payload/share/vulkan/implicit_layer.d/VkLayer_MAKO_render.json"
tar -cJf "$test_root/renderer.tar.xz" -C "$test_root/payload" .

git -C "$fixture_repo" init -q -b main
git -C "$fixture_repo" config user.name 'MAKO test'
git -C "$fixture_repo" config user.email 'mako-test@example.invalid'
git -C "$fixture_repo" add .
git -C "$fixture_repo" commit -qm 'Create candidate check fixture'
git init -q --bare "$test_root/origin.git"
git -C "$fixture_repo" remote add origin "$test_root/origin.git"
git -C "$fixture_repo" push -q -u origin main
source_commit="$(git -C "$fixture_repo" rev-parse HEAD)"

make_candidate() {
  local archive="$1"
  local candidate_commit="$2"
  local engine_commit="$3"
  local include_second_flatpak="$4"
  python3 - "$archive" "$candidate_commit" "$engine_commit" "$include_second_flatpak" "$test_root/renderer.tar.xz" <<'PY'
import json
import sys
import zipfile

archive, commit, engine_commit, include_second, renderer = sys.argv[1:]
manifest = {
    'bundled_renderer': {
        'name': 'renderer.tar.xz',
        'source_commit': engine_commit,
        'local_worktree_dirty': False,
        'architectures': ['64', '32'],
        'host_architectures': ['x86_64'],
    },
    'local_package_source_commit': commit,
    'local_package_worktree_dirty': False,
}
with zipfile.ZipFile(archive, 'w') as package:
    package.writestr('Mako/package.json', json.dumps(manifest))
    package.write(renderer, 'Mako/bin/renderer.tar.xz')
    package.writestr('Mako/bin/runtime-a.flatpak', 'bundle a')
    if include_second == 'yes':
        package.writestr('Mako/bin/runtime-b.flatpak', 'bundle b')
PY
}

make_candidate "$test_root/valid.zip" "$source_commit" "$source_commit" yes
"$fixture_repo/scripts/check-release-candidate.sh" "$test_root/valid.zip" > /dev/null

printf 'Release guide changed.\n' > "$fixture_repo/HOW_TO_RELEASE.md"
git -C "$fixture_repo" add HOW_TO_RELEASE.md
git -C "$fixture_repo" commit -qm 'Update release guide only'
git -C "$fixture_repo" push -q origin main
latest_commit="$(git -C "$fixture_repo" rev-parse HEAD)"
make_candidate "$test_root/latest.zip" "$latest_commit" "$source_commit" yes
"$fixture_repo/scripts/check-release-candidate.sh" "$test_root/latest.zip" > /dev/null

make_candidate "$test_root/stale.zip" "$source_commit" "$source_commit" yes
if "$fixture_repo/scripts/check-release-candidate.sh" "$test_root/stale.zip" > /dev/null 2>&1; then
  echo 'A ZIP from another source commit unexpectedly passed.' >&2
  exit 1
fi

make_candidate "$test_root/incomplete.zip" "$latest_commit" "$source_commit" no
if "$fixture_repo/scripts/check-release-candidate.sh" "$test_root/incomplete.zip" > /dev/null 2>&1; then
  echo 'A ZIP without all Flatpak runtimes unexpectedly passed.' >&2
  exit 1
fi

echo 'Release-candidate ZIP checks passed.'
