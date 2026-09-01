#!/usr/bin/env bash
# Deterministic contract tests for the standalone MAKO Renderer installer.
set -euo pipefail

installer="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/mako-installer}"

fail() {
    echo "mako-installer test failed: $*" >&2
    exit 1
}

[[ -x "$installer" ]] || fail "installer is not executable: $installer"
bash -n "$installer"

test_root="$(mktemp -d /tmp/mako-installer-contract.XXXXXX)"
trap 'rm -rf -- "$test_root"' EXIT
package_root="$test_root/package"
install_prefix="$test_root/installed prefix"
config_home="$test_root/config"
mkdir -p "$package_root/bin" "$package_root/share/applications"
cp "$installer" "$package_root/Install MAKO Renderer"
cp "$installer" "$package_root/bin/mako-installer"
chmod 0755 "$package_root/Install MAKO Renderer" "$package_root/bin/mako-installer"
printf '%s\n' '#!/usr/bin/env bash' 'exit 0' > "$package_root/bin/mako-ui"
chmod 0755 "$package_root/bin/mako-ui"
printf '%s\n' '[Desktop Entry]' 'Name=MAKO Renderer Configuration' 'Exec=mako-ui %U' > "$package_root/share/applications/io.github.eugeniosegala.mako.desktop"
printf '%s\n' '[Desktop Entry]' 'Name=Uninstall MAKO Renderer' 'Exec=mako-installer --uninstall' > "$package_root/share/applications/io.github.eugeniosegala.mako.uninstaller.desktop"
printf '%s\n' 'test-version' > "$package_root/MAKO-Renderer-version.txt"
(
    cd "$package_root"
    find bin share -type f -print | LC_ALL=C sort | xargs sha256sum
) > "$package_root/MAKO-Renderer-install-manifest.txt"

MAKO_INSTALL_PREFIX="$install_prefix" \
XDG_CONFIG_HOME="$config_home" \
MAKO_INSTALLER_ASSUME_YES=1 \
MAKO_INSTALLER_NO_LAUNCH=1 \
"$package_root/Install MAKO Renderer" --install >"$test_root/install.log"

[[ -x "$install_prefix/bin/mako-ui" ]] || fail "UI was not installed"
grep -Fq 'For a native Steam or Proton game, use this Steam launch option:' \
    "$test_root/install.log" ||
    fail "installer completion did not explain how to activate the Renderer"
grep -Fq "\"$install_prefix/bin/mako-launch\" %command%" \
    "$test_root/install.log" ||
    fail "installer completion did not show the selected prefix's launcher"

dialog_bin="$test_root/dialog-bin"
dialog_capture="$test_root/kdialog.log"
dialog_install_prefix="$test_root/dialog-install"
mkdir -p "$dialog_bin"
printf '%s\n' \
    '#!/usr/bin/env bash' \
    'printf '\''XDG_DATA_DIRS=%s\n'\'' "$XDG_DATA_DIRS" >> "$MAKO_DIALOG_CAPTURE"' \
    'printf '\''%s\n'\'' "$@" >> "$MAKO_DIALOG_CAPTURE"' \
    'exit 0' > "$dialog_bin/kdialog"
chmod 0755 "$dialog_bin/kdialog"
PATH="$dialog_bin:$PATH" \
DISPLAY=:mako-installer-test \
MAKO_DIALOG_CAPTURE="$dialog_capture" \
MAKO_INSTALL_PREFIX="$dialog_install_prefix" \
XDG_CONFIG_HOME="$config_home" \
MAKO_INSTALLER_ASSUME_YES=0 \
MAKO_INSTALLER_NO_LAUNCH=1 \
"$package_root/Install MAKO Renderer" --install >/dev/null
grep -Fxq -- '--title' "$dialog_capture" ||
    fail "KDialog invocation did not set a title"
grep -Fxq 'MAKO Renderer' "$dialog_capture" ||
    fail "KDialog invocation did not use the MAKO Renderer title"
grep -Fxq -- '--icon' "$dialog_capture" ||
    fail "KDialog invocation did not set an icon"
grep -Fxq 'io.github.eugeniosegala.mako' "$dialog_capture" ||
    fail "KDialog invocation did not use the MAKO shark icon"
grep -Fq "XDG_DATA_DIRS=$package_root/share:" "$dialog_capture" ||
    fail "KDialog invocation could not discover the extracted shark icon"
grep -Fxq -- '--yesno' "$dialog_capture" ||
    fail "branded KDialog confirmation was not exercised"
grep -Fxq -- '--msgbox' "$dialog_capture" ||
    fail "branded KDialog completion was not exercised"
[[ -f "$install_prefix/share/mako-render/installer/installed-files.sha256" ]] ||
    fail "installer state was not written"
grep -Fq '"owner": "standalone"' \
    "$install_prefix/share/mako-render/active-renderer.json" ||
    fail "installer did not select the standalone Renderer as active"
grep -Fxq "Exec=\"$install_prefix/bin/mako-ui\" %U" \
    "$install_prefix/share/applications/io.github.eugeniosegala.mako.desktop" ||
    fail "configuration launcher does not use the absolute installed UI path"
grep -Fxq "Exec=\"$install_prefix/bin/mako-installer\" --uninstall" \
    "$install_prefix/share/applications/io.github.eugeniosegala.mako.uninstaller.desktop" ||
    fail "uninstaller launcher does not use the absolute installed command path"
(
    cd "$install_prefix"
    sha256sum --check --status share/mako-render/installer/installed-files.sha256
) || fail "installed state does not describe the rewritten desktop entries"

printf '%s\n' '#!/usr/bin/env bash' 'exit 42' > "$package_root/bin/mako-ui"
chmod 0755 "$package_root/bin/mako-ui"
(
    cd "$package_root"
    find bin share -type f -print | LC_ALL=C sort | xargs sha256sum
) > "$package_root/MAKO-Renderer-install-manifest.txt"

MAKO_INSTALL_PREFIX="$install_prefix" \
XDG_CONFIG_HOME="$config_home" \
MAKO_INSTALLER_ASSUME_YES=1 \
MAKO_INSTALLER_NO_LAUNCH=1 \
"$package_root/Install MAKO Renderer" --install >/dev/null

grep -Fq 'exit 42' "$install_prefix/bin/mako-ui" || fail "update did not replace the UI"
printf '%s\n' 'user modification' > "$install_prefix/share/applications/io.github.eugeniosegala.mako.desktop"
rm -f -- "$install_prefix/bin/mako-ui"
mkdir -p "$config_home/mako-render"
printf '%s\n' 'version = 1' > "$config_home/mako-render/conf.toml"
decky_plugin_dir="$test_root/homebrew/plugins/Mako"
mkdir -p "$decky_plugin_dir"
printf '%s\n' '{}' > "$decky_plugin_dir/plugin.json"

HOME="$test_root" \
MAKO_INSTALL_PREFIX="$install_prefix" \
XDG_CONFIG_HOME="$config_home" \
MAKO_INSTALLER_ASSUME_YES=1 \
"$install_prefix/bin/mako-installer" --uninstall >/dev/null

[[ ! -e "$install_prefix/bin/mako-ui" ]] || fail "uninstaller left the managed UI"
[[ ! -e "$install_prefix/share/mako-render/active-renderer.json" ]] ||
    fail "uninstaller left the active Renderer identity"
[[ -f "$install_prefix/share/applications/io.github.eugeniosegala.mako.desktop" ]] ||
    fail "uninstaller removed a modified file"
[[ -f "$config_home/mako-render/conf.toml" ]] || fail "uninstaller removed configuration by default"

printf 'n\n' | HOME="$test_root" \
        DISPLAY= \
        WAYLAND_DISPLAY= \
        MAKO_INSTALL_PREFIX="$install_prefix" \
        XDG_CONFIG_HOME="$config_home" \
        MAKO_INSTALLER_ASSUME_YES=0 \
        MAKO_INSTALLER_NO_LAUNCH=1 \
        "$package_root/Install MAKO Renderer" --install >"$test_root/decky-install.log" 2>&1
grep -Fq 'Warning: MAKO Decky is installed' "$test_root/decky-install.log" ||
    fail "installer did not explain the MAKO Decky compatibility warning"
[[ ! -e "$install_prefix/bin/mako-ui" ]] ||
    fail "installer changed the standalone payload after the MAKO Decky warning was declined"

HOME="$test_root" \
MAKO_INSTALL_PREFIX="$install_prefix" \
XDG_CONFIG_HOME="$config_home" \
MAKO_INSTALLER_ASSUME_YES=1 \
MAKO_INSTALLER_NO_LAUNCH=1 \
"$package_root/Install MAKO Renderer" --install >/dev/null
[[ -x "$install_prefix/bin/mako-ui" ]] ||
    fail "installer did not continue after accepting the MAKO Decky warning"
grep -Fq '"version": "test-version"' \
    "$install_prefix/share/mako-render/active-renderer.json" ||
    fail "installer did not record the active standalone Renderer version"

HOME="$test_root" \
XDG_CONFIG_HOME="$config_home" \
MAKO_INSTALLER_ASSUME_YES=1 \
MAKO_INSTALLER_NO_LAUNCH=1 \
"$package_root/Install MAKO Renderer" --install >/dev/null
mkdir -p "$test_root/.local/share/mako-render/lib"
printf '%s\n' 'decky renderer' > \
    "$test_root/.local/share/mako-render/lib/libmako-render.so"
printf '%s\n' '#!/usr/bin/env bash' > "$test_root/.local/bin/mako-run"

printf 'n\n' | HOME="$test_root" \
        DISPLAY= \
        WAYLAND_DISPLAY= \
        XDG_CONFIG_HOME="$config_home" \
        MAKO_INSTALLER_ASSUME_YES=0 \
        "$test_root/.local/bin/mako-installer" --uninstall \
        >"$test_root/decky-uninstall.log" 2>&1
grep -Fq 'MAKO Decky will remain installed, but its shared native Renderer will be removed.' \
    "$test_root/decky-uninstall.log" ||
    fail "uninstaller did not explain the shared MAKO Decky Renderer consequence"
grep -Fq 'Open MAKO Decky and select Install Renderer before using MAKO again.' \
    "$test_root/decky-uninstall.log" ||
    fail "uninstaller did not explain how to restore MAKO Decky"
[[ -f "$test_root/.local/share/mako-render/lib/libmako-render.so" ]] ||
    fail "declining the shared Renderer warning changed the Decky payload"

HOME="$test_root" \
XDG_CONFIG_HOME="$config_home" \
MAKO_INSTALLER_ASSUME_YES=1 \
"$test_root/.local/bin/mako-installer" --uninstall >/dev/null

[[ ! -e "$test_root/.local/bin/mako-ui" ]] ||
    fail "default uninstaller left the standalone UI"
[[ ! -e "$test_root/.local/share/mako-render/lib/libmako-render.so" ]] ||
    fail "default uninstaller left the Decky-supplied Renderer"
[[ ! -e "$test_root/.local/bin/mako-run" ]] ||
    fail "default uninstaller left the Decky Renderer wrapper"
[[ ! -e "$test_root/.local/share/mako-render/active-renderer.json" ]] ||
    fail "default uninstaller left the active Renderer identity"
[[ ! -e "$test_root/.local/share/mako-render" ]] ||
    fail "default uninstaller left the managed Renderer data directory"

printf '%s\n' 'mako-installer contract test passed'
