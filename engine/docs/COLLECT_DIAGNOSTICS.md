# Collect standalone MAKO Renderer diagnostics

Use this guide for MAKO Renderer installed directly from an archive or source. If you normally launch with MAKO Decky's `/home/deck/.local/bin/mako-run`, use the [MAKO Decky diagnostics guide](https://github.com/eugeniosegala/MAKO/blob/main/plugin/docs/COLLECT_DIAGNOSTICS.md).

The workflow is: validate configuration, enable diagnostics for one affected game, reproduce once, create `MAKO-diagnostics.txt`, restore the launch settings, and submit the report through the [MAKO diagnostic form][diagnostic-form].

Review the report before sharing it. It can contain usernames, game names, application IDs, ROM filenames, and paths. Remove personal path components and never include passwords, credentials, device serial numbers, licence keys, or `Lossless.dll`. Do not paste the report into a public GitHub issue.

## 1. Validate and prepare

Validate the active configuration:

```bash
mako-cli validate
```

Use `$HOME/.local/bin/mako-cli` if the command is not on `PATH`. Fully close the game, then choose its normal launch method.

### Native Steam or Proton

Temporarily replace the game's Steam launch options with:

```text
MAKO_PRESENT_DIAGNOSTICS=1 MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 ~/.local/bin/mako-launch %command%
```

### Direct terminal command

Capture the complete terminal session:

```bash
MAKO_PRESENT_DIAGNOSTICS=1 \
MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 \
~/.local/bin/mako-launch your-game-command \
2>&1 | tee "$HOME/Desktop/MAKO-renderer-session.log"
```

### Existing Heroic or Flatpak setup

Keep the existing launch method and Flatpak overrides. Add these variables only to the affected game:

```text
MAKO_PRESENT_DIAGNOSTICS=1
MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25
```

Do not add the Decky `mako-run` wrapper to a standalone installation.

## 2. Reproduce once

1. Start the game through the same route that normally shows the problem.
2. Reproduce the issue and note the action and approximate time.
3. Fully exit the game rather than suspending it.
4. Wait for Wine, Proton, or emulator child processes to close.

Create the report before another diagnostics-enabled standalone run. Steam console logs are shared and are not rotated into MAKO Decky's private five-session history.

## 3. Create the report

For a user-local archive installation:

```bash
"$HOME/.local/bin/mako-diagnostics" --lines 2000 all > "$HOME/Desktop/MAKO-diagnostics.txt" 2>&1
```

For a system or source installation:

```bash
mako-diagnostics --lines 2000 all > "$HOME/Desktop/MAKO-diagnostics.txt" 2>&1
```

For the direct terminal capture:

```bash
mako-diagnostics --log "$HOME/Desktop/MAKO-renderer-session.log" --lines 2000 all > "$HOME/Desktop/MAKO-diagnostics.txt" 2>&1
```

The helper prefers MAKO Decky's private log when present; otherwise it selects the newest native or Flatpak Steam console log. If that is not the reproduction you want, pass its log path explicitly with `--log`. `all` keeps the last 2,000 matching MAKO, loader, and Gamescope lines rather than copying the complete log.

Run `mako-diagnostics --list` to see focused presets. `startup`, `layers`, `config`, `scaling`, `adaptive`, `recovery`, `performance`, `lifecycle`, `hdr`, and `errors` may be combined. These records describe Renderer state and queueing, not reconstructed image quality or compositor scanout.

`--session previous`, `oldest`, `previous-two`, or `all` applies only when the selected base log has MAKO Decky's rotated session files. A standalone Steam console log has no such history.

## 4. Restore normal settings

- Native Steam or Proton: restore `~/.local/bin/mako-launch %command%`.
- Direct command: close the terminal and use the normal command next time.
- Heroic or Flatpak: remove the two diagnostics variables while keeping the normal activation and overrides.

Published builds keep presentation diagnostics off when `MAKO_PRESENT_DIAGNOSTICS` is absent.

## 5. Submit

Open the [MAKO diagnostic form][diagnostic-form], choose **MAKO Renderer (standalone/direct installation)**, and attach `MAKO-diagnostics.txt`. Answer **Unknown** instead of guessing. Keep screenshots, videos, and public discussion in the original issue, but send the diagnostic text through the form.

## Retention and cleanup

Standalone Renderer does not create or rotate a private diagnostics log. Steam owns its console logs; a `tee` capture remains until you remove it.

Small `runtime-state/` files are status records, not logs. Normal context teardown removes its own record and lock. Before the next process publishes its first primary context, one bounded non-recursive pass removes only unlocked stale MAKO runtime files from that exact configuration directory. Unrelated files, symlinks, held locks, uncertain ownership, and cleanup failures are preserved and never block game startup.

## If collection fails

- `mako-diagnostics: command not found`: reinstall the current archive into the same prefix and confirm `~/.local/bin/mako-diagnostics` exists. Profiles under `~/.config/mako-render/` are preserved.
- `Diagnostics log not found`: verify that the diagnostics variables reached the game, reproduce again, fully quit, and immediately rerun the report command.
- No `render layer active` line: submit the report; absence is useful loader evidence.
- The game no longer starts: remove the two temporary diagnostics variables and test the original standalone launch command.

[diagnostic-form]: https://docs.google.com/forms/d/e/1FAIpQLScSd9qgkYCq3Kbbc3_52k4_82iTmEqt3_FxOqGuxQ6FsjutgA/viewform
