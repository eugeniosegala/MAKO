#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
validator="$repo_root/scripts/validate-gym-selection.sh"
bridge="$repo_root/engine/scripts/run-mako-gym.sh"
workflow="$repo_root/.github/workflows/steamos-hardware-validation.yml"
test_root="$(mktemp -d "${TMPDIR:-/tmp}/mako-gym-selection-test.XXXXXX")"
trap 'rm -rf -- "$test_root"' EXIT

expect_failure() {
  if "$validator" "$@" >/dev/null 2>&1; then
    echo "Invalid Gym selection unexpectedly passed: $*" >&2
    exit 1
  fi
}

"$validator" recovery "Adaptive recovery changed" "$test_root/selected.txt" >/dev/null
grep -Fxq 'selection=recovery' "$test_root/selected.txt"
grep -Fxq 'selected=recovery' "$test_root/selected.txt"
grep -Fxq 'omitted=vulkan,quality,repeatability,performance,spatial-performance,runtime-overhead,sync-validation,external-recovery,gamescope-e2e,direct-desktop-e2e,sustained-health,proton-e2e,proton-compatibility' "$test_root/selected.txt"
grep -Fxq 'reason=Adaptive recovery changed' "$test_root/selected.txt"

"$validator" quality,gamescope-e2e "Pixels and WSI changed" >/dev/null
"$validator" none "Documentation-only release" >/dev/null
"$validator" all "Explicit maintainer-requested broad audit" >/dev/null

expect_failure "" "Missing selection"
expect_failure recovery ""
expect_failure unknown "Unknown suite"
expect_failure recovery,recovery "Duplicate suite"
expect_failure none,recovery "Mixed none"
expect_failure all,recovery "Mixed all"
expect_failure recovery $'Two\nlines'

grep -Fq './scripts/validate-gym-selection.sh' "$workflow"
grep -Fq 'engine/out/mako-gym-selection.txt' "$workflow"
while IFS= read -r suite_name; do
  handled_count="$({
    grep -F -- "--suite $suite_name" "$workflow" || true
    grep -F -- "run_packaged_suite $suite_name " "$workflow" || true
  } | wc -l | tr -d '[:space:]')"
  if [[ "$handled_count" != 1 ]]; then
    echo "Hardware workflow must handle MAKO Gym suite '$suite_name' exactly once." >&2
    exit 1
  fi
  if ! grep -Fq "\`$suite_name\`" "$repo_root/TESTING.md"; then
    echo "TESTING.md must route MAKO Gym suite '$suite_name'." >&2
    exit 1
  fi
done < <("$bridge" --list-suites)

echo "MAKO Gym selection contract tests passed."
