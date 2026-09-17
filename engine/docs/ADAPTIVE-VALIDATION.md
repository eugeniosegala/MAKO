# Adaptive validation

Adaptive Frame Generation needs two kinds of evidence: deterministic policy tests for scheduler decisions, and real Vulkan tests for drivers, presentation, Gamescope, GPU work, and game behavior. Passing one does not substitute for the other.

## Portable policy tests

From `engine/`, run:

```bash
scripts/test-adaptive-scheduler.sh
MAKO_ENABLE_SANITIZERS=ON scripts/test-adaptive-scheduler.sh
```

The script builds only the portable-policy targets. It supplies deterministic clocks and needs no Vulkan device, runtime loader, `Lossless.dll`, Gamescope, or Linux host. Vulkan headers are still required because some policy types use Vulkan declarations. Coverage includes:

- warm-up, ramping, multiplier ceilings, target behavior, and reset boundaries;
- Fractional placement, near-target native preference, Smooth Cadence, and cadence recovery;
- Fixed refresh budgeting and ordered-FIFO collapse recovery;
- generated-frame request, admission, scheduling, and delivery accounting;
- acquire and present recovery policy, private-transition state, and swapchain retirement; and
- trace replay plus the scheduler matrix.

The matrix runs 384 combinations of source cadence, target, multiplier ceiling, and Smooth Cadence. To keep its CSV output:

```bash
cmake -S . -B build/adaptive-policy \
  -DMAKO_BUILD_VK_LAYER=OFF \
  -DMAKO_BUILD_UI=OFF \
  -DMAKO_BUILD_CLI=OFF \
  -DBUILD_TESTING=ON
cmake --build build/adaptive-policy --target mako-adaptive-matrix
build/adaptive-policy/mako-render/mako-adaptive-matrix > adaptive-policy-matrix.csv
```

For a same-machine CPU-cost comparison, run `scripts/benchmark-adaptive-scheduler.sh`. Its nanoseconds-per-decision results are not portable performance limits.

The Linux package build runs the full registered CTest suite, which includes these policy tests. [Testing MAKO](../../TESTING.md) owns the complete portable checklist.

## Scheduling contract

`AdaptiveScheduler::planFrame()` is deterministic. It observes real-frame cadence, selects zero to four generated frames, and returns a `GeneratedFramePlan` whose interpolation timestamps are stored inline without per-frame heap allocation.

The presentation path keeps three facts separate:

1. **Requested:** timestamps chosen by Fixed or Adaptive policy.
2. **Admitted:** generated images the active transport can accept.
3. **Scheduled:** timestamps actually sent to the backend.

Full admission preserves requested timestamps. A partial batch is evenly re-spaced across the real-frame interval instead of taking an early prefix. On ordered SDR, partial admission during an Adaptive multiplier evaluation can lower the proven capacity for that WSI context; a new game-owned swapchain starts with fresh evidence. The original real frame is outside the generated plan and keeps priority on failure.

### Fixed mode

Fixed uses the configured 2x–5x multiplier. When Gamescope reports a nonzero refresh, a fractional display budget suppresses outputs that the display cannot consume; without refresh feedback, exact Fixed behavior is retained.

Ordered SDR also has an automatic collapse guard. After a healthy Fixed baseline is established, a sustained cadence loss can trigger a short native-only probe. A clearly faster native cadence rebases timing; a true game or GPU slowdown rejects the probe. After rejection, bounded retry backoff alone does not rearm the guard: cadence must move outside the rejected baseline's 0.8–1.25 band, or healthy output must requalify the baseline before a later collapse. This avoids periodic native-only interruptions under unchanged load while preserving recovery after new evidence. This guard is independent of optional Dynamic Cadence Recovery.

With Smooth Cadence enabled and an ordered Gamescope SDR transport, Fixed requests its full configured multiplier and leaves pacing to FIFO instead of applying an automatic real-frame CPU cap. This requires confirmed refresh feedback, full private-output capacity, no explicit real-frame cap, and no active acquire recovery or Dynamic Cadence Recovery. The display budget continues to observe source cadence so leaving this path restores its normal policy without an unnecessary cold start. First-frame and long-stall guards remain active.

### Adaptive mode

Adaptive varies generated work toward `target_fps` without exceeding `adaptive_max_multiplier`. Fractional mode owns the long-term output budget and keeps timestamps evenly spaced within each real-frame interval. Near target, it may prefer native presentation when measured interval quality and output coverage are already sufficient.

When demand temporarily exceeds the validated multiplier ceiling, the target clock retains less than one output of bounded credit instead of wrapping it away. This lets a source returning from an external throttle recover an achievable integer cadence without dropping generated work on ordinary timing jitter, while still preventing impossible whole-output debt from accumulating or producing later catch-up bursts.

Smooth Cadence may retain a delivery-validated integer multiplier. Fractional Adaptive uses it to stabilize a validated generated-frame plan while retaining its moving target clock. On ordered Gamescope SDR with matching refresh, Steady Adaptive can hand pacing to FIFO for a proven 2x cadence or select an exact target/multiplier base cap for validated 3x–5x demand. Fixed can request its full multiplier under ordered FIFO, avoiding alternating real-only and generated presents without an automatic CPU cap. Explicit Fixed caps, Dynamic Cadence Recovery, transport recovery, and insufficient private-output capacity keep Fixed's normal budget policy. Load shedding and efficiency probes roll back when a cheaper level preserves output better. Exact thresholds and traces are owned by `adaptive_scheduler.*` and presentation-policy tests.

The Adaptive 3x–5x base-cap policy requires one second at or above 95% of its selected cadence before entry. An active cap tolerates dips down to 90%; a lower cadence or a return to the previous multiplier's range must persist for 250 ms before releasing it. This prevents entry-threshold jitter from repeatedly resetting the real-frame pacer. Transport guards, scheduler recovery, ramp evaluation, loss of eligibility and a return to 2x still restore normal pacing immediately; lower-load efficiency probes retain their separate cap transitions. Fixed's FIFO path has no automatic base-cap qualification or timed CPU sleep.

`adaptive_auto_base_fps_cap` normally starts at half the target. If ordered SDR proves that this cap is sustaining a severe combined-workload collapse, Adaptive releases only the automatic cap for that swapchain; manual and Fixed caps remain authoritative.

Cadence-drop detection holds the proven interval while confirming three consecutive samples at least twice as long. It also accumulates those candidate samples in a shadow smoothed estimate: if the next sample rejects the sustained-drop hypothesis, the estimator incorporates the delayed observations before the current interval. Discarding them instead can lock onto only the fast half of a bursty stream, for example reporting 125 FPS for alternating 8/32 ms intervals whose actual rate is 50 FPS, and suppress useful generated work. Timing resets, impossible fast bursts, and transport backoff discard pending drop evidence. The existing hard-stall, history, delivery, load-protection, and multiplier bounds still apply. Portable tests cover both recovery policies and Smooth Cadence settings; Gym's `adaptive-bursty-source` row exercises a 75 → noisy 50 → 75 FPS round trip with a 150 FPS target and 240 Hz headless Gamescope. This is source-cadence evidence relevant to mouse-stutter investigation, not a simulation of HID polling, game input, or physical scanout.

### Dynamic Cadence Recovery

Dynamic Cadence Recovery is an optional per-profile policy for games or emulators that switch native rates. Ordered FIFO can make a native 60 FPS mode look like 30 FPS when generation is active, so MAKO periodically requests a native-only sample. Three consecutive samples at least 25% faster than the captured baseline confirm the new cadence.

Adaptive keeps its configured target and ceiling. Fixed uses confirmed Gamescope refresh as its target and treats its multiplier as a ceiling; without refresh feedback it stays exact Fixed. Enabling recovery disables manual and automatic base caps. The probe does not run on the nonblocking HDR transport.

## Transport recovery

Ordered generated-image acquisition uses one application-present deadline, not one full deadline per generated image. Each normal image acquire can extend its original one-and-a-half-display-period window toward two and a half periods, provided the extension leaves half a display period below the slow-acquire pressure threshold. The 8 ms floor and remaining cumulative budget still take precedence. This allows 20.8 ms at 120 Hz and 19.4 ms at 90 Hz while preserving the original 25 ms at 60 Hz. The margin keeps a short deadline miss eligible for the zero-wait guard instead of turning an isolated late image into native drain. Sustained pressure switches to native presentation, warms temporal history, and makes one bounded single-image probe after backoff. If native cadence already meets the requested target, the probe is deferred until cadence falls.

An isolated short acquire timeout below the slow-pressure threshold uses the same zero-wait guard without discarding validated cadence. Only a healthy unrestricted generated batch clears repeated-pressure evidence; a successful guard or temporal warm-up cannot clear it. Repeated failures back off through 250 ms, 500 ms, one, two, five, 15, and 30 seconds. Native cadence qualifies during backoff, so confirmed renewed demand after a target-satisfying menu can re-arm one probe early; a failed demand probe retains the failure count.

A recovery probe runs only when temporal history is ready and the current Fixed or Adaptive policy requests generated work. A real-only policy frame performs no generated-image acquisition and leaves the pending probe intact for the next eligible frame; it cannot count as a probe failure or increase recovery backoff.

A later slow lower `QueuePresentKHR` call has a separate stall quarantine. While either recovery owns the context, MAKO submits no synthetic work and retains the real-frame path. Recovery probes are bounded, do not destroy the game swapchain, and do not treat a skipped generated image as corruption. The standalone acquire timeout remains an independent compatibility setting described in [Troubleshooting](TROUBLESHOOTING.md).

`GeneratedDeliveryWindow` compares requested outputs with outputs queued inside MAKO's budget. It does not claim compositor scanout. Diagnostics distinguish requested, admitted, scheduled, and delivered counts.

Private Flow Scale, model, or generated-capacity changes retain the old policy while replacement resources are prepared. Capacity growth that does not fit the current WSI pool remains pending for game-owned recreation. [Runtime configuration transitions](RUNTIME-TRANSITIONS.md) owns that lifecycle. Ultra Performance is a restart-bound resource policy, not a separate scheduler.

## Runtime validation

Use the sibling MAKO Gym checkout between portable policy tests and commercial-game testing. Its manifests own current rows, assertions, and thresholds:

```bash
scripts/run-mako-gym.sh --suite recovery --list
scripts/run-mako-gym.sh --suite recovery --filter '(stall|cadence-drop|recreate)$'
```

Select coverage according to [Testing MAKO](../../TESTING.md#selecting-mako-gym-coverage). Scheduler changes normally begin with `recovery`; external overlay pause/throttle and workload-proven source-return changes use `external-recovery`; construction changes add `vulkan`; Gamescope lifecycle changes add `gamescope-e2e`; translation changes add `proton-e2e` or `proton-compatibility`. A filtered pass proves only its selected rows.

For affected changes, cover these runtime boundaries:

| Boundary | Minimum evidence |
| --- | --- |
| Generation modes | Frame Generation Off, Fixed 2x and affected higher multipliers, Fractional Adaptive, Smooth Cadence, and unreachable targets. |
| Cadence changes | Startup, gameplay/menu rate changes, true fixed-rate rejection, short hitches, long interruptions, and fast-present bursts. |
| Presentation | Ordered SDR, acquire pressure, lower-present stalls, focus and overlays, resize, recreation, and shutdown. |
| Resource transitions | Live Off/On, Flow Scale or model replacement, capacity growth with and without WSI headroom, and history warm-up. |
| API/runtime | Affected native Vulkan, DXVK, VKD3D-Proton, Gamescope, direct desktop, Flatpak, architecture, and Proton-family paths. |
| Quality/performance | Real FPS, displayed FPS, frame-time percentiles, generated share, visible artifacts, latency impression, GPU time, and memory where relevant. |

Record the commit and build identity, hardware, driver, compositor, runtime, game and resolution, policy settings, transitions, diagnostic path, and verdict. Mark unavailable hardware or paths **not tested**.

Presentation diagnostics are opt-in and can perturb pacing. Use `adaptive`, `recovery`, and `performance` presets for a separate diagnostic run, then disable them for subjective comparison. Their timestamps describe MAKO policy and queueing, not compositor scanout.

## Release gates

An affected candidate is ready for broader testing when:

1. portable and sanitizer policy tests pass;
2. affected Fixed and Adaptive modes stay within their ceilings;
3. no case enters an unexpected indefinite wait, recreation loop, or permanent unintended native-only state;
4. Off/On and recreation restart from valid temporal and recovery state;
5. failures have context-correlated diagnostics; and
6. unavailable rows and known title-specific failures are recorded.

## Code and test ownership

| Responsibility | Source of truth |
| --- | --- |
| Adaptive policy and state | `mako-render/src/adaptive_scheduler.*` |
| Generated-frame plan | `mako-render/src/generated_frame_plan.hpp` |
| Delivery windows | `mako-render/src/generated_frame_delivery.hpp` |
| Fixed budgets and presentation recovery | `mako-render/src/presentation_policy.hpp`, `mako-render/src/swapchain/present.cpp` |
| Private-resource transitions | `mako-render/src/profile_update.hpp`, `mako-render/src/runtime_transition.hpp`, `mako-render/src/swapchain/resources.cpp` |
| Diagnostics | `mako-render/src/present_diagnostics.*` |
| Portable policy tests and matrix | `mako-render/tests/`, `scripts/test-adaptive-scheduler.sh` |
| Real hardware and runtime evidence | Sibling MAKO Gym checkout |
