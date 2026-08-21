#!/usr/bin/env bash
# Inspect or explicitly remove only MAKO's retained hardware-CI caches.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ci_root="${MAKO_HARDWARE_CI_ROOT:-$repo_root/engine/build/cache}"
confirm=false

usage() {
  cat <<'EOF'
Usage: scripts/prune-hardware-ci-cache.sh [--confirm]

Reports the compiler, Flatpak, native SDK, pnpm, and GitHub Actions tool caches
retained between MAKO's local and ephemeral SteamOS hardware-validation jobs.
Nothing is removed without --confirm.

Options:
  --confirm   Remove only the named cache directories reported beneath the
              configured MAKO hardware-CI root.
  -h, --help  Show this help.

The one-job runner, checkout, credentials, generated packages, and staging
directories are not caches and are removed automatically after every run.
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

case "$ci_root" in
  /*) ;;
  *)
    echo "MAKO_HARDWARE_CI_ROOT must be an absolute path: $ci_root" >&2
    exit 2
    ;;
esac
case "$ci_root" in
  /|/home|"$HOME")
    echo "Refusing unsafe MAKO_HARDWARE_CI_ROOT: $ci_root" >&2
    exit 2
    ;;
esac

cache_paths=(
  "$ci_root/ccache"
  "$ci_root/flatpak"
  "$ci_root/native-sdk"
  "$ci_root/quality-regression"
  "$ci_root/pnpm-store"
  "$ci_root/tool-cache"
)

echo "MAKO hardware-CI cache root: $ci_root"
for path in "${cache_paths[@]}"; do
  if [[ -e "$path" ]]; then
    du -sh "$path"
  else
    echo "Not present: $path"
  fi
done
runner_present=false
if compgen -G "$ci_root/runner.*" >/dev/null; then
  runner_present=true
  echo "Warning: a runner directory exists; do not prune while validation is active." >&2
fi

if [[ "$confirm" != true ]]; then
  echo
  echo "Dry run only. Add --confirm to remove the cache directories above."
  exit 0
fi

if [[ "$runner_present" == true ]]; then
  echo "Refusing to prune caches while a hardware runner directory exists." >&2
  exit 1
fi

for path in "${cache_paths[@]}"; do
  case "$path" in
    "$ci_root"/ccache|"$ci_root"/flatpak|"$ci_root"/native-sdk|\
    "$ci_root"/quality-regression|"$ci_root"/pnpm-store|"$ci_root"/tool-cache)
      if [[ -e "$path" ]]; then
        rm -rf --one-file-system -- "$path"
        echo "Removed: $path"
      fi
      ;;
  esac
done

echo "MAKO hardware-CI caches pruned; installed software and runner state were preserved."
