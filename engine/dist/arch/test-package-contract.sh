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
grep -Fq "'libx11'" "$script_dir/PKGBUILD"
grep -Fq "'lib32-libx11'" "$script_dir/PKGBUILD"
grep -Fq 'done < MAKO-Renderer-install-manifest.txt' "$script_dir/PKGBUILD"
bash -n "$script_dir/verify-release-package.sh"

fake_tools="$work_dir/fake-tools"
mkdir -p "$fake_tools"
cat > "$fake_tools/makepkg" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
[[ "$*" == "--nodeps --cleanbuild --noconfirm" ]]
[[ -f "MAKO-Renderer-v${MAKO_TEST_PKGVER}-linux.tar.xz" ]]
package_root="$(mktemp -d "${TMPDIR:-/tmp}/mako-arch-fake-package.XXXXXX")"
trap 'rm -rf -- "$package_root"' EXIT
mkdir -p \
    "$package_root/usr/bin" \
    "$package_root/usr/lib" \
    "$package_root/usr/lib32" \
    "$package_root/usr/share/applications" \
    "$package_root/usr/share/doc/mako-renderer-bin"
touch \
    "$package_root/usr/bin/mako-cli" \
    "$package_root/usr/bin/mako-diagnostics" \
    "$package_root/usr/bin/mako-launch" \
    "$package_root/usr/bin/mako-ui" \
    "$package_root/usr/lib/libmako-render.so" \
    "$package_root/usr/lib/libmako-render-scaling.so" \
    "$package_root/usr/lib32/libmako-render.so" \
    "$package_root/usr/lib32/libmako-render-scaling.so" \
    "$package_root/usr/share/applications/io.github.eugeniosegala.mako.desktop"
printf '%s\n' install-script > "$package_root/.INSTALL"
printf '%s\n' \
    'pkgname = mako-renderer-bin' \
    "pkgver = ${MAKO_TEST_PACKAGE_VERSION_OVERRIDE:-${MAKO_TEST_PKGVER}-${MAKO_TEST_PKGREL}}" \
    'arch = x86_64' \
    > "$package_root/.PKGINFO"
printf '%s\n' "${MAKO_TEST_RENDERER_VERSION_OVERRIDE:-$MAKO_TEST_PKGVER}" \
    > "$package_root/usr/share/doc/mako-renderer-bin/MAKO-Renderer-version.txt"
tar -cf "mako-renderer-bin-${MAKO_TEST_PKGVER}-${MAKO_TEST_PKGREL}-x86_64.pkg.tar.zst" \
    -C "$package_root" .INSTALL .PKGINFO usr
EOF
cat > "$fake_tools/bsdtar" <<'EOF'
#!/bin/sh
exec tar "$@"
EOF
chmod +x "$fake_tools/makepkg" "$fake_tools/bsdtar"
current_pkgver="$(sed -n 's/^pkgver=\(.*\)$/\1/p' "$script_dir/PKGBUILD")"
current_pkgrel="$(sed -n 's/^pkgrel=\(.*\)$/\1/p' "$script_dir/PKGBUILD")"
fake_archive="$work_dir/MAKO-Renderer-v${current_pkgver}-linux.tar.xz"
touch "$fake_archive"
retained_package="$work_dir/mako-renderer-bin-${current_pkgver}-${current_pkgrel}-x86_64.pkg.tar.zst"
MAKO_TEST_PKGVER="$current_pkgver" \
    MAKO_TEST_PKGREL="$current_pkgrel" \
    PATH="$fake_tools:/usr/bin:/bin" \
    "$script_dir/verify-release-package.sh" \
        "$fake_archive" "$retained_package" >/dev/null
[[ -f "$retained_package" ]]
tar -tf "$retained_package" | grep -Fqx usr/bin/mako-launch
if MAKO_TEST_PKGVER="$current_pkgver" \
        MAKO_TEST_PKGREL="$current_pkgrel" \
        PATH="$fake_tools:/usr/bin:/bin" \
        "$script_dir/verify-release-package.sh" \
        "$fake_archive" "$work_dir/wrong-package-name.pkg.tar.zst" \
        >/dev/null 2>&1; then
    echo "Arch release package verification accepted the wrong output name" >&2
    exit 1
fi
if MAKO_TEST_PKGVER="$current_pkgver" \
        MAKO_TEST_PKGREL="$current_pkgrel" \
        MAKO_TEST_PACKAGE_VERSION_OVERRIDE="${current_pkgver}-99" \
        PATH="$fake_tools:/usr/bin:/bin" \
        "$script_dir/verify-release-package.sh" "$fake_archive" \
        >/dev/null 2>&1; then
    echo "Arch release package verification accepted a stale internal package version" >&2
    exit 1
fi
if MAKO_TEST_PKGVER="$current_pkgver" \
        MAKO_TEST_PKGREL="$current_pkgrel" \
        MAKO_TEST_RENDERER_VERSION_OVERRIDE=0.0.0 \
        PATH="$fake_tools:/usr/bin:/bin" \
        "$script_dir/verify-release-package.sh" "$fake_archive" \
        >/dev/null 2>&1; then
    echo "Arch release package verification accepted a stale embedded Renderer version" >&2
    exit 1
fi
touch "$work_dir/wrong-name.tar.xz"
if PATH="$fake_tools:/usr/bin:/bin" \
        "$script_dir/verify-release-package.sh" "$work_dir/wrong-name.tar.xz" \
        >/dev/null 2>&1; then
    echo "Arch release package verification accepted the wrong archive name" >&2
    exit 1
fi

mapping_root="$work_dir/mapping"
mapping_pkgdir="$work_dir/mapping-package"
mkdir -p \
    "$mapping_root/bin" \
    "$mapping_root/lib/vkbasalt" \
    "$mapping_root/lib32/vkbasalt" \
    "$mapping_root/share/applications" \
    "$mapping_root/share/doc/mako-render/vkbasalt" \
    "$mapping_root/share/mako-render/future"
printf '%s\n' tool > "$mapping_root/bin/future-tool"
printf '%s\n' installer > "$mapping_root/bin/mako-installer"
printf '%s\n' library > "$mapping_root/lib/vkbasalt/libvkbasalt.so"
printf '%s\n' library32 > "$mapping_root/lib32/vkbasalt/libvkbasalt.so"
printf '%s\n' uninstaller > "$mapping_root/share/applications/io.github.eugeniosegala.mako.uninstaller.desktop"
printf '%s\n' license > "$mapping_root/share/doc/mako-render/vkbasalt/LICENSE"
printf '%s\n' provenance > "$mapping_root/share/doc/mako-render/vkbasalt/SOURCE"
printf '%s\n' future > "$mapping_root/share/mako-render/future/payload.dat"
(
    # shellcheck disable=SC1091
    . "$script_dir/PKGBUILD"
    pkgdir="$mapping_pkgdir"
    cd "$mapping_root"
    _mako_install_payload_file bin/future-tool
    _mako_install_payload_file bin/mako-installer
    _mako_install_payload_file lib/vkbasalt/libvkbasalt.so
    _mako_install_payload_file lib32/vkbasalt/libvkbasalt.so
    _mako_install_payload_file share/applications/io.github.eugeniosegala.mako.uninstaller.desktop
    _mako_install_payload_file share/doc/mako-render/vkbasalt/LICENSE
    _mako_install_payload_file share/doc/mako-render/vkbasalt/SOURCE
    _mako_install_payload_file share/mako-render/future/payload.dat
    if _mako_install_payload_file ../escape 2>/dev/null; then
        echo "Arch package accepted an unsafe manifest path" >&2
        exit 1
    fi
)
[[ -x "$mapping_pkgdir/usr/bin/future-tool" ]]
[[ -x "$mapping_pkgdir/usr/lib/vkbasalt/libvkbasalt.so" ]]
[[ -x "$mapping_pkgdir/usr/lib32/vkbasalt/libvkbasalt.so" ]]
[[ -f "$mapping_pkgdir/usr/share/licenses/mako-renderer-bin/vkbasalt/LICENSE" ]]
[[ -f "$mapping_pkgdir/usr/share/doc/mako-renderer-bin/vkbasalt/SOURCE" ]]
[[ -f "$mapping_pkgdir/usr/share/mako-render/future/payload.dat" ]]
[[ ! -e "$mapping_pkgdir/usr/bin/mako-installer" ]]
[[ ! -e "$mapping_pkgdir/usr/share/applications/io.github.eugeniosegala.mako.uninstaller.desktop" ]]

decky_marker_home="$work_dir/decky-marker-home"
standalone_marker_home="$work_dir/standalone-marker-home"
mkdir -p \
    "$decky_marker_home/.local/share/mako-render/lib" \
    "$standalone_marker_home/.local/lib"
printf '%s\n' decky > \
    "$decky_marker_home/.local/share/mako-render/lib/libmako-render.so"
printf '%s\n' standalone > \
    "$standalone_marker_home/.local/lib/libmako-render.so"
(
    # shellcheck disable=SC1091
    . "$script_dir/mako-renderer-bin.install"
    [[ "$(mako_renderer_bin_user_local_owner "$decky_marker_home")" == unknown ]]
    [[ "$(mako_renderer_bin_user_local_owner "$standalone_marker_home")" == unknown ]]
)

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

native_archive="$work_dir/MAKO-Renderer-v9.8.7-linux.tar.xz"
flatpak_archive="$work_dir/MAKO-Renderer-v9.8.7-flatpaks.tar.xz"
arch_package="$work_dir/mako-renderer-bin-9.8.7-7-x86_64.pkg.tar.zst"
node "$repo_root/plugin/scripts/pin-renderer-release.mjs" \
    "$work_dir/package.json" \
    9.8.7 render-v9.8.7 "$(printf 'cd%.0s' {1..20})" \
    eugeniosegala/MAKO \
    "$native_archive" "$(printf '12%.0s' {1..32})" \
    "$flatpak_archive" "$(printf '34%.0s' {1..32})" \
    "$arch_package" "$(printf '56%.0s' {1..32})" >/dev/null
node -e '
const binary = require(process.argv[1]).remote_binary[0];
if (!binary.arch_package ||
    binary.arch_package.name !== "mako-renderer-bin-9.8.7-7-x86_64.pkg.tar.zst" ||
    binary.arch_package.sha256hash !== "56".repeat(32)) process.exit(1);
' "$work_dir/package.json"
node "$repo_root/plugin/scripts/pin-renderer-release.mjs" \
    "$work_dir/package.json" \
    9.8.7 render-v9.8.7 "$(printf 'cd%.0s' {1..20})" \
    eugeniosegala/MAKO \
    "$native_archive" "$(printf '12%.0s' {1..32})" \
    "$flatpak_archive" "$(printf '34%.0s' {1..32})" >/dev/null
node -e '
const binary = require(process.argv[1]).remote_binary[0];
if (Object.prototype.hasOwnProperty.call(binary, "arch_package")) process.exit(1);
' "$work_dir/package.json"

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
