#!/usr/bin/env bash
# Inspect or explicitly remove the isolated Flatpak development cache.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cache_dir="$repo_root/build/cache/flatpak"
tmp_dir="$repo_root/build/work/flatpak"
confirm=false

usage() {
    cat <<'EOF'
Usage: scripts/prune-steamos-flatpak-cache.sh [--confirm]

Reports the persistent Flatpak development cache used by MAKO's SteamOS
development commands. With no arguments this is a dry run: nothing is removed.

Options:
  --confirm   Remove only this checkout's Flatpak download cache and temporary
              staging directory.
  -h, --help  Show this help.

This never removes native build trees, the installed MAKO Decky, its bundled
Flatpak files, or your normal user Flatpak installation. A later dev:flatpaks
or dev:e2e run will download the Flatpak SDK/runtime dependencies again.
EOF
}

while (($#)); do
    case "$1" in
        --confirm)
            confirm=true
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
    shift
done

report_dir() {
    local label="$1"
    local path="$2"

    if [[ -e "$path" ]]; then
        printf '%s: ' "$label"
        du -sh "$path"
    else
        printf '%s: not present\n' "$label"
    fi
}

echo "Flatpak development cache for: $repo_root"
report_dir "Download cache" "$cache_dir"
report_dir "Temporary staging" "$tmp_dir"

if [[ "$confirm" != true ]]; then
    cat <<EOF

Dry run only: nothing was removed.
To remove exactly these two directories, run:
  $0 --confirm
EOF
    exit 0
fi

# These paths are constructed from the checkout root rather than user input;
# the confirmation step deliberately never follows environment overrides.
for path in "$cache_dir" "$tmp_dir"; do
    if [[ -e "$path" ]]; then
        rm -rf --one-file-system -- "$path"
        echo "Removed: $path"
    fi
done

echo "Flatpak development cache pruned. Native builds and installed MAKO artifacts were preserved."
