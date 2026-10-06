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
export HOME="$test_root/home"
mkdir -p "$HOME"
package_root="$test_root/package"
install_prefix="$test_root/installed prefix"
config_home="$test_root/config"
mkdir -p "$package_root/bin" "$package_root/share/applications"
cp "$installer" "$package_root/Install MAKO Renderer"
cp "$installer" "$package_root/bin/mako-installer"
chmod 0755 "$package_root/Install MAKO Renderer" "$package_root/bin/mako-installer"
printf '%s\n' '#!/usr/bin/env bash' 'exit 0' > "$package_root/bin/mako-ui"
chmod 0755 "$package_root/bin/mako-ui"
printf '%s\n' '[Desktop Entry]' 'Name=MAKO Renderer Configuration' 'Exec=mako-ui' > "$package_root/share/applications/io.github.eugeniosegala.mako.desktop"
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
grep -Fxq "Exec=\"$install_prefix/bin/mako-ui\"" \
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
printf '%s\n' 'retain diagnostic session' > "$config_home/mako-render/present-diagnostics.log"
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
[[ -f "$config_home/mako-render/present-diagnostics.log" ]] ||
    fail "uninstaller removed diagnostics by default"

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

mkdir -p "$test_root/.local/share/mako-render/lib" "$test_root/.local/bin"
printf '%s\n' 'decky renderer' > \
    "$test_root/.local/share/mako-render/lib/libmako-render.so"
printf '%s\n' '#!/usr/bin/env bash' > "$test_root/.local/bin/mako-run"
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
grep -Fxq 'decky renderer' \
    "$test_root/.local/share/mako-render/lib/libmako-render.so" ||
    fail "standalone installation changed Decky's private Renderer payload"
[[ -f "$test_root/.local/bin/mako-run" ]] ||
    fail "standalone installation removed Decky's launch wrapper"

HOME="$test_root" \
XDG_CONFIG_HOME="$config_home" \
MAKO_INSTALLER_ASSUME_YES=1 \
MAKO_INSTALLER_NO_LAUNCH=1 \
"$package_root/Install MAKO Renderer" --install >/dev/null
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
[[ -f "$config_home/mako-render/present-diagnostics.log" ]] ||
    fail "shared Renderer uninstall removed diagnostics without confirmation"

# An invalid late payload must not partially update an existing installation.
rollback_prefix="$test_root/rollback"
export MAKO_INSTALL_PREFIX="$rollback_prefix" MAKO_INSTALLER_ASSUME_YES=1 MAKO_INSTALLER_NO_LAUNCH=1
printf 'obsolete payload\n' > "$package_root/bin/obsolete"
(
    cd "$package_root"
    find bin share -type f -print | LC_ALL=C sort | xargs sha256sum
) > "$package_root/MAKO-Renderer-install-manifest.txt"
"$package_root/Install MAKO Renderer" --install >/dev/null
cp "$rollback_prefix/share/mako-render/installer/installed-files.sha256" "$test_root/before.sha256"
rm "$package_root/bin/obsolete"
printf 'new payload\n' > "$package_root/bin/new-file"
printf '%s\n' '#!/usr/bin/env bash' 'exit 77' > "$package_root/bin/mako-ui"
(
    cd "$package_root"
    find bin share -type f -print | LC_ALL=C sort | xargs sha256sum
) > "$package_root/MAKO-Renderer-install-manifest.txt"
late_payload="$package_root/share/applications/io.github.eugeniosegala.mako.uninstaller.desktop"
cp "$late_payload" "$test_root/late-payload"
printf '%s\n' 'corrupt late file' > "$late_payload"
if "$package_root/Install MAKO Renderer" --install >"$test_root/rejected.log" 2>&1; then
    fail "installer accepted a corrupted late package file"
fi
(
    cd "$rollback_prefix"
    sha256sum --check --status "$test_root/before.sha256"
) || fail "failed package verification partially replaced the old installation"
cp "$test_root/late-payload" "$late_payload"

# Fail the last identity rename once, after binaries and manifests changed.
mkdir -p "$test_root/failing-bin"
cat > "$test_root/failing-bin/mv" <<'EOF'
#!/usr/bin/env bash
if [[ "${@: -1}" == "$MAKO_TEST_FAIL_DESTINATION" && ! -e "$MAKO_TEST_FAILURE_MARKER" ]]; then
    touch "$MAKO_TEST_FAILURE_MARKER"
    exit 1
fi
exec /usr/bin/mv "$@"
EOF
chmod 0755 "$test_root/failing-bin/mv"
cp "$rollback_prefix/share/mako-render/active-renderer.json" "$test_root/before-active.json"
if PATH="$test_root/failing-bin:$PATH" \
        MAKO_TEST_FAIL_DESTINATION="$rollback_prefix/share/mako-render/active-renderer.json" \
        MAKO_TEST_FAILURE_MARKER="$test_root/failed-once" \
        "$package_root/Install MAKO Renderer" --install >"$test_root/rollback.log" 2>&1; then
    fail "installer reported success after the active identity rename failed"
fi
[[ -f "$test_root/failed-once" ]] || fail "test never reached the final identity write"
(
    cd "$rollback_prefix"
    sha256sum --check --status "$test_root/before.sha256"
) || fail "late failure did not roll back installed files and their ownership record"
[[ ! -e "$rollback_prefix/bin/new-file" ]] || fail "late failure left a new payload file behind"
cmp "$test_root/before-active.json" "$rollback_prefix/share/mako-render/active-renderer.json" ||
    fail "late failure changed the selected Renderer owner"
[[ -z "$(find "$rollback_prefix" -name '.mako-install-*' -o -name '*.mako-install.*')" ]] ||
    fail "installer left transaction files after rollback"

if ((EUID != 0)); then
    chmod 0500 "$rollback_prefix"
    if "$package_root/Install MAKO Renderer" --install >"$test_root/prefix-permissions.log" 2>&1; then
        fail "installer ignored a non-writable installation prefix"
    fi
    chmod 0755 "$rollback_prefix"
    grep -Fq 'previous installation was preserved' "$test_root/prefix-permissions.log" ||
        fail "installer did not report a staging permission failure"
    chmod 0500 "$rollback_prefix/bin"
    if "$package_root/Install MAKO Renderer" --install >"$test_root/permissions.log" 2>&1; then
        fail "installer ignored a non-writable destination directory"
    fi
    chmod 0755 "$rollback_prefix/bin"
    (
        cd "$rollback_prefix"
        sha256sum --check --status "$test_root/before.sha256"
    ) || fail "permission failure damaged the existing installation"
fi

ui_result="$(MAKO_INSTALLER_NO_LAUNCH=0 "$package_root/Install MAKO Renderer" --install 2>&1)"
[[ "$ui_result" == *'configuration app exited with an error'* ]] ||
    fail "installer hid the configuration app startup failure"
[[ ! -e "$rollback_prefix/bin/obsolete" && -f "$rollback_prefix/bin/new-file" ]] ||
    fail "successful retry did not update the obsolete and new payloads"
(
    cd "$rollback_prefix"
    sha256sum --check --status share/mako-render/installer/installed-files.sha256
) || fail "successful retry left an incorrect ownership record"

purge_prefix="$test_root/purge-install"
purge_config_home="$test_root/purge-config"
MAKO_INSTALL_PREFIX="$purge_prefix" \
XDG_CONFIG_HOME="$purge_config_home" \
"$package_root/Install MAKO Renderer" --install >/dev/null
mkdir -p "$purge_config_home/mako-render"
printf '%s\n' 'version = 1' > "$purge_config_home/mako-render/conf.toml"
printf '%s\n' 'private diagnostics' > \
    "$purge_config_home/mako-render/present-diagnostics.log"
MAKO_INSTALL_PREFIX="$purge_prefix" \
XDG_CONFIG_HOME="$purge_config_home" \
"$purge_prefix/bin/mako-installer" --uninstall --purge-configuration >/dev/null
[[ ! -e "$purge_config_home/mako-render" ]] ||
    fail "explicit configuration purge left profiles or diagnostics behind"

# Remote Play restoration must precede every payload/configuration removal.
remote_prefix="$test_root/remote-install"
remote_config="$test_root/remote-config"
MAKO_INSTALL_PREFIX="$remote_prefix" XDG_CONFIG_HOME="$remote_config" \
    "$package_root/Install MAKO Renderer" --install >/dev/null
mkdir -p "$remote_config/mako-render"
printf '%s\n' '{}' > "$remote_config/mako-render/native-remote-play.json"
if MAKO_INSTALL_PREFIX="$remote_prefix" XDG_CONFIG_HOME="$remote_config" \
        "$remote_prefix/bin/mako-installer" --uninstall --purge-configuration >"$test_root/remote-blocked.log" 2>&1; then
    fail "older Renderer uninstall ignored an existing Remote Play override"
fi
[[ -f "$remote_prefix/bin/mako-ui" && -f "$remote_config/mako-render/native-remote-play.json" ]] ||
    fail "blocked restoration removed Renderer or configuration"
# The shared override record remains under HOME even with a custom XDG path.
mkdir -p "$HOME/.config/mako-render"
mv "$remote_config/mako-render/native-remote-play.json" "$HOME/.config/mako-render/"
if MAKO_INSTALL_PREFIX="$remote_prefix" XDG_CONFIG_HOME="$remote_config" \
        "$remote_prefix/bin/mako-installer" --uninstall >"$test_root/remote-home-blocked.log" 2>&1; then
    fail "older Renderer uninstall ignored the shared HOME override record"
fi
mv "$HOME/.config/mako-render/native-remote-play.json" "$remote_config/mako-render/"
printf '%s\n' '#!/bin/sh' 'exit 1' > "$remote_prefix/bin/mako-remote-play"
chmod 0755 "$remote_prefix/bin/mako-remote-play"
if MAKO_INSTALL_PREFIX="$remote_prefix" XDG_CONFIG_HOME="$remote_config" \
        "$remote_prefix/bin/mako-installer" --uninstall --purge-configuration >"$test_root/remote-failed.log" 2>&1; then
    fail "Renderer uninstall ignored a restoration failure"
fi
[[ -f "$remote_prefix/bin/mako-ui" && -f "$remote_config/mako-render/native-remote-play.json" ]] ||
    fail "failed restoration removed Renderer or configuration"
printf '%s\n' '#!/bin/sh' 'test "$1" = restore-before-uninstall' > "$remote_prefix/bin/mako-remote-play"
MAKO_INSTALL_PREFIX="$remote_prefix" XDG_CONFIG_HOME="$remote_config" \
    "$remote_prefix/bin/mako-installer" --uninstall --purge-configuration >/dev/null
[[ ! -e "$remote_prefix/bin/mako-ui" && ! -e "$remote_config/mako-render" ]] ||
    fail "successful restoration did not allow uninstall"

printf '%s\n' 'mako-installer contract test passed'
