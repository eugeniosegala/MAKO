# MAKO trace extractor

[`scripts/capture-trace.sh`](scripts/capture-trace.sh) turns one completed game session into a sanitized evidence bundle for comparing Renderer versions.

> **Private evidence:** Game-session logs, machine and runtime metadata, test observations, and performance evidence belong outside the public MAKO repository. External evidence storage is not required to build, test, package, install, or release MAKO.

## Repository boundary

| Location | Visibility | Responsibility |
| --- | --- | --- |
| `MAKO/scripts/capture-trace.sh` | Public | Session extraction, time slicing, sanitization, metadata generation, credential rejection, and the initial checksum manifest |
| `MAKO/TRACES.md` | Public | Extractor behavior, safety contract, and maintainer workflow |
| External evidence archive | Private | Controlled traces, user reports, schemas, checksum verification, history, validation, and comparison policy |

The archive is never a runtime or distribution dependency; MAKO must work normally without it.

## Local setup

Keep the authorized evidence archive outside the public MAKO worktree and pass it with `--trace-repo <path>`.

## Capture a completed session

Close the game so buffered diagnostics are complete. The managed wrapper stores the latest session at `~/.config/mako-render/present-diagnostics.log` and four older sessions as `.1` through `.4`. The extractor uses the latest by default; select one rotated file with `--diagnostics` for an earlier run. Do not pass a combined multi-session report.

```bash
./scripts/capture-trace.sh \
  --game "Example Game" \
  --version "dev-f1f6a1c" \
  --label "adaptive-overlay-recovery" \
  --session-start "2026-09-04T12:30:52+01:00" \
  --trace-repo /path/to/MAKO-Traces
```

The game, version label, and offset-aware session start are required. The run index defaults to 1, the session end defaults to capture time, and local diagnostics, configuration, and source checkout have defaults. Use `--help` for optional game ID, logs, notes, paths, and repeated-run controls.

Development builds must use an explicit label such as `dev-f1f6a1c`. Do not archive a branch build as a released version merely because the Renderer binary reports that base version.

## Capture transaction

The extractor validates identity, timestamps, paths, and the private Git checkout; stages inside that checkout; sanitizes local paths and common credentials; clips optional logs; derives an event index; records source, host, display, and artifact metadata; writes SHA-256 checksums; and publishes atomically. If the private validator is available, it runs before the capture is accepted.

Existing destinations are never overwritten. Dot segments, symlink escapes, and paths outside `traces/` are rejected. Failed capture or validation removes the owned staging and destination, leaving no partial run.

## Stored evidence

Each run is written to `traces/<version-label>/<game-slug>/<run-id>/`. The run ID combines the UTC session start, scenario label, and repetition number. A run normally contains:

- `metadata.json`: machine-readable identity and provenance;
- `present-diagnostics.log`: raw session-scoped MAKO Renderer evidence;
- `events.log`: derived navigation index for transitions, misses, fallback, and failures;
- `config.toml`: sanitized Renderer configuration;
- `decky-session.log` and `steam-session.log`: optional time-clipped context;
- `notes.md`: subjective observations and evidence-backed interpretation; and
- `checksums.sha256`: integrity record for every other run file.

Timing logs do not prove ghosting, fluidity, or image quality. Keep those as tester observations unless repeatable visual evidence supports them.

## External user reports

Externally supplied diagnostics belong under `user-reports/<issue>/<report-id>/`, not `traces/`. State any missing build, host, timing, configuration, or comparison context; never present a user report as a controlled benchmark.

Do not run `capture-trace.sh` against another user's diagnostics because it would record false local provenance. Sanitize the files, record authorization, generate checksums, and use the archive's user-report workflow instead.

## Protected inputs and privacy

Never capture or upload `Lossless.dll`, game binaries, Vulkan layer binaries, shader caches, crash dumps containing process memory, access tokens, cookies, account data, personal correspondence, or unrelated application logs. Sanitized `$HOME` paths are permitted; user-specific absolute home paths are not. Preserve only the minimum report text needed to understand an external observation.

Private storage does not replace minimization. Inspect every run before committing or sharing it.

## Validation and contract changes

Validate the private archive after every capture or user-report import from the MAKO Traces checkout:

```bash
./scripts/check.sh
# or: just check
```

The public producer creates the initial manifest. MAKO Traces owns schemas, chronology, path identity, checksums, privacy, protected-input checks, and history. Permitted notes, derived-event, report, or factual metadata corrections use `./scripts/refresh-checksums.sh <evidence-directory>` (or `just refresh <evidence-directory>`) followed by `./scripts/check.sh`. Raw diagnostics, configurations, clipped logs, and supplied user artifacts remain immutable.

When the contract changes, update the producer, its test, this guide, and the corresponding MAKO Traces schema, guides, templates, validator, and tests together. Preserve historical raw evidence through compatible readers or factual metadata migration.
