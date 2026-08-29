#!/usr/bin/env bash
set -euo pipefail

bridge="${1:?usage: test-run-mako-gym.sh /path/to/run-mako-gym.sh}"
bridge_dir="$(cd "$(dirname "$bridge")" && pwd)"
expected_version="$(tr -d '[:space:]' < "$bridge_dir/mako-gym-contract-version.txt")"
temporary_root="$(mktemp -d)"
trap 'rm -rf -- "$temporary_root"' EXIT

listed_suites="$($bridge --list-suites)"
expected_suites=$'vulkan\nquality\nrepeatability\nperformance\nspatial-performance\nruntime-overhead\nsync-validation\nrecovery\ngamescope-e2e\nsustained-health\nproton-e2e\nproton-compatibility'
if [[ "$listed_suites" != "$expected_suites" ]]; then
    echo "Gym bridge suite inventory is not canonical." >&2
    exit 1
fi
if "$bridge" --list-suites --suite quality >/dev/null 2>&1; then
    echo "Conflicting Gym suite-list request unexpectedly succeeded." >&2
    exit 1
fi

absent="$temporary_root/absent"
optional_output="$($bridge --gym-repo "$absent" --list)"
if [[ "$optional_output" != *'MAKO Gym: SKIP checkout absent:'* ]]; then
    echo "Optional missing-Gym invocation did not report a skip." >&2
    exit 1
fi
if "$bridge" --gym-repo "$absent" --require --list >/dev/null 2>&1; then
    echo "Required missing-Gym invocation unexpectedly succeeded." >&2
    exit 1
fi

fake_gym="$temporary_root/MAKO-Gym"
mkdir -p "$fake_gym/scripts"
printf '%s\n' "$expected_version" > "$fake_gym/GYM_CONTRACT_VERSION"
printf '%s\n' '#!/usr/bin/env bash' 'printf "%s\\n" "$@"' \
    > "$fake_gym/scripts/run-vulkan-feature-matrix.sh"
chmod +x "$fake_gym/scripts/run-vulkan-feature-matrix.sh"
printf '%s\n' '#!/usr/bin/env bash' 'printf "quality:%s\\n" "$@"' \
    > "$fake_gym/scripts/run-amd-quality-regression.sh"
chmod +x "$fake_gym/scripts/run-amd-quality-regression.sh"
printf '%s\n' '#!/usr/bin/env bash' 'printf "repeatability:%s\\n" "$@"' \
    > "$fake_gym/scripts/run-quality-repeatability.sh"
chmod +x "$fake_gym/scripts/run-quality-repeatability.sh"
printf '%s\n' '#!/usr/bin/env bash' 'printf "performance:%s\\n" "$@"' \
    > "$fake_gym/scripts/run-render-performance.sh"
chmod +x "$fake_gym/scripts/run-render-performance.sh"
printf '%s\n' '#!/usr/bin/env bash' 'printf "spatial-performance:%s\\n" "$@"' \
    > "$fake_gym/scripts/run-spatial-performance.sh"
chmod +x "$fake_gym/scripts/run-spatial-performance.sh"
printf '%s\n' '#!/usr/bin/env bash' 'printf "runtime-overhead:%s\\n" "$@"' \
    > "$fake_gym/scripts/run-runtime-overhead.sh"
chmod +x "$fake_gym/scripts/run-runtime-overhead.sh"
printf '%s\n' '#!/usr/bin/env bash' 'printf "sync-validation:%s\\n" "$@"' \
    > "$fake_gym/scripts/run-synchronization-validation.sh"
chmod +x "$fake_gym/scripts/run-synchronization-validation.sh"
printf '%s\n' '#!/usr/bin/env bash' 'printf "recovery:%s\\n" "$@"' \
    > "$fake_gym/scripts/run-runtime-recovery-matrix.sh"
chmod +x "$fake_gym/scripts/run-runtime-recovery-matrix.sh"
printf '%s\n' '#!/usr/bin/env bash' 'printf "gamescope-e2e:%s\\n" "$@"' \
    > "$fake_gym/scripts/run-gamescope-end-to-end.sh"
chmod +x "$fake_gym/scripts/run-gamescope-end-to-end.sh"
printf '%s\n' '#!/usr/bin/env bash' 'printf "sustained-health:%s\\n" "$@"' \
    > "$fake_gym/scripts/run-sustained-health.sh"
chmod +x "$fake_gym/scripts/run-sustained-health.sh"
printf '%s\n' '#!/usr/bin/env bash' 'printf "proton-e2e:%s\\n" "$@"' \
    > "$fake_gym/scripts/run-proton-end-to-end.sh"
chmod +x "$fake_gym/scripts/run-proton-end-to-end.sh"
printf '%s\n' '#!/usr/bin/env bash' 'printf "proton-compatibility:%s\\n" "$@"' \
    > "$fake_gym/scripts/run-proton-compatibility-matrix.sh"
chmod +x "$fake_gym/scripts/run-proton-compatibility-matrix.sh"

forwarded="$($bridge --gym-repo "$fake_gym" --list --filter '^fixed-')"
expected=$'--list\n--filter\n^fixed-'
if [[ "$forwarded" != "$expected" ]]; then
    echo "Gym arguments were not forwarded exactly." >&2
    exit 1
fi

quality_forwarded="$($bridge --gym-repo "$fake_gym" --suite quality --cli /tmp/mako-cli)"
quality_expected=$'quality:--cli\nquality:/tmp/mako-cli'
if [[ "$quality_forwarded" != "$quality_expected" ]]; then
    echo "Gym quality-suite arguments were not forwarded exactly." >&2
    exit 1
fi
repeatability_forwarded="$($bridge --gym-repo "$fake_gym" --suite repeatability --repetitions 4)"
repeatability_expected=$'repeatability:--repetitions\nrepeatability:4'
if [[ "$repeatability_forwarded" != "$repeatability_expected" ]]; then
    echo "Gym repeatability-suite arguments were not forwarded exactly." >&2
    exit 1
fi
performance_forwarded="$($bridge --gym-repo "$fake_gym" --suite performance --baseline /tmp/baseline.json)"
performance_expected=$'performance:--baseline\nperformance:/tmp/baseline.json'
if [[ "$performance_forwarded" != "$performance_expected" ]]; then
    echo "Gym performance-suite arguments were not forwarded exactly." >&2
    exit 1
fi
spatial_performance_forwarded="$($bridge --gym-repo "$fake_gym" --suite spatial-performance --baseline /tmp/spatial.json)"
spatial_performance_expected=$'spatial-performance:--baseline\nspatial-performance:/tmp/spatial.json'
if [[ "$spatial_performance_forwarded" != "$spatial_performance_expected" ]]; then
    echo "Gym spatial-performance arguments were not forwarded exactly." >&2
    exit 1
fi
runtime_overhead_forwarded="$($bridge --gym-repo "$fake_gym" --suite runtime-overhead --tier fast)"
runtime_overhead_expected=$'runtime-overhead:--tier\nruntime-overhead:fast'
if [[ "$runtime_overhead_forwarded" != "$runtime_overhead_expected" ]]; then
    echo "Gym runtime-overhead arguments were not forwarded exactly." >&2
    exit 1
fi
sync_forwarded="$($bridge --gym-repo "$fake_gym" --suite sync-validation --layer-dir /tmp/layers)"
sync_expected=$'sync-validation:--layer-dir\nsync-validation:/tmp/layers'
if [[ "$sync_forwarded" != "$sync_expected" ]]; then
    echo "Gym sync-validation arguments were not forwarded exactly." >&2
    exit 1
fi
recovery_forwarded="$($bridge --gym-repo "$fake_gym" --suite recovery --filter '^adaptive-')"
recovery_expected=$'recovery:--filter\nrecovery:^adaptive-'
if [[ "$recovery_forwarded" != "$recovery_expected" ]]; then
    echo "Gym recovery-suite arguments were not forwarded exactly." >&2
    exit 1
fi
gamescope_e2e_forwarded="$($bridge --gym-repo "$fake_gym" --suite gamescope-e2e --filter '^gamescope-live-')"
gamescope_e2e_expected=$'gamescope-e2e:--filter\ngamescope-e2e:^gamescope-live-'
if [[ "$gamescope_e2e_forwarded" != "$gamescope_e2e_expected" ]]; then
    echo "Gym Gamescope E2E arguments were not forwarded exactly." >&2
    exit 1
fi
sustained_health_forwarded="$($bridge --gym-repo "$fake_gym" --suite sustained-health --filter '^sustained-deck-')"
sustained_health_expected=$'sustained-health:--filter\nsustained-health:^sustained-deck-'
if [[ "$sustained_health_forwarded" != "$sustained_health_expected" ]]; then
    echo "Gym sustained-health arguments were not forwarded exactly." >&2
    exit 1
fi
proton_e2e_forwarded="$($bridge --gym-repo "$fake_gym" --suite proton-e2e --filter '^proton-vkd3d-')"
proton_e2e_expected=$'proton-e2e:--filter\nproton-e2e:^proton-vkd3d-'
if [[ "$proton_e2e_forwarded" != "$proton_e2e_expected" ]]; then
    echo "Gym Proton E2E arguments were not forwarded exactly." >&2
    exit 1
fi
proton_compatibility_forwarded="$($bridge --gym-repo "$fake_gym" --suite proton-compatibility --runtime-tier core --case-tier fast)"
proton_compatibility_expected=$'proton-compatibility:--runtime-tier\nproton-compatibility:core\nproton-compatibility:--case-tier\nproton-compatibility:fast'
if [[ "$proton_compatibility_forwarded" != "$proton_compatibility_expected" ]]; then
    echo "Gym Proton compatibility arguments were not forwarded exactly." >&2
    exit 1
fi
all_suites_forwarded="$($bridge --gym-repo "$fake_gym" --all-suites --validate)"
all_suites_expected=$'--validate\nquality:--validate\nrepeatability:--validate\nperformance:--validate\nspatial-performance:--validate\nruntime-overhead:--validate\nsync-validation:--validate\nrecovery:--validate\ngamescope-e2e:--validate\nsustained-health:--validate\nproton-e2e:--validate\nproton-compatibility:--validate'
if [[ "$all_suites_forwarded" != "$all_suites_expected" ]]; then
    echo "Gym all-suites validation did not invoke all twelve runners exactly once." >&2
    exit 1
fi
if "$bridge" --gym-repo "$fake_gym" --all-suites --suite quality --validate >/dev/null 2>&1; then
    echo "Conflicting Gym suite selections unexpectedly succeeded." >&2
    exit 1
fi
if "$bridge" --gym-repo "$fake_gym" --suite unknown >/dev/null 2>&1; then
    echo "Unknown Gym suite unexpectedly succeeded." >&2
    exit 1
fi

printf '%s\n' "${expected_version}-mismatch" > "$fake_gym/GYM_CONTRACT_VERSION"
if "$bridge" --gym-repo "$fake_gym" --list >/dev/null 2>&1; then
    echo "Incompatible Gym contract unexpectedly succeeded." >&2
    exit 1
fi

printf '%s\n' "$expected_version" > "$fake_gym/GYM_CONTRACT_VERSION"
chmod -x "$fake_gym/scripts/run-vulkan-feature-matrix.sh"
if "$bridge" --gym-repo "$fake_gym" --list >/dev/null 2>&1; then
    echo "Non-executable Gym runner unexpectedly succeeded." >&2
    exit 1
fi
