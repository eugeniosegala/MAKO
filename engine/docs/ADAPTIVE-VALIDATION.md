# Adaptive validation

Adaptive Frame Generation has two complementary validation layers. The deterministic layer verifies scheduling policy without a Vulkan device. The runtime layer verifies the driver, compositor, game, and GPU interactions that cannot be modeled faithfully in a unit test.

## Deterministic policy tests

Run the portable Renderer policy suite from the `engine/` directory:

```bash
scripts/test-adaptive-scheduler.sh
```

The test build disables the Vulkan layer, UI, and CLI. It therefore does not need a Vulkan SDK, GPU, `Lossless.dll`, Gamescope, or a Linux host. Every clock value is supplied by the test, so a cadence trace produces the same decisions on every run.

Run the same policy boundaries under AddressSanitizer and UndefinedBehaviorSanitizer with:

```bash
MAKO_ENABLE_SANITIZERS=ON scripts/test-adaptive-scheduler.sh
```

The suite currently locks down:

- three-frame temporal-history warm-up;
- steady target ramping and multiplier validation;
- real-only behavior when native cadence is already above target;
- policy freezing during generated-image acquisition backoff;
- short validated-2x gameplay-hitch recovery;
- conservative recovery for longer menu or focus discontinuities;
- rejection and cooldown of counterproductive load probes;
- exclusion of impossible fast-present bursts;
- Smooth Cadence validation near an integer output ratio, including separate qualification and retention hysteresis;
- timestamp ordering, capacity bounds, and deterministic trace replay.

`adaptive-scheduler-matrix` also runs 120 combinations of base cadence, target, multiplier ceiling, and Smooth Cadence. It checks output bounds and can emit CSV when built in a persistent directory and run directly:

```bash
cmake -S . -B build/adaptive-policy \
  -DMAKO_BUILD_VK_LAYER=OFF \
  -DMAKO_BUILD_UI=OFF \
  -DMAKO_BUILD_CLI=OFF \
  -DBUILD_TESTING=ON
cmake --build build/adaptive-policy --target mako-adaptive-matrix
build/adaptive-policy/mako-render/mako-adaptive-matrix > adaptive-policy-matrix.csv
```

The CSV includes average generated frames, generated-frame share, and frame-count changes. Generated share is useful as an artifact-exposure proxy: 2x, 3x, and 4x can display up to 50%, 67%, and 75% generated frames respectively, but it is not a measurement of ghosting or model quality.

The normal Linux packaging script builds and runs both tests before producing an archive. A policy regression therefore blocks local release packaging.

For a local CPU-cost comparison, run:

```bash
scripts/benchmark-adaptive-scheduler.sh
```

This reports scheduler nanoseconds per decision for real-only, strict 2x, strict 4x, and Smooth Cadence cases. It is intentionally not a pass/fail test: CPU frequency, compiler version, and host load affect absolute timings. Compare results only on the same machine and toolchain. The hot-path frame plan stores its maximum three timestamps inline, so planning performs no per-frame heap allocation.

## Future Adaptive pacing work

The current fractional scheduler is target-average rather than target-clock. It accumulates output credit from the smoothed real-frame interval, selects an integer number of generated frames for each real-frame interval, and spaces those generated frames evenly inside that interval. Integer relationships such as 45 real FPS to 90 displayed FPS can therefore use a uniform 2x cadence, while genuine fractional relationships such as 60 to 90 or 80 to 120 must alternate generated-frame counts. The long-term average reaches the target, but the displayed motion intervals can still vary and uneven game frame times can amplify that variation.

Smooth Cadence is the near-term mitigation, not a complete fractional-pacing solution. A cadence must still qualify within 98% of the target before MAKO adopts a constant integer ratio; once validated, 2.1 retains it down to 95% so a mild performance dip does not immediately resume fractional decisions. The hysteresis deliberately changes neither initial admission nor genuinely fractional ratios. It prefers a slightly lower but more regular output rate during a modest dip and keeps the existing recovery path for a sustained collapse.

Future work should proceed in this order:

1. **Measure displayed cadence rather than only average FPS.** Add low-overhead counters for source-interval variance, generated-count changes, target-clock phase error, actual present intervals, and p95/p99 outliers. Synchronous diagnostics must remain off during subjective pacing comparisons.
2. **Classify cadence quality.** Detect when a near-integer lock is preferable, when source jitter makes fractional output unstable, and when MAKO should fall back to a proven constant multiplier. Any automatic choice needs hysteresis and a bounded retry policy so it cannot chatter between modes.
3. **Prototype a MAKO-owned target clock.** Schedule output against an absolute display-rate phase instead of independently filling each source interval. Presentation feedback or scheduling from [`VK_EXT_present_timing`](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_present_timing.html) may help when the device and surface expose it, but capability probing and a deterministic fallback are mandatory.
4. **Define the real-frame and latency contract.** Perfectly regular fractional output may require bounded delay or occasional resampling of real frames; it cannot be achieved by changing the generated-frame count alone. A prototype must quantify latency, never reorder content, preserve nonblocking native presentation on failure, and make any real-frame drop policy explicit before it can become a supported mode.
5. **Validate fixed-refresh and VRR separately.** Cover steady and noisy 45-to-90, 60-to-90, 60-to-120, 80-to-120, and 90-to-120 traces, then repeat the relevant cases in real DXVK, VKD3D-Proton, and native Vulkan games. AMD's frame-interpolation guidance likewise treats equal display time, precise presentation, a stable base limiter, and VRR behavior as separate pacing concerns; see the [FidelityFX Frame Interpolation Swapchain documentation](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/frame-interpolation-swap-chain/).

This research must preserve MAKO's WSI isolation: MAKO remains the sole owner of generated/original ordering, and the work must not re-admit a competing Gamescope WSI pacing policy merely to obtain timing data. Optional timing feedback is useful only when MAKO can consume it without surrendering presentation ownership. Until those gates are met, Fractional Adaptive remains an opt-in trade-off and Steady remains the safer default.

## Runtime compatibility matrix

Deterministic tests cannot validate Vulkan synchronization, generated-image availability, compositor pacing, model quality, or game behavior during a swapchain rebuild. Record those results separately and retain the diagnostic trace for every failure.

For a ghosting-sensitive comparison, capture the same repeatable scene with Fixed 2x, Adaptive capped at 2x, and Adaptive at the intended higher ceiling. Compare moving edges after startup, overlay recovery, and a short hitch. A higher target is not a quality win if it increases generated share, cadence switches, or interpolation distance enough to worsen visible trails. The three-frame history warm-up and history-only fallback should remain enabled; they are correctness work, not optional performance overhead.

Use this minimum matrix for a release candidate:

| Platform | API path | Required scenarios |
| --- | --- | --- |
| Steam Deck / AMD RDNA2 / Gamescope | DXVK (DX11) | Fixed baseline, Adaptive steady target, Steam overlay, live Off → On |
| Steam Deck / AMD RDNA2 / Gamescope | VKD3D-Proton (DX12) | Menu transition, fast-present burst, 100–250 ms hitch, longer interruption |
| Desktop AMD or Intel / Wayland compositor | Native Vulkan or DXVK | Fractional target, Smooth Cadence, unreachable target, resize/recreation |
| Desktop NVIDIA / current proprietary driver | Native Vulkan or DXVK | Fixed regression baseline, Adaptive ramp/load shedding, resize/recreation |

If hardware for a row is unavailable, mark it **not tested** rather than inferring compatibility from another driver.

For each run, capture:

| Field | Value |
| --- | --- |
| Release commit | Git commit and build version |
| Device | CPU, GPU, handheld/desktop |
| Software | Kernel, Mesa/NVIDIA driver, compositor, Proton version |
| Game path | Game, renderer/API, resolution, quality settings |
| Policy | Fixed/Adaptive, target, maximum multiplier, Smooth Cadence |
| Baseline | Real FPS and frame-time percentiles with generation off |
| Result | Real FPS, displayed FPS, average generated ratio, p95/p99 frame time |
| Transitions | Startup, menu/overlay, focus, hitch, resize, Off → On |
| Recovery | Acquire timeouts, bypassed frames, rebuilds, time to stable output |
| Quality | Visible artifacts, latency impression, stutter/flicker |
| Evidence | Diagnostic-log path and benchmark capture |
| Verdict | Pass, conditional, fail, or not tested |

## Release gates

A candidate is ready for broader testing when:

1. deterministic tests and all policy-matrix cases pass;
2. Fixed 2x/3x/4x behavior has no observed regression;
3. no scenario enters an unbounded wait, recreate loop, or permanent real-only state;
4. generation never exceeds its configured ceiling;
5. Off → On starts from fresh temporal and recovery state;
6. every runtime failure has a context-correlated diagnostic trace;
7. unsupported matrix rows and known game-specific failures are documented in the release notes.

These gates deliberately separate **policy correctness** from **runtime compatibility**. Passing the deterministic suite is necessary, but it is not a claim that every Vulkan driver or game has been validated.
