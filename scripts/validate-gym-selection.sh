#!/usr/bin/env bash
set -euo pipefail

selection="${1:-}"
reason="${2:-}"
evidence_path="${3:-}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

suite_names=()
while IFS= read -r suite_name; do
  suite_names+=("$suite_name")
done < <("$repo_root/engine/scripts/run-mako-gym.sh" --list-suites)
if ((${#suite_names[@]} == 0)); then
  echo "MAKO Gym bridge exposed no suites." >&2
  exit 1
fi

fail() {
  echo "Invalid MAKO Gym hardware selection: $1" >&2
  exit 2
}

if [[ -z "$selection" ]]; then
  fail "choose named suites, 'none', or 'all'."
fi
if [[ -z "${reason//[[:space:]]/}" ]]; then
  fail "provide a non-empty rationale."
fi
if ((${#reason} > 240)); then
  fail "the rationale must be at most 240 characters."
fi
if [[ "$reason" == *$'\n'* || "$reason" == *$'\r'* ]]; then
  fail "the rationale must stay on one line."
fi

selected_names=()
omitted_names=()
case "$selection" in
  all)
    selected_names=("${suite_names[@]}")
    ;;
  none)
    omitted_names=("${suite_names[@]}")
    ;;
  *)
    IFS=',' read -r -a requested_names <<<"$selection"
    if ((${#requested_names[@]} == 0)); then
      fail "choose at least one suite."
    fi
    for requested_name in "${requested_names[@]}"; do
      if [[ -z "$requested_name" ]]; then
        fail "suite names cannot be empty."
      fi
      valid=false
      for suite_name in "${suite_names[@]}"; do
        if [[ "$requested_name" == "$suite_name" ]]; then
          valid=true
          break
        fi
      done
      if [[ "$valid" != true ]]; then
        fail "unknown suite '$requested_name'."
      fi
      for selected_name in "${selected_names[@]}"; do
        if [[ "$selected_name" == "$requested_name" ]]; then
          fail "suite '$requested_name' was selected more than once."
        fi
      done
      selected_names+=("$requested_name")
    done
    for suite_name in "${suite_names[@]}"; do
      selected=false
      for selected_name in "${selected_names[@]}"; do
        if [[ "$selected_name" == "$suite_name" ]]; then
          selected=true
          break
        fi
      done
      if [[ "$selected" != true ]]; then
        omitted_names+=("$suite_name")
      fi
    done
    ;;
esac

join_names() {
  local joined=""
  local name
  for name in "$@"; do
    if [[ -n "$joined" ]]; then
      joined+=","
    fi
    joined+="$name"
  done
  printf '%s' "$joined"
}

selected_record="$(join_names "${selected_names[@]}")"
omitted_record="$(join_names "${omitted_names[@]}")"
selected_record="${selected_record:-none}"
omitted_record="${omitted_record:-none}"

echo "MAKO Gym hardware selection: $selected_record"
echo "MAKO Gym hardware omission: $omitted_record"
echo "MAKO Gym selection rationale: $reason"

if [[ -n "$evidence_path" ]]; then
  mkdir -p "$(dirname "$evidence_path")"
  {
    printf 'selection=%s\n' "$selection"
    printf 'selected=%s\n' "$selected_record"
    printf 'omitted=%s\n' "$omitted_record"
    printf 'reason=%s\n' "$reason"
  } >"$evidence_path"
fi
