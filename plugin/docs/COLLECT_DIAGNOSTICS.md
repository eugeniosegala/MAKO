# Collect MAKO Decky Diagnostics

Use this guide when MAKO Decky opens and games use `mako-run`. For `mako-launch`, use the [standalone guide](../../engine/docs/COLLECT_DIAGNOSTICS.md).

## 1. Enable logs

Confirm **MAKO Renderer is installed** in MAKO Decky. Fully close the game and save its current launch settings so you can restore them afterward.

**Steam or Proton:** temporarily use these Launch Options:

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

If MAKO Decky's **Wrapper path for this device** uses another home directory, replace `/home/deck` in these commands with that directory.

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

For HDR comparisons, the report retains `HDR generation precision` records with requested Flow Scale and actual motion dimensions. These show whether Reduced HDR Precision changed HDR10 motion processing.

## 4. Restore your settings

Restore the saved Launch Options and any changed Target, or remove the two diagnostics environment variables from Heroic/Lutris.

## 5. Send the report

Review `MAKO-diagnostics.txt` and remove personal information. Upload it through the [diagnostic form](https://docs.google.com/forms/d/e/1FAIpQLScSd9qgkYCq3Kbbc3_52k4_82iTmEqt3_FxOqGuxQ6FsjutgA/viewform), choosing **MAKO Decky (Decky Loader plugin)**. Answer **Unknown** when unsure. Do not post the log publicly or attach `Lossless.dll`.
