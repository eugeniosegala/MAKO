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
- ordered-SDR Smooth Cadence efficiency probes that retain a cheaper multiplier only when it preserves the target, with immediate rollback and a long retry backoff when it does not;
- requested-versus-accepted delivery-window health for ramp and Smooth Cadence evaluation;
- opt-in ordered-SDR native-cadence probes, including Adaptive and refresh-targeted Fixed recovery from a self-hidden 30-to-60 FPS transition, rejection at a true fixed 30 FPS, and exclusion from the nonblocking HDR transport;
- timestamp ordering, inline capacity bounds, full-admission preservation, and partial-admission respacing;
- deterministic trace replay plus characterization fingerprints for 45-to-90, the 42.5-to-43 FPS transition region, and a mixed disruption corpus.

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

This reports scheduler nanoseconds per decision for real-only, strict 2x, strict 4x, noisy fractional placement-clock, and Smooth Cadence cases. It is intentionally not a pass/fail test: CPU frequency, compiler version, and host load affect absolute timings. Compare results only on the same machine and toolchain.

## Scheduler and frame-plan ownership

`AdaptiveScheduler::planFrame()` is deterministic and advances explicit stages for cadence observation, discontinuity recovery, rescue measurement, stabilization and multiplier validation, Smooth Cadence, fractional placement-clock planning, optional native-cadence recovery, and strict-load protection. Related mutable values are grouped under one `SchedulerState` by responsibility: history warm-up, cadence, diagnostics throttling, pacing aggregates, fast bursts, fractional workload credit and target-output phase, native-cadence probing, stabilization, ramp, rearm, stable cadence, rescue, strict load, and discontinuity recovery. The snapshot phase remains a derived diagnostic view of that state, not an independent state machine that can drift from the policy.

On MAKO's ordered SDR FIFO path, application-present timing while generation is active cannot distinguish a game that truly runs at 30 FPS from a native 60 FPS menu held to 30 FPS by one generated plus one original FIFO present. Dynamic Cadence Recovery resolves that ambiguity in either generation mode by periodically requesting one native-only frame and requiring three consecutive samples at least 25% faster than the captured baseline before rebasing cadence. Adaptive retains its configured Target FPS and maximum multiplier. Fixed uses the confirmed Gamescope refresh as its target and treats the selected 2x-4x multiplier as a ceiling; without a supported refresh signal, it fails closed to exact Fixed behavior rather than borrowing Adaptive's hidden target. A failed probe resumes the validated generated policy on the next frame and waits the configured 0.25-to-three-second interval before retrying; the default two-second interval balances detection time against the frequency of brief checks, while shorter opt-in values bound a self-hidden native-rate transition and its associated emulator/audio slowdown to approximately the selected interval plus its three confirmation frames. Interval-only live updates reschedule the next inactive probe without discarding cadence history, the validated generation limit, or an already-active confirmation. Because even a rejected probe briefly changes pacing in a genuine fixed-rate game, the compatibility policy is per-profile and off by default. Enabling it clears both base-FPS caps automatically; the probe excludes the nonblocking HDR bridge, where the presentation signal has different ownership.

Every decision returns a `GeneratedFramePlan`, which stores at most three normalized interpolation timestamps inline and performs no per-frame heap allocation. The presentation path keeps three distinct facts: the **requested** plan from Fixed or Adaptive policy, the **admitted** count allowed by the selected transport, and the **scheduled** plan sent to the backend. Full admission preserves the requested timestamps exactly. If the Gamescope HDR transport admits only part of a request, the accepted count is evenly re-spaced across the real-frame interval, preserving the established partial-admission behavior rather than taking an early timestamp prefix. The native original frame remains outside this generated-frame plan and retains presentation priority on failure.

The backend's per-output uniform cache uploads an interpolation timestamp only when that destination's scheduled value changes. Submission boundaries remain unchanged so signal-first backend callers retain prepass and generated-output CPU/GPU overlap. This is an orchestration optimization only: it does not remove or reorder model dispatches, image barriers, generated outputs, timeline milestones, or presentation work.

Ultra Performance is a restart-only profile policy rather than a scheduler shortcut. It may improve frame-generation performance by up to 30% in favourable GPU-limited scenarios by forcing an effective 0.75 Flow Scale and the lighter frame-generation model, allowing FP16 on capable hardware, sizing private generated-output capacity only for the active Fixed or Adaptive policy, and skipping live profile-configuration checks. The startup profile remains frozen for the game process, but compositor-owned refresh and HDR feedback, bounded waits, load shedding, native fallback, and recovery continue operating. Hardware validation must compare the same repeatable scene against the normal 0.90 quality-model profile, record generated-frame GPU time and unified-memory use, and inspect thin geometry, occlusion, particles, UI edges, and rapid camera motion for the combined quality loss.

`GeneratedDeliveryWindow` records requested frames against frames accepted for presentation during one ramp or Smooth Cadence evaluation window. “Accepted” means queued within MAKO's delivery budget; it does not claim compositor scanout. The established integer tolerance allows at most five percent missed delivery, so windows with fewer than twenty requested generated frames require complete acceptance. This transport observation remains separate from cadence measurement and placement-clock accounting.

Ordered Acquire Recovery is separately owned by the presentation transport and applies to both Adaptive and Fixed generation. The configured generated-image acquire ceiling is one shared application-present budget: every generated image receives only the unspent remainder, cumulative acquire time owns slow and severe classification, and exhaustion stops further acquisition instead of multiplying the ceiling by a 3x or 4x plan. One successful application-present acquisition total exceeding the greater of 25 ms and one-and-a-half display periods arms a zero-wait, one-generated-frame guard for the next application present. That guard prevents the transport delay from entering Adaptive's source-cadence clock and prevents the next present from repeating a blocking acquisition. An immediately available guard image resumes retained policy without a stabilization window. A guard miss presents one native relief frame, warms three current temporal-history frames, and then permits normal policy to retry; only another slow total, a Vulkan timeout result, cumulative acquisition that exhausts its requested wall-time budget, or cumulative acquisition lasting at least twice the slow threshold proves sustained pressure and quarantines generated work. MAKO then presents natively for 250 ms, warms three temporal-history frames, and makes exactly one bounded single-image recovery probe even when retained policy is 3x or 4x. Its timeout starts at one display period, expands across repeated failures to at most three periods, never exceeds 25 ms, and never exceeds the configured normal acquire ceiling. Failure is terminal for that probe attempt and returns to native backoff rather than leaving generation permanently probe-pending. A successful drain probe starts one absolute two-second native-only stabilization interval; later availability cannot extend that deadline or intermittently admit synthetic frames. At the deadline MAKO warms three current history frames and resumes retained policy. Adaptive freezes cadence, ramp, load-shed, and delivery evaluation while recovery is constrained. Repeated pressure backs off to 500 ms, one second, and then a bounded two seconds. The drain and stabilization perform no backend scheduling or synthetic swapchain acquisition, so a Steam overlay transition can release ordered-FIFO images without destroying the game-owned swapchain or resetting the validated Adaptive multiplier.

Strict-load protection compares the active multiplier's target-capped displayed capacity with the previously proven lower multiplier. Because those samples can span different gameplay scenes, Ordered SDR trusts the historical lower-level estimate only when the active level is at or below 75% of target. Within that severe deficit, it returns directly to the cheaper level when that level can preserve the current displayed rate; when both levels are below the threshold, a base-cadence collapse with less than 15% displayed-throughput gain is also treated as presentation saturation so menu/compositor pressure cannot pin the more expensive level. Repeated saturated probes back off from 15 to 30 and then 60 seconds; a sustained 15% base-cadence recovery can retry early. The Gamescope HDR bridge retains its real-only measurement because a collapse there can instead reflect colour-transition or admission pressure.

After a qualified 3x or 4x Smooth Cadence has held at least 98% of the requested target for five seconds, Ordered SDR briefly tests one lower multiplier while retaining the qualified policy for immediate rollback. The lower level becomes the new qualified cadence only when its delivery window remains healthy and its measured capacity still reaches at least 98% of target. The normal evaluation lasts 250 ms; when output is already at least 90% of target and native cadence has risen by at least 20%, MAKO grants one additional 250 ms for ordered-FIFO backpressure to settle while retaining the same 98% acceptance requirement. A failed probe restores the previous multiplier on the same scheduling decision and waits 60 seconds before another attempt. The probe does not run for Fractional placement, 2x, the nonblocking HDR transport, stabilization, discontinuity or rescue recovery, ramp or rearm evaluation, or a native-cadence probe; generated-image backoff and impossible fast-present bursts pause its clock. This closes the local optimum where 3x can hold a game near 40 real FPS even though removing one generated output lets the game recover to a cleaner 60-to-120 2x cadence.

## Fractional Adaptive placement clock

Fractional Adaptive separates workload budgeting from temporal placement. The established smoothed-cadence credit remains the only owner of how many outputs are earned, preserving the previous generated-work envelope and its multiplier-ceiling behavior. Independently, each observed raw source interval advances a bounded target-output phase by `interval × target_fps`. When the workload budget has already earned multiple outputs, at least one quarter output per source frame of ceiling headroom remains, and both that phase and the local spacing error show that the current interval is materially better left with one fewer output, MAKO moves exactly one earned output into a separate deferred-output ledger. A deferral may not exceed the running worst requested spacing of the unmodified workload plan. MAKO repays that output only on a later frame with spare capacity whose squared spacing cost does not consume more than the benefit saved by the deferral. Raw timing can therefore move already-budgeted work away from a short interval, but it can never mint additional work after a compositor-delayed frame, increase the running maximum requested interval, or make cumulative requested-spacing cost worse than the previous smoothed-budget plan.

The raw placement residual is normalized to half one output in either direction. Deferral requires at least a one-percent target-period spacing improvement; a separate rounding epsilon handles floating-point boundaries. The placement ledger carries at most one already-earned output and never repays above the multiplier ceiling. Separately, impossible whole-output credit created by multiplier saturation retains the established no-backlog rule and is discarded rather than producing a later catch-up burst. The smoothed budget, raw phase, and bounded deferred output reset together on timing discontinuities, history recovery, multiplier changes, Smooth Cadence transitions, native-cadence probes, load shedding, and acquisition bypasses, so stale state cannot cross a policy boundary.

Generated timestamps remain evenly spaced inside the selected source interval. With every original frame mandatory, ordered, and undelayed, equal subdivision minimizes the largest and total within-interval spacing error. Positioning only the generated timestamp on an absolute tick would increase local variance without controlling when the compositor scans out either image. Steady integer relationships such as 45-to-90 and 60-to-120 remain constant midpoint cadences, and the tested alternating-noise integer case retains its previous target average. Bursty source timing can still exercise the established smoothed-budget and ceiling rules. Genuine fractional relationships such as 60-to-90, 80-to-120, and 90-to-120 still alternate counts, but the placement clock can keep an earned extra output off a clearly shorter interval.

Smooth Cadence remains the near-integer classifier and takes precedence over the fractional clock. A cadence must qualify within 98% of the target before MAKO adopts a constant integer ratio; once validated, it retains that cadence down to 95%. Its two-second qualification, one-second delivery evaluation, 500 ms exit grace, bounded retry, and collapse recovery are unchanged. MAKO does not automatically force an unproven constant multiplier merely because source timing is noisy: that could overshoot the target, increase GPU work and generated-frame share, or lower real-frame responsiveness.

When present diagnostics are enabled, active-policy `adaptive-plan` records include Smooth Cadence and target-clock state, the current workload-budget credit and deferred-output flag, plus allocation-free aggregate measurements from their contiguous policy window: source-interval samples, mean, standard deviation, p95 and p99; generated-count changes; requested policy-plan motion-interval samples, mean, standard deviation, p95 and p99; and the current target-phase error plus its sample count, RMS, and maximum. The requested interval metrics are captured before transport admission or partial Gamescope HDR respacing. Policy transitions and skipped scheduling frames start a fresh window rather than mixing phases. Percentiles are nearest-rank estimates from one-millisecond buckets reported at the bucket midpoint. The cumulative-cost and running-maximum guards do not make every finite diagnostic p95 or p99 window monotonic, so treat those window percentiles as A/B measurements rather than scheduler invariants. These are scheduler/content-timing measurements, not compositor scanout timestamps. Process-identity and swapchain-context records correlate interleaved processes with their executable, profile, build fingerprint, extent, image count, format, presentation mode, and transport. Released source archives use the Renderer version as their fingerprint; Git development builds include the component commit and, when dirty, a digest of the component diff and untracked paths. Ordered-acquire policy records expose the effective timeout and slow/severe thresholds, while a slow `present-total` emits one phase breakdown rather than logging every phase on every frame. The once-per-second Adaptive and Fixed aggregates are assembled before one `stderr` emission so field formatting does not introduce repeated unbuffered flush points in the presentation path. Diagnostics remain opt-in instrumentation rather than a zero-overhead benchmark mode; keep them disabled during final subjective pacing comparisons, then use a separate diagnostic capture to explain the result.

There is no explicit added-delay mechanism: this clock adds no sleep, buffering, generated work beyond the smoothed budget, real-frame drop, content reorder, worker thread, or presentation owner. Native presentation and the existing failure path remain unchanged. Redistributing one generated output can still change per-frame GPU work, FIFO backpressure, and end-to-end latency, so cadence, base FPS, and latency remain hardware measurements rather than deterministic guarantees. Ordered-FIFO application-present intervals can include backpressure from prior presentation work; preventing raw timing from creating budget avoids amplifying that pre-existing estimator coupling, but fixed-refresh hardware traces must still confirm that fractional policy does not settle at a lower real cadence. Perfect fractional scanout regularity is outside this contract because every real frame must still be shown. The optional [`VK_EXT_present_timing`](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_present_timing.html) path remains future hardware work: its device and surface features, FIFO-only scheduling semantics, dynamic timing properties, feedback queue, and failure behavior require capability probing and asynchronous validation, and current SteamOS support cannot be assumed.

Deterministic coverage includes steady and noisy 45-to-90, 60-to-90, 60-to-120, 80-to-120, and 90-to-120 cases; a noisy multi-level 45-to-120 case; the half-output phase bound, target average, timestamp validity, a representative 60-to-90 short/long assignment, a plus-or-minus-40-percent noisy 60-to-120 ceiling case, adversarial irregular repayment orderings, saturation recovery, complete placement-state reset, and diagnostic-window restart after acquisition bypass. Hardware validation must still repeat the relevant cases on fixed-refresh and VRR displays in real DXVK, VKD3D-Proton, and native Vulkan games. AMD's frame-interpolation guidance likewise treats equal display time, precise presentation, a stable base limiter, and VRR behavior as separate pacing concerns; see the [FidelityFX Frame Interpolation Swapchain documentation](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/frame-interpolation-swap-chain/).

## Runtime compatibility matrix

Deterministic tests cannot validate Vulkan synchronization, generated-image availability, compositor pacing, model quality, or game behavior during a swapchain rebuild. Record those results separately and retain the diagnostic trace for every failure.

For a ghosting-sensitive comparison, capture the same repeatable scene with Fixed 2x, Adaptive capped at 2x, and Adaptive at the intended higher ceiling. Compare moving edges after startup, overlay recovery, and a short hitch. A higher target is not a quality win if it increases generated share, cadence switches, or interpolation distance enough to worsen visible trails. The three-frame history warm-up and history-only fallback should remain enabled; they are correctness work, not optional performance overhead.

Use this minimum matrix for a release candidate:

| Platform | API path | Required scenarios |
| --- | --- | --- |
| Steam Deck / AMD RDNA2 / Gamescope | DXVK (DX11) | Fixed baseline, Adaptive steady target, Steam overlay, live Off → On, normal 0.90 quality model versus Ultra Performance 0.75 lighter model after restart |
| Steam Deck / AMD RDNA2 / Gamescope | Native Vulkan emulator | Adaptive and Fixed 2x 30 FPS gameplay ↔ 60 FPS menu with Dynamic Cadence Recovery, true fixed-30 rejection |
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
| Policy | Fixed/Adaptive, target, maximum multiplier, Smooth Cadence, Dynamic Cadence Recovery, Ultra Performance, effective Flow Scale and model |
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
