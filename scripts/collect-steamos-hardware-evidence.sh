#!/usr/bin/env bash
# Record useful SteamOS/Vulkan evidence without persistent host identifiers.
set -euo pipefail

if (($# != 1)); then
  echo "Usage: scripts/collect-steamos-hardware-evidence.sh OUTPUT" >&2
  exit 2
fi

output="$1"
case "$output" in
  /*) ;;
  *) output="$PWD/$output" ;;
esac
mkdir -p "$(dirname "$output")"

for command in vulkaninfo uname sed grep; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Required hardware-evidence command not found: $command" >&2
    exit 1
  fi
done

temporary_root="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"
raw_vulkan="$(mktemp "$temporary_root/mako-vulkan-summary.XXXXXX")"
temporary_output="$(mktemp "$temporary_root/mako-hardware-evidence.XXXXXX")"
cleanup() {
  rm -f -- "$raw_vulkan" "$temporary_output"
}
trap cleanup EXIT

if ! vulkaninfo --summary > "$raw_vulkan" 2>&1; then
  echo "vulkaninfo could not enumerate the hardware-validation GPU:" >&2
  sed -n '1,20p' "$raw_vulkan" >&2
  exit 1
fi
{
  echo "commit=${GITHUB_SHA:-unknown}"
  echo "generated_at=$(date --utc --iso-8601=seconds)"
  echo "kernel_name=$(uname -s)"
  echo "kernel_release=$(uname -r)"
  echo "machine_architecture=$(uname -m)"
  while IFS= read -r os_field; do
    case "$os_field" in
      PRETTY_NAME=*|ID=*|VERSION_ID=*|BUILD_ID=*|VARIANT_ID=*)
        printf '%s\n' "$os_field"
        ;;
    esac
  done < /etc/os-release
  echo
  echo "Vulkan summary (persistent UUID fields removed):"
  sed -E '/^[[:space:]]*(deviceUUID|driverUUID)[[:space:]]*=/d' "$raw_vulkan"
} > "$temporary_output"

host_name="$(uname -n 2>/dev/null || true)"
if [[ -n "$host_name" ]] && grep -Fq "$host_name" "$temporary_output"; then
  echo "Hardware evidence unexpectedly contains the host name." >&2
  exit 1
fi
if grep -Eqi 'deviceUUID|driverUUID|steam\.token|/home/[^/[:space:]]+' "$temporary_output"; then
  echo "Hardware evidence contains a prohibited host identifier or user path." >&2
  exit 1
fi

chmod 0644 "$temporary_output"
mv -f -- "$temporary_output" "$output"
echo "Recorded sanitized SteamOS hardware evidence: ${output##*/}"
