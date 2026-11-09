# Collect MAKO Renderer Diagnostics

For standalone installations using `mako-launch`. If you use `mako-run`, follow the [MAKO Decky guide](../../plugin/docs/COLLECT_DIAGNOSTICS.md).

## 1. Enable logs

Fully close the game and save its current launch settings so you can restore them afterward.

**Steam or Proton:** prepend the two diagnostics variables to your existing Launch Options, preserving any profile, shader, or other settings. For a basic launch, the result is:

```text
MAKO_PRESENT_DIAGNOSTICS=1 MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 ~/.local/bin/mako-launch %command%
```

For a system installation, use your normal `mako-launch` path.

**Heroic, Lutris or Flatpak:** keep your working setup and add these per-game environment variables:

```text
MAKO_PRESENT_DIAGNOSTICS=1
MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25
```

**Terminal launch:** capture the game output:

```bash
MAKO_PRESENT_DIAGNOSTICS=1 MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 \
    mako-launch your-game-command 2>&1 | tee "$HOME/Desktop/MAKO-renderer-session.log"
```

Replace `your-game-command` with your normal game command.

## 2. Reproduce the problem

Start the game, reproduce the issue once, note what happened and fully exit. Collect the report before another test run.

## 3. Save the report

In Desktop Mode, open Konsole and run:

```bash
mako-diagnostics --lines 5000 all > "$HOME/Desktop/MAKO-diagnostics.txt" 2>&1
```

If the command is not found, try `~/.local/bin/mako-diagnostics` instead. For the terminal capture above, add `--log "$HOME/Desktop/MAKO-renderer-session.log"` before `--lines`.

The file appears on your Desktop; Konsole normally prints nothing. If no log is found, check the temporary launch settings and repeat the test.

Check the report's `MAKO Renderer diagnostics source:` path. Automatic selection prefers an existing MAKO Decky diagnostics log, even for a standalone run. If it selected an older Decky session, use `--log "$HOME/.steam/steam/logs/console-linux.txt"` for native Steam, or pass the path to your terminal capture. Heroic and Lutris users should save the launcher's game output and pass that file with `--log`.

For a slowdown after a long session, replace `--lines 5000` with `--lines 200000`. Collect before starting another game.

Collect from the original session log, not an older filtered report, which may have omitted error details. The report does not include the separate Gamescope system journal; support may request it if the game log is insufficient.

With `PROTON_LOG=1` in Launch Options, Proton writes the game's output, including MAKO Renderer records, to its own `steam-<game ID>.log` in your home directory or `PROTON_LOG_DIR` instead. The report then shows a `proton log:` line naming that file; send that file as well, or remove `PROTON_LOG` and collect again.

If support asks for Vulkan loader details, also put `VK_LOADER_DEBUG=error,warn,layer` at the front of Launch Options. The report then includes the loader's layer search and load errors, including from loaders bundled in AppImages. Use `--lines 200000`, because the loader prints these lines at startup.

For comparisons, collect a separate report after each run and note which settings changed. Use the same scene and game settings where possible. For HDR sharpening comparisons, also note the game resolution and display resolution. For VRR comparisons, record the VRR setting, display refresh rate, desktop or Gaming Mode session, and Gamescope version if used.

For timing-field meanings and the limits of log-based comparisons, see [Interpreting presentation records](WSI-ISOLATION.md#interpreting-presentation-records).

If the optional Lossless Scaling model cannot initialize, MAKO tries MAKO FG. See [model selection and fallback](MAKO-FG.md) for details.

## 4. Restore your settings

Restore the original launch settings and remove the two temporary diagnostics variables.

## 5. Send the report

Review `MAKO-diagnostics.txt` and remove personal information. Upload it through the [diagnostic form](https://docs.google.com/forms/d/e/1FAIpQLScSd9qgkYCq3Kbbc3_52k4_82iTmEqt3_FxOqGuxQ6FsjutgA/viewform), choosing **MAKO Renderer (standalone/direct installation)**. Answer **Unknown** when unsure. Do not post the log publicly or attach `Lossless.dll`.

The report alone is enough for most problems. For a visual problem, a short video or photo of the screen can also help. Support can reproduce Frame Generation artifacts offline from [recorded test frames](MAKO-FG.md#record-test-frames), which hold the game's own frames before MAKO processes them; record them only if support asks.

## GPU Headroom evidence

Compare the same scene with GPU Headroom Off and On. Collect a report after each run and note the reserve, Fixed/Adaptive mode, multiplier ceiling and any game FPS cap. Brief utilization spikes can still reach 100%; the readings cover the whole GPU and do not measure per-game GPU time or power. See [presentation records](WSI-ISOLATION.md#interpreting-presentation-records) for the retained fields and controller states.
