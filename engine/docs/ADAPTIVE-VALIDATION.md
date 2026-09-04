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

Ordered SDR also has an automatic collapse guard. After a healthy Fixed baseline is established, a sustained cadence loss can trigger a short native-only probe. A clearly faster native cadence rebases timing; a true game or GPU slowdown rejects the probe and uses bounded retry backoff. This guard is independent of optional Dynamic Cadence Recovery.

### Adaptive mode

Adaptive varies generated work toward `target_fps` without exceeding `adaptive_max_multiplier`. Fractional mode owns the long-term output budget and keeps timestamps evenly spaced within each real-frame interval. Near target, it may prefer native presentation when measured interval quality and output coverage are already sufficient.

Smooth Cadence may retain a delivery-validated integer multiplier. On ordered Gamescope SDR with matching refresh, it can also hand pacing to FIFO for a proven 2x cadence or select an exact target/multiplier base cap for validated 3x–5x demand. Load shedding and efficiency probes roll back when a cheaper level preserves output better. Exact thresholds and traces are owned by `adaptive_scheduler.*` and its tests.

`adaptive_auto_base_fps_cap` normally starts at half the target. If ordered SDR proves that this cap is sustaining a severe combined-workload collapse, Adaptive releases only the automatic cap for that swapchain; manual and Fixed caps remain authoritative.

### Dynamic Cadence Recovery

Dynamic Cadence Recovery is an optional per-profile policy for games or emulators that switch native rates. Ordered FIFO can make a native 60 FPS mode look like 30 FPS when generation is active, so MAKO periodically requests a native-only sample. Three consecutive samples at least 25% faster than the captured baseline confirm the new cadence.

Adaptive keeps its configured target and ceiling. Fixed uses confirmed Gamescope refresh as its target and treats its multiplier as a ceiling; without refresh feedback it stays exact Fixed. Enabling recovery disables manual and automatic base caps. The probe does not run on the nonblocking HDR transport.

## Transport recovery

Ordered generated-image acquisition uses one application-present deadline, not one full deadline per generated image. Slow pressure first arms a zero-wait guard. Sustained pressure switches to native presentation, warms temporal history, and makes one bounded single-image probe after backoff. If native cadence already meets the requested target, the probe is deferred until cadence falls.

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
| Fixed budgets and presentation recovery | `mako-render/src/presentation_policy.hpp`, `mako-render/src/swapchain_present.cpp` |
| Private-resource transitions | `mako-render/src/profile_update.hpp`, `mako-render/src/runtime_transition.hpp`, `mako-render/src/swapchain.cpp` |
| Diagnostics | `mako-render/src/present_diagnostics.*` |
| Portable policy tests and matrix | `mako-render/tests/`, `scripts/test-adaptive-scheduler.sh` |
| Real hardware and runtime evidence | Sibling MAKO Gym checkout |
