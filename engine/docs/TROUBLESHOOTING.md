# Troubleshooting
This page documents common issues, known incompatibilities and contains a guide to help you create a helpful bug report.

Before reporting a bug, please read through the following sections to see if your issue is already addressed.

### Basic Troubleshooting Steps
If MAKO Renderer does not seem to be doing *anything*:
- Ensure the game you are trying to run is using Vulkan (not OpenGL).
- For a 32-bit game, ensure both `lib32/libmako-render.so` and
  `VkLayer_MAKO_render.x86.json` were installed from the Linux
  archive. The manifest must report `"library_arch": "32"`.
- Install `vulkan-tools` and run
  `ENABLE_MAKO=1 vulkaninfo | grep -i VK_LAYER_MAKO_render`.
  - If there is no output revisit the installation steps.
- Launch the game with the environment variable `VK_LOADER_DEBUG=layer` set.
  - Look for `VK_LAYER_MAKO_render` and the
    `mako: render layer active` build marker between `<Loader>` and `<Device>`.
  - If you can't find any, try again using `MAKO_ENV=1`.
    - If it still doesn't show up, you may be running in flatpak.
    - If it does show up, then the `active_in` property of your profile is likely misconfigured. Reconfigure it, then try again without `MAKO_ENV=1`.
- Check for warnings/errors from MAKO Renderer in the terminal/log output. These will often give clues as to what is going wrong.
- If there are no errors/warnings and you have gone through all above steps, then move onto the next section.

If MAKO Renderer is loaded, but frame generation is not working:
- (When using `pacing_mode = none`): Disable VRR.
- (When using `pacing_mode = none`): Explicitly enable V-Sync in your game settings.
- (When using `pacing_mode = none` on Gamescope/SteamDeck): Set `ENABLE_GAMESCOPE_WSI=0`.
- (When using `pacing_mode = none` on Wayland): Disable tearing control & direct passthrough in your compositor
- (When using `pacing_mode = none` on Wayland): Try running in windowed mode.
- Disable in-game upscaling options (e.g. DLSS, FSR, etc).
- Disable other Vulkan layers (e.g. VkBasalt, MangoHud)

If games do not open at all with MAKO Renderer enabled for them (stuck at black screen):
- Ensure you configured the correct `gpu` for this profile, in case you have multiple GPUs and/or drivers (mako-ui will show all available GPUs in a dropdown), MAKO Renderer might be defaulting to a different one than the game is using

Should none of the above help, please proceed to the bug reporting section.

### Performance Overlays
If you are using performance overlays like Steam's built-in overlay, there is a good chance that they will not show the correct framerate.

This is a known limitation of Vulkan layers and without directly working with the overlay developers, there is little that can be done to fix this.

### Opening a Bug Report
When opening a bug report, please include the following information to help us diagnose and fix the issue:
- A detailed description of the issue you are experiencing.
- What system you are running on (OS, GPU, drivers, etc).
- The game you are trying to run (and through what platform, e.g. Steam Proton, native Linux, etc).
- The relevant section of your MAKO Renderer configuration file.

Ideally, also include a log file with the environment variables `VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` and `VK_LOADER_DEBUG=all` set. You might need to install the Vulkan validation layers package for your distribution to do this.

If you're running the game through Steam, the log file is located at `~/.steam/steam/logs/console-linux.txt`. Please clear it before launching the game to ensure it only contains relevant information.

### Diagnosing presentation stalls

MAKO Renderer can log Vulkan presentation operations that take longer than expected. This is intended for
targeted debugging and is disabled by default, so it has no effect on normal runs.

Add `MAKO_PRESENT_DIAGNOSTICS=1` before the normal launch command. Operations taking at least 20 ms are written to
Steam's `~/.steam/steam/logs/console-linux.txt` log. Override the threshold in milliseconds with
`MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS`.

When using MAKO Decky, keep its wrapper in the launch option:

```bash
MAKO_PRESENT_DIAGNOSTICS=1 MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 ~/.local/bin/mako-run %command%
```

To test recovery from a stalled generated-image acquisition, add an opt-in timeout in milliseconds. If the timeout is
reached, MAKO Renderer skips the remaining generated frames for that presentation and safely presents the original game
frame. Following attempts probe image availability before scheduling output passes, so a Gamescope overlay cannot impose
the full timeout or waste GPU work on generated frames that cannot be presented. A shared history-only pre-pass still
updates the model's temporal features for every real frame instead of leaving older slots untouched.
If zero-timeout probes keep missing
the release window, the layer makes one bounded reacquisition attempt per second after the first second of fallback.
The real game frames continue updating the two source images. Fixed mode resumes automatically as soon as a probe
succeeds. By default, Adaptive mode first presents three real frames while repopulating its deepest temporal-history
ring, then attempts generated output again. The same three-frame warm-up runs when an Adaptive context first starts.
When Adaptive is capped at 2x and has already validated that level, a short hard gameplay hitch of up to 250 ms uses
the same three-frame history refresh while retaining 2x. Longer interruptions retain the full menu/focus recovery.
For example:

```bash
MAKO_PRESENT_ACQUIRE_TIMEOUT_MS=25 MAKO_PRESENT_DIAGNOSTICS=1 MAKO_PRESENT_DIAGNOSTICS_THRESHOLD_MS=25 ~/.local/bin/mako-run %command%
```

This recovery is experimental and disabled when `MAKO_PRESENT_ACQUIRE_TIMEOUT_MS` is absent or set to `0`.
With diagnostics enabled, `skip-generated-frames` reports whether the fallback followed the initial timeout, a
non-blocking retry, or a periodic `bounded-retry`. Its `backend_work` field records whether full output work was already
`scheduled` or only the temporal `history-only` pre-pass ran. Expected repeated non-blocking failures are aggregated;
Fixed-mode recovery uses `resume-generated-frames`. Adaptive recovery reports `generated-image-recovered`, followed by
three `history-warmup` entries with `reason=recovery`. Adaptive startup uses the same entries with `reason=startup`.
The recovery record includes the total number of frames whose output work was bypassed.

Every successful Adaptive recovery probe stays in the current swapchain and performs the normal three-frame history
warm-up. Diagnostics report `swapchain-recreation-suppressed reason=in-place-only` and
`generated-image-recovered recovery_action=in-place`. MAKO Renderer does not return `VK_ERROR_OUT_OF_DATE_KHR` to apply a
setting or recovery decision; this avoids game/Wine-specific swapchain rebuild failures. Fixed mode resumes in place
without the Adaptive history-policy reset.

For a normal non-isolated installation, place the same environment variables before its usual launch command.

Clear the Steam log before reproducing the problem. After reproducing it, extract the most recent diagnostic entries
with:

```bash
grep -aE 'mako: present diagnostics: operation=(runtime-transition-pending|runtime-state-applied|fixed-plan|adaptive-plan|adaptive-discontinuity|adaptive-stabilization|adaptive-gameplay-hitch|adaptive-fast-cadence-burst|adaptive-stable-cadence|adaptive-ramp|adaptive-recovery-resume-scheduled|adaptive-load-shed|adaptive-rescue|adaptive-bridge|adaptive-probe-aborted|adaptive-rearm|skip-generated-frames|generated-image-recovered|swapchain-recreation-suppressed|swapchain-context-create|swapchain-context-destroy)' ~/.steam/steam/logs/console-linux.txt | tail -n 800
```

`adaptive-stabilization` and `adaptive-ramp` show the normal restart sequence. `adaptive-load-shed` means a tested
multiplier reduced useful throughput and was rolled back. `adaptive-bridge` is the single bounded test used when the
first generated-frame step may have encountered a Gamescope cadence divisor. Its accepted/rejected record gives the
measured result. `adaptive-rearm-scheduled` means another probe will wait at least 15 seconds and two stable seconds;
`adaptive-rearm-ready` confirms those conditions were met. `swapchain-recreation-suppressed reason=first-recovery`
confirms an isolated recovery remained in-place; `reason=cooldown` confirms the separate cross-context guard prevented
a repeated recreation request.
`adaptive-fast-cadence-burst` means an implausibly short DX12/Vulkan presentation interval was excluded from the base
rate. Its aggregated frame counts and matching completion record show how long policy evaluation remained paused.
`adaptive-gameplay-hitch-recovery` means a validated 2x Adaptive policy kept its level through a short gameplay hitch
and refreshed temporal history instead of entering the longer menu/focus recovery path.
Every presentation-diagnostic record includes a `context=<ID>` field. Use it to separate concurrent or replacement
swapchains before comparing ramp, recovery, and presentation events; records with different context IDs may describe
different windows or an old context being destroyed while its replacement starts.
For HDR10, `HDR10 transport: mode=packed-10-bit` confirms that both Vulkan devices accepted the compact exchange path.
`nominal_bytes_saved` is the exact reduction across the private transport images for that context; it is not a claimed
whole-engine VRAM reduction. `mode=rgba16f` with either support field at `0` means the engine retained the validated
float transport automatically. The model and its temporal working images remain 16-bit float in both cases.
`activation_source=gamescope-app-colorspace` is the preferred Gamescope signal;
`gamescope-app-hdr-metadata` is equivalent positive application evidence. `output_hdr=1` only confirms that the
Gamescope output can expose HDR formats; it is not evidence that the application selected HDR and never promotes a
swapchain by itself. If application feedback remains unset, a Gamescope-normalized high-precision swapchain remains in
real-frame passthrough until positive application evidence arrives. Ordinary 8-bit SDR is never promoted.
For a live-compatible in-game configuration change, `runtime-state-applied transition=live` records the requested state
revision and the active mode. If a change needs different private GPU resources or HDR encoding,
`runtime-transition-pending action=wait-for-natural-swapchain-recreation` records that it was deliberately not forced;
the next naturally-created context reports `runtime-state-applied` with the same or a newer `state_revision`. Its
`adaptive`, `target_fps`, multiplier, Smooth Cadence, and `hdr` fields are the authoritative engine state; a Decky UI
value alone does not prove the active context changed.

Disable the diagnostic variables after collecting the trace. Remove the acquire-timeout and recreation variables too
if you do not want to continue testing the recovery path.
