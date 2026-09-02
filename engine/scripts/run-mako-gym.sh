#!/usr/bin/env bash
set -euo pipefail

engine_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mako_root="$(cd "$engine_root/.." && pwd)"
gym_repo="${MAKO_GYM_REPO:-$mako_root/../MAKO-Gym}"
required=false
suite=vulkan
suite_selected=false
all_suites=false
list_suites=false
arguments=()
suite_names=(
    vulkan
    quality
    repeatability
    performance
    spatial-performance
    runtime-overhead
    sync-validation
    recovery
    external-recovery
    gamescope-e2e
    direct-desktop-e2e
    sustained-health
    proton-e2e
    proton-compatibility
)

usage() {
    cat <<'EOF'
Usage: engine/scripts/run-mako-gym.sh [bridge options] [Gym options]

Bridge options:
  --gym-repo PATH  Use an explicit MAKO Gym checkout.
  --require        Fail when MAKO Gym is absent; intended for release gates.
  --suite NAME     Select vulkan (default), quality, repeatability, performance,
                   spatial-performance, runtime-overhead, sync-validation, recovery,
                   external-recovery, gamescope-e2e, direct-desktop-e2e,
                   sustained-health, proton-e2e, or proton-compatibility.
  --all-suites     Run all fourteen suites sequentially with the forwarded Gym options.
  --list-suites    Print the canonical bridge suite names without requiring Gym.
  -h, --help       Show this bridge help.

Every other argument is forwarded unchanged to the selected MAKO Gym runner.
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
            suite_selected=true
            shift
            ;;
        --all-suites)
            all_suites=true
            ;;
        --list-suites)
            list_suites=true
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

if [[ "$list_suites" == true ]]; then
    if [[ "$all_suites" == true || "$suite_selected" == true || ${#arguments[@]} -ne 0 ]]; then
        echo "--list-suites cannot be combined with suite selections or Gym options" >&2
        exit 2
    fi
    printf '%s\n' "${suite_names[@]}"
    exit 0
fi

if [[ "$all_suites" == true && "$suite_selected" == true ]]; then
    echo "--all-suites cannot be combined with --suite" >&2
    exit 2
fi

if [[ ! -d "$gym_repo" ]]; then
    if [[ "$required" == true ]]; then
        echo "MAKO Gym required but checkout is absent: $gym_repo" >&2
        exit 1
    fi
    echo "MAKO Gym: SKIP checkout absent: $gym_repo"
    exit 0
fi

expected_version="$(tr -d '[:space:]' < "$engine_root/scripts/mako-gym-contract-version.txt")"
version_file="$gym_repo/GYM_CONTRACT_VERSION"
if [[ ! -f "$version_file" ]]; then
    echo "MAKO Gym checkout has no GYM_CONTRACT_VERSION: $gym_repo" >&2
    exit 1
fi
actual_version="$(tr -d '[:space:]' < "$version_file")"
if [[ "$actual_version" != "$expected_version" ]]; then
    echo "MAKO Gym contract mismatch: MAKO expects $expected_version, Gym provides $actual_version" >&2
    exit 1
fi

resolve_runner() {
    case "$1" in
        vulkan) runner="$gym_repo/scripts/run-vulkan-feature-matrix.sh" ;;
        quality) runner="$gym_repo/scripts/run-amd-quality-regression.sh" ;;
        repeatability) runner="$gym_repo/scripts/run-quality-repeatability.sh" ;;
        performance) runner="$gym_repo/scripts/run-render-performance.sh" ;;
        spatial-performance) runner="$gym_repo/scripts/run-spatial-performance.sh" ;;
        runtime-overhead) runner="$gym_repo/scripts/run-runtime-overhead.sh" ;;
        sync-validation) runner="$gym_repo/scripts/run-synchronization-validation.sh" ;;
        recovery) runner="$gym_repo/scripts/run-runtime-recovery-matrix.sh" ;;
        external-recovery) runner="$gym_repo/scripts/run-external-recovery.sh" ;;
        gamescope-e2e) runner="$gym_repo/scripts/run-gamescope-end-to-end.sh" ;;
        direct-desktop-e2e) runner="$gym_repo/scripts/run-direct-desktop-end-to-end.sh" ;;
        sustained-health) runner="$gym_repo/scripts/run-sustained-health.sh" ;;
        proton-e2e) runner="$gym_repo/scripts/run-proton-end-to-end.sh" ;;
        proton-compatibility) runner="$gym_repo/scripts/run-proton-compatibility-matrix.sh" ;;
        *) return 1 ;;
    esac
}

require_runner() {
    if ! resolve_runner "$1"; then
        echo "Unknown MAKO Gym suite: $1" >&2
        exit 2
    fi
    if [[ ! -x "$runner" ]]; then
        echo "MAKO Gym runner is missing or not executable: $runner" >&2
        exit 1
    fi
}

if [[ "$all_suites" == true ]]; then
    runners=()
    for selected_suite in "${suite_names[@]}"; do
        require_runner "$selected_suite"
        runners+=("$runner")
    done
    for runner in "${runners[@]}"; do
        "$runner" "${arguments[@]}"
    done
    exit 0
fi

require_runner "$suite"
exec "$runner" "${arguments[@]}"
