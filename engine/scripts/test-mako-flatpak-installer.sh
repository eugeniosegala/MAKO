#!/usr/bin/env bash
# Deterministic contract tests for the graphical MAKO Flatpak installer.
set -euo pipefail

installer="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/mako-install-flatpaks}"

fail() {
    echo "mako-flatpak-installer test failed: $*" >&2
    exit 1
}

[[ -x "$installer" ]] || fail "installer is not executable: $installer"
bash -n "$installer"

test_root="$(mktemp -d /tmp/mako-flatpak-installer-contract.XXXXXX)"
trap 'rm -rf -- "$test_root"' EXIT
package_root="$test_root/package"
fake_bin="$test_root/bin"
invocation_log="$test_root/flatpak.log"
mkdir -p "$package_root" "$fake_bin"
cp "$installer" "$package_root/Install MAKO Flatpak Extensions"
chmod 0755 "$package_root/Install MAKO Flatpak Extensions"
touch "$package_root/org.freedesktop.Platform.VulkanLayer.makorender-24.08.flatpak"
printf '%s\n' \
    '#!/usr/bin/env bash' \
    'printf "%s\\n" "$*" > "$MAKO_TEST_FLATPAK_LOG"' \
    > "$fake_bin/flatpak"
chmod 0755 "$fake_bin/flatpak"

PATH="$fake_bin:$PATH" \
MAKO_TEST_FLATPAK_LOG="$invocation_log" \
MAKO_INSTALLER_ASSUME_YES=1 \
MAKO_FLATPAK_RUNTIME=24.08 \
"$package_root/Install MAKO Flatpak Extensions" >/dev/null

package_root_physical="$(cd "$package_root" && pwd -P)"
expected="install --user --noninteractive $package_root_physical/org.freedesktop.Platform.VulkanLayer.makorender-24.08.flatpak"
[[ "$(<"$invocation_log")" == "$expected" ]] ||
    fail "installer did not invoke the selected user-scoped Flatpak bundle"

printf '%s\n' 'mako-flatpak-installer contract test passed'
