#!/usr/bin/env bash
# Inspect or explicitly remove this checkout's centralized build storage.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cache_dir="$repo_root/build/cache"
work_dir="$repo_root/build/work"
confirm=false

usage() {
    cat <<'EOF'
Usage: scripts/prune-build-cache.sh [--confirm]

Reports MAKO's repository-local reusable cache and disposable work storage.
With no arguments this is a dry run: nothing is removed.

Options:
  --confirm   Remove only this checkout's build/cache and build/work trees.
  -h, --help  Show this help.

This preserves source, incremental CMake build trees, release artifacts,
installed plugins/renderers, profiles, and the normal user Flatpak install.
Later release builds will download the Qt and Flatpak SDK dependencies again.
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

echo "Build storage for: $repo_root"
report_dir "Reusable cache" "$cache_dir"
report_dir "Disposable work" "$work_dir"

if [[ "$confirm" != true ]]; then
    cat <<EOF

Dry run only: nothing was removed.
To remove exactly these two directories, run:
  $0 --confirm
EOF
    exit 0
fi

for path in "$cache_dir" "$work_dir"; do
    if [[ -e "$path" ]]; then
        rm -rf --one-file-system -- "$path"
        echo "Removed: $path"
    fi
done

echo "Repository-local build storage pruned; installed MAKO data was preserved."
