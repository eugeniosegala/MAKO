# MAKO trace extractor

The MAKO trace extractor turns one completed real-game session into a structured, sanitized evidence bundle for comparison across Renderer versions. The producer is [`scripts/capture-trace.sh`](scripts/capture-trace.sh) in this public MAKO repository so diagnostic-format changes and extraction behavior evolve together.

> **Private repository:** The companion [`eugeniosegala/MAKO-Traces`](https://github.com/eugeniosegala/MAKO-Traces) repository is private. It contains game-session logs, machine and runtime metadata, test observations, and performance evidence that must not be copied into the public MAKO repository. Access to that repository is not required to build, test, package, install, or release MAKO.

## Repository boundary

| Location | Visibility | Responsibility |
| --- | --- | --- |
| `MAKO/scripts/capture-trace.sh` | Public | Session extraction, time slicing, sanitization, metadata generation, credential rejection, and checksums |
| `MAKO/TRACES.md` | Public | Extractor behavior, safety contract, and maintainer workflow |
| `MAKO-Traces` | Private | Versioned evidence, metadata schema, notes template, archive validation, and comparison history |

The private repository is a development evidence store, not a runtime dependency or distribution input. MAKO must continue to behave normally when no trace checkout is present.

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

Close the game first so buffered diagnostics are complete, then capture before starting another game because the development wrapper begins the next session with a fresh presentation log.

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

1. validates inputs, timestamps, version identity, and the private Git checkout;
2. creates a temporary staging directory inside the private repository;
3. sanitizes home-directory paths and common credential assignments;
4. clips optional Decky and Steam logs to the session window;
5. derives a compact event index without altering the raw presentation evidence;
6. records the game, session, source commit, dirty state, Renderer-reported build, operating system, architecture, GPU, refresh rate, and artifact list;
7. creates a stable UTC run ID from the session start, scenario label, and repetition index;
8. rejects likely remaining credentials; and
9. writes SHA-256 checksums and atomically installs the completed run directory.

An existing destination is never overwritten. A failed capture removes its staging directory and does not publish a partial run.

## Stored evidence

Each run is written to `traces/<version-label>/<game-slug>/<run-id>/` in the private repository. Version-first grouping makes the test coverage for one build immediately visible. A run ID such as `20260820T113052Z-fixed-adaptive-fifo-pressure-r01` combines the UTC session start, a readable scenario label, and a repetition number. The exact local start and end timestamps remain in metadata. The usual files are:

- `metadata.json`: machine-readable identity and provenance;
- `present-diagnostics.log`: raw session-scoped MAKO Renderer evidence;
- `events.log`: derived navigation index for transitions, misses, fallback, and failures;
- `config.toml`: sanitized Renderer configuration;
- `decky-session.log` and `steam-session.log`: optional time-clipped context;
- `notes.md`: subjective observations and evidence-backed interpretation; and
- `checksums.sha256`: integrity record for every other run file.

The extractor does not infer image quality from timing logs. Ghosting, fluidity, and visual artifacts remain tester observations unless supported by repeatable captures or image-quality evidence.

## Protected inputs and privacy

Never capture or upload `Lossless.dll`, game binaries, Vulkan layer binaries, shader caches, crash dumps containing process memory, access tokens, cookies, account data, or unrelated application logs. Sanitized `$HOME` paths are permitted; user-specific absolute home paths are not.

The archive being private reduces exposure but does not remove the need for minimization and review. Inspect every new run before committing or sharing it.

## Validation and contract changes

Validate the private archive after every capture:

```bash
../MAKO-Traces/scripts/validate.sh
```

If an intentional notes or factual metadata correction changes a run, refresh its checksums with the helper in the private repository and validate again. Raw diagnostic and clipped source logs are immutable evidence and should not be rewritten.

When the capture contract changes, update `scripts/capture-trace.sh` and `TRACES.md` in MAKO together with `schema/metadata.schema.json`, `docs/TRACE-FORMAT.md`, the notes template, and the validator in MAKO-Traces. Historical raw evidence should remain intact; use a compatible reader or factual metadata migration instead of rewriting it.
