#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scanner="$repository_root/scripts/check-protected-inputs.sh"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/mako-protected-input-test.XXXXXX")"
cleanup() {
    rm -rf "$test_root"
}
trap cleanup EXIT

create_repository() {
    local name="$1"
    local path="$test_root/$name"
    mkdir -p "$path"
    git -C "$path" init -q
    printf '%s\n' "$path"
}

safe_repository="$(create_repository safe)"
printf '%s\n' 'Lossless.dll is a user-supplied path, never a tracked payload.' > "$safe_repository/README.md"
git -C "$safe_repository" add README.md
"$scanner" "$safe_repository" >/dev/null

expect_rejection() {
    local name="$1"
    local filename="$2"
    local bytes="$3"
    local repository
    repository="$(create_repository "$name")"
    printf '%b' "$bytes" > "$repository/$filename"
    git -C "$repository" add "$filename"
    if "$scanner" "$repository" >"$test_root/$name.log" 2>&1; then
        printf 'Expected protected-input rejection for %s\n' "$name" >&2
        exit 1
    fi
    if ! grep -q '^Protected-input violation:' "$test_root/$name.log"; then
        printf 'Scanner failed without identifying a protected-input violation for %s\n' "$name" >&2
        exit 1
    fi
}

expect_rejection dll-extension Lossless.dll 'documentation only\n'
expect_rejection disguised-pe fixture.dat '\x4d\x5a\x90\x00payload'
expect_rejection disguised-dxbc fixture.dat '\x44\x58\x42\x43payload'
expect_rejection disguised-spirv fixture.dat '\x03\x02\x23\x07payload'
expect_rejection disguised-zip fixture.dat '\x50\x4b\x03\x04payload'
expect_rejection lfs-pointer fixture.dat 'version https://git-lfs.github.com/spec/v1\noid sha256:0000000000000000000000000000000000000000000000000000000000000000\nsize 1\n'
expect_rejection model-extension model.onnx 'documentation only\n'

printf '%s\n' 'Protected-input scanner tests passed.'
