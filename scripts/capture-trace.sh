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
  --trace-repo PATH        MAKO Traces checkout that receives the capture
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

credential_key_pattern='(?:(?:[A-Za-z0-9]+[_-])*(?:access[_-]?token|refresh[_-]?token|session[_-]?token|token|password|passwd|api[_-]?key|client[_-]?secret|secret[_-]?access[_-]?key|access[_-]?key[_-]?id)|authorization|cookie)'

slugify() {
  printf '%s' "$1" | tr '[:upper:]' '[:lower:]' | sed -E 's/[^a-z0-9]+/-/g; s/^-+//; s/-+$//; s/-+/-/g'
}

iso_value() {
  python3 -c '
from datetime import datetime, timezone
import re
import sys
mode = sys.argv[1]
value = sys.argv[2]
if re.fullmatch(r"[0-9]{4}-[0-9]{2}-[0-9]{2}[Tt][0-9]{2}:[0-9]{2}:[0-9]{2}(?:\.[0-9]+)?(?:[Zz]|[+-][0-9]{2}:[0-9]{2})", value) is None:
    raise ValueError("timestamp must use RFC 3339 date-time syntax with a UTC offset")
if value.endswith("-00:00"):
    raise ValueError("timestamp must use a known UTC offset")
if value[17:19] == "60":
    raise ValueError("leap-second timestamps are not supported")
value = value[:10] + "T" + value[11:]
if value.endswith(("Z", "z")):
    value = value[:-1] + "+00:00"
parsed = datetime.fromisoformat(value)
if parsed.tzinfo is None or parsed.utcoffset() is None:
    raise ValueError("timestamp must include a UTC offset")
if mode == "microseconds":
    epoch = datetime(1970, 1, 1, tzinfo=timezone.utc)
    delta = parsed.astimezone(timezone.utc) - epoch
    print(delta.days * 86400000000 + delta.seconds * 1000000 + delta.microseconds)
elif mode == "utc-prefix":
    print(parsed.astimezone(timezone.utc).strftime("%Y%m%dT%H%M%SZ"))
else:
    raise ValueError(f"unknown timestamp conversion mode: {mode}")
' "$1" "$2"
}

iso_now() {
  python3 -c 'from datetime import datetime; print(datetime.now().astimezone().isoformat(timespec="microseconds"))'
}

local_log_time() {
  local value=$1
  [[ "$value" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}[Tt][0-9]{2}:[0-9]{2}:[0-9]{2} ]] || die "invalid ISO timestamp: $value"
  printf '%s %s' "${value:0:10}" "${value:11:8}"
}

sanitize_file() {
  local source=$1
  local destination=$2
  python3 - "$source" "$destination" "$credential_key_pattern" <<'PY'
from pathlib import Path
import os
import re
import sys

source = Path(sys.argv[1])
destination = Path(sys.argv[2])
credential_key_pattern = sys.argv[3]
text = source.read_text(encoding="utf-8", errors="surrogateescape")
home = os.environ.get("HOME")
if home:
    home_path = re.compile(
        re.escape(home) + r"(?=$|[/\\]|[\s\"'`,;:)\]}])"
    )
    text = home_path.sub("$HOME", text)
text = re.sub(
    rf"(?im)^(\s*{credential_key_pattern}\s*:\s*).*$",
    lambda match: f"{match.group(1)}[REDACTED]",
    text,
)
assignment = re.compile(
    rf"(?i)(\b{credential_key_pattern}\b[\"']?\s*[:=]\s*)"
    r"(\"[^\"\r\n]*\"|'[^'\r\n]*'|[^\s,}]+)"
)
text = assignment.sub(
    lambda match: match.group(1)
    + (f'{match.group(2)[0]}[REDACTED]{match.group(2)[0]}' if match.group(2)[:1] in {'"', "'"} else "[REDACTED]"),
    text,
)
destination.write_text(text, encoding="utf-8", errors="surrogateescape")
PY
}

publish_and_validate_capture() {
  local source=$1
  local destination_path=$2
  local root=$3
  local lock_path=$4
  local validator=$5
  python3 - "$source" "$destination_path" "$root" "$lock_path" "$validator" <<'PY'
from pathlib import Path
import fcntl
import os
import signal
import shutil
import subprocess
import sys

source = Path(sys.argv[1])
destination = Path(sys.argv[2])
root = Path(sys.argv[3])
lock_path = Path(sys.argv[4])
validator = Path(sys.argv[5])


class DestinationExists(Exception):
    pass


class ValidatorRejected(Exception):
    pass


class TransactionInterrupted(Exception):
    def __init__(self, signum: int):
        super().__init__(signum)
        self.signum = signum


managed_signals = (signal.SIGHUP, signal.SIGINT, signal.SIGTERM)


def handle_interruption(signum: int, _frame: object) -> None:
    # A second terminal signal must not interrupt owned rollback once unwinding starts.
    signal.pthread_sigmask(signal.SIG_BLOCK, managed_signals)
    raise TransactionInterrupted(signum)


def ensure_directory(path: Path) -> bool:
    if path.is_symlink():
        raise RuntimeError(f"capture parent cannot be a symbolic link: {path}")
    try:
        path.mkdir()
        return True
    except FileExistsError:
        if path.is_symlink() or not path.is_dir():
            raise RuntimeError(f"capture parent is not a regular directory: {path}")
        return False


def remove_owned_destination(identity: tuple[int, int]) -> None:
    try:
        current = destination.lstat()
    except FileNotFoundError:
        return
    if (current.st_dev, current.st_ino) != identity:
        raise RuntimeError(
            f"refusing to remove a capture destination no longer owned by this process: {destination}"
        )
    if destination.is_dir() and not destination.is_symlink():
        shutil.rmtree(destination)
    else:
        destination.unlink()


def stop_validator(process: subprocess.Popen[bytes] | None) -> list[str]:
    if process is None or process.poll() is not None:
        return []
    errors = []
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    except OSError as error:
        errors.append(f"cannot terminate the private validator: {error}")
    try:
        process.wait(timeout=2)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        except OSError as error:
            errors.append(f"cannot kill the private validator: {error}")
        try:
            process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            errors.append("the private validator did not stop during rollback")
    return errors


def remove_created_parents(
    game_dir: Path,
    version_dir: Path,
    game_created: bool,
    version_created: bool,
) -> list[str]:
    errors = []
    for path, created in ((game_dir, game_created), (version_dir, version_created)):
        if not created:
            continue
        try:
            path.rmdir()
        except FileNotFoundError:
            pass
        except OSError as error:
            errors.append(f"cannot remove empty capture parent {path}: {error}")
    return errors


def rollback_capture(
    process: subprocess.Popen[bytes] | None,
    identity: tuple[int, int] | None,
    game_dir: Path,
    version_dir: Path,
    game_created: bool,
    version_created: bool,
) -> bool:
    signal.pthread_sigmask(signal.SIG_BLOCK, managed_signals)
    errors = stop_validator(process)
    if identity is not None:
        try:
            remove_owned_destination(identity)
        except (OSError, RuntimeError) as error:
            errors.append(str(error))
    errors.extend(remove_created_parents(game_dir, version_dir, game_created, version_created))
    for error in errors:
        print(f"capture rollback failed: {error}", file=sys.stderr)
    return not errors


initial_signal_mask = signal.pthread_sigmask(signal.SIG_BLOCK, managed_signals)
for managed_signal in managed_signals:
    signal.signal(managed_signal, handle_interruption)
signal.pthread_sigmask(signal.SIG_SETMASK, initial_signal_mask)

version_dir = destination.parent.parent
game_dir = destination.parent
version_created = False
game_created = False
installed_identity = None
validator_process = None
committed = False

try:
    with lock_path.open("a+b") as lock:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX)
        try:
            resolved_root = root.resolve(strict=True)
            if root.is_symlink():
                raise RuntimeError("trace root became a symbolic link before publication")
            destination.relative_to(root)
            previous_mask = signal.pthread_sigmask(signal.SIG_BLOCK, managed_signals)
            try:
                version_created = ensure_directory(version_dir)
                game_created = ensure_directory(game_dir)
                destination.parent.resolve(strict=True).relative_to(resolved_root)
                if os.path.lexists(destination):
                    raise DestinationExists

                source_stat = source.lstat()
                os.rename(source, destination)
                installed_identity = (source_stat.st_dev, source_stat.st_ino)
                installed_stat = destination.lstat()
                if (installed_stat.st_dev, installed_stat.st_ino) != installed_identity:
                    raise RuntimeError("capture destination identity changed during publication")
            finally:
                signal.pthread_sigmask(signal.SIG_SETMASK, previous_mask)

            if validator.is_file() and os.access(validator, os.X_OK):
                previous_mask = signal.pthread_sigmask(signal.SIG_BLOCK, managed_signals)
                try:
                    validator_process = subprocess.Popen(
                        [str(validator)],
                        stdout=subprocess.DEVNULL,
                        start_new_session=True,
                        preexec_fn=lambda: signal.pthread_sigmask(
                            signal.SIG_SETMASK, previous_mask
                        ),
                    )
                finally:
                    signal.pthread_sigmask(signal.SIG_SETMASK, previous_mask)
                validator_status = validator_process.wait()
                validator_process = None
                if validator_status != 0:
                    raise ValidatorRejected
            committed = True
        except DestinationExists:
            rollback_ok = rollback_capture(
                validator_process,
                installed_identity,
                game_dir,
                version_dir,
                game_created,
                version_created,
            )
            raise SystemExit(17 if rollback_ok else 18)
        except ValidatorRejected:
            rollback_ok = rollback_capture(
                validator_process,
                installed_identity,
                game_dir,
                version_dir,
                game_created,
                version_created,
            )
            raise SystemExit(19 if rollback_ok else 18)
        except TransactionInterrupted as error:
            if not committed:
                rollback_capture(
                    validator_process,
                    installed_identity,
                    game_dir,
                    version_dir,
                    game_created,
                    version_created,
                )
            print(
                f"capture publication interrupted by {signal.Signals(error.signum).name}",
                file=sys.stderr,
            )
            raise SystemExit(128 + error.signum)
        except KeyboardInterrupt:
            signal.pthread_sigmask(signal.SIG_BLOCK, managed_signals)
            if not committed:
                rollback_capture(
                    validator_process,
                    installed_identity,
                    game_dir,
                    version_dir,
                    game_created,
                    version_created,
                )
            print("capture publication interrupted by SIGINT", file=sys.stderr)
            raise SystemExit(130)
        except BaseException as error:
            if not committed:
                rollback_capture(
                    validator_process,
                    installed_identity,
                    game_dir,
                    version_dir,
                    game_created,
                    version_created,
                )
            print(f"cannot publish capture: {error}", file=sys.stderr)
            raise SystemExit(18)
except TransactionInterrupted as error:
    print(
        f"capture publication interrupted by {signal.Signals(error.signum).name}",
        file=sys.stderr,
    )
    raise SystemExit(128 + error.signum)
except KeyboardInterrupt:
    print("capture publication interrupted by SIGINT", file=sys.stderr)
    raise SystemExit(130)
PY
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
command -v python3 >/dev/null || die 'python3 is required'

game_slug=$(slugify "$game")
label_slug=$(slugify "$label")
[[ -n "$game_slug" ]] || die 'game name does not produce a safe path component'
[[ -n "$label_slug" ]] || die 'label does not produce a safe path component'
[[ "$version" =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$ ]] || die 'version must begin with a letter or number and contain only letters, numbers, dots, underscores, and dashes'
[[ "$version" != '.' && "$version" != '..' ]] || die 'version cannot be a dot segment'
version_component=$version

start_epoch_microseconds=$(iso_value microseconds "$session_start") || die "invalid ISO timestamp: $session_start"

if [[ -z "$trace_repo" ]]; then
  trace_repo=$(dirname -- "$mako_repo")/MAKO-Traces
fi
[[ -d "$trace_repo" ]] || die "trace repository does not exist: $trace_repo"
git -C "$trace_repo" rev-parse --is-inside-work-tree >/dev/null 2>&1 || die "trace repository is not a Git checkout: $trace_repo"
trace_repo=$(cd -- "$trace_repo" && pwd -P)

if [[ -z "$session_end" ]]; then
  session_end=$(iso_now)
fi
captured_at=$(iso_now)

start_log_time=$(local_log_time "$session_start")
end_log_time=$(local_log_time "$session_end")
end_epoch_microseconds=$(iso_value microseconds "$session_end") || die "invalid ISO timestamp: $session_end"
captured_epoch_microseconds=$(iso_value microseconds "$captured_at") || die "invalid capture timestamp: $captured_at"
((end_epoch_microseconds >= start_epoch_microseconds)) || die 'session end precedes session start'
((captured_epoch_microseconds >= end_epoch_microseconds)) || die 'session end is later than the capture time'

utc_start=$(iso_value utc-prefix "$session_start") || die "invalid ISO timestamp: $session_start"
run_index_number=$((10#$run_index))
printf -v run_component 'r%02d' "$run_index_number"
session_component="$utc_start-$label_slug-$run_component"
traces_root="$trace_repo/traces"
destination="$traces_root/$version_component/$game_slug/$session_component"
[[ -d "$traces_root" ]] || die "trace repository is missing its traces directory: $traces_root"
[[ ! -L "$traces_root" ]] || die "trace root cannot be a symbolic link: $traces_root"
resolved_traces_root=$(python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$traces_root")
resolved_destination=$(python3 -c 'import os, sys; print(os.path.realpath(sys.argv[1]))' "$destination")
case "$resolved_traces_root" in
  "$trace_repo"/*) ;;
  *) die "trace root escapes the private repository: $traces_root" ;;
esac
case "$resolved_destination" in
  "$resolved_traces_root"/*) ;;
  *) die "capture destination escapes the trace root: $destination" ;;
esac
[[ ! -e "$destination" && ! -L "$destination" ]] || die "capture already exists: $destination"

working=''

cleanup_capture() {
  local capture_status=$?
  trap - EXIT HUP INT TERM
  if [[ -n "$working" && ( -e "$working" || -L "$working" ) ]]; then
    rm -rf -- "$working"
  fi
  exit "$capture_status"
}

trap cleanup_capture EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

working=$(mktemp -d "$trace_repo/.capture.XXXXXX")

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
    printf -- '- Renderer-reported build: `<reported build>`\n'
    printf -- '- Source: `<branch>` at `<commit>`\n'
    printf -- '- Session: `%s` to `%s`\n' "$session_start" "$session_end"
    printf -- '- Runtime: `<Proton/runtime and version>`\n'
    printf -- '- GPU/display: `<GPU, driver, refresh rate, HDR/SDR>`\n'
    printf -- '- Game settings: `<resolution and relevant graphics settings>`\n'
    printf -- '- MAKO profile: `<mode, target, multiplier, precision, flow scale, performance mode, pacing>`\n'
    if [[ -n "$game_id" ]]; then
      printf -- '- Game ID: `%s`\n' "$game_id"
    fi
    printf '\n## Route and mode sequence\n\nDescribe the repeatable scene or route and record mode-change timestamps.\n\n'
    printf '## Tester observations\n\nAdd subjective image quality, stability, scene, route, and mode-change observations here.\n\n'
    printf '## Evidence summary\n\nAdd measured ranges, recovery behavior, errors, and comparison conclusions here.\n'
  } >"$working/notes.md"
fi

source_branch='unknown'
source_commit='unknown'
source_dirty=false
source_path=$mako_repo
if [[ "$source_path" == "$HOME" || "$source_path" == "$HOME/"* ]]; then
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
if [[ -r /etc/os-release ]]; then
  os_name=$(sed -n -E 's/^PRETTY_NAME="?(.*)"?/\1/p' /etc/os-release | head -n 1)
  os_name=${os_name%\"}
else
  os_name=$(uname -s)
fi

artifacts_json=$(python3 -c '
import json
from pathlib import Path
import sys
root = Path(sys.argv[1])
print(json.dumps(sorted(path.name for path in root.iterdir() if path.is_file())))
' "$working")

jq -n \
  --arg game_name "$game" \
  --arg game_slug "$game_slug" \
  --arg game_id "$game_id" \
  --arg version_label "$version" \
  --arg label "$label" \
  --arg session_id "$session_component" \
  --arg started_at "$session_start" \
  --arg ended_at "$session_end" \
  --arg captured_at "$captured_at" \
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

if python3 - "$working" "$credential_key_pattern" <<'PY'
from pathlib import Path
import re
import sys

root = Path(sys.argv[1])
credential_key_pattern = sys.argv[2].encode("ascii")
patterns = (
    re.compile(
        rb"\b" + credential_key_pattern + rb"\b"
        rb"[\"']?\s*[:=]\s*[\"']?(?!\[REDACTED\])[^\s\"',}]+",
        re.I,
    ),
    re.compile(rb"(?:github_pat_[A-Za-z0-9_]{20,}|gh[pousr]_[A-Za-z0-9]{20,}|sk-[A-Za-z0-9]{20,}|(?:AKIA|ASIA)[0-9A-Z]{16})"),
    re.compile(
        rb"(?:/(?:home|Users|var/home)/[^/\\\r\n\"'`,;:)\]}]+|/root|[A-Za-z]:\\Users\\[^/\\\r\n\"'`,;:)\]}]+)"
        rb"(?=$|[/\\\s\"'`,;:)\]}])",
        re.I,
    ),
    re.compile(rb"\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b", re.I),
)
for path in root.iterdir():
    if path.is_file() and any(pattern.search(path.read_bytes()) for pattern in patterns):
        print(path.name, file=sys.stderr)
        raise SystemExit(0)
raise SystemExit(1)
PY
then
  die 'a possible credential remains in the capture; inspect the sources and sanitize them explicitly'
fi

(
  python3 - "$working" <<'PY'
from hashlib import sha256
from pathlib import Path
import sys

root = Path(sys.argv[1])
lines = []
for path in sorted((entry for entry in root.iterdir() if entry.is_file()), key=lambda entry: entry.name):
    digest = sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    lines.append(f"{digest.hexdigest()}  {path.name}\n")
(root / "checksums.sha256").write_text("".join(lines), encoding="utf-8")
PY
)

capture_lock_path="$(git -C "$trace_repo" rev-parse --absolute-git-dir)/mako-capture.lock"
if publish_and_validate_capture "$working" "$destination" "$traces_root" "$capture_lock_path" "$trace_repo/scripts/validate.sh"; then
  working=''
else
  publication_status=$?
  if [[ "$publication_status" -eq 17 ]]; then
    die "capture already exists: $destination"
  elif [[ "$publication_status" -eq 19 ]]; then
    die 'the private archive rejected the capture; the incomplete destination was removed'
  elif [[ "$publication_status" -eq 129 || "$publication_status" -eq 130 || "$publication_status" -eq 143 ]]; then
    exit "$publication_status"
  fi
  die "capture could not be published safely: $destination"
fi
printf '%s\n' "$destination"
