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
- Fixed refresh budgeting and direct ordered-FIFO acquire recovery;
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

Full admission preserves requested timestamps. A batch admitted before backend work is evenly re-spaced across the real-frame interval when only part of it fits instead of taking an early prefix. Normal Adaptive ordered SDR uses an explicit presentation-policy adapter only for variable refresh. Fixed refresh preserves the 3.3 sequential acquire contract: MAKO Decky's configured 50 ms application-present ceiling lets ordinary FIFO image release complete instead of classifying a transient zero-wait miss as failed generated load. Requested or active VRR uses the same finite ceiling because lower-image release is not locked to a fixed output-period ladder. A shorter explicit ceiling still wins, and the boundary never feeds MAKO's own acquire delay back into a measured source interval. A VRR timeout arms zero-wait pre-admission on following frames and generated delivery resumes as soon as a full batch is available, but the bounded wait is re-armed only after consecutive full batches account for one lower-swapchain turnover; a single lucky frame therefore cannot repeatedly expose gameplay to the 50 ms ceiling. The turnover proof is derived from returned image count and batch size, not refresh rate, source FPS, device identity, or a timer. Admission pressure does not directly demote the scheduler. Allow Tearing does not select another policy because MAKO's private ordered SDR swapchain remains FIFO. Sustained delivery loss can still reject an active multiplier through its normal delivery window, while partial headroom pre-admission during a multiplier evaluation can lower the capacity for that WSI context until game-owned recreation. The original real frame is outside the generated plan and keeps priority on failure.

### VRR and fixed-refresh pacing paths

This map covers Frame Generation on MAKO Renderer's private ordered Gamescope SDR transport. Both columns use a lower FIFO swapchain. MAKO reads Gamescope VRR feedback to choose pacing behavior; it does not enable or disable physical VRR or change the user's Steam setting. VRR-aware behavior is selected when Gamescope explicitly reports active VRR, or reports VRR enabled without explicitly reporting an incapable connector. Missing VRR feedback selects the fixed-refresh column. An overlay can temporarily inhibit active scanout while the enabled setting keeps the VRR-aware path selected. The [WSI isolation guide](WSI-ISOLATION.md#vrr-and-tearing-feedback) owns feedback sampling and transport details.

| Mode or boundary | Fixed refresh or absent VRR feedback | Requested or active VRR |
| --- | --- | --- |
| Fixed 2x–5x with Smooth Cadence | With confirmed refresh, full private-output capacity, no manual Base FPS Cap, and no recovery guard, request the configured full batch and let FIFO backpressure pace it. | Same full-batch FIFO path and eligibility; VRR feedback does not change the chosen Fixed multiplier. |
| Fixed without that FIFO eligibility | Apply the fractional display budget when refresh is known; otherwise retain exact Fixed behavior. An explicit Base FPS Cap, Dynamic Cadence Recovery, or acquire recovery keeps this path. | Same budget, caps, and recovery guards. Physical VRR may still be active, but it does not make an ineligible Fixed plan request a full batch. |
| Fractional Adaptive with variable generated counts | Keep the target-output clock and the selected real/generated ratio; place admitted timestamps evenly within the real-frame interval. | Same variable plan and target clock; VRR does not force a constant batch. |
| Fractional Adaptive with an accepted constant integer cadence | Retain the accepted generated count instead of the moving target clock; no VRR-specific FIFO handoff. | An uncapped, accepted 2x–5x plan may hand its existing count to FIFO when Smooth Cadence, ordered transport, target-matched refresh, projected output rate, and recovery guards qualify. A manual or explicit Fractional Real Frame Priority cap prevents this handoff. |
| Steady Adaptive at a proven 2x | With target-matched refresh and ordered transport, hand pacing to FIFO. | Same proven 2x FIFO handoff; short application-return jitter can retain it while sustained scheduler or delivery loss still ends it. |
| Steady Adaptive at a validated 3x–5x | Keep scheduler/target-clock pacing; after one second of qualifying cadence, refine the automatic base cap to target divided by the validated multiplier. Release it after sustained loss or a safety transition. | With target-matched refresh and a projected full rate from 98% to 125% of target, request the validated complete batch and hand pacing to FIFO. Outside a qualified handoff, retain the ordinary automatic half-target base cap; a planned lower-load probe pauses handoff without consuming its retry, while genuine guard loss starts the per-rung retry. |
| Adaptive generated-image acquire pressure | Use the configured sequential FIFO acquire contract; an explicit timeout, budget exhaustion, or a completed batch that reaches the configured cumulative acquire deadline can enter ordered native-drain recovery. | For an ordinary full-headroom Adaptive batch, bound the application-present acquire wait to 50 ms or a shorter configured ceiling. After timeout, use zero-wait pre-admission until returned-image turnover proves blocking can be retried; keep the real frame and validated scheduler level eligible. |

The Steady and Fractional handoffs require Smooth Cadence, a confirmed refresh matching the target, sufficient output capacity, and no active ordered-acquire recovery. Steady 3x–5x and accepted Fractional 3x–5x use the 125% upper projected-rate bound; their 2x entry bound is 102%. Dynamic Cadence Recovery and explicit caps retain their documented authority. Fixed generated-image acquisition and direct-failure recovery use the same contract in both columns. Feedback changes can reset affected pacing helpers without resetting the Adaptive scheduler, changing its validated multiplier, or recreating the swapchain. These are MAKO plan and queueing rules; they do not prove physical scanout timing.

### Fixed mode

Fixed uses the configured 2x–5x multiplier. When Gamescope reports a nonzero refresh, a fractional display budget normally suppresses outputs that the display cannot consume; without refresh feedback, exact Fixed behavior is retained.

With Smooth Cadence enabled and an ordered Gamescope SDR transport, Fixed requests its full configured multiplier and lets ordered FIFO backpressure pace real frames, including when Gamescope requests or activates VRR. This avoids a separate CPU sleep clock that can drift against physical VRR presentation. The policy requires confirmed refresh feedback, full private-output capacity, no explicit real-frame cap, and no active acquire recovery or Dynamic Cadence Recovery. The display budget continues to observe source cadence so a transition out of this policy restores the applicable budget without a cold start. First-frame and long-stall guards remain active. Without Smooth Cadence or when these guards fail, Fixed retains its normal fractional display budget.

### Adaptive mode

Adaptive varies generated work toward `target_fps` without exceeding `adaptive_max_multiplier`. Fractional mode normally owns the long-term output budget and keeps timestamps evenly spaced within each real-frame interval; VRR consumes that target-clocked cadence without changing its multiplier rule. Near target, it may prefer native presentation when measured interval quality and output coverage are already sufficient.

`adaptive_fractional_real_frame_priority = auto` is the compatibility default and does not alter Fractional planning or the saved manual `base_fps_cap`. Explicit Low, Medium, High, and Very High priorities apply target-relative real-frame caps using 3:2, 2:1, 3:1, and 4:1 real/generated ratios. At a 120 FPS target those caps are 72, 80, 90, and 96 real FPS. The explicit cap takes precedence over the manual cap only while Fractional Adaptive is active; Fixed and Steady ignore it. A priority or target change resets the real-frame pacer and affected scheduler policy through the ordinary live profile transition.

When demand temporarily exceeds the validated multiplier ceiling, the target clock retains less than one output of bounded credit instead of wrapping it away. This lets a source returning from an external throttle recover an achievable integer cadence without dropping generated work on ordinary timing jitter, while still preventing impossible whole-output debt from accumulating or producing later catch-up bursts.

Smooth Cadence may retain a delivery-validated integer multiplier. Fractional Adaptive normally retains its moving target clock, but an accepted constant cadence already replaces that clock with a fixed generated-frame count. Under requested or active VRR with ordered Gamescope SDR and matching refresh, this accepted Fractional 2x–5x cadence may hand pacing to FIFO when it has no manual or Fractional Real Frame Priority cap and Dynamic Cadence Recovery is off. Its projected full rate must be within 98–102% of target for 2x or 98–125% for 3x–5x. Variable Fractional plans keep their target clock and do not request extra generated work.

On ordered Gamescope SDR with matching refresh, Steady Adaptive hands a proven 2x cadence to FIFO. Under requested or active VRR, a validated 3x–5x Steady rung can also request its complete integer batch and hand pacing to FIFO when its projected full rate is at least 98% and at most 125% of the target; the upper bound limits the predicted real-FPS reduction from FIFO backpressure to 20%. This higher Steady experiment does not require a separately accepted constant-cadence probe, so it can cover a validated variable 3x workload such as a 50 FPS source at a 120 FPS target. Probe, recovery, and transport guards restore normal pacing. A planned lower-load efficiency probe pauses a higher Steady VRR FIFO handoff without starting its 60-second retry, so rejection can restore that rung and acceptance does not delay a later revalidated return. A genuine lost qualification or safety guard still starts the retry only for that rung. Confirmed fixed-refresh eligibility keeps the established target/multiplier base-cap ladder for validated Steady 3x–5x demand and does not use either higher-rung FIFO policy. Fixed requests its full multiplier under eligible ordered FIFO regardless of VRR feedback. Explicit Fixed caps, Dynamic Cadence Recovery, transport recovery, and insufficient private-output capacity keep Fixed's normal budget policy.

VRR feedback never enters Adaptive promotion, target, ceiling, delivery validation, or recovery. It may select a full-batch FIFO presentation policy for an already validated higher Steady rung; the scheduler rechecks that rung after its frame stages before requesting the full batch. It may also hand an already accepted constant Fractional plan to FIFO without changing that plan's generated count. A live feedback transition resets only the pacing helpers whose ownership changed; it preserves scheduler history and the validated Adaptive level, and it cannot arm recovery or recreation. Allow Tearing is recorded but has no scheduling effect because the ordered SDR path deliberately owns FIFO.

Every ordinary Adaptive promotion from 2x through 5x uses one adjacent-workload rule. The higher load is accepted only when generated delivery is healthy and its target-capped displayed-FPS gain is at least the real-FPS cost measured during the same bounded probe. There are no multiplier-specific retention percentages, base-collapse boundaries, bridge probes, or 2x-only hitch exception. A rejected level preserves the best proven lower-load baseline and cannot be retried immediately against a degraded replacement. Higher-level retries require that proven lower load to recover. After a rejected first 2x probe, native presentation may instead adopt a new gameplay baseline when the bounded cooldown has elapsed and output remains continuously below the near-target envelope for two seconds; retry delays back off from 15 to 30 and 60 seconds after repeated rejection. This prevents an ordinary scene change from leaving Adaptive permanently native-only without weakening the same-probe workload comparison. Later gameplay cadence cannot retroactively classify an accepted workload as a transport failure or authorize recreation; Adaptive's established cadence-rebasing policy remains feature-owned. Lower-load Smooth Cadence efficiency probes retain their separate user-selected pacing behavior.

The fixed-refresh Adaptive 3x–5x base-cap policy requires one second at or above 95% of its selected cadence before entry. An active cap tolerates dips down to 90%; a lower cadence or a return to the previous multiplier's range must persist for 250 ms before releasing it. This prevents entry-threshold jitter from repeatedly resetting the real-frame pacer. Transport guards, scheduler recovery, ramp evaluation, loss of eligibility and a return to 2x still restore normal pacing immediately; lower-load efficiency probes retain their separate cap transitions. The Steady 2x FIFO handoff requires a close target match to enter; under active VRR it retains the handoff across short application-return jitter while the scheduler still owns sustained cadence and delivery exits. Higher VRR rungs use the 125% entry bound and a per-rung retry delay. Fixed's FIFO path has no automatic base-cap qualification or timed CPU sleep.

`adaptive_auto_base_fps_cap` normally starts at half the target. Smooth Cadence rescue may release only this automatic cap while it performs its existing real-only pacing measurement; manual, explicit Fractional priority, and Fixed caps remain authoritative.

Cadence-drop detection holds the proven interval while confirming three consecutive samples at least twice as long. It also accumulates those candidate samples in a shadow smoothed estimate: if the next sample rejects the sustained-drop hypothesis, the estimator incorporates the delayed observations before the current interval. Discarding them instead can lock onto only the fast half of a bursty stream, for example reporting 125 FPS for alternating 8/32 ms intervals whose actual rate is 50 FPS, and suppress useful generated work. Timing resets, impossible fast bursts, and transport backoff discard pending drop evidence. The existing hard-stall, history, delivery, and multiplier bounds still apply. Portable tests cover both recovery policies and Smooth Cadence settings; Gym's `adaptive-bursty-source` row exercises a 75 → noisy 50 → 75 FPS round trip with a 150 FPS target and 240 Hz headless Gamescope. This is source-cadence evidence relevant to mouse-stutter investigation, not a simulation of HID polling, game input, or physical scanout.

### Dynamic Cadence Recovery

Dynamic Cadence Recovery is an optional per-profile policy for games or emulators that switch native rates. Ordered FIFO can make a native 60 FPS mode look like 30 FPS when generation is active, so MAKO periodically requests a native-only sample. Three consecutive samples at least 25% faster than the captured baseline confirm the new cadence.

Adaptive keeps its configured target and ceiling. Fixed uses confirmed Gamescope refresh as its target and treats its multiplier as a ceiling; without refresh feedback it stays exact Fixed. Enabling recovery disables manual and automatic base caps. The probe does not run on the nonblocking HDR transport.

## Transport recovery

Fixed ordered generated-image acquisition uses one application-present deadline, not a refresh-derived slow-call threshold per generated image. Each image receives only the remaining cumulative configured budget. An unconfigured Fixed path retains its existing unbounded compatibility contract, while headroom-tight admission remains nonblocking. Normal Adaptive ordered delivery preserves the established sequential FIFO acquisition point. Fixed refresh retains the configured sequential acquire contract from 3.3. Requested or active VRR uses the same finite 50 ms application-present ceiling because its release timing is variable. The VRR deadline cannot recursively enlarge when its own wait lowers measured source FPS, and any shorter explicit ceiling remains authoritative.

A `VK_TIMEOUT`/`VK_NOT_READY` result, exhaustion of the configured cumulative acquire budget, or completion of an ordered batch after that cumulative wall-clock budget has been reached starts blocking ordered transport recovery. The deadline is the explicit application-present budget; it is not derived from output FPS, GPU load, device, resolution, scaling, or post-processing cost. Fixed-refresh Adaptive and direct Fixed deadline failures enter the ordered native-drain retry ladder of 250 ms, 500 ms, one, two, five, 15, and 30 seconds. Recovery probes retry one image using the explicit configured acquire budget or one confirmed display period, and without either contract the retry is nonblocking. An Adaptive VRR timeout instead drains already scheduled backend timeline values, presents the real frame, and arms zero-wait admission before later backend work. Availability resumes generated delivery immediately, while consecutive complete batches must cover one returned-image turnover before another bounded wait is permitted; persistent misses remain visible to the ordinary delivery window without turning one VRR hold into a 5-30 second multiplier shutdown. There is no FPS-derived acquire deadline, native-cadence hold, repeated-episode counter, failure-count-derived acquire timeout, or timed recovery window.

A confirmed Steam-menu return or an explicit live generation-policy transition arms one one-shot recreation permission. Multiple policy writes under confirmed Steam UI focus are last-value-wins: generation remains suspended, the scheduler is reconstructed once from the final values on return, and that return owns the single transport permission. The first generated-image acquire batch completed within its configured budget consumes the permission without action. If an acquire timeout, budget exhaustion, or cumulative deadline overrun occurs first, one successful retirement-protected real present may request a game-owned recreation; the context cannot signal it twice, and a replacement context initializes from the current Gamescope return sequence so the same old event cannot rearm a loop. Fixed direct deadline failures retain bounded in-place backoff. Adaptive VRR gameplay instead uses bounded delivery followed by nonblocking pressure retry; ordinary VRR pressure cannot authorize recreation or directly start a long multiplier backoff.

A recovery probe runs only when temporal history is ready and the current Fixed or Adaptive policy requests generated work. A real-only policy frame performs no generated-image acquisition and leaves the pending probe intact for the next eligible frame; it cannot count as a probe failure or increase recovery backoff.

Lower `QueuePresentKHR` duration is diagnostic only. A successful but slow present cannot quarantine generation, change a multiplier, warm history, or request recreation. MAKO cannot reliably distinguish compositor throttling, stacked GPU work, VRR behavior, or a game-scene change from a wedged queue by elapsed time alone.

Cadence stalls and low FPS never request swapchain recreation. Confirmed Gamescope Steam UI focus suspends generated acquisition and scheduler observations while preserving real/scaled presentation. Menu entry discards incomplete ordered-acquire retry state; a confirmed return restarts exactly three history frames and arms the one-shot direct-failure permission across exact Fixed, Fixed + Dynamic Cadence Recovery, Fractional Adaptive, and Smooth/Steady Adaptive. There is no additional time-based menu-return settling delay when a proven Adaptive level exists. Live Fixed/Adaptive mode, multiplier, base-cap, target, Smooth Cadence, and Dynamic Cadence Recovery changes apply the same transient boundary. A confirmed return retains a proven Adaptive level and its best lower-load baseline. When that level satisfied the target before the menu, it remains the post-return workload ceiling until it satisfies the same configured target again; a menu-induced source deficit cannot launch a higher-multiplier probe. Once recovered, later gameplay deficits use the ordinary adjacent-workload rule. Unknown feedback may end menu-only suspension with fresh history but cannot authorize a rebuild. Fixed never launches a cadence-collapse probe: its user-selected multiplier remains exact unless a direct Vulkan failure owns bounded transport safety.

After a confirmed menu return or Fractional/Steady recovery event, history warm-up and scheduler planning use the same application-present start timestamp, captured after real-frame pacing. Backend scheduling and GPU waits must not move that event-recovery clock forward within the frame: doing so seeds cadence from only the remaining fraction of a frame and can repeatedly mistake a steady source for a cadence drop. Ordinary generated-image timeout recovery deliberately retains the 3.3 completion boundary, preventing recovery work from being reinterpreted as new gameplay cadence. This applies to Adaptive and Fixed + Dynamic Cadence Recovery; exact Fixed has no scheduler cadence history. Portable recovery coverage models 12 ms of private work inside steady 60 FPS event-recovery frames; hardware recovery and external-overlay suites must still prove resumed delivery.

`GeneratedDeliveryWindow` compares requested outputs with outputs queued inside MAKO's budget. It does not claim compositor scanout. Diagnostics distinguish requested, admitted, scheduled, and delivered counts.

Private Flow Scale, model, or generated-capacity changes retain the old policy while replacement resources are prepared. Capacity growth that does not fit the current WSI pool remains pending for game-owned recreation. [Runtime configuration transitions](RUNTIME-TRANSITIONS.md) owns that lifecycle. Ultra Performance is a restart-bound resource policy, not a separate scheduler.

## Runtime validation

Use the sibling MAKO Gym checkout between portable policy tests and commercial-game testing. Its manifests own current rows, assertions, and thresholds:

```bash
scripts/run-mako-gym.sh --suite recovery --list
scripts/run-mako-gym.sh --suite recovery --filter '(stall|cadence-drop|recreate)$'
scripts/run-mako-gym.sh --suite pacing --list
scripts/run-mako-gym.sh --suite pacing --filter '^vrr-(fixed-smooth|steady)-'
```

Select coverage according to [Testing MAKO](../../TESTING.md#selecting-mako-gym-coverage). Scheduler changes normally begin with `recovery`; pacing-owner and Gamescope VRR-feedback changes use `pacing`; external overlay pause/throttle and workload-proven source-return changes use `external-recovery`; construction changes add `vulkan`; Gamescope lifecycle changes add `gamescope-e2e`; translation changes add `proton-e2e` or `proton-compatibility`. A filtered pass proves only its selected rows.

The external-recovery inventory pairs unidentified-overlay controls with confirmed Gamescope menu rows across Adaptive 2x and 3x, Fixed, Frame Generation-only, combined Frame Generation plus scaling, and scaling-only operation. Pause, covered-throttle, and repeated short/long menu journeys must independently prove source recovery, target-output recovery, focus sequencing, fresh history, generation suspension where applicable, and context stability. Portable policy tests separately cover Fractional/Smooth mode, automatic base-cap state, 2x–5x ceilings, failed-baseline retention, exact Fixed stability, transition-scoped acquire failure, inert successful transport, and the absence of FPS-authorized recovery so hardware rows do not carry the full combinatorial burden.

For affected changes, cover these runtime boundaries:

| Boundary | Minimum evidence |
| --- | --- |
| Generation modes | Live `0x`, Fixed 2x and affected higher multipliers, Fractional Adaptive, Smooth Cadence, and unreachable targets. |
| Cadence changes | Startup, gameplay/menu rate changes, true fixed-rate rejection, short hitches, long interruptions, and fast-present bursts. |
| Presentation | Ordered SDR, explicit acquire failure, configured cumulative acquire-deadline overrun, slow lower-present non-authority, focus and overlays, resize, recreation, and shutdown. |
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
