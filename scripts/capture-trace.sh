#!/usr/bin/env bash

set -euo pipefail

usage() {
  cat <<'EOF'
Capture one completed MAKO Renderer game session.

Usage:
  capture-trace.sh --game NAME --version LABEL --session-start ISO_TIME [options]

Required:
  --game NAME              Human-readable game name
  --version LABEL          Released or explicit development build label
  --session-start TIME     Local ISO timestamp, for example 2026-08-20T12:30:52+01:00

Options:
  --game-id ID             Store/application identifier
  --label LABEL            Short scenario label; defaults to playthrough
  --run-index NUMBER       Repetition number for this scenario; defaults to 1
  --session-end TIME       Local ISO timestamp; defaults to the capture time
  --diagnostics PATH       Present diagnostics source
  --decky-log PATH         Decky log to clip to the session window
  --steam-log PATH         Steam console log to clip to the session window
  --config PATH            Renderer configuration source
  --mako-repo PATH         MAKO source checkout used for branch/commit identity
  --trace-repo PATH        MAKO-Traces checkout that receives the capture
  --notes PATH             Prepared Markdown observations
  -h, --help               Show this help

The resulting directory is printed on success. Existing captures are never overwritten.
EOF
}

die() {
  printf 'capture-trace: %s\n' "$*" >&2
  exit 1
}

require_value() {
  [[ $# -ge 2 && -n "$2" ]] || die "$1 requires a value"
}

slugify() {
  printf '%s' "$1" | tr '[:upper:]' '[:lower:]' | sed -E 's/[^a-z0-9._]+/-/g; s/^-+//; s/-+$//; s/-+/-/g'
}

safe_component() {
  printf '%s' "$1" | sed -E 's/[^A-Za-z0-9._-]+/-/g; s/^-+//; s/-+$//; s/-+/-/g'
}

local_log_time() {
  local value=$1
  [[ "$value" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2} ]] || die "invalid ISO timestamp: $value"
  printf '%s %s' "${value:0:10}" "${value:11:8}"
}

sanitize_file() {
  local source=$1
  local destination=$2
  sed \
    -e "s|$HOME|\$HOME|g" \
    -E \
    -e 's/((access[_-]?token|refresh[_-]?token|password|passwd|authorization|cookie)[=:][[:space:]]*)[^[:space:]]+/\1[REDACTED]/Ig' \
    "$source" >"$destination"
}

slice_bracketed_log() {
  local source=$1
  local destination=$2
  local start=$3
  local end=$4
  local temporary
  temporary=$(mktemp)
  awk -v start="$start" -v end="$end" '
    match($0, /^\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}/) {
      stamp = substr($0, 2, 19)
      selected = stamp >= start && stamp <= end
    }
    selected { print }
  ' "$source" >"$temporary"
  sanitize_file "$temporary" "$destination"
  rm -f -- "$temporary"
}

game=''
game_id=''
version=''
label='playthrough'
run_index=1
session_start=''
session_end=''
diagnostics="$HOME/.config/mako-render/present-diagnostics.log"
config="$HOME/.config/mako-render/conf.toml"
decky_log=''
steam_log=''
notes=''

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
mako_repo=$(cd -- "$script_dir/.." && pwd)
trace_repo=''

while [[ $# -gt 0 ]]; do
  case "$1" in
    --game)
      require_value "$@"
      game=$2
      shift 2
      ;;
    --game-id)
      require_value "$@"
      game_id=$2
      shift 2
      ;;
    --version)
      require_value "$@"
      version=$2
      shift 2
      ;;
    --label)
      require_value "$@"
      label=$2
      shift 2
      ;;
    --run-index)
      require_value "$@"
      run_index=$2
      shift 2
      ;;
    --session-start)
      require_value "$@"
      session_start=$2
      shift 2
      ;;
    --session-end)
      require_value "$@"
      session_end=$2
      shift 2
      ;;
    --diagnostics)
      require_value "$@"
      diagnostics=$2
      shift 2
      ;;
    --decky-log)
      require_value "$@"
      decky_log=$2
      shift 2
      ;;
    --steam-log)
      require_value "$@"
      steam_log=$2
      shift 2
      ;;
    --config)
      require_value "$@"
      config=$2
      shift 2
      ;;
    --mako-repo)
      require_value "$@"
      mako_repo=$2
      shift 2
      ;;
    --trace-repo)
      require_value "$@"
      trace_repo=$2
      shift 2
      ;;
    --notes)
      require_value "$@"
      notes=$2
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      die "unknown argument: $1"
      ;;
  esac
done

[[ -n "$game" ]] || die '--game is required'
[[ -n "$version" ]] || die '--version is required'
[[ -n "$session_start" ]] || die '--session-start is required'
[[ "$run_index" =~ ^[1-9][0-9]{0,2}$ ]] || die '--run-index must be an integer from 1 to 999'
[[ -r "$diagnostics" ]] || die "diagnostics file is not readable: $diagnostics"
[[ -s "$diagnostics" ]] || die "diagnostics file is empty: $diagnostics"
[[ -r "$config" ]] || die "configuration file is not readable: $config"
[[ -z "$decky_log" || -r "$decky_log" ]] || die "Decky log is not readable: $decky_log"
[[ -z "$steam_log" || -r "$steam_log" ]] || die "Steam log is not readable: $steam_log"
[[ -z "$notes" || -r "$notes" ]] || die "notes file is not readable: $notes"
command -v jq >/dev/null || die 'jq is required'
command -v sha256sum >/dev/null || die 'sha256sum is required'

if [[ -z "$trace_repo" ]]; then
  trace_repo=$(dirname -- "$mako_repo")/MAKO-Traces
fi
[[ -d "$trace_repo" ]] || die "trace repository does not exist: $trace_repo"
git -C "$trace_repo" rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "trace repository is not a Git checkout: $trace_repo"
trace_repo=$(cd -- "$trace_repo" && pwd)

if [[ -z "$session_end" ]]; then
  session_end=$(date --iso-8601=seconds)
fi

start_log_time=$(local_log_time "$session_start")
end_log_time=$(local_log_time "$session_end")
[[ "$end_log_time" > "$start_log_time" || "$end_log_time" == "$start_log_time" ]] || die 'session end precedes session start'

game_slug=$(slugify "$game")
version_component=$(safe_component "$version")
label_slug=$(slugify "$label")
[[ -n "$game_slug" ]] || die 'game name does not produce a safe path component'
[[ -n "$version_component" ]] || die 'version does not produce a safe path component'
[[ "$version_component" == "$version" ]] || die 'version must contain only letters, numbers, dots, underscores, and dashes'
[[ -n "$label_slug" ]] || die 'label does not produce a safe path component'

utc_start=$(date --date="$session_start" --utc '+%Y%m%dT%H%M%SZ') || die "invalid ISO timestamp: $session_start"
run_index_number=$((10#$run_index))
printf -v run_component 'r%02d' "$run_index_number"
session_component="$utc_start-$label_slug-$run_component"
destination="$trace_repo/traces/$version_component/$game_slug/$session_component"
[[ ! -e "$destination" ]] || die "capture already exists: $destination"

mkdir -p -- "$(dirname -- "$destination")"
working=$(mktemp -d "$trace_repo/.capture.XXXXXX")
trap 'rm -rf -- "$working"' EXIT

sanitize_file "$diagnostics" "$working/present-diagnostics.log"
sanitize_file "$config" "$working/config.toml"

if [[ -n "$decky_log" ]]; then
  slice_bracketed_log "$decky_log" "$working/decky-session.log" "$start_log_time" "$end_log_time"
fi

if [[ -n "$steam_log" ]]; then
  slice_bracketed_log "$steam_log" "$working/steam-session.log" "$start_log_time" "$end_log_time"
fi

grep -E \
  'operation=(runtime-state-applied|adaptive-ramp|adaptive-load-shed|adaptive-cadence-refresh|adaptive-recovery-resume-scheduled|generated-delivery-miss|skip-generated-frames)|pipeline[- ]busy|device[- ]lost|backend[^[:space:]]*[=:][^[:space:]]*(fail|error)|timed out' \
  "$working/present-diagnostics.log" >"$working/events.log" || true

if [[ -n "$notes" ]]; then
  sanitize_file "$notes" "$working/notes.md"
else
  {
    printf '# %s — %s\n\n' "$game" "$label"
    printf '## Test conditions\n\n'
    printf -- '- Version: `%s`\n' "$version"
    printf -- '- Run: `%s` (index `%d`)\n' "$session_component" "$run_index_number"
    printf -- '- Session: `%s` to `%s`\n' "$session_start" "$session_end"
    if [[ -n "$game_id" ]]; then
      printf -- '- Game ID: `%s`\n' "$game_id"
    fi
    printf '\n## Tester observations\n\nAdd subjective image quality, stability, scene, route, and mode-change observations here.\n\n'
    printf '## Evidence summary\n\nAdd measured ranges, recovery behavior, errors, and comparison conclusions here.\n'
  } >"$working/notes.md"
fi

source_branch='unknown'
source_commit='unknown'
source_dirty=false
source_path=$mako_repo
if [[ "$source_path" == "$HOME"* ]]; then
  source_path="\$HOME${source_path#"$HOME"}"
fi
if git -C "$mako_repo" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  source_branch=$(git -C "$mako_repo" branch --show-current)
  source_branch=${source_branch:-detached}
  source_commit=$(git -C "$mako_repo" rev-parse HEAD)
  if [[ -n "$(git -C "$mako_repo" status --porcelain)" ]]; then
    source_dirty=true
  fi
fi

renderer_build=$(sed -n -E 's/.*render layer active;.*build=([^[:space:]]+).*/\1/p' "$working/present-diagnostics.log" | head -n 1)
gpu=$(sed -n -E 's/.*backend GPU selection: following game device=(.*) \(0x[0-9a-fA-F]+:0x[0-9a-fA-F]+\).*/\1/p' "$working/present-diagnostics.log" | head -n 1)
refresh_hz=$(sed -n -E 's/.*refresh_hz=([0-9.]+).*/\1/p' "$working/present-diagnostics.log" | head -n 1)
renderer_build=${renderer_build:-unknown}
gpu=${gpu:-unknown}
refresh_hz=${refresh_hz:-unknown}
os_name=$(sed -n -E 's/^PRETTY_NAME="?(.*)"?/\1/p' /etc/os-release | head -n 1)
os_name=${os_name%\"}

artifact_list="$working/.artifacts"
find "$working" -maxdepth 1 -type f ! -name '.artifacts' -printf '%f\n' | sort >"$artifact_list"
artifacts_json=$(jq -Rn '[inputs]' <"$artifact_list")

jq -n \
  --arg game_name "$game" \
  --arg game_slug "$game_slug" \
  --arg game_id "$game_id" \
  --arg version_label "$version" \
  --arg label "$label" \
  --arg session_id "$session_component" \
  --arg started_at "$session_start" \
  --arg ended_at "$session_end" \
  --arg captured_at "$(date --iso-8601=seconds)" \
  --arg source_path "$source_path" \
  --arg source_branch "$source_branch" \
  --arg source_commit "$source_commit" \
  --arg renderer_build "$renderer_build" \
  --arg arch "$(uname -m)" \
  --arg os "$os_name" \
  --arg gpu "$gpu" \
  --arg refresh_hz "$refresh_hz" \
  --argjson run_index "$run_index_number" \
  --argjson source_dirty "$source_dirty" \
  --argjson artifacts "$artifacts_json" \
  '{
    schema_version: 2,
    game: {name: $game_name, slug: $game_slug, id: $game_id},
    version_label: $version_label,
    renderer_reported_build: $renderer_build,
    session: {id: $session_id, label: $label, run_index: $run_index, started_at: $started_at, ended_at: $ended_at, captured_at: $captured_at},
    source: {path: $source_path, branch: $source_branch, commit: $source_commit, dirty: $source_dirty},
    host: {architecture: $arch, os: $os, gpu: $gpu, refresh_hz: $refresh_hz},
    artifacts: $artifacts
  }' >"$working/metadata.json"

rm -f -- "$artifact_list"

if grep -Eirn \
  '(access[_-]?token|refresh[_-]?token|password|passwd|authorization|cookie)[=:][[:space:]]*[^[:space:]]+' \
  "$working" >/dev/null; then
  die 'a possible credential remains in the capture; inspect the sources and sanitize them explicitly'
fi

(
  cd -- "$working"
  find . -maxdepth 1 -type f ! -name 'checksums.sha256' -printf '%P\0' | sort -z | xargs -0 sha256sum >checksums.sha256
)

mv -- "$working" "$destination"
trap - EXIT
printf '%s\n' "$destination"
