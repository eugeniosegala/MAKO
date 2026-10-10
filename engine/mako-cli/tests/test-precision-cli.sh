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

expect_precision 'profile samples=200 warmup=200 hdr10=0 reduced=0' benchmark --profile
expect_precision 'profile samples=11 warmup=6 hdr10=0 reduced=0' benchmark --profile --profile-samples 11 --profile-warmup 6
expect_precision 'profile samples=200 warmup=200 hdr10=1 reduced=0' benchmark --profile-hdr10 --profile
expect_precision 'profile samples=200 warmup=200 hdr10=1 reduced=1' benchmark --profile --profile-hdr10 --profile-hdr-reduced-precision
for arguments in '--profile-hdr-reduced-precision' '--profile --profile-hdr-reduced-precision' '--profile-hdr10' '--profile-samples 10' '--profile --profile-samples 0' '--profile --profile-samples 10001' '--profile --profile-warmup 5' '--profile --profile-warmup 10001'; do
    read -r -a args <<< "$arguments"
    if "$cli" benchmark "${args[@]}" >/dev/null 2>&1; then
        printf 'Invalid frame-profile options accepted: %s\n' "$arguments" >&2
        exit 1
    fi
done

for command in spatial-quality-regression spatial-profile; do
    expect_precision fp32 "$command"
    expect_precision fp32 "$command" --no-fp16
    expect_precision fp16-allowed "$command" --allow-fp16
    expect_precision fp16-allowed "$command" -a
    expect_precision fp32 "$command" --allow-fp16 --no-fp16
    expect_precision fp16-allowed "$command" --no-fp16 --allow-fp16
done

expect_precision fp16-allowed inspect-dll --dll "synthetic input" --lsfg
expect_precision fp32 inspect-dll --dll "synthetic input" --lsfg --no-fp16
expect_precision fp16-allowed inspect-dll --dll "synthetic input" --ls1 ls1
expect_precision fp32 inspect-dll --dll "synthetic input" --ls1 ls1 --no-fp16
for invalid in ls1 no-fp16; do
    args=(--dll "synthetic input")
    if [[ "$invalid" == ls1 ]]; then
        args+=(--lsfg --ls1 ls1)
    else
        args+=(--no-fp16)
    fi
    if "$cli" inspect-dll "${args[@]}" >/dev/null 2>&1; then
        printf 'Conflicting inspection options accepted\n' >&2
        exit 1
    fi
done

printf 'CLI precision contract: PASS\n'
