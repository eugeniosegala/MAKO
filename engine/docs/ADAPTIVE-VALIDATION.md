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
- Smooth Cadence validation near an integer output ratio;
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
