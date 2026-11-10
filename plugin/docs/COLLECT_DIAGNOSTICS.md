# Collect MAKO Decky Diagnostics

Use this guide when MAKO Decky opens and games use `mako-run`. For `mako-launch`, use the [standalone guide](../../engine/docs/COLLECT_DIAGNOSTICS.md).

## 1. Enable logs

Confirm **MAKO Renderer is installed** in MAKO Decky. Fully close the game and save its current launch settings so you can restore them afterward.

**Steam or Proton:** prepend the two diagnostics variables to your existing Launch Options, preserving any other settings. For a basic launch, the result is:

```text
MAKO_PRESENT_DIAGNOSTICS=1 MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 /home/deck/.local/bin/mako-run %command%
```

**Heroic or Lutris:** keep the working Wrapper or Command prefix and add these per-game environment variables:

```text
MAKO_PRESENT_DIAGNOSTICS=1
MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25
```

**EmuDeck Flatpak shortcut:** save the original **Target** and **Launch Options**. Set Target to `/usr/bin/env`, keep Start In as `/usr/bin`, and prepend this to the original Launch Options, followed by a space:

```text
MAKO_PRESENT_DIAGNOSTICS=1 MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 /home/deck/.local/bin/mako-run
```

Keep all original emulator arguments and the ROM path. Do not add `%command%` here. Native emulators and AppImages use the Steam method above.

If MAKO Decky's **Wrapper path for this device** uses another home directory, replace `/home/deck` in all commands in this guide with that directory.

## 2. Reproduce the problem

Start the game, reproduce the issue once and note what happened. Fully exit the game and wait a few seconds.

## 3. Save the report

Switch to **Desktop Mode**, open **Konsole**, paste this command and press Enter:

```bash
/home/deck/.local/bin/mako-diagnostics --lines 5000 all > /home/deck/Desktop/MAKO-diagnostics.txt 2>&1
```

Open your **Desktop** folder to find `MAKO-diagnostics.txt`. Konsole normally prints nothing.

If the command is missing, select **Install MAKO Renderer** in Decky and retry. If no log is found, check the temporary launch settings and repeat the test.

For a slowdown after a long session, replace `--lines 5000` with `--lines 200000`. Collect before starting another game.

With diagnostics enabled, `mako-run` retains ten launch sessions: the current `~/.config/mako-render/present-diagnostics.log` and nine older logs numbered `.1` through `.9`. Add `--session previous` to collect the preceding run, `--session oldest` for `.9`, or `--session all` to combine the available retained sessions.

Collect from the original session log, not an older filtered report, which may have omitted error details. The report does not include the separate Gamescope system journal; support may request it if the game log is insufficient.

With `PROTON_LOG=1` in Launch Options, Proton writes the game's output, including MAKO Renderer records, to its own `steam-<game ID>.log` in your home directory or `PROTON_LOG_DIR` instead. The report then shows a `proton log:` line naming that file; send that file as well, or remove `PROTON_LOG` and collect again.

If support asks for Vulkan loader details, also put `VK_LOADER_DEBUG=error,warn,layer` at the front of Launch Options. The report then includes the loader's layer search and load errors, including from loaders bundled in AppImages. Use `--lines 200000`, because the loader prints these lines at startup.

For comparisons, collect a separate report after each run and note which settings changed. Use the same scene and game settings where possible. For HDR sharpening comparisons, also note the game resolution and display resolution. For VRR comparisons, record the VRR setting, display refresh rate, desktop or Gaming Mode session, and Gamescope version if used.

For timing-field meanings and the limits of log-based comparisons, see [Interpreting presentation records](../../engine/docs/WSI-ISOLATION.md#interpreting-presentation-records).

For Zink pacing comparisons, also note Fixed or Fractional Adaptive mode, Smooth Cadence, Real Frame Priority and any FPS caps. The report retains automatic-cap changes and FIFO handoff/drain records; see [Zink FIFO pacing](../../engine/docs/ADAPTIVE-VALIDATION.md#zink-fifo-pacing) for their meaning.

If the optional Lossless Scaling model cannot initialize, MAKO tries MAKO FG. See [model selection and fallback](../../engine/docs/MAKO-FG.md) for details.

## 4. Restore your settings

Restore the saved Launch Options and any changed Target, or remove the two diagnostics environment variables from Heroic/Lutris.

## 5. Send the report

Review `MAKO-diagnostics.txt` and remove personal information. Upload it through the [diagnostic form](https://docs.google.com/forms/d/e/1FAIpQLScSd9qgkYCq3Kbbc3_52k4_82iTmEqt3_FxOqGuxQ6FsjutgA/viewform), choosing **MAKO Decky (Decky Loader plugin)**. Answer **Unknown** when unsure. Do not post the log publicly or attach `Lossless.dll`.

The report alone is enough for most problems. For a visual problem, a short video or photo of the screen can also help. Support can reproduce Frame Generation artifacts offline from [recorded test frames](../../engine/docs/MAKO-FG.md#record-test-frames), which hold the game's own frames before MAKO processes them; record them only if support asks.

## GPU Headroom evidence

Compare the same scene with GPU Headroom Off and On. Collect a report after each run and note the reserve, Fixed/Adaptive mode, multiplier ceiling and any game FPS cap. Brief utilization spikes can still reach 100%; the readings cover the whole GPU and do not measure per-game GPU time or power. See [presentation records](../../engine/docs/WSI-ISOLATION.md#interpreting-presentation-records) for the retained fields and controller states.
