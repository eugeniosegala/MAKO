# Collect MAKO Decky Diagnostics

Use this guide when **MAKO Decky** opens in Decky Loader and games launch through `mako-run`. Standalone `mako-launch` installations use the [MAKO Renderer guide](https://github.com/eugeniosegala/MAKO/blob/main/engine/docs/COLLECT_DIAGNOSTICS.md).

You will enable diagnostics temporarily, reproduce the problem, create a Desktop report, restore normal launch settings, and upload it through the [MAKO diagnostic report form][diagnostic-form]. MAKO retains the latest five diagnostics-enabled game sessions.

Do not attach `MAKO-diagnostics.txt` to a public issue. Review it for usernames, game names, application IDs, ROM names, and paths. Remove details you do not want to share, and never send passwords, credentials, serial numbers, licence keys, or `Lossless.dll`. Google sign-in is required for the form upload.

The process does not copy `Lossless.dll`, install system packages, factory reset the device, or change SteamOS system files.

These commands use the standard SteamOS home directory, `/home/deck`. If MAKO Decky shows a different **Wrapper path for this device**, use that home directory in the wrapper, diagnostics-helper, and Desktop paths below.

## 1. Prepare the game

Open MAKO Decky and confirm that **MAKO Renderer is installed**. Fully close the game before changing its launch settings, then follow exactly one setup below.

### Native Steam or Proton game

Temporarily replace the game's normal **Steam Properties > Launch Options** with:

```text
MAKO_PRESENT_DIAGNOSTICS=1 MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 /home/deck/.local/bin/mako-run %command%
```

### Heroic game

Keep the game's normal **Wrapper** and **Arguments** fields unchanged. In that game's Heroic settings, add these two environment variables:

```text
MAKO_PRESENT_DIAGNOSTICS=1
MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25
```

Do not add `%command%` to Heroic.

### EmuDeck Flatpak shortcut

An EmuDeck Flatpak shortcut keeps its emulator ID, ROM path, and emulator flags in **Launch Options**. Save the current **Target** and **Launch Options** somewhere safe before editing them.

Temporarily configure the shortcut as follows:

| Field | Temporary diagnostic value |
| --- | --- |
| **Target** | `/usr/bin/env` |
| **Start In** | Keep `/usr/bin` |
| **Launch Options** | Add the prefix below before every existing EmuDeck argument |

Add this at the beginning of the existing **Launch Options**:

```text
MAKO_PRESENT_DIAGNOSTICS=1 MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 /home/deck/.local/bin/mako-run
```

Leave a space after `mako-run`, then keep the complete original EmuDeck launch options after it. Do not add `%command%`, remove the emulator ID, alter the ROM path, or change any existing flags.

If the emulator is a native application or AppImage instead of a Flatpak, use the **Native Steam or Proton game** method above.

## 2. Reproduce the problem

1. Start the affected game using the same Steam or Heroic entry that normally shows the problem.
2. Reproduce the problem. Note what you did and, if possible, the approximate time it happened.
3. Fully exit the game. Do not merely suspend it.
4. Wait a few seconds for the game and emulator processes to close.

Each diagnostics-enabled MAKO Decky launch starts a fresh private session log. The latest session remains `present-diagnostics.log`, and the four previous sessions become `present-diagnostics.log.1` through `present-diagnostics.log.4`, with `.4` the oldest retained session. Starting a sixth diagnostics-enabled game replaces the oldest retained session. Fully exit one game before starting the next so each log represents one completed run.

## 3. Create the Desktop report

1. Switch the Steam Deck or Steam Machine to **Desktop Mode**.
2. Open **Konsole**.
3. Paste this complete command and press Enter:

    ```bash
    /home/deck/.local/bin/mako-diagnostics --lines 2000 all > /home/deck/Desktop/MAKO-diagnostics.txt 2>&1
    ```

    Konsole normally shows no report text because the command sends all output to the Desktop file.

4. Open the **Desktop** folder in Dolphin. The new report is:

    ```text
    MAKO-diagnostics.txt
    ```

The `all` preset is the best first report when the cause is unknown. It does not copy the entire Steam log: it filters for relevant MAKO, Vulkan-loader, and Gamescope lines. `--lines 2000` keeps the most recent 2,000 matching lines, which normally provides enough startup and failure context without creating an unnecessarily large attachment.

The standard command deliberately omits `--session`, so it always reports the latest session. When the maintainer asks for an earlier retained run, use one of these commands:

```bash
# Immediately previous session
/home/deck/.local/bin/mako-diagnostics --session previous --lines 2000 all > /home/deck/Desktop/MAKO-diagnostics-previous.txt 2>&1

# Oldest of the five retained sessions
/home/deck/.local/bin/mako-diagnostics --session oldest --lines 2000 all > /home/deck/Desktop/MAKO-diagnostics-oldest.txt 2>&1

# Two immediately previous sessions, ordered chronologically
/home/deck/.local/bin/mako-diagnostics --session previous-two --lines 2000 all > /home/deck/Desktop/MAKO-diagnostics-previous-two.txt 2>&1

# Every available retained session, ordered from oldest to latest
/home/deck/.local/bin/mako-diagnostics --session all --lines 2000 all > /home/deck/Desktop/MAKO-diagnostics-all.txt 2>&1
```

In these commands, `--session all` selects the retained sessions while the final `all` selects the diagnostic preset. A combined report applies `--lines 2000` separately to every available session, includes a source and session label for each one, and preserves chronological session order. Until all five slots have been recorded, `--session all` includes only the sessions that are available.

Focused presets include `startup`, `errors`, `scaling`, `adaptive`, `recovery`, `performance`, `layers`, and `hdr`. Use them only for a requested follow-up; the first report should remain `all`. Presets retain process and swapchain identity. `scaling` records policy, extents, format/queue selection, live recreation, capacity, and failures, not image quality; `active=0`, `source_presentation_split=0`, or an `inactive_reason` other than `none` means the spatial model did not run. `adaptive` records scheduler state rather than scanout timestamps, `recovery` records ordered recovery phases and delivery counts, and `performance` adds phase timing for slow presents.

## 4. Restore normal launch settings

After creating the report:

- **Native Steam or Proton:** restore the normal launch option:

    ```text
    /home/deck/.local/bin/mako-run %command%
    ```

- **Heroic:** remove `MAKO_PRESENT_DIAGNOSTICS` and `MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS` from the game's environment. Keep the normal Wrapper and Arguments.
- **EmuDeck Flatpak:** restore the exact original **Target** and **Launch Options** saved before testing.

All builds keep diagnostics off after these temporary settings are removed. Local development ZIPs and direct `dev:*` deployments also require explicit opt-in so synchronous log traffic cannot distort performance testing.

## 5. Submit the report

Open the [MAKO diagnostic report form][diagnostic-form], choose **MAKO Decky (Decky Loader plugin)**, answer the remaining short questions, and attach `/home/deck/Desktop/MAKO-diagnostics.txt`. It is fine to select or write **Unknown** when you do not know an answer. Submit the form once; the maintainer can request a more specific follow-up if one is needed.

Keep screenshots, videos, and discussion in the original GitHub issue, but use the form for the diagnostic text file so it is not posted publicly.

## If the report command fails

- **`mako-diagnostics: No such file or directory`:** open MAKO Decky and select **Install MAKO Renderer**, then try again.
- **`Diagnostics log not found`:** diagnostics did not reach the tested game, or the requested earlier session has already rotated out. Recheck the setup for its launch type and the requested `--session`, reproduce the issue again if necessary, and fully quit the game before rerunning the report command.
- **The report contains no `render layer active` line:** send the report anyway. That absence is useful evidence that the Vulkan layer did not load.
- **An EmuDeck shortcut no longer starts:** restore its saved Target and Launch Options, confirm it starts normally, then repeat the temporary diagnostic setup without removing or reordering any original EmuDeck arguments.

[diagnostic-form]: https://docs.google.com/forms/d/e/1FAIpQLScSd9qgkYCq3Kbbc3_52k4_82iTmEqt3_FxOqGuxQ6FsjutgA/viewform
