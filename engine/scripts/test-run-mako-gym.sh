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
printf '1\n' > "$fake_gym/GYM_CONTRACT_VERSION"
printf '%s\n' '#!/usr/bin/env bash' 'printf "%s\\n" "$@"' \
    > "$fake_gym/scripts/run-vulkan-feature-matrix.sh"
chmod +x "$fake_gym/scripts/run-vulkan-feature-matrix.sh"
printf '%s\n' '#!/usr/bin/env bash' 'printf "quality:%s\\n" "$@"' \
    > "$fake_gym/scripts/run-amd-quality-regression.sh"
chmod +x "$fake_gym/scripts/run-amd-quality-regression.sh"

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
if "$bridge" --gym-repo "$fake_gym" --suite unknown >/dev/null 2>&1; then
    echo "Unknown Gym suite unexpectedly succeeded." >&2
    exit 1
fi

printf '2\n' > "$fake_gym/GYM_CONTRACT_VERSION"
if "$bridge" --gym-repo "$fake_gym" --list >/dev/null 2>&1; then
    echo "Incompatible Gym contract unexpectedly succeeded." >&2
    exit 1
fi

printf '1\n' > "$fake_gym/GYM_CONTRACT_VERSION"
chmod -x "$fake_gym/scripts/run-vulkan-feature-matrix.sh"
if "$bridge" --gym-repo "$fake_gym" --list >/dev/null 2>&1; then
    echo "Non-executable Gym runner unexpectedly succeeded." >&2
    exit 1
fi
