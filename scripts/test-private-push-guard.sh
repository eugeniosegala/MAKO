#!/usr/bin/env bash
set -euo pipefail

repository_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/mako-private-push-guard-test.XXXXXX")"
cleanup() {
    rm -rf "$test_root"
}
trap cleanup EXIT

git init -q --bare "$test_root/public.git"
git init -q --bare "$test_root/private.git"
checkout="$test_root/checkout"
git init -q -b main "$checkout"
git -C "$checkout" config user.email test@example.invalid
git -C "$checkout" config user.name Test
# Only the guard under test; the repository's pre-commit belongs to MAKO's own checkout.
git -C "$checkout" config core.hooksPath "$test_root/hooks"
mkdir "$test_root/hooks"
cp "$repository_root/.githooks/pre-push" "$test_root/hooks/pre-push"
git -C "$checkout" remote add origin "$test_root/public.git"
git -C "$checkout" commit -q --allow-empty -m public
git -C "$checkout" switch -q -c feature
git -C "$checkout" commit -q --allow-empty -m private

expect_push() {
    local outcome="$1"
    shift
    if git -C "$checkout" push -q "$@" >"$test_root/push.log" 2>&1; then
        [[ "$outcome" == allowed ]] && return
    else
        [[ "$outcome" == blocked ]] && grep -q 'MAKO pre-push:' "$test_root/push.log" && return
    fi
    printf 'Expected git push %s to be %s:\n' "$*" "$outcome" >&2
    cat "$test_root/push.log" >&2
    exit 1
}

# Without the private remote, maintainers push branches as usual.
expect_push allowed origin feature
git -C "$checkout" push -q origin --delete feature

git -C "$checkout" remote add mako-private "$test_root/private.git"
expect_push blocked origin feature
expect_push blocked origin feature:main
expect_push blocked origin HEAD:refs/heads/other
git -C "$checkout" tag private-tag feature
expect_push blocked origin private-tag
expect_push allowed mako-private HEAD:main
expect_push allowed mako-private feature
expect_push allowed origin main
git -C "$checkout" tag -a -m release public-tag main
expect_push allowed origin public-tag
git -C "$checkout" push -q --no-verify origin feature
expect_push allowed origin --delete feature

printf '%s\n' 'Private push guard tests passed.'
