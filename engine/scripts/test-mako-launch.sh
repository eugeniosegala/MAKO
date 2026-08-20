#!/usr/bin/env bash
# Deterministic contract tests for the standalone MAKO Renderer launcher.
set -euo pipefail

launcher="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/mako-launch}"

fail() {
    echo "mako-launch test failed: $*" >&2
    exit 1
}

if [[ ! -f "$launcher" ]]; then
    fail "launcher does not exist: $launcher"
fi

launcher_dir="$(cd -- "$(dirname -- "$launcher")" && pwd -P)"
install_prefix="$(cd -- "$launcher_dir/.." && pwd -P)"
test_data_home="/tmp/mako-launch-contract-data"
expected_layer_path="$install_prefix/share/mako-render/vulkan/implicit_layer.d"
if [[ ! -d "$expected_layer_path" && -d "$test_data_home/mako-render/vulkan/implicit_layer.d" ]]; then
    expected_layer_path="$test_data_home/mako-render/vulkan/implicit_layer.d"
elif [[ ! -d "$expected_layer_path" ]]; then
    expected_layer_path="$install_prefix/share/vulkan/implicit_layer.d"
fi

default_output="$({
    env -u ENABLE_MAKO -u DISABLE_MAKO \
        -u ENABLE_GAMESCOPE_WSI -u DISABLE_GAMESCOPE_WSI \
        -u MAKO_DISABLE_HDR_EXPOSURE -u DXVK_HDR \
        -u DISABLE_LSFG -u DISABLE_LSFGVK \
        -u MAKO_ALLOW_COMPETING_LAYERS \
        XDG_DATA_HOME="$test_data_home" \
        VK_IMPLICIT_LAYER_PATH="/caller/override" \
        VK_ADD_IMPLICIT_LAYER_PATH="/caller/additional" \
        MAKO_PROFILE="profile with spaces" \
        "$launcher" bash -c '
            printf "%s\n" \
                "${ENABLE_MAKO:-unset}" \
                "${DISABLE_LSFG:-unset}" \
                "${DISABLE_LSFGVK:-unset}" \
                "${DISABLE_GAMESCOPE_WSI:-unset}" \
                "${ENABLE_GAMESCOPE_WSI:-unset}" \
                "${MAKO_DISABLE_HDR_EXPOSURE:-unset}" \
                "${DXVK_HDR:-unset}" \
                "${VK_IMPLICIT_LAYER_PATH:-unset}" \
                "${VK_ADD_IMPLICIT_LAYER_PATH:-unset}" \
                "${MAKO_PROFILE:-unset}" \
                "$1" "$2"
        ' _ "argument with spaces" '$literal'
} 2>&1)" || fail "default launch failed: $default_output"

expected_default="$(printf '1\n1\n1\n1\nunset\n1\nunset\n%s\nunset\nprofile with spaces\nargument with spaces\n$literal' "$expected_layer_path")"
if [[ "$default_output" != "$expected_default" ]]; then
    fail "default environment or argument forwarding changed:\n$default_output"
fi

allow_output="$({
    DISABLE_LSFG=1 DISABLE_LSFGVK=1 ENABLE_GAMESCOPE_WSI=1 \
        MAKO_DISABLE_HDR_EXPOSURE=0 DXVK_HDR=1 \
        XDG_DATA_HOME="$test_data_home" \
        VK_IMPLICIT_LAYER_PATH="/caller/override" \
        VK_ADD_IMPLICIT_LAYER_PATH="/caller/additional" \
        MAKO_ALLOW_COMPETING_LAYERS=1 \
        "$launcher" bash -c '
            printf "%s\n" \
                "${ENABLE_MAKO:-unset}" \
                "${DISABLE_LSFG:-unset}" \
                "${DISABLE_LSFGVK:-unset}" \
                "${DISABLE_GAMESCOPE_WSI:-unset}" \
                "${ENABLE_GAMESCOPE_WSI:-unset}" \
                "${MAKO_DISABLE_HDR_EXPOSURE:-unset}" \
                "${DXVK_HDR:-unset}" \
                "${VK_IMPLICIT_LAYER_PATH:-unset}" \
                "${VK_ADD_IMPLICIT_LAYER_PATH:-unset}" \
                "${MAKO_ALLOW_COMPETING_LAYERS:-unset}"
        '
} 2>&1)" || fail "advanced opt-out launch failed: $allow_output"

expected_allow="$(printf '1\nunset\nunset\n1\nunset\n1\nunset\n%s\nunset\nunset' "$expected_layer_path")"
if [[ "$allow_output" != "$expected_allow" ]]; then
    fail "advanced opt-out did not remove only the conflict guards:\n$allow_output"
fi

disable_output="$({
    DISABLE_MAKO=1 "$launcher" bash -c \
        'printf "%s\n" "${ENABLE_MAKO:-unset}" "${DISABLE_MAKO:-unset}"'
} 2>&1)" || fail "MAKO disable-gate forwarding failed: $disable_output"
if [[ "$disable_output" != $'1\n1' ]]; then
    fail "DISABLE_MAKO was not preserved:\n$disable_output"
fi

set +e
no_command_output="$({ "$launcher"; } 2>&1)"
no_command_status=$?
invalid_flag_output="$({ MAKO_ALLOW_COMPETING_LAYERS=yes "$launcher" true; } 2>&1)"
invalid_flag_status=$?
"$launcher" bash -c 'exit 23'
forwarded_status=$?
set -e

if [[ $no_command_status -ne 2 || "$no_command_output" != *"requires a command"* ]]; then
    fail "missing-command validation changed"
fi
if [[ $invalid_flag_status -ne 2 || "$invalid_flag_output" != *"must be 0 or 1"* ]]; then
    fail "invalid escape-hatch validation changed"
fi
if [[ $forwarded_status -ne 23 ]]; then
    fail "child exit status was not forwarded: $forwarded_status"
fi

help_output="$("$launcher" --help)" || fail "--help failed"
if [[ "$help_output" != *"Usage: mako-launch"* ]]; then
    fail "--help output is incomplete"
fi

echo "mako-launch contract: PASS"
