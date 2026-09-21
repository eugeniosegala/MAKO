#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/../../.." && pwd)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/mako-arch-package-test.XXXXXX")"
cleanup() {
    rm -rf -- "$work_dir"
}
trap cleanup EXIT

set_pkgrel() {
    python3 - "$1" "$2" <<'PY'
import re
import sys

path, value = sys.argv[1:]
with open(path, encoding="utf-8") as source_file:
    contents = source_file.read()
updated, count = re.subn(
    r"^pkgrel=.*$", f"pkgrel={value}", contents, flags=re.MULTILINE
)
if count != 1:
    raise SystemExit("expected exactly one pkgrel assignment")
with open(path, "w", encoding="utf-8") as destination_file:
    destination_file.write(updated)
PY
}

"$script_dir/check-release-pin.sh" >/dev/null

cp "$script_dir/PKGBUILD" "$work_dir/PKGBUILD"
python3 - "$repo_root/plugin/package.json" "$work_dir/package.json" <<'PY'
import json
import sys

source, destination = sys.argv[1:]
with open(source, encoding="utf-8") as source_file:
    manifest = json.load(source_file)
binary = manifest["remote_binary"][0]
version = "9.8.7"
binary["name"] = f"MAKO-Renderer-v{version}-linux.tar.xz"
binary["version"] = version
binary["url"] = (
    "https://github.com/eugeniosegala/MAKO/releases/download/"
    f"render-v{version}/{binary['name']}"
)
binary["sha256hash"] = "ab" * 32
with open(destination, "w", encoding="utf-8") as destination_file:
    json.dump(manifest, destination_file, indent=2)
    destination_file.write("\n")
PY

set_pkgrel "$work_dir/PKGBUILD" 42
python3 "$script_dir/sync-release-pin.py" \
    "$work_dir/package.json" "$work_dir/PKGBUILD" >/dev/null
grep -Fqx 'pkgver=9.8.7' "$work_dir/PKGBUILD"
grep -Fqx 'pkgrel=1' "$work_dir/PKGBUILD"
grep -Fqx "sha256sums=('$(printf 'ab%.0s' {1..32})')" "$work_dir/PKGBUILD"
"$script_dir/check-release-pin.sh" \
    "$work_dir/PKGBUILD" "$work_dir/package.json" >/dev/null

set_pkgrel "$work_dir/PKGBUILD" 7
python3 "$script_dir/sync-release-pin.py" \
    "$work_dir/package.json" "$work_dir/PKGBUILD" >/dev/null
grep -Fqx 'pkgrel=7' "$work_dir/PKGBUILD"

test_home="$work_dir/home with spaces"
mkdir -p "$test_home/.local/share/mako-render"
printf '%s\n' '{"owner":"\033[31mhostile"}' \
    > "$test_home/.local/share/mako-render/active-renderer.json"
tree_hash() {
    python3 - "$1" <<'PY'
import hashlib
import sys
from pathlib import Path

root = Path(sys.argv[1])
digest = hashlib.sha256()
for path in sorted(item for item in root.rglob("*") if item.is_file()):
    digest.update(str(path.relative_to(root)).encode())
    digest.update(b"\0")
    digest.update(path.read_bytes())
print(digest.hexdigest())
PY
}

before_hash="$(tree_hash "$test_home")"
hook_output="$work_dir/hooks.out"
(
    # shellcheck disable=SC1091
    . "$script_dir/mako-renderer-bin.install"
    mako_renderer_bin_user_homes() {
        printf '%s\n' "$test_home"
    }
    pre_install
    post_install
    pre_upgrade
    post_upgrade
    pre_remove
    post_remove
) > "$hook_output"
after_hash="$(tree_hash "$test_home")"

[[ "$before_hash" == "$after_hash" ]]
grep -Fq "$test_home (owner: unknown)" "$hook_output"
if grep -Fq 'hostile' "$hook_output"; then
    echo "Arch package hooks exposed an untrusted owner value" >&2
    exit 1
fi

printf '%s\n' "Arch package contract tests passed."
