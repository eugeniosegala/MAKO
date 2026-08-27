#!/usr/bin/env bash
set -euo pipefail

bridge="${1:?usage: test-run-mako-gym.sh /path/to/run-mako-gym.sh}"
temporary_root="$(mktemp -d)"
trap 'rm -rf -- "$temporary_root"' EXIT

absent="$temporary_root/absent"
optional_output="$($bridge --gym-repo "$absent" --list)"
if [[ "$optional_output" != *'MAKO-Gym: SKIP checkout absent:'* ]]; then
    echo "Optional missing-Gym invocation did not report a skip." >&2
    exit 1
fi
if "$bridge" --gym-repo "$absent" --require --list >/dev/null 2>&1; then
    echo "Required missing-Gym invocation unexpectedly succeeded." >&2
    exit 1
fi

fake_gym="$temporary_root/MAKO-Gym"
mkdir -p "$fake_gym/scripts"
printf '5\n' > "$fake_gym/GYM_CONTRACT_VERSION"
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
printf '%s\n' '#!/usr/bin/env bash' 'printf "sync-validation:%s\\n" "$@"' \
    > "$fake_gym/scripts/run-synchronization-validation.sh"
chmod +x "$fake_gym/scripts/run-synchronization-validation.sh"
printf '%s\n' '#!/usr/bin/env bash' 'printf "recovery:%s\\n" "$@"' \
    > "$fake_gym/scripts/run-runtime-recovery-matrix.sh"
chmod +x "$fake_gym/scripts/run-runtime-recovery-matrix.sh"

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
if "$bridge" --gym-repo "$fake_gym" --suite unknown >/dev/null 2>&1; then
    echo "Unknown Gym suite unexpectedly succeeded." >&2
    exit 1
fi

printf '1\n' > "$fake_gym/GYM_CONTRACT_VERSION"
if "$bridge" --gym-repo "$fake_gym" --list >/dev/null 2>&1; then
    echo "Incompatible Gym contract unexpectedly succeeded." >&2
    exit 1
fi

printf '5\n' > "$fake_gym/GYM_CONTRACT_VERSION"
chmod -x "$fake_gym/scripts/run-vulkan-feature-matrix.sh"
if "$bridge" --gym-repo "$fake_gym" --list >/dev/null 2>&1; then
    echo "Non-executable Gym runner unexpectedly succeeded." >&2
    exit 1
fi
