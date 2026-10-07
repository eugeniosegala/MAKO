# Collect MAKO Diagnostics

Choose your installation:

- **MAKO Decky:** [Collect logs](plugin/docs/COLLECT_DIAGNOSTICS.md).
- **Standalone MAKO Renderer:** [Collect logs](engine/docs/COLLECT_DIAGNOSTICS.md).

If MAKO Decky will not install or open, use the [installation failure guide](plugin/docs/DECKY_INSTALLATION_FAILURES.md).

With presentation diagnostics enabled, reports cover the following runtime questions. Unsupported readings remain `unknown`; their absence is not evidence of a healthy device.

| Question | Available evidence |
| --- | --- |
| Which build, process, profile, and rendering setup produced the problem? | Build fingerprint, process identity, swapchain dimensions, initial state, and live configuration transitions |
| Where did frame delivery slow down? | Application call intervals, acquire/present/wait durations and results, private-present phase means/maxima, slow operations, and scheduling/delivery records |
| Is resource use growing or paging increasing? | Process RSS/swap, threads/FDs, page faults, system memory/pressure, and scoped MAKO allocation counts/current/peak bytes |
| Did device conditions change? | Supported AMD GPU load, clocks, memory, temperature and power, CPU utilization, one CPU policy's clock, and storage activity |
| Did a transition or recovery coincide with onset? | Swapchain replacement, applied/pending settings, focus/presentation state, fallback, and recovery records |
| Is the evidence complete and inexpensive enough to use? | Sample wall/CPU/output cost, sampling backoff, unknown/capped readings, and export truncation/context-retention counts |

Collection preserves a bounded set of earlier identity/state/health records alongside the recent trace. For sustained degradation, use the guides' short collection check before a long session and export with `--lines 200000` to compare repeated healthy samples with onset. These CPU boundaries and sampled counters do not trace GPU execution, physical scanout, game internals, or all driver allocations; they help choose the next focused investigation.

Include the game, Proton/runtime version, handheld or docked mode, resolution/refresh/frame cap, power setup, Frame Generation/scaling/shader settings, approximate elapsed time at onset, and any live changes. A timestamped symptom description makes the same records useful for stalls, configuration regressions, memory growth, and device-load changes as well as persistent stutter.

Each guide explains how to reproduce the problem and save `MAKO-diagnostics.txt`. Review it for personal information, then upload it through the [diagnostic form](https://docs.google.com/forms/d/e/1FAIpQLScSd9qgkYCq3Kbbc3_52k4_82iTmEqt3_FxOqGuxQ6FsjutgA/viewform). Keep diagnostic logs out of public GitHub issues.
