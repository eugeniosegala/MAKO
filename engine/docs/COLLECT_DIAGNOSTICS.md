# Collect Standalone MAKO Renderer Diagnostics

This guide is for troubleshooting **MAKO Renderer** installed directly from an archive or source. These installations use `mako-ui`, `mako-cli`, and launch-scoped environment variables without MAKO Decky managing the game launch.

Use this guide when you installed `MAKO-Renderer-v<version>-linux.tar.xz` or ran `cmake --install`. If **MAKO Decky** is installed through Decky Loader and you normally use `/home/deck/.local/bin/mako-run`, use its [companion guide](https://github.com/eugeniosegala/MAKO/blob/main/plugin/docs/COLLECT_DIAGNOSTICS.md) instead.

You will temporarily enable diagnostics for the affected game, reproduce the problem once, create `MAKO-diagnostics.txt` on the Desktop, restore the normal launch settings, and upload the report through the [MAKO diagnostic report form][diagnostic-form]. The form contains the short questions needed to understand the report.

Please do not attach `MAKO-diagnostics.txt` directly to a public GitHub issue. Review it before uploading because it can contain usernames, game names, application IDs, ROM filenames, and filesystem paths. Remove personal path components you do not want to share, and never send passwords, account credentials, device serial numbers, licence keys, or `Lossless.dll`. Google sign-in is required to upload the file through the form.

The process does not copy `Lossless.dll`, install system packages, factory reset the device, or change operating-system files.

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

Create the report before starting another diagnostics-enabled standalone game. Standalone Steam console logs are shared logs and are not split into MAKO Decky's three-session private history, so another run can add unrelated lines.

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

The helper prefers a MAKO Decky private log when one exists. Otherwise, it selects the newest native or Flatpak Steam console log. The `all` preset keeps the most recent 2,000 relevant MAKO, Vulkan-loader, and compositor lines rather than copying the complete source log. The default session is always the latest. When `--log PATH` names the base of a three-session MAKO Decky history, `--session previous`, `--session oldest`, `--session previous-two`, or `--session all` selects `PATH.1`, `PATH.2`, both earlier sessions, or every available retained session respectively; standalone Steam console logs do not create those rotated files.

Open the **Desktop** folder and confirm that `MAKO-diagnostics.txt` exists. Focused presets such as `startup`, `errors`, `scaling`, `adaptive`, `recovery`, `performance`, `layers`, or `hdr` are for follow-up reports requested by the maintainer. Every preset retains process identity, swapchain context, and low-frequency spatial-scaling policy records so interleaved helper, launcher, game, and overlay processes can be distinguished and combined scaling/frame-generation sessions remain visible. The `scaling` preset adds fixed/variable surface mode, advertised, actual, and selected source/presentation extents, capability-policy revisions and query generation, stable inactive reason, actual source/presentation split, format/shape/queue selection, variable-surface feedback-guard decisions, activation, scaling-only backend decisions, HDR feedback, pending and delivered live-recreation requests, requested versus available generated-frame capacity, and swapchain failures; it records policy selection and failures, not reconstructed-image quality. `inactive_reason=application-extent-override-no-source-presentation-split`, `source_presentation_split=0`, or `active=0` means the spatial model did not run. The `adaptive` preset includes contiguous requested-policy pacing aggregates and target-clock state; these are scheduler measurements rather than compositor scanout timestamps. The `recovery` preset includes the effective application-present acquire budget, the one-shot total-versus-longest acquisition classification split, budget exhaustion, first-slow guards and native relief, quarantine, bounded single-image drain probes, retry backoff, native-only stabilization, terminal history warmup, generated-image pressure, pipeline bypasses, and render-fence budget misses. Recovery events identify their phase, elapsed recovery time, and generated-frame request, admission, and delivery counts where those values exist. The `performance` preset includes a phase breakdown when total presentation work crosses the configured slow-operation threshold.

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
