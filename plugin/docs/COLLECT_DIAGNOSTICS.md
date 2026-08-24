# Collect MAKO Decky Diagnostics

This guide is for troubleshooting **MAKO Decky**, installed through Decky Loader. It covers the `mako-run` wrapper, private MAKO Renderer installation, and Flatpak extensions prepared from the **Flatpak Setup** screen.

Use this guide when **MAKO Decky** appears in Decky Loader and you normally launch games through `/home/deck/.local/bin/mako-run`. If you installed MAKO Renderer directly or built it from source and activate games with `mako-launch`, use the [standalone guide](https://github.com/eugeniosegala/MAKO/blob/main/engine/docs/COLLECT_DIAGNOSTICS.md) instead.

You will temporarily enable diagnostics for the affected game, reproduce the problem once, create `MAKO-diagnostics.txt` on the Desktop, restore the game's normal launch settings, and upload the report through the [MAKO diagnostic report form][diagnostic-form]. The form contains the short questions needed to understand the report, so you do not need to copy a separate template into the GitHub issue.

Please do not attach `MAKO-diagnostics.txt` directly to a public GitHub issue. Review it before uploading because it can contain usernames, game names, application IDs, ROM filenames, and filesystem paths. Remove personal path components you do not want to share, and never send passwords, account credentials, device serial numbers, licence keys, or `Lossless.dll`. Google sign-in is required to upload the file through the form.

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
2. Reproduce the problem once. Note what you did and, if possible, the approximate time it happened.
3. Fully exit the game. Do not merely suspend it.
4. Wait a few seconds for the game and emulator processes to close.

Do not start another diagnostics-enabled MAKO game before creating the report. The next diagnostic launch can replace the current private log.

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

Focused presets such as `startup`, `errors`, `adaptive`, `recovery`, `performance`, `layers`, or `hdr` are useful when the maintainer requests a follow-up report. Every preset retains process identity and swapchain context so interleaved helper, launcher, game, and overlay processes can be distinguished. The `adaptive` preset includes contiguous requested-policy pacing aggregates and target-clock state; these are scheduler measurements rather than compositor scanout timestamps. The `recovery` preset includes the effective application-present acquire budget, budget exhaustion, first-slow guards, quarantine, zero-wait drain probes, bounded native-only stabilization, terminal history warmup, generated-image pressure, pipeline bypasses, and render-fence budget misses. Recovery events identify their phase, elapsed recovery time, and generated-frame request, admission, and delivery counts where those values exist. The `performance` preset includes a phase breakdown when total presentation work crosses the configured slow-operation threshold. Do not substitute a focused preset for the first `all` report unless specifically asked.

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
- **`Diagnostics log not found`:** diagnostics did not reach the tested game. Recheck the setup for its launch type, reproduce the issue again, fully quit the game, and immediately rerun the report command.
- **The report contains no `render layer active` line:** send the report anyway. That absence is useful evidence that the Vulkan layer did not load.
- **An EmuDeck shortcut no longer starts:** restore its saved Target and Launch Options, confirm it starts normally, then repeat the temporary diagnostic setup without removing or reordering any original EmuDeck arguments.

[diagnostic-form]: https://docs.google.com/forms/d/e/1FAIpQLScSd9qgkYCq3Kbbc3_52k4_82iTmEqt3_FxOqGuxQ6FsjutgA/viewform
