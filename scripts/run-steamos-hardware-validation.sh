#!/usr/bin/env bash
# Dispatch one trusted SteamOS hardware-validation job on an ephemeral runner.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
workflow="steamos-hardware-validation.yml"
branch=""
deploy_to_decky=false
minimum_free_gib="${MAKO_HARDWARE_MIN_FREE_GIB:-30}"
ci_root="${MAKO_HARDWARE_CI_ROOT:-$repo_root/engine/build/cache}"
runner_root=""
runner_pid=""
runner_name=""
repository=""
gym_repo="${MAKO_GYM_REPO:-$repo_root/../MAKO-Gym}"

usage() {
  cat <<'EOF'
Usage: scripts/run-steamos-hardware-validation.sh [options]

Creates an official one-job GitHub Actions runner on this SteamOS/AMD host,
dispatches the hardware-validation workflow for a clean remote-synchronized
branch, waits for the result, and removes the runner and its complete checkout.

Options:
  --branch NAME       Branch to validate (default: current branch).
  --deploy-to-decky   Deploy the verified ZIP to an existing MAKO Decky test
                      installation and reload the plugin.
  -h, --help          Show this help.

Reusable compiler, Flatpak, pnpm, and tool caches share the repository's
ignored engine/build/cache tree by default. Runner credentials, source, work
files, and generated packages stay in a disposable child directory. Set
MAKO_HARDWARE_CI_ROOT to move the complete scoped cache to another disk.
EOF
}

while (($#)); do
  case "$1" in
    --branch)
      if (($# < 2)); then
        echo "--branch requires a name" >&2
        exit 2
      fi
      branch="$2"
      shift
      ;;
    --deploy-to-decky)
      deploy_to_decky=true
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

cleanup() {
  local runner_id=""

  if [[ -n "$runner_pid" ]] && kill -0 "$runner_pid" 2>/dev/null; then
    kill "$runner_pid" 2>/dev/null || true
    wait "$runner_pid" 2>/dev/null || true
  fi

  if [[ -n "$repository" && -n "$runner_name" ]]; then
    runner_id="$(
      gh api "repos/$repository/actions/runners" --paginate \
        --jq ".runners[] | select(.name == \"$runner_name\") | .id" \
        2>/dev/null | head -n 1 || true
    )"
    if [[ -n "$runner_id" ]]; then
      gh api --method DELETE \
        "repos/$repository/actions/runners/$runner_id" >/dev/null 2>&1 || true
    fi
  fi

  if [[ -n "$runner_root" ]]; then
    case "$runner_root" in
      "$ci_root"/runner.*)
        rm -rf --one-file-system -- "$runner_root"
        ;;
      *)
        echo "Refusing to remove unexpected runner path: $runner_root" >&2
        ;;
    esac
  fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [[ "$(id -u)" == 0 ]]; then
  echo "Run the hardware runner as the normal SteamOS user, not root." >&2
  exit 1
fi
if ! [[ "$minimum_free_gib" =~ ^[1-9][0-9]*$ ]]; then
  echo "MAKO_HARDWARE_MIN_FREE_GIB must be a positive integer." >&2
  exit 2
fi

for command in gh git curl sha256sum tar python3; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "Required command not found: $command" >&2
    exit 1
  fi
done

if [[ "$(uname -s)" != Linux ]] || ! grep -qi '^ID=steamos' /etc/os-release; then
  echo "This launcher is restricted to SteamOS." >&2
  exit 1
fi
if ! grep -qi '0x1002' /sys/class/drm/renderD*/device/vendor; then
  echo "No AMD Vulkan render device was detected." >&2
  exit 1
fi

case "$ci_root" in
  /*) ;;
  *)
    echo "MAKO_HARDWARE_CI_ROOT must be an absolute path: $ci_root" >&2
    exit 2
    ;;
esac
case "$ci_root" in
  /tmp|/tmp/*|/run|/run/*)
    echo "MAKO hardware CI storage must use a persistent filesystem, not $ci_root." >&2
    exit 2
    ;;
esac
mkdir -p "$ci_root"
chmod 0700 "$ci_root"
available_kib="$(df -Pk "$ci_root" | awk 'NR == 2 {print $4}')"
required_kib="$((minimum_free_gib * 1024 * 1024))"
if ((available_kib < required_kib)); then
  echo "Hardware validation needs at least ${minimum_free_gib} GiB free under $ci_root." >&2
  exit 1
fi

cd "$repo_root"
if [[ -n "$(git status --porcelain)" ]]; then
  echo "Hardware validation requires a clean worktree." >&2
  exit 1
fi
if ! git -C "$gym_repo" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  echo "Hardware validation requires an initialized MAKO-Gym checkout: $gym_repo" >&2
  exit 1
fi
if [[ -n "$(git -C "$gym_repo" status --porcelain)" ]]; then
  echo "Hardware validation requires a clean MAKO-Gym worktree: $gym_repo" >&2
  exit 1
fi
./engine/scripts/run-mako-gym.sh \
  --gym-repo "$gym_repo" --require --validate
export MAKO_GYM_REPO="$gym_repo"
branch="${branch:-$(git branch --show-current)}"
if [[ -z "$branch" ]]; then
  echo "Select a branch with --branch; detached HEADs are not dispatched." >&2
  exit 1
fi
if ! git check-ref-format --branch "$branch" >/dev/null 2>&1; then
  echo "Invalid branch name: $branch" >&2
  exit 2
fi

gh auth status >/dev/null
repository="$(gh repo view --json nameWithOwner --jq .nameWithOwner)"
git fetch --quiet origin "$branch"
local_head="$(git rev-parse --verify "refs/heads/$branch^{commit}")"
remote_head="$(git rev-parse "origin/$branch")"
if [[ "$local_head" != "$remote_head" ]]; then
  echo "Local HEAD is not synchronized with origin/$branch." >&2
  echo "Local:  $local_head" >&2
  echo "Remote: $remote_head" >&2
  exit 1
fi

active_runs="$(
  gh run list --repo "$repository" --workflow "$workflow" --limit 50 \
    --json status --jq '[.[] | select(.status != "completed")] | length'
)"
if [[ "$active_runs" != 0 ]]; then
  echo "A SteamOS hardware-validation run is already active or queued." >&2
  exit 1
fi
known_run_ids="$(
  gh run list --repo "$repository" --workflow "$workflow" \
    --branch "$branch" --event workflow_dispatch --commit "$local_head" \
    --limit 100 --json databaseId --jq '.[].databaseId'
)"

runner_release="$(gh api repos/actions/runner/releases/latest)"
runner_tag="$(
  python3 -c 'import json,sys; print(json.load(sys.stdin)["tag_name"])' \
    <<<"$runner_release"
)"
runner_version="${runner_tag#v}"
runner_asset="actions-runner-linux-x64-${runner_version}.tar.gz"
runner_checksum="$(
  python3 -c '
import json
import re
import sys

release = json.load(sys.stdin)
asset = sys.argv[1]
if asset not in {entry["name"] for entry in release.get("assets", [])}:
    raise SystemExit(f"Official runner release is missing {asset}")
match = re.search(
    rf"^- {re.escape(asset)} .*?([0-9a-f]{{64}}).*?$",
    release.get("body", ""),
    flags=re.MULTILINE,
)
if not match:
    raise SystemExit(f"Official runner release has no SHA-256 for {asset}")
print(match.group(1))
' "$runner_asset" <<<"$runner_release"
)"

runner_root="$(mktemp -d "$ci_root/runner.XXXXXX")"
runner_archive="$runner_root/$runner_asset"
curl --fail --location --retry 3 --silent --show-error \
  "https://github.com/actions/runner/releases/download/$runner_tag/$runner_asset" \
  --output "$runner_archive"
printf '%s  %s\n' "$runner_checksum" "$runner_archive" | sha256sum --check --status
tar -xzf "$runner_archive" -C "$runner_root"
rm -f "$runner_archive"

runner_name="mako-steamos-$(date --utc +%Y%m%d%H%M%S)-$$"
registration_token="$(
  gh api --method POST "repos/$repository/actions/runners/registration-token" --jq .token
)"
(
  cd "$runner_root"
  ./config.sh --unattended --ephemeral \
    --url "https://github.com/$repository" \
    --token "$registration_token" \
    --name "$runner_name" \
    --labels steamos,amd-gpu \
    --work _work
)
unset registration_token

mkdir -p "$ci_root" "$ci_root/pnpm-store" "$ci_root/tool-cache"
export MAKO_HARDWARE_CI_ROOT="$ci_root"
export RUNNER_TOOL_CACHE="$ci_root/tool-cache"
(
  cd "$runner_root"
  ./run.sh
) &
runner_pid=$!

echo "Dispatching $workflow for $repository@$branch on $runner_name..."
gh workflow run "$workflow" --repo "$repository" --ref "$branch" \
  -f "deploy_to_decky=$deploy_to_decky"

run_id=""
for _attempt in $(seq 1 30); do
  candidate_run_ids="$(
    gh run list --repo "$repository" --workflow "$workflow" \
      --branch "$branch" --event workflow_dispatch --commit "$local_head" \
      --limit 10 --json databaseId --jq '.[].databaseId'
  )"
  while IFS= read -r candidate_run_id; do
    if [[ -n "$candidate_run_id" ]] && \
        ! grep -Fxq "$candidate_run_id" <<<"$known_run_ids"; then
      run_id="$candidate_run_id"
      break
    fi
  done <<<"$candidate_run_ids"
  [[ -n "$run_id" ]] && break
  sleep 1
done
if [[ -z "$run_id" ]]; then
  echo "GitHub accepted the dispatch, but its workflow run could not be identified." >&2
  exit 1
fi

set +e
gh run watch "$run_id" --repo "$repository" --exit-status
workflow_status=$?
wait "$runner_pid"
runner_status=$?
runner_pid=""
set -e

if ((workflow_status != 0)); then
  echo "SteamOS hardware validation failed: https://github.com/$repository/actions/runs/$run_id" >&2
  exit "$workflow_status"
fi
if ((runner_status != 0)); then
  echo "The ephemeral runner exited unexpectedly after workflow success." >&2
  exit "$runner_status"
fi

echo "SteamOS hardware validation passed: https://github.com/$repository/actions/runs/$run_id"
