# Collect MAKO Renderer Diagnostics

For standalone installations using `mako-launch`. If you use `mako-run`, follow the [MAKO Decky guide](../../plugin/docs/COLLECT_DIAGNOSTICS.md).

## 1. Enable logs

Fully close the game and save its current launch settings so you can restore them afterward.

**Steam or Proton:** temporarily use these Launch Options:

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
mako-diagnostics --lines 2000 all > "$HOME/Desktop/MAKO-diagnostics.txt" 2>&1
```

If the command is not found, try `~/.local/bin/mako-diagnostics` instead. For the terminal capture above, add `--log "$HOME/Desktop/MAKO-renderer-session.log"` before `--lines`.

The file appears on your Desktop; Konsole normally prints nothing. If no log is found, check the temporary launch settings and repeat the test.

`--lines N` keeps the last N matching trace lines per selected session, plus up to 96 earlier context records. The separate context section preserves build/process/GPU identity, first/latest swapchain and applied-state snapshots, and first/latest health samples within independent bounded groups; records already in the recent trace are not repeated. The collection header reports matching lines, trace-budget omissions, retained context, and context-key evictions. These are original source records, not new events: compare their context/monitor/role identifiers and timestamps rather than assuming the sections form a continuous timeline. An immediate health baseline can precede resource warm-up, so compare repeated healthy samples before interpreting growth. Context retention helps short reports but does not replace the larger trace budget for a long-session investigation. Report collection runs after gameplay and adds no Renderer sampling or logging cost.

For a long-session slowdown, use `--lines 200000` to retain the healthy period and onset. The `adaptive`, `recovery`, `performance`, and `scaling` presets include one-second `application-acquire` and `application-queue-present` CPU summaries when diagnostics are enabled. Their entry intervals show gaps between application calls; acquire durations include the lower Vulkan acquisition call, and queue-present durations include MAKO's full entrypoint, configuration updates, and intentional pacing. Configuration-update timings identify work before the private present timer. These observations preserve driver results and timeouts and do not trigger recovery. They measure CPU boundaries, not GPU execution or physical scanout; a short call can submit work that causes a later wait elsewhere.

With the same diagnostics flag, `operation=runtime-health` adds an immediate baseline and roughly five-second samples from a background worker, including during periods with no application presents. No extra toggle, privileged helper, Vulkan memory-budget query, or live GPU query pool is required. The record contains elapsed `session_ms`, collection `sample_ms`, process RSS/virtual/swap bytes, thread and bounded FD counts, cumulative CPU ticks and page faults, interval CPU utilization (100% is one fully occupied CPU core), available system memory, swap capacity/free bytes, memory/I/O pressure averages, and supported AMD GPU busy/memory/clock/temperature/power readings. `unknown` means missing, denied, malformed, or unsupported; zero is a valid reading. FD counts include the sampler's transient directory handle and are approximate; `process_fds_capped=1` means at least 4096 entries. The first CPU utilization sample is unknown because it has no previous interval.

GPU readings follow the application's Vulkan DRM render node (`gpu_render_node`), never an assumed `card0`, and describe the whole device, including other processes. `gpu_clock_source=hwmon` identifies the reported graphics clock; `active-dpm-level` is a fallback selected power-management level, not a measured average. Temperature is in millidegrees Celsius, frequency in Hz, and power in microwatts. On Steam Deck and other AMD APUs, reported device power can include the CPU and GPU together. VRAM/GTT values are driver accounting, not additional RAM to sum on a shared-memory device. Separate `mako_application_*` and `mako_backend_*` byte/allocation counters show MAKO wrapper allocations and their byte peaks; imported mappings remain separate from owned internal/exported memory. They exclude game/WSI, vkBasalt, and other driver allocations. An explicitly selected backend GPU may differ from the application GPU being sampled. Split layer roles and devices can repeat process/backend totals; compare records by `pid`, `role`, and `monitor` instead of summing them. Hardware/sysfs access may be restricted in Flatpak, so retain partial records. These observations help distinguish growing allocations, paging, and changing clocks or heat; they do not identify a leak or prove throttling by themselves.

For a sustained slowdown test, retain a healthy baseline, play past the usual onset (at least 75–90 minutes for a one-hour report), and note the elapsed time when stutter starts. Keep the log running for another minute or two. If practical, note the times of a live Frame Generation Off/On comparison before exiting; restarting would erase the affected state. Collect with `--lines 200000` before starting another game.

One-second `present-phase-summary` records retain mean/max private-present CPU durations even below the slow-event threshold: render-fence waiting, backend scheduling, source copy, image acquisition, generated submit/present, original present, and unattributed work. This is the healthy baseline for comparison with onset; individual slow-operation records remain available. These private durations exclude work before the private timer and intentional upper-entrypoint pacing. Application acquire/present/wait summaries now include caller `tid` and `sample_end_monotonic_ms`; health records include the same clock as `monotonic_ms`, allowing correlation across threads without relying on line ordering. Both window types keep fixed-size statistics and emit at most once per second for their current context/caller; switching that key discards its partial window.

Health records also include cumulative process storage read/write bytes, system CPU pressure, CPU 0's reported scaling clock and configured ceiling in kHz, and the GPU memory clock in Hz where available. CPU 0 is one policy observation, not an all-core measured average; a scaling clock can reflect a requested power state. Unsupported values remain unknown. The worker reports its own collection CPU time (`sample_cpu_us`), collection wall time (`sample_ms`), previous formatting/output wall time (`previous_emit_ms`), and its next wait (`next_sample_delay_ms`). Reads, formatting, and output stay off presentation for the health sampler. Normal waits remain five seconds; a collection taking at least 25 ms wall time or 5 ms CPU, or previous output taking at least 25 ms, changes only its diagnostic wait to 30 seconds. Collection exceptions also back off. There is no queued retry, automatic game reset, or change to scheduling policy. No worker or file sampling runs with diagnostics disabled. Existing frame-path diagnostics still have measurement/output cost, which requires on/off qualification on the target hardware.

Before asking for a long session, run a two-minute collection check with the candidate build and normal gameplay. Confirm the build fingerprint, repeated `runtime-health` records, positive MAKO allocation counters for enabled Frame Generation, application/phase summaries, and useful GPU clock/temperature/load values. Retain this short report if the Deck returns unknown critical fields, sampler costs repeatedly exceed their limits, or polling backs off; investigate that before spending an hour on reproduction. Keep the same game scene, configuration, display, cap, and power setup throughout the long test and note live FG Off/On times at onset. Short collection checks and logging overhead comparisons do not prove the hour-long bug has been found.

## 4. Restore your settings

Restore the original launch settings and remove the two temporary diagnostics variables.

## 5. Send the report

Review `MAKO-diagnostics.txt` and remove personal information. Upload it through the [diagnostic form](https://docs.google.com/forms/d/e/1FAIpQLScSd9qgkYCq3Kbbc3_52k4_82iTmEqt3_FxOqGuxQ6FsjutgA/viewform), choosing **MAKO Renderer (standalone/direct installation)**. Answer **Unknown** when unsure. Do not post the log publicly or attach `Lossless.dll`.
