#!/usr/bin/env bash
# Production CLI parsing with tool-entry probes; no Vulkan device or DLL.
set -euo pipefail

cli=${1:?precision probe path is required}

expect_precision() {
    local expected="$1"
    shift
    local output
    output="$("$cli" "$@" 2>&1)" || {
        printf 'CLI precision command failed: %s\n' "$output" >&2
        exit 1
    }
    [[ "$output" == "$expected" ]] || {
        printf 'CLI precision expected %s, got %s\n' "$expected" "$output" >&2
        exit 1
    }
}

for command in benchmark debug quality-regression combined-quality-regression; do
    arguments=()
    [[ "$command" != debug ]] || arguments=("frames with spaces")
    expect_precision fp16-allowed "$command" "${arguments[@]}"
    expect_precision fp32 "$command" --no-fp16 "${arguments[@]}"
    expect_precision fp16-allowed "$command" --allow-fp16 "${arguments[@]}"
    expect_precision fp16-allowed "$command" -a "${arguments[@]}"
    expect_precision fp32 "$command" --allow-fp16 --no-fp16 "${arguments[@]}"
    expect_precision fp16-allowed "$command" --no-fp16 --allow-fp16 "${arguments[@]}"
done

printf 'CLI precision contract: PASS\n'
