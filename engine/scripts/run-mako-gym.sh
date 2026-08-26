#!/usr/bin/env bash
set -euo pipefail

engine_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mako_root="$(cd "$engine_root/.." && pwd)"
gym_repo="${MAKO_GYM_REPO:-$mako_root/../MAKO-Gym}"
required=false
suite=vulkan
arguments=()

usage() {
    cat <<'EOF'
Usage: engine/scripts/run-mako-gym.sh [bridge options] [Gym options]

Bridge options:
  --gym-repo PATH  Use an explicit MAKO-Gym checkout.
  --require        Fail when MAKO-Gym is absent; intended for release gates.
  --suite NAME     Select vulkan (default) or quality.
  -h, --help       Show this bridge help.

Every other argument is forwarded unchanged to MAKO-Gym's Vulkan matrix.
Without --require, an absent sibling checkout is reported as a clear skip.
EOF
}

while (($#)); do
    case "$1" in
        --gym-repo)
            if (($# < 2)); then
                echo "--gym-repo requires a value" >&2
                exit 2
            fi
            gym_repo="$2"
            shift
            ;;
        --require)
            required=true
            ;;
        --suite)
            if (($# < 2)); then
                echo "--suite requires a value" >&2
                exit 2
            fi
            suite="$2"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            arguments+=("$@")
            break
            ;;
        *)
            arguments+=("$1")
            ;;
    esac
    shift
done

if [[ ! -d "$gym_repo" ]]; then
    if [[ "$required" == true ]]; then
        echo "MAKO-Gym required but checkout is absent: $gym_repo" >&2
        exit 1
    fi
    echo "MAKO-Gym: SKIP checkout absent: $gym_repo"
    exit 0
fi

expected_version="$(tr -d '[:space:]' < "$engine_root/scripts/mako-gym-contract-version.txt")"
version_file="$gym_repo/GYM_CONTRACT_VERSION"
if [[ ! -f "$version_file" ]]; then
    echo "MAKO-Gym checkout has no GYM_CONTRACT_VERSION: $gym_repo" >&2
    exit 1
fi
actual_version="$(tr -d '[:space:]' < "$version_file")"
if [[ "$actual_version" != "$expected_version" ]]; then
    echo "MAKO-Gym contract mismatch: MAKO expects $expected_version, Gym provides $actual_version" >&2
    exit 1
fi

case "$suite" in
    vulkan) runner="$gym_repo/scripts/run-vulkan-feature-matrix.sh" ;;
    quality) runner="$gym_repo/scripts/run-amd-quality-regression.sh" ;;
    *)
        echo "Unknown MAKO-Gym suite: $suite" >&2
        exit 2
        ;;
esac
if [[ ! -x "$runner" ]]; then
    echo "MAKO-Gym runner is missing or not executable: $runner" >&2
    exit 1
fi

exec "$runner" "${arguments[@]}"
