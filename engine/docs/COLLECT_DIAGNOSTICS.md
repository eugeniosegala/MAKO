# Collect standalone MAKO Renderer diagnostics

This guide is for troubleshooting **MAKO Renderer** installed directly from an archive or source. These installations use `mako-ui`, `mako-cli`, and launch-scoped environment variables without MAKO Decky managing the game launch.

Use this guide when you installed `MAKO-Renderer-v<version>-linux.tar.xz` or ran `cmake --install`. If **MAKO Decky** is installed through Decky Loader and you normally use `/home/deck/.local/bin/mako-run`, use its [companion guide](https://github.com/eugeniosegala/MAKO/blob/main/plugin/docs/COLLECT_DIAGNOSTICS.md) instead.

The workflow is: enable diagnostics for the affected game, reproduce once, create `MAKO-diagnostics.txt` on the Desktop, restore normal launch settings, and upload the report through the [MAKO diagnostic report form][diagnostic-form].

Please do not attach `MAKO-diagnostics.txt` directly to a public GitHub issue. Review it before uploading because it can contain usernames, game names, application IDs, ROM filenames, and filesystem paths. Remove personal path components you do not want to share, and never send passwords, account credentials, device serial numbers, licence keys, or `Lossless.dll`. Google sign-in is required to upload the file through the form.

This process does not copy `Lossless.dll`, install packages, reset the device, or change operating-system files.

## Log and runtime-state retention

Standalone MAKO Renderer does not create or rotate a private diagnostics log. Steam console logs remain shared logs managed by Steam, while a terminal capture created with `tee` remains at the path chosen in the command until the user removes it. MAKO Decky's separate opt-in diagnostic capture retains at most five sessions as described in its companion guide.

The configuration directory's small `runtime-state/` records are not logs and exist whether presentation diagnostics are enabled or disabled. Normal Renderer context teardown removes only that context's record and liveness lock. After an abnormal exit, one bounded pass before the next Renderer process publishes its first primary context prunes unlocked stale pairs and orphaned regular files with recognized current or legacy MAKO runtime filenames only from the exact runtime-state directory derived from `MAKO_CONFIG`, `XDG_CONFIG_HOME`, or the user-home fallback. The pass does not repeat for setting changes, swapchain recreation, or later contexts in that process. Cleanup is non-recursive and best-effort: held locks, unrelated filenames, symlinks, non-regular or differently owned entries, inaccessible paths, uncertain filesystem results, and deletion failures are preserved and never prevent the game from starting.

## 1. Prepare the game

Validate the current configuration before testing:

```bash
mako-cli validate
```

If `mako-cli` is not on `PATH`, use `$HOME/.local/bin/mako-cli`. Fully close the game, then follow the setup that matches its normal launch method.

### Native Steam or Proton game

Temporarily replace the game's normal **Steam Properties > Launch Options** with:

```text
MAKO_PRESENT_DIAGNOSTICS=1 MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 ~/.local/bin/mako-launch %command%
```

### Direct terminal command

Start the application from a terminal with the diagnostics variables and save its complete session output:

```bash
MAKO_PRESENT_DIAGNOSTICS=1 \
MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 \
~/.local/bin/mako-launch your-game-command \
2>&1 | tee "$HOME/Desktop/MAKO-renderer-session.log"
```

Replace `your-game-command` with the normal executable and arguments.

### Existing Heroic or Flatpak setup

Keep the launch method, wrapper, and existing Flatpak overrides unchanged. Add these two diagnostic variables for the affected game only:

```text
MAKO_PRESENT_DIAGNOSTICS=1
MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25
```

Do not add the Decky `mako-run` wrapper to a standalone Renderer installation.

## 2. Reproduce the problem

1. Start the affected game through the same entry or command that normally shows the problem.
2. Reproduce the problem once. Note what you did and, if possible, the approximate time it happened.
3. Fully exit the game. Do not merely suspend it.
4. Wait a few seconds for the game, Wine, Proton, or emulator processes to close.

Create the report before starting another diagnostics-enabled standalone game. Standalone Steam console logs are shared logs and are not split into MAKO Decky's five-session private history, so another run can add unrelated lines.

## 3. Create the Desktop report

Switch to Desktop Mode or open a terminal, then use the command that matches the installation.

### User-local Renderer archive

```bash
"$HOME/.local/bin/mako-diagnostics" --lines 2000 all > "$HOME/Desktop/MAKO-diagnostics.txt" 2>&1
```

### System or source installation

```bash
mako-diagnostics --lines 2000 all > "$HOME/Desktop/MAKO-diagnostics.txt" 2>&1
```

For the direct terminal method, tell the helper to read the captured session:

```bash
mako-diagnostics --log "$HOME/Desktop/MAKO-renderer-session.log" --lines 2000 all > "$HOME/Desktop/MAKO-diagnostics.txt" 2>&1
```

Use the full `$HOME/.local/bin/mako-diagnostics` path in that command when it is not on `PATH`.

The helper prefers a MAKO Decky private log when one exists. Otherwise, it selects the newest native or Flatpak Steam console log. The `all` preset keeps the most recent 2,000 relevant MAKO, Vulkan-loader, and compositor lines rather than copying the complete source log. The default session is always the latest. When `--log PATH` names the base of a five-session MAKO Decky history, `--session previous`, `--session oldest`, `--session previous-two`, or `--session all` selects `PATH.1`, `PATH.4`, the two immediately previous sessions, or every available retained session respectively; standalone Steam console logs do not create those rotated files.

Open the **Desktop** folder and confirm that `MAKO-diagnostics.txt` exists. The default `all` preset is best for an initial report; maintainers may request `startup`, `errors`, `scaling`, `adaptive`, `recovery`, `performance`, `layers`, or `hdr` for follow-up. Every preset retains process and swapchain identity, including the bounded backend-stabilization phase after a known swapchain replacement. `scaling` records extent, format, queue, activation, fallback, capacity, and transition decisions but not reconstructed-image quality; `inactive_reason=application-extent-override-no-source-presentation-split`, `source_presentation_split=0`, or `active=0` means the scaler did not run. `adaptive` reports scheduler measurements, not scanout timestamps. `recovery` records acquire budgets, pressure classification, native relief, bounded probes, backoff, stabilization, history warm-up, and request/admission/delivery counts. `performance` adds phase breakdowns only when the configured slow-operation threshold is crossed.

## 4. Restore normal launch settings

After creating the report:

- **Native Steam or Proton:** restore the normal standalone launch option, `~/.local/bin/mako-launch %command%`.
- **Direct command:** close the terminal session. Start the game with its normal command next time.
- **Heroic or Flatpak:** remove `MAKO_PRESENT_DIAGNOSTICS` and `MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS`. Keep the normal Renderer activation, wrapper, arguments, and Flatpak overrides.

Published Renderer builds keep presentation diagnostics off after the temporary variables are removed.

## 5. Submit the report

Open the [MAKO diagnostic report form][diagnostic-form], choose **MAKO Renderer (standalone/direct installation)**, answer the remaining short questions, and attach the `MAKO-diagnostics.txt` file from the Desktop. It is fine to write **Unknown** rather than guessing.

Keep screenshots, videos, and discussion in the original GitHub issue, but use the form for the diagnostic text file so it is not posted publicly.

## If the report command fails

- **`mako-diagnostics: command not found`:** reinstall the current MAKO Renderer archive into the same prefix, confirm `~/.local/bin/mako-diagnostics` exists, and repeat the report command. Your configuration remains under `~/.config/mako-render/`.

- **`Diagnostics log not found`:** diagnostics did not reach the tested game. Recheck the launch environment, reproduce the issue again, fully quit the game, and immediately rerun the report command.
- **The report contains no `render layer active` line:** submit it anyway. The absence is useful evidence that the Vulkan layer did not load.
- **The game no longer starts:** remove only the two temporary diagnostics variables and confirm the original standalone launch command still works.

[diagnostic-form]: https://docs.google.com/forms/d/e/1FAIpQLScSd9qgkYCq3Kbbc3_52k4_82iTmEqt3_FxOqGuxQ6FsjutgA/viewform
