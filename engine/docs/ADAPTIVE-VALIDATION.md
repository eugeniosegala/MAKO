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

Smooth Cadence may retain a delivery-validated integer multiplier. Fractional Adaptive uses it to stabilize a validated generated-frame plan while retaining its moving target clock. On ordered Gamescope SDR with matching refresh, Steady Adaptive can hand pacing to FIFO for a proven 2x cadence or select an exact target/multiplier base cap for validated 3x–5x demand. Fixed can request its full multiplier under ordered FIFO, avoiding alternating real-only and generated presents without an automatic CPU cap. Explicit Fixed caps, Dynamic Cadence Recovery, transport recovery, and insufficient private-output capacity keep Fixed's normal budget policy. Load shedding and efficiency probes roll back when a cheaper level preserves output better. Ordinary multiplier acceptance retains the 3.3 throughput and base-collapse checks; the extra severe-deficit marginal-gain rejection from menu-recovery experiments has been removed. Exact thresholds and traces are owned by `adaptive_scheduler.*` and presentation-policy tests.

The Adaptive 3x–5x base-cap policy requires one second at or above 95% of its selected cadence before entry. An active cap tolerates dips down to 90%; a lower cadence or a return to the previous multiplier's range must persist for 250 ms before releasing it. This prevents entry-threshold jitter from repeatedly resetting the real-frame pacer. Transport guards, scheduler recovery, ramp evaluation, loss of eligibility and a return to 2x still restore normal pacing immediately; lower-load efficiency probes retain their separate cap transitions. Fixed's FIFO path has no automatic base-cap qualification or timed CPU sleep.

`adaptive_auto_base_fps_cap` normally starts at half the target. If ordered SDR proves that this cap is sustaining a severe combined-workload collapse, Adaptive releases only the automatic cap for that swapchain; manual and Fixed caps remain authoritative.

Cadence-drop detection holds the proven interval while confirming three consecutive samples at least twice as long. It also accumulates those candidate samples in a shadow smoothed estimate: if the next sample rejects the sustained-drop hypothesis, the estimator incorporates the delayed observations before the current interval. Discarding them instead can lock onto only the fast half of a bursty stream, for example reporting 125 FPS for alternating 8/32 ms intervals whose actual rate is 50 FPS, and suppress useful generated work. Timing resets, impossible fast bursts, and transport backoff discard pending drop evidence. The existing hard-stall, history, delivery, load-protection, and multiplier bounds still apply. Portable tests cover both recovery policies and Smooth Cadence settings; Gym's `adaptive-bursty-source` row exercises a 75 → noisy 50 → 75 FPS round trip with a 150 FPS target and 240 Hz headless Gamescope. This is source-cadence evidence relevant to mouse-stutter investigation, not a simulation of HID polling, game input, or physical scanout.

### Dynamic Cadence Recovery

Dynamic Cadence Recovery is an optional per-profile policy for games or emulators that switch native rates. Ordered FIFO can make a native 60 FPS mode look like 30 FPS when generation is active, so MAKO periodically requests a native-only sample. Three consecutive samples at least 25% faster than the captured baseline confirm the new cadence.

Adaptive keeps its configured target and ceiling. Fixed uses confirmed Gamescope refresh as its target and treats its multiplier as a ceiling; without refresh feedback it stays exact Fixed. Enabling recovery disables manual and automatic base caps. The probe does not run on the nonblocking HDR transport.

## Transport recovery

Ordered generated-image acquisition uses one application-present deadline, not one full deadline per generated image. Each normal image acquire can extend its original one-and-a-half-display-period window toward two and a half periods, provided the extension leaves half a display period below the slow-acquire pressure threshold. The 8 ms floor and remaining cumulative budget still take precedence. This allows 20.8 ms at 120 Hz and 19.4 ms at 90 Hz while preserving the original 25 ms at 60 Hz. The margin keeps a short deadline miss eligible for the zero-wait guard instead of turning an isolated late image into native drain. Sustained pressure switches to native presentation, warms temporal history, and makes one bounded single-image probe after backoff. If native cadence already meets the requested target, the probe is deferred until cadence falls.

An isolated short acquire timeout below the slow-pressure threshold uses the same zero-wait guard without discarding validated cadence. Only a healthy unrestricted generated batch clears repeated-pressure evidence; a successful guard or temporal warm-up cannot clear it. Repeated failures back off through 250 ms, 500 ms, one, two, five, 15, and 30 seconds. Native cadence qualifies during backoff, so confirmed renewed demand after a target-satisfying menu can re-arm one probe early; a failed demand probe retains the failure count. Inside a confirmed Steam-menu return window, three failed bounded probes spanning at least three seconds prove that the current Fixed or Adaptive ordered-acquire context exhausted one continuous in-place recovery, while three separate native-drain episodes inside 15 seconds provide the same direct transport evidence even when each short probe temporarily succeeds. A confirmed Fractional/Steady live transition arms a 60-second window in which three native-drain episodes provide the same qualification; it does not admit FPS evidence. One successful retirement-protected lower present may then request a game-owned recreation, subject to the shared surface budget and per-context one-shot guard. Ordinary gameplay without either event cannot activate recreation or immediate multiplier-probe rejection, unrelated widely spaced episodes do not accumulate, and a live reset cannot rearm an already signalled context. During an active event-recovery window, a generated-image timeout in an unvalidated 2x–5x Adaptive probe rejects that probe immediately, returns to the exact previously proven multiplier, and preserves escalating higher-probe backoff across the following cadence refresh. Outside those windows, the established delivery qualification remains unchanged from 3.3. Native-only recovery remains the fallback when no generated level has been validated.

A recovery probe runs only when temporal history is ready and the current Fixed or Adaptive policy requests generated work. A real-only policy frame performs no generated-image acquisition and leaves the pending probe intact for the next eligible frame; it cannot count as a probe failure or increase recovery backoff.

A later severely blocked lower `QueuePresentKHR` call has a separate stall quarantine. Its 250 ms floor applies to the longest individual call in a generated batch, never the sum of separate FIFO waits; returned 50–114 ms gameplay or overlay hitches remain diagnostic evidence but do not start seconds of native-only backoff, and a 7 ms generated present followed by a 47 ms original present must not become a 54 ms stall. Timing and output accounting remain active independently of diagnostic logging. Genuine stalls retain the existing bounded, escalating quarantine. While either recovery owns the context, MAKO submits no synthetic work and retains the real-frame path. Two additional severe native presents can arm an application-owned recreation in Fixed or Adaptive mode only on a Frame Generation-only context. A combined spatial-scaling context never turns recovery timing into a forced WSI replacement because rebuilding the complete scaled context during device-memory pressure can cause device loss; it relies on in-place recovery or a natural game-owned recreation. One second of sub-threshold native presents cancels pending evidence. An unavailable recreation extends the guard only to the later of the original stabilization deadline and 30 seconds from quarantine start, then retries with fresh history. FG Off clears it; ignored recreation signals cannot rearm the same context after a live/private reset. This path shares the 30-second surface spacing but does not require a menu event. Recovery probes are bounded and do not treat a skipped generated image as corruption.

Cadence stalls alone no longer request swapchain recreation. They retain existing history refresh and transport safety behavior. Confirmed Gamescope Steam UI focus suspends generated acquisition and scheduler observations, preserving real/scaled presentation; menu-throttled lower presents cannot escalate native-stall quarantine. A confirmed return with a proven generated level uses 250 ms of Adaptive settling while retaining that level; three fresh temporal-history frames remain mandatory even if they take longer. The shorter path never advances an already-active stabilization deadline. Every Fixed or Adaptive menu return restarts all three history frames if another menu interrupted warm-up. Unknown feedback ends any menu-only suspension with fresh history and the ordinary one-second settling, but does not authorize a rebuild. A new scheduler after a live policy reset has no proven level and also retains one-second settling and ordinary multiplier qualification. Startup, non-menu stabilization, unsupported transports, backend/fence protection, acquire guards, Fixed collapse probes, optional Dynamic Cadence Recovery, and normal Adaptive load protection retain their independent gameplay policies.

The post-menu performance watchdog applies the same event-backed baseline policy to Fixed and Adaptive generation. Adaptive compares against its configured target; Fixed compares against confirmed Gamescope refresh while retaining its separate cadence-collapse probe. The watchdog requires a confirmed Steam focus round trip and captures an allocation-free history of eight quarter-second output and lower-present aggregates. At menu entry it freezes the median of at least one second of samples last observed within 15 seconds, including games that were stable below the selected target; long menu visits preserve that baseline. After return, four continuous seconds below 75% of the lower of pre-menu output and target, with at least 50% lower-present time, can qualify an action; warm-up, Fixed cadence probes, Adaptive probes, transport recovery, failed presents, and missing measurements cannot qualify. A return episode expires after 75 seconds and allows at most two actions. Frame Generation-only contexts may request application-owned recreation after a successful retirement-fenced lower present, with 30-second surface spacing. Combined scaling first rebuilds the applicable generation policy and temporal history in place. If the same event-backed deficit persists for three seconds after that attempt, one retirement-protected application-owned recreation may follow unless scaling was already memory-constrained; the episode cannot request another. The baseline survives that MAKO-requested same-policy replacement only to prevent fresh qualification from becoming a loop. One second back inside the pre-menu comparison band ends the episode, but the last pre-menu baseline remains available for three minutes on the unchanged context. If a later confirmed menu begins with output below 60% of that baseline while lower presents consume at least 75% of the measured interval, the watchdog compares the return against the retained baseline and permits one two-stage recovery sequence. This covers a delayed transport collapse without turning uncorrelated low FPS into recovery authority. Changed settings, generation mode, context, refresh, extents, unknown/stale focus, Off/ineligible modes, and unrequested replacement invalidate the retained baseline. A heavier scene after a real menu return can still resemble failed recovery, so ordinary direct-baseline episodes retain their existing bounded policy: lower-present time is not proof of a wedged compositor.

History warm-up and normal Adaptive planning use the same application-present start timestamp, captured after real-frame pacing. Backend scheduling and GPU waits must not move the warm-up clock forward within that frame: doing so seeds cadence from only the remaining fraction of a frame and can repeatedly mistake a steady source for a cadence drop. Portable recovery coverage models 12 ms of private work inside steady 60 FPS frames; hardware recovery and external-overlay suites must still prove resumed delivery.

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
