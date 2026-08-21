#!/usr/bin/env bash

set -euo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "$script_dir/.." && pwd)
capture="$script_dir/capture-trace.sh"
temporary=$(mktemp -d)
trap 'rm -rf -- "$temporary"' EXIT

trace_repo="$temporary/MAKO-Traces"
mkdir -p "$trace_repo/traces" "$trace_repo/scripts"
git -C "$trace_repo" init -q

set_validator() {
  local behavior=$1
  case "$behavior" in
    pass)
      cat >"$trace_repo/scripts/validate.sh" <<'EOF'
#!/usr/bin/env bash
printf 'validator stdout must not pollute producer stdout\n'
exit 0
EOF
      ;;
    slow-pass)
      cat >"$trace_repo/scripts/validate.sh" <<'EOF'
#!/usr/bin/env bash
sleep 0.2
printf 'validator stdout must not pollute producer stdout\n'
exit 0
EOF
      ;;
    slow-fail)
      cat >"$trace_repo/scripts/validate.sh" <<'EOF'
#!/usr/bin/env bash
sleep 0.2
printf 'synthetic archive rejection\n'
exit 1
EOF
      ;;
    fail)
      cat >"$trace_repo/scripts/validate.sh" <<'EOF'
#!/usr/bin/env bash
printf 'synthetic archive rejection\n'
exit 1
EOF
      ;;
    interrupt)
      cat >"$trace_repo/scripts/validate.sh" <<'EOF'
#!/usr/bin/env bash
validator_repo=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
printf '%s\n' "$$" >"$validator_repo/validator-interrupt.started"
sleep 30
exit 0
EOF
      ;;
    *)
      printf 'Unknown validator behavior: %s\n' "$behavior" >&2
      exit 1
      ;;
  esac
  chmod +x "$trace_repo/scripts/validate.sh"
}

set_validator pass

diagnostics="$temporary/present-diagnostics.log"
config="$temporary/conf.toml"
cat >"$diagnostics" <<'EOF'
MAKO Renderer: render layer active; build=2.1.0
MAKO Renderer: backend GPU selection: following game device=Example GPU (0x1002:0x0001)
MAKO Renderer: presentation refresh_hz=90
{"password": "secret-value"}
{"cookie": "secret-cookie-value"}
Authorization: Bearer secret-header-value
AWS_SECRET_ACCESS_KEY="aws-secret-value"
AWS_ACCESS_KEY_ID=ASIA1234567890ABCDEF
AWS_SESSION_TOKEN=session-secret-value
STEAM_WEB_API_KEY=steam-secret-value
GITHUB_TOKEN=opaque-token-value
archive_authorization=maintainer-private-diagnostic-use
EOF
printf 'version = 2\n' >"$config"

invoke_capture() {
  "$capture" \
    --game "Example Game" \
    --version "2.1.0-dev-test" \
    --session-start "2026-08-21T13:00:00+01:00" \
    --session-end "2026-08-21T13:01:00+01:00" \
    --diagnostics "$diagnostics" \
    --config "$config" \
    --mako-repo "$repo_root" \
    --trace-repo "$trace_repo" \
    "$@"
}

expect_failure() {
  local expected=$1
  shift
  local output
  if output=$(invoke_capture "$@" 2>&1); then
    printf 'Expected capture failure containing %q\n' "$expected" >&2
    exit 1
  fi
  [[ "$output" == *"$expected"* ]] || {
    printf 'Capture failure did not contain %q:\n%s\n' "$expected" "$output" >&2
    exit 1
  }
}

expect_failure 'version must begin' --version '..'
expect_failure 'version must begin' --version '_development'
expect_failure 'game name does not produce' --game '..'
expect_failure 'label does not produce' --label '..'
expect_failure 'session end precedes' \
  --session-start '2026-08-21T13:00:00+00:00' \
  --session-end '2026-08-21T14:30:00+02:00'
expect_failure 'session end precedes' \
  --session-start '2026-08-21T13:00:00.900000+01:00' \
  --session-end '2026-08-21T13:00:00.100000+01:00'
expect_failure 'invalid ISO timestamp' \
  --session-start '2026-08-21 13:00:00+01:00'
expect_failure 'session end is later than the capture time' \
  --session-end '2999-08-21T13:01:00+01:00'

symlink_root_repo="$temporary/symlink-root-repo"
symlink_root_target="$temporary/symlink-root-target"
mkdir -p "$symlink_root_repo" "$symlink_root_target"
git -C "$symlink_root_repo" init -q
ln -s "$symlink_root_target" "$symlink_root_repo/traces"
original_trace_repo=$trace_repo
trace_repo=$symlink_root_repo
expect_failure 'trace root cannot be a symbolic link'
trace_repo=$original_trace_repo

outside="$temporary/outside"
mkdir -p "$outside"
ln -s "$outside" "$trace_repo/traces/2.1.0-dev-symlink"
expect_failure 'escapes the trace root' --version '2.1.0-dev-symlink'

safe_source_repo="$temporary/source-repo"
ln -s "$repo_root" "$safe_source_repo"
home_boundary_diagnostics="$temporary/home-boundary-diagnostics.log"
cat >"$home_boundary_diagnostics" <<'EOF'
home_exact=/Users/alice
home_descendant=/Users/alice/Documents/session.log
benign_unix_lookalike=/rooted/project
benign_windows_lookalike=C:\UsersBackup\Alice
EOF
home_boundary_destination=$(
  export HOME=/Users/alice
  invoke_capture \
    --game 'Home Boundary' \
    --version '2.1.0-dev-home-boundary' \
    --diagnostics "$home_boundary_diagnostics" \
    --mako-repo "$safe_source_repo"
)
grep -Fx 'home_exact=$HOME' "$home_boundary_destination/present-diagnostics.log" >/dev/null
grep -Fx 'home_descendant=$HOME/Documents/session.log' "$home_boundary_destination/present-diagnostics.log" >/dev/null
grep -Fx 'benign_unix_lookalike=/rooted/project' "$home_boundary_destination/present-diagnostics.log" >/dev/null
grep -Fx 'benign_windows_lookalike=C:\UsersBackup\Alice' "$home_boundary_destination/present-diagnostics.log" >/dev/null

home_collision_diagnostics="$temporary/home-collision-diagnostics.log"
printf 'profile_path=/Users/alice-backup\n' >"$home_collision_diagnostics"
home_collision_stdout="$temporary/home-collision.stdout"
home_collision_stderr="$temporary/home-collision.stderr"
if (
  export HOME=/Users/alice
  invoke_capture \
    --game 'Home Collision' \
    --version '2.1.0-dev-home-collision' \
    --diagnostics "$home_collision_diagnostics" \
    --mako-repo "$safe_source_repo"
) >"$home_collision_stdout" 2>"$home_collision_stderr"; then
  printf 'Expected a sibling HOME-prefix path to remain intact and fail privacy validation\n' >&2
  exit 1
fi
[[ ! -s "$home_collision_stdout" ]]
grep -F 'present-diagnostics.log' "$home_collision_stderr" >/dev/null
grep -F 'possible credential remains' "$home_collision_stderr" >/dev/null
[[ ! -e "$trace_repo/traces/2.1.0-dev-home-collision" ]]

synthetic_source_root="$temporary/Users"
mkdir -p "$synthetic_source_root"
synthetic_source_home="$synthetic_source_root/alice"
synthetic_source_sibling="$synthetic_source_root/alice-backup"
ln -s "$repo_root" "$synthetic_source_home"
ln -s "$repo_root" "$synthetic_source_sibling"
source_boundary_stdout="$temporary/source-boundary.stdout"
source_boundary_stderr="$temporary/source-boundary.stderr"
if (
  export HOME="$synthetic_source_home"
  invoke_capture \
    --game 'Source Boundary' \
    --version '2.1.0-dev-source-boundary' \
    --mako-repo "$synthetic_source_sibling"
) >"$source_boundary_stdout" 2>"$source_boundary_stderr"; then
  printf 'Expected a sibling HOME-prefix source path to remain intact and fail privacy validation\n' >&2
  exit 1
fi
[[ ! -s "$source_boundary_stdout" ]]
grep -F 'metadata.json' "$source_boundary_stderr" >/dev/null
grep -F 'possible credential remains' "$source_boundary_stderr" >/dev/null
[[ ! -e "$trace_repo/traces/2.1.0-dev-source-boundary" ]]

bare_home_index=0
while IFS= read -r bare_home_path; do
  bare_home_index=$((bare_home_index + 1))
  bare_home_version="2.1.0-dev-bare-home-$bare_home_index"
  bare_home_diagnostics="$temporary/bare-home-$bare_home_index.log"
  bare_home_stdout="$temporary/bare-home-$bare_home_index.stdout"
  bare_home_stderr="$temporary/bare-home-$bare_home_index.stderr"
  printf 'profile_path=%s\n' "$bare_home_path" >"$bare_home_diagnostics"
  if invoke_capture \
    --game "Bare Home $bare_home_index" \
    --version "$bare_home_version" \
    --diagnostics "$bare_home_diagnostics" \
    >"$bare_home_stdout" 2>"$bare_home_stderr"; then
    printf 'Expected bare home path %q to fail privacy validation\n' "$bare_home_path" >&2
    exit 1
  fi
  [[ ! -s "$bare_home_stdout" ]]
  grep -F 'present-diagnostics.log' "$bare_home_stderr" >/dev/null
  grep -F 'possible credential remains' "$bare_home_stderr" >/dev/null
  [[ ! -e "$trace_repo/traces/$bare_home_version" ]]
done <<'EOF'
/Users/alice
/home/alice
/var/home/alice
/root
C:\Users\Alice
/Users/Alice Smith/Documents
C:\Users\Alice Smith\Documents
EOF
if find "$trace_repo" -maxdepth 1 -name '.capture.*' -print -quit | grep -q .; then
  printf 'Rejected home-path captures left a staging directory behind\n' >&2
  exit 1
fi

set_validator fail
rejected_stdout="$temporary/rejected.stdout"
rejected_stderr="$temporary/rejected.stderr"
if invoke_capture \
  --game 'Rejected Game' \
  --version '2.1.0-dev-rejected' \
  --label 'rejected' \
  >"$rejected_stdout" 2>"$rejected_stderr"; then
  printf 'Expected the private validator to reject the synthetic capture\n' >&2
  exit 1
fi
[[ ! -s "$rejected_stdout" ]]
grep -F 'private archive rejected the capture' "$rejected_stderr" >/dev/null
[[ ! -e "$trace_repo/traces/2.1.0-dev-rejected" ]]
if find "$trace_repo" -maxdepth 1 -name '.capture.*' -print -quit | grep -q .; then
  printf 'Rejected capture left a staging directory behind\n' >&2
  exit 1
fi
set_validator pass

unsafe_diagnostics="$temporary/unsafe-present-diagnostics.log"
printf 'unlabelled github_pat_abcdefghijklmnopqrstuvwxyz\n' >"$unsafe_diagnostics"
privacy_stdout="$temporary/privacy.stdout"
privacy_stderr="$temporary/privacy.stderr"
if invoke_capture \
  --version '2.1.0-dev-privacy-rejection' \
  --diagnostics "$unsafe_diagnostics" \
  >"$privacy_stdout" 2>"$privacy_stderr"; then
  printf 'Expected the residual privacy scanner to reject the synthetic secret\n' >&2
  exit 1
fi
[[ ! -s "$privacy_stdout" ]]
grep -F 'present-diagnostics.log' "$privacy_stderr" >/dev/null
grep -F 'possible credential remains' "$privacy_stderr" >/dev/null
[[ ! -e "$trace_repo/traces/2.1.0-dev-privacy-rejection" ]]

destination=$(
  invoke_capture \
    --game 'Game_Name' \
    --game-id 1 \
    --version '2.1.0-dev-test' \
    --label '_Steady Scenario' \
    --session-start '2016-12-31t23:59:60z' \
    --session-end '2017-01-01T00:01:00+00:00'
)
[[ "$destination" == "$trace_repo/traces/2.1.0-dev-test/game-name/20170101T000000Z-steady-scenario-r01" ]]
[[ -f "$destination/metadata.json" ]]
grep -F '[REDACTED]' "$destination/present-diagnostics.log" >/dev/null
for secret in secret-value secret-cookie-value secret-header-value aws-secret-value ASIA1234567890ABCDEF session-secret-value steam-secret-value opaque-token-value; do
  ! grep -F "$secret" "$destination/present-diagnostics.log" >/dev/null
done
grep -F 'archive_authorization=maintainer-private-diagnostic-use' "$destination/present-diagnostics.log" >/dev/null
for heading in '## Test conditions' '## Route and mode sequence' '## Tester observations' '## Evidence summary'; do
  grep -F "$heading" "$destination/notes.md" >/dev/null
done
for field in 'Renderer-reported build:' 'Source:' 'Runtime:' 'GPU/display:' 'Game settings:' 'MAKO profile:'; do
  grep -F "$field" "$destination/notes.md" >/dev/null
done

expected_source_path=$repo_root
if [[ "$expected_source_path" == "$HOME" || "$expected_source_path" == "$HOME/"* ]]; then
  expected_source_path="\$HOME${expected_source_path#"$HOME"}"
fi
expected_source_branch=$(git -C "$repo_root" branch --show-current)
expected_source_branch=${expected_source_branch:-detached}
expected_source_commit=$(git -C "$repo_root" rev-parse HEAD)
expected_source_dirty=false
if [[ -n "$(git -C "$repo_root" status --porcelain)" ]]; then
  expected_source_dirty=true
fi

python3 - "$destination" "$expected_source_path" "$expected_source_branch" "$expected_source_commit" "$expected_source_dirty" <<'PY'
from datetime import datetime
from hashlib import sha256
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
metadata = json.loads((root / "metadata.json").read_text(encoding="utf-8"))
assert metadata["schema_version"] == 2
assert metadata["game"] == {"name": "Game_Name", "slug": "game-name", "id": "1"}
assert metadata["version_label"] == "2.1.0-dev-test"
assert metadata["renderer_reported_build"] == "2.1.0"
assert metadata["session"]["id"] == "20170101T000000Z-steady-scenario-r01"
assert metadata["session"]["label"] == "_Steady Scenario"
assert metadata["session"]["run_index"] == 1
assert metadata["session"]["started_at"] == "2016-12-31t23:59:60z"
assert metadata["session"]["ended_at"] == "2017-01-01T00:01:00+00:00"
captured_at = datetime.fromisoformat(metadata["session"]["captured_at"])
ended_at = datetime.fromisoformat(metadata["session"]["ended_at"])
assert captured_at.tzinfo is not None and ended_at <= captured_at
assert metadata["source"] == {
    "path": sys.argv[2],
    "branch": sys.argv[3],
    "commit": sys.argv[4],
    "dirty": sys.argv[5] == "true",
}
assert metadata["host"]["architecture"]
assert metadata["host"]["os"]
assert metadata["host"]["gpu"] == "Example GPU"
assert metadata["host"]["refresh_hz"] == "90"
expected_artifacts = ["config.toml", "events.log", "notes.md", "present-diagnostics.log"]
assert metadata["artifacts"] == expected_artifacts

manifest_path = root / "checksums.sha256"
lines = manifest_path.read_text(encoding="utf-8").splitlines()
expected_names = sorted(path.name for path in root.iterdir() if path.is_file() and path.name != manifest_path.name)
assert [line[66:] for line in lines] == expected_names
assert len(lines) == len(expected_names)
for line, name in zip(lines, expected_names):
    digest, separator, manifest_name = line.partition("  ")
    assert separator == "  " and manifest_name == name and len(digest) == 64
    assert digest == sha256((root / name).read_bytes()).hexdigest()
PY

set_validator slow-pass
concurrent_one_stdout="$temporary/concurrent-one.stdout"
concurrent_one_stderr="$temporary/concurrent-one.stderr"
concurrent_two_stdout="$temporary/concurrent-two.stdout"
concurrent_two_stderr="$temporary/concurrent-two.stderr"
invoke_capture --game 'Concurrent Game' --version '2.1.0-dev-concurrent' --label 'same-id' \
  >"$concurrent_one_stdout" 2>"$concurrent_one_stderr" &
concurrent_one_pid=$!
invoke_capture --game 'Concurrent Game' --version '2.1.0-dev-concurrent' --label 'same-id' \
  >"$concurrent_two_stdout" 2>"$concurrent_two_stderr" &
concurrent_two_pid=$!
concurrent_one_status=0
concurrent_two_status=0
wait "$concurrent_one_pid" || concurrent_one_status=$?
wait "$concurrent_two_pid" || concurrent_two_status=$?
if [[ "$concurrent_one_status" -eq 0 ]]; then
  [[ "$concurrent_two_status" -ne 0 ]]
  concurrent_stdout=$concurrent_one_stdout
else
  [[ "$concurrent_two_status" -eq 0 ]]
  concurrent_stdout=$concurrent_two_stdout
fi
concurrent_destination="$trace_repo/traces/2.1.0-dev-concurrent/concurrent-game/20260821T120000Z-same-id-r01"
[[ "$(<"$concurrent_stdout")" == "$concurrent_destination" ]]
[[ -f "$concurrent_destination/metadata.json" ]]
[[ -z "$(find "$concurrent_destination" -mindepth 1 -type d -print -quit)" ]]
[[ "$(find "$trace_repo/traces/2.1.0-dev-concurrent/concurrent-game" -mindepth 1 -maxdepth 1 -type d | wc -l | tr -d ' ')" == 1 ]]

set_validator slow-fail
rejected_alpha_stdout="$temporary/rejected-alpha.stdout"
rejected_alpha_stderr="$temporary/rejected-alpha.stderr"
rejected_beta_stdout="$temporary/rejected-beta.stdout"
rejected_beta_stderr="$temporary/rejected-beta.stderr"
invoke_capture --game 'Rejected Alpha' --version '2.1.0-dev-shared-rejection' --label 'same-version' \
  >"$rejected_alpha_stdout" 2>"$rejected_alpha_stderr" &
rejected_alpha_pid=$!
invoke_capture --game 'Rejected Beta' --version '2.1.0-dev-shared-rejection' --label 'same-version' \
  >"$rejected_beta_stdout" 2>"$rejected_beta_stderr" &
rejected_beta_pid=$!
rejected_alpha_status=0
rejected_beta_status=0
wait "$rejected_alpha_pid" || rejected_alpha_status=$?
wait "$rejected_beta_pid" || rejected_beta_status=$?
[[ "$rejected_alpha_status" -ne 0 && "$rejected_beta_status" -ne 0 ]]
[[ ! -s "$rejected_alpha_stdout" && ! -s "$rejected_beta_stdout" ]]
grep -F 'private archive rejected the capture' "$rejected_alpha_stderr" >/dev/null
grep -F 'private archive rejected the capture' "$rejected_beta_stderr" >/dev/null
[[ ! -e "$trace_repo/traces/2.1.0-dev-shared-rejection" ]]
if find "$trace_repo" -maxdepth 1 -name '.capture.*' -print -quit | grep -q .; then
  printf 'Concurrent rejected captures left a staging directory behind\n' >&2
  exit 1
fi

set_validator interrupt
python3 - "$capture" "$repo_root" "$trace_repo" "$diagnostics" "$config" <<'PY'
from pathlib import Path
import os
import signal
import subprocess
import sys
import time

capture = Path(sys.argv[1])
repo_root = Path(sys.argv[2])
trace_repo = Path(sys.argv[3])
diagnostics = Path(sys.argv[4])
config = Path(sys.argv[5])
marker = trace_repo / "validator-interrupt.started"


def kill_group(pid: int, signum: int) -> None:
    try:
        os.killpg(pid, signum)
    except ProcessLookupError:
        pass


for signum in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM):
    signal_name = signal.Signals(signum).name.lower()
    version = f"2.1.0-dev-interrupted-{signal_name}"
    version_dir = trace_repo / "traces" / version
    marker.unlink(missing_ok=True)
    process = subprocess.Popen(
        [
            str(capture),
            "--game",
            f"Interrupted {signal_name}",
            "--version",
            version,
            "--session-start",
            "2026-08-21T13:00:00+01:00",
            "--session-end",
            "2026-08-21T13:01:00+01:00",
            "--diagnostics",
            str(diagnostics),
            "--config",
            str(config),
            "--mako-repo",
            str(repo_root),
            "--trace-repo",
            str(trace_repo),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        start_new_session=True,
    )
    validator_pid = None
    validator_stopped = False
    try:
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if marker.is_file() and version_dir.is_dir():
                validator_pid = int(marker.read_text(encoding="utf-8").strip())
                break
            if process.poll() is not None:
                stdout, stderr = process.communicate()
                raise AssertionError(
                    f"capture exited before {signal_name} interruption: "
                    f"status={process.returncode}, stdout={stdout!r}, stderr={stderr!r}"
                )
            time.sleep(0.02)
        else:
            raise AssertionError(f"capture did not reach validation before {signal_name}")

        kill_group(process.pid, signum)
        stdout, stderr = process.communicate(timeout=10)
        assert process.returncode == 128 + signum, (signal_name, process.returncode, stderr)
        assert stdout == "", (signal_name, stdout)
        assert not version_dir.exists(), (signal_name, version_dir)
        assert not list(trace_repo.glob(".capture.*")), signal_name
        assert "Traceback (most recent call last)" not in stderr, (signal_name, stderr)
        try:
            os.killpg(validator_pid, 0)
        except ProcessLookupError:
            validator_stopped = True
        else:
            raise AssertionError(f"private validator survived {signal_name} rollback")
    finally:
        if process.poll() is None:
            kill_group(process.pid, signal.SIGKILL)
            process.wait()
        if validator_pid is None and marker.is_file():
            validator_pid = int(marker.read_text(encoding="utf-8").strip())
        if validator_pid is not None and not validator_stopped:
            kill_group(validator_pid, signal.SIGKILL)
        marker.unlink(missing_ok=True)
PY

printf 'MAKO trace producer contract tests passed.\n'
