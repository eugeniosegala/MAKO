# MAKO trace extractor

The MAKO trace extractor turns one completed real-game session into a structured, sanitized evidence bundle for comparison across Renderer versions. The producer is [`scripts/capture-trace.sh`](scripts/capture-trace.sh) in this public MAKO repository so diagnostic-format changes and extraction behavior evolve together.

> **Private repository:** The companion [MAKO Traces](https://github.com/eugeniosegala/MAKO-Traces) repository is private. It contains game-session logs, machine and runtime metadata, test observations, and performance evidence that must not be copied into the public MAKO repository. Access to that repository is not required to build, test, package, install, or release MAKO.

## Repository boundary

| Location | Visibility | Responsibility |
| --- | --- | --- |
| `MAKO/scripts/capture-trace.sh` | Public | Session extraction, time slicing, sanitization, metadata generation, credential rejection, and the initial checksum manifest |
| `MAKO/TRACES.md` | Public | Extractor behavior, safety contract, and maintainer workflow |
| `MAKO-Traces` | Private | Controlled traces, external reports, executable schemas, the canonical checksum contract and verification, guarded initialization and refresh, append-only history, archive validation, and comparison policy |

The archive is never a runtime or distribution dependency; MAKO must work normally without it.

## Local setup

Keep both repositories as siblings rather than nesting the private archive inside the public worktree:

```text
<workspace>/
  MAKO/
  MAKO-Traces/
```

Authorized maintainers can clone the private repository with their normal GitHub credentials:

```bash
git clone https://github.com/eugeniosegala/MAKO-Traces.git ../MAKO-Traces
```

The extractor defaults to that sibling path. Use `--trace-repo <path>` when the private checkout is elsewhere.

## Capture a completed session

Close the game first so buffered diagnostics are complete. The managed development wrapper keeps the latest session at `~/.config/mako-render/present-diagnostics.log` and retains the previous two sessions as `.1` and `.2`; a fourth diagnostics-enabled launch replaces the oldest. The extractor defaults to the latest session. To archive an earlier retained run, pass its exact rotated path through `--diagnostics`; do not pass a combined `mako-diagnostics --session all` report because one trace must contain one raw game session.

```bash
./scripts/capture-trace.sh \
  --game "Resident Evil 4" \
  --game-id 2050650 \
  --version "2.0.0-dev-f1f6a1c" \
  --label "fixed-adaptive-fifo-pressure" \
  --run-index 1 \
  --session-start "2026-08-20T12:30:52+01:00" \
  --session-end "2026-08-20T12:36:25+01:00" \
  --decky-log "$HOME/homebrew/logs/Mako/<decky-log>.log" \
  --steam-log "$HOME/.steam/steam/logs/console-linux.txt"
```

Required inputs are the game name, explicit archive version label, and local ISO session-start timestamp. The optional run index defaults to 1 and distinguishes repeated trials of the same scenario. The session end defaults to capture time. Present diagnostics, Renderer configuration, and the MAKO source checkout have sensible local defaults; Decky and Steam logs are optional and are clipped to the requested time window.

Development builds must use an explicit label such as `2.0.0-dev-f1f6a1c`. Do not archive a branch build as a released version merely because the Renderer binary reports that base version.

## Capture transaction

The extractor:

1. validates inputs, offset-aware timestamp ordering, version identity, path components, destination containment, and the private Git checkout;
2. creates a temporary staging directory inside the private repository;
3. sanitizes home-directory paths and common credential assignments;
4. clips optional Decky and Steam logs to the session window;
5. derives a compact event index without altering the raw presentation evidence;
6. records the game, session, source commit, dirty state, Renderer-reported build, operating system, architecture, GPU, refresh rate, and artifact list;
7. creates a stable UTC run ID from the session start, scenario label, and repetition index;
8. rejects likely remaining credentials, secrets, email addresses, and cross-platform personal home paths;
9. writes the initial manifest in the private archive's canonical SHA-256 format and atomically installs the completed run directory; and
10. invokes the private repository validator when its stable entry point is available, removing the new directory if the archive rejects it.

An existing destination is never overwritten. Final archive components cannot be dot segments or escape the private `traces/` root, including through an existing symlink. A failed capture removes its staging or newly rejected destination and does not publish a partial run. The private validator remains the authority for the complete archive; its output is suppressed on success so this producer prints exactly one destination path.

## Stored evidence

Each run is written to `traces/<version-label>/<game-slug>/<run-id>/` in the private repository. Version-first grouping makes the test coverage for one build immediately visible. A run ID such as `20260820T113052Z-fixed-adaptive-fifo-pressure-r01` combines the UTC session start, a readable scenario label, and a repetition number. The exact local start and end timestamps remain in metadata. The usual files are:

- `metadata.json`: machine-readable identity and provenance;
- `present-diagnostics.log`: raw session-scoped MAKO Renderer evidence;
- `events.log`: derived navigation index for transitions, misses, fallback, and failures;
- `config.toml`: sanitized Renderer configuration;
- `decky-session.log` and `steam-session.log`: optional time-clipped context;
- `notes.md`: subjective observations and evidence-backed interpretation; and
- `checksums.sha256`: integrity record for every other run file.

Timing logs do not prove ghosting, fluidity, or image quality. Keep those as tester observations unless repeatable visual evidence supports them.

## External user reports

Externally supplied diagnostics belong under `user-reports/<issue>/<report-id>/` in the private archive rather than under `traces/`. A user report may preserve a compatibility observation tied to a specific issue even when exact build, host, session timing, configuration, or comparative evidence is unavailable. Its metadata and notes must state those limitations and must never present the report as a controlled benchmark.

Do not run `capture-trace.sh` against another user's diagnostics. The extractor records the local source checkout and host, which would give imported evidence false provenance. Sanitize the supplied files, remove personal identifiers, record how private archival use was authorized, generate checksums, and validate the private archive's user-report contract instead.

## Protected inputs and privacy

Never capture or upload `Lossless.dll`, game binaries, Vulkan layer binaries, shader caches, crash dumps containing process memory, access tokens, cookies, account data, personal correspondence, or unrelated application logs. Sanitized `$HOME` paths are permitted; user-specific absolute home paths are not. Preserve only the minimum report text needed to understand an external observation.

Private storage does not replace minimization and review. Inspect every run before committing or sharing it.

## Validation and contract changes

Validate the private archive after every capture or user-report import from the MAKO Traces checkout:

```bash
./scripts/check.sh
# or: just check
```

The public producer creates the initial controlled-trace manifest. Private schemas own field shape; the MAKO Traces validator owns chronology, path identity, checksums, initialization/refresh, privacy, protected inputs, and history. Permitted notes, derived-event, report, or factual metadata corrections use `./scripts/refresh-checksums.sh <evidence-directory>` (or `just refresh <evidence-directory>`) followed by `./scripts/check.sh`. Raw diagnostics, configurations, clipped source logs, and supplied user artifacts are immutable and cannot be resealed.

When the capture contract changes, update `scripts/capture-trace.sh`, `scripts/test-capture-trace.sh`, and `TRACES.md` in MAKO together with the schema, format/validation guides, notes template, validator, and mutation tests in MAKO Traces. Historical raw evidence should remain intact; use a compatible reader or factual metadata migration instead of rewriting it.
