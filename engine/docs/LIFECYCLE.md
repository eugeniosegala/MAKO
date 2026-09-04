# MAKO Renderer lifecycle

This guide explains how MAKO Renderer discovers its environment, creates and replaces resources, applies settings, paces frames, recovers from pressure, and shuts down. It is a conceptual inventory of lifecycle-affecting probes and policies rather than a source-file map. The exact setting contract remains in [Configuration](CONFIGURATION.md), [Runtime configuration transitions](RUNTIME-TRANSITIONS.md), [Spatial scaling architecture](SCALING.md), [Adaptive validation](ADAPTIVE-VALIDATION.md), [HDR pipeline](HDR-PIPELINE.md), and [WSI isolation](WSI-ISOLATION.md).

## Safety model

MAKO separates proof from policy:

- Vulkan ownership, format support, queue support, image counts, external-memory support, and completion are established through Vulkan capabilities, return values, and fences. A timing or performance heuristic must never substitute for one of those proofs.
- Missing evidence fails closed when proceeding could corrupt presentation or violate ownership. Scaling is disabled when surface provenance, output geometry, queue support, format support, or memory admission cannot be established.
- Missing performance feedback normally fails open to native or less-managed presentation. It may reduce smoothness or efficiency, but it must not invent support.
- The application owns swapchain creation, recreation, and destruction. MAKO may request one spec-defined recreation signal when the active presentation boundary can support it, but it does not destroy an active application swapchain merely to apply a setting.
- The original application frame has priority. Frame Generation, scaling, or recovery failure falls back to the real frame wherever the negotiated swapchain geometry still makes that safe.

## Lifetime map

| Lifetime | Created by | What becomes fixed | What can still change |
| --- | --- | --- | --- |
| Process | Vulkan layer activation and profile match | Scaling enablement, layer-chain role, Gamescope isolation, HDR exposure, Game Swapchain Images compatibility, and launcher-owned compatibility behavior | The configuration can be re-read, but changes to process-static choices remain pending until restart |
| Vulkan device | The application's device creation | Available queues, device extensions, synchronization features, and optional swapchain-maintenance support | Per-swapchain policy can change within those capabilities |
| Private Frame Generation backend | The first active swapchain that needs Frame Generation | Selected GPU, DLL-derived model family, FP16 permission, and Ultra Performance topology | Per-swapchain Frame Generation contexts may be replaced; the process backend is shared |
| Surface | Application surface creation | Native window-system provenance and cached scaling eligibility | Current capabilities and output feedback are rechecked at swapchain creation |
| Application swapchain | Application creation or recreation | Source and presentation extents, scaling placement, transport, image pool, color pipeline, and generated-output headroom | Live policy and compatible private resources may change |
| Private scaling or Frame Generation context | Initial swapchain setup or a live private transition | Images, pipelines, model topology, and synchronization objects for that context | A complete replacement can be built beside the old context and switched atomically |
| Present policy | Each successful application present | Nothing beyond the current frame plan | Caps, Fixed or Adaptive decisions, refresh guards, admission, recovery, and diagnostics update continuously |
| Deferred retirement | Application destruction of a swapchain | The retired lower swapchain and its completion evidence | Later same-surface presents, replacement creation, surface destruction, or device destruction complete retirement |

The normal progression is:

```text
process/profile match
  -> device capability registration
  -> surface observation
  -> swapchain negotiation and private context construction
  -> warm-up
  -> steady presentation
  -> live policy update or private-context replacement
  -> application-requested recreation
  -> deferred retirement
  -> surface/device/instance shutdown
```

## Probe inventory and cost

Costs are qualitative: **negligible** is cached state, arithmetic, or a nonblocking readiness check; **low** is a syscall or a small set of window-system queries; **moderate** is a group of Vulkan capability queries; **high** is GPU allocation, pipeline/model construction, or a potentially blocking wait. Driver calls can exceed their expected cost, so bounded waits are called out separately.

### Periodic and change-driven probes

| Probe | Trigger or cadence | Rough work and cost | Effect and safety behavior |
| --- | --- | --- | --- |
| Configuration freshness | At most once every 250 ms while application presents are reaching MAKO; forced at every application swapchain creation | One filesystem metadata check, **low**. A changed file causes a full parse and process-identity rematch, **low to moderate** | The last valid configuration remains active after read or parse failure. A failed parse can be retried after 50 ms, although the outer 250 ms poll usually sets the effective retry cadence |
| Process identity | Once at startup and again only after a changed configuration parses successfully | Executable/name reads and, for Wine or Proton, a process-map scan, **low to moderate** | An unmatched profile leaves MAKO dormant and presentation native |
| Gamescope and display feedback | One synchronous startup sample, then every 250 ms when Gamescope is hinted or detected, otherwise every 1 second on ordinary X11; absent when there is no X display | Several X11 root-property round trips on a background thread, **low**, with no X11 work on the present thread | Publishes cached Gamescope identity, output size, refresh, HDR capability, application HDR metadata, and application HDR intent. Unknown data does not manufacture support |
| Gamescope root discovery | Event-driven when the connection is stale, the observed display is not the required root, or Gamescope was hinted but not identified; retry no faster than once per second | Opening candidate X displays and reading identity properties, **moderate burst** | Only a server-zero display belonging to the same Gamescope process is accepted. Failed discovery retains no unsafe root claim |
| Cached display-feedback consumption | At most once every 250 ms on the presentation path | Lock and copy of cached state, **negligible** | Refresh and output geometry can update policy. HDR active state requires separate stability evidence |
| HDR stability | Evaluated from feedback changes; 750 ms of uninterrupted agreement | Clock comparisons only, **negligible** | Prevents color-pipeline rebuilds from transient HDR evidence. Unknown evidence cancels the candidate and retains the last confirmed state |
| Runtime status publication | Only when requested/applied/pending state changes | Atomic small-file replacement, **low but not steady-state work** | Status is observational and cannot block rendering on cleanup errors. The first publisher performs one bounded cleanup of unlocked MAKO-owned stale entries |

There is no general 10-second, 15-second, or 60-second health sweep. Longer intervals in MAKO are scheduler or recovery backoffs reached only after a specific failure or experiment; they are not background polling.

### Event-driven capability and allocation probes

| Probe | Trigger | Rough work and cost | Effect and safety behavior |
| --- | --- | --- | --- |
| Device support | First application device creation | Queue-family, extension, feature, and optional maintenance capability enumeration, **moderate once per device** | Unsupported queues or synchronization features constrain later presentation rather than being guessed |
| Surface scaling eligibility | First relevant surface-capability observation, cached for the surface lifetime | Surface shape, queue support, format enumeration with at most three incomplete retries, and format-feature queries, **moderate once per physical-device/surface pair** | Any missing provenance, incompatible shape, unsupported queue, unsupported format, or stale relay fails scaling closed |
| Current swapchain capabilities | Every swapchain creation | Surface capabilities plus selected queue/format checks, **moderate** | The current maximum image count and extents are authoritative even when earlier observations were favorable |
| Memory admission | Every scaling-capable swapchain creation and relevant private-resource admission | Heap properties and memory budget/usage when available, followed by conservative allocation estimates, **moderate** | MAKO may reduce the effective scale or disable the feature. The estimate can reject a viable configuration, but it must not overrule an explicit Vulkan limit |
| Direct generated output | Frame Generation context construction | External-image format and usage capability queries, **moderate** | Supported devices can write directly to the presentation-compatible output; otherwise MAKO uses a private image and copy path |
| Private resource construction | Initial context creation or an accepted live transition | Image allocation, exported/imported synchronization, pipelines, model translation, and backend context creation, **high** | The old context stays active until a complete candidate is ready. Failure retains the old context and schedules a bounded retry |
| Frame Generation backend creation | Once, lazily, on the first swapchain that actually needs Frame Generation | DLL discovery and validation, GPU matching, a private Vulkan instance/device, resource extraction/translation, and pipelines, **high one-time cost** | Failure disables Frame Generation without changing the user-owned input. Licensed scaling resources are owned by the private scaling context instead; neither path performs periodic backend work |

### Per-present observations and probes

| Probe | Trigger or cadence | Rough work and cost | Effect and safety behavior |
| --- | --- | --- | --- |
| Cadence observation | Every application present | Steady-clock samples and allocation-free arithmetic, **negligible** | Drives Fixed refresh budgeting, Adaptive planning, caps, and recovery. It observes application delivery, not physical scanout |
| Policy-plan admission | Every present that requests generated outputs | Arithmetic and state checks, **negligible** | May shorten or remove the synthetic plan before any backend work while preserving the real frame |
| Tight-headroom image admission | Once for each planned generated output in HDR or other headroom-tight ordered paths | Zero-time image acquisition, **low per attempted output** | A miss drops synthetic work before the backend starts. Partial Adaptive admission is re-spaced rather than bunched |
| Private-context readiness | While a live replacement is pending | Zero-time fence and timeline checks, **negligible** | Presentation temporarily uses real frames instead of waiting for device-wide idle |
| Retired-image completion | When a protected image is reacquired and while retirement can progress | Zero-time maintenance-fence checks, **negligible** | Recreation is not signalled and destruction is not completed until ownership evidence is available |
| Generated-path recovery | Only after timeout, stall, busy, or backend failure | Zero-time checks in normal recovery; a deliberately bounded generated-image probe after backoff can wait roughly one to three refresh periods, clamped to 8–25 ms and any lower user budget, **low to bounded high** | Failure returns to native drain and increases backoff. Success still passes through a native-only stabilization and history warm-up |
| Render completion | Before reusing generated work in the normal ordered path | Fence wait capped at 150 ms, **bounded high** | A miss abandons generated output for the present and enters backend recovery; it does not sacrifice the real frame |
| Lower presentation | Every delivered image | Driver/compositor queue-present call, **potentially unbounded outside MAKO's control** | MAKO measures a stall only after the call returns, then quarantines generated output. It cannot cancel a currently blocked driver call |
| Optional diagnostics | Only when explicitly enabled | Clock reads around selected operations and conditional stderr writes; slow-operation default threshold 20 ms, plus one-second summaries for active scheduler diagnostics, **negligible until logging, potentially visible when verbose** | Diagnostics are observational. Logging overhead means performance measurements should compare equivalent diagnostic settings |

## Timing ledger

This table centralizes lifecycle timers. Frame-count gates are included because they act like time gates at the current cadence.

| Area | Current interval or threshold | Purpose |
| --- | --- | --- |
| Configuration | 250 ms poll; 50 ms earliest parse retry | Avoid a filesystem read/parse on every present while recovering quickly from an atomic editor update |
| X11 feedback | 250 ms under Gamescope; 1 second on ordinary X11 | Keep compositor state current without X11 round trips on the present thread |
| Root discovery | At most once per second | Bound repeated candidate-display probing |
| HDR confirmation | 750 ms stable evidence | Reject transient output/application HDR state |
| Private resource coalescing | Immediate for discrete scaler method or supersampling changes; 500 ms for numeric/private topology changes | Apply completed choices promptly while coalescing slider movement and related edits |
| Private transition retry | 1 second after a color-readiness exception; 5 seconds after construction failure | Avoid a rebuild or exception storm while retaining the old context |
| Private spatial drain | Up to 50 ms total in one present call | Bound replacement progress without device-wide idle |
| Replacement warm-up | 250 ms real-only, then three real history frames | Prevent interpolation across old/new swapchain history |
| Adaptive stabilization | 3 seconds at initial startup; 1 second after ordinary recreation, cadence reset, or recovery | Establish a trustworthy base cadence before increasing load |
| Fixed collapse detection | 1 second healthy baseline; 250 ms collapse evidence; three native-only samples at least 25% faster; 1 second verification | Require repeatable evidence before dropping or restoring generated load |
| Fixed collapse retry | 2, 5, 15, then 30 seconds | Back off repeated rejected recovery probes |
| Adaptive ramp | 1 second target deficit; 250 ms step delay; 1 second evaluation | Add one generated level at a time and measure its value |
| Adaptive ramp retry | 5, 15, 30, then 60 seconds after repeated rejection; two seconds of stability for rearm | Avoid oscillating into a known-unhelpful load level |
| Adaptive interrupted or failed probe | 2-second cooldown after interruption; 15-second cooldown after failure; 2 seconds of stable evidence before rearm | Keep a cadence disruption from immediately restarting the same load experiment |
| Adaptive near-target preference | 1 second of qualifying evidence; opposite evidence decays twice as fast; each sample contributes at most 100 ms | Prefer native pacing near target without allowing one long frame or rapid oscillation to dominate the decision |
| Smooth Cadence | 2 seconds candidate evidence; 1 second evaluation; 500 ms exit evidence | Enter or leave a constant-multiplier cadence only after stable evidence |
| Smooth Cadence retry | Normally 15 seconds; 60 seconds for the special ordered 2x handoff | Prevent repeated pacing-mode churn |
| Exact-rung automatic cap | 1 second qualification | Select target divided by the already validated 3x–5x multiplier only when that base rung is sustainable |
| Efficiency probe | 5 seconds healthy operation; 250 ms evaluation with at most one 250 ms extension | Test whether one less generated frame preserves the target more efficiently |
| Efficiency retry | 60, 120, or 300 seconds according to failure severity | Make damaging efficiency experiments increasingly rare |
| Adaptive collapse rescue | 1 second collapse measurement; 15-second rescue cooldown; 2 seconds healthy evidence clears failure state | Shed generated load after severe base/output collapse without repeatedly toggling it |
| Conservative discontinuity recovery | 1 second recovered evidence; at most 5 seconds before resuming from zero generated load | Bound HDR or other conservative real-only recovery after a cadence discontinuity |
| Generated-image drain | 250, 500, 1000, then 2000 ms | Back off acquisition pressure before a bounded probe |
| Recovery success | 250 ms native-only stabilization, then three history frames | Re-enter interpolation without stale temporal history |
| Pipeline busy | 250 ms uninterrupted busy state | Treat a single busy frame as harmless but reset history after sustained pressure |
| Lower-present stall quarantine | 2, 10, 30, then 60 seconds; consecutive count clears after 30 seconds healthy | Keep generated delivery away from a repeatedly blocking lower presentation path |
| Swapchain retirement | At least 50 ms compositor grace plus fence completion | Avoid destroying a lower swapchain still being observed or used |
| Device teardown retirement | Up to 250 ms total, then forced finalization | Bound application device destruction while still attempting orderly retirement |

## Configuration lifecycle

Settings are classified by the earliest boundary at which they can be applied safely:

| Class | Examples | Behavior after a live edit |
| --- | --- | --- |
| Process-static | Scaling enablement, Game Swapchain Images compatibility, layer chain, Gamescope WSI isolation, HDR exposure, GPU selection, and Ultra Performance | The requested value is reported as pending and becomes effective on the next process start |
| Swapchain-static | Source/presentation geometry, scaling placement, present transport, color format, WSI image pool, and generated-output headroom | Unrelated live settings apply immediately. The static change waits for application recreation or, on supported presentation boundaries, one safe recreation request |
| Private-resource | Scaling method/sharpness, Flow Scale, lighter model, and capacity within WSI headroom | MAKO coalesces the request, constructs a complete candidate beside the old context, drains only MAKO-owned work, then switches atomically |
| Live policy | Frame Generation switch, refresh threshold, Fixed multiplier within capacity, Adaptive target/limit, base cap, Smooth Cadence, and recovery controls | Applied on the next successful present without rebuilding the swapchain |
| Feedback-derived | Confirmed refresh, output target, HDR state, and presentation role | Consumed from the monitor cache; changes may update policy, start a color transition, or make a future geometry recreation necessary |
| Dormant | Values for an inactive mode or absent resource | Saved and reported, but consume no resource until that mode becomes active |

Requested, applied, pending, and effective state are deliberately distinct. A UI save therefore does not imply immediate resource mutation. Last-value-wins cancellation prevents an older candidate from overtaking a newer edit, and reverting to the applied value cancels pending work.

## Swapchain lifecycle

### Creation

For every application swapchain creation, MAKO:

1. Forces a fresh configuration check so multiple presentation roles use the same revision.
2. Resolves the surface provenance, Gamescope role, current output target, source and presentation extents, queue support, format support, surface limits, and memory admission.
3. Chooses immutable presentation transport, color pipeline, scaling placement, and generated-output headroom.
4. Requests the application minimum image count plus generated capacity when allowed. Ordered Frame-Generation-only paths may reserve one relief image; combined scaling omits that relief reservation for compatibility. The surface maximum remains authoritative, and Game Swapchain Images compatibility preserves the application's original minimum from process start.
5. Creates the lower swapchain. If only an expanded minimum fails with initialization or host/device memory exhaustion, it retries exactly once with the application's original minimum.
6. Reads the images actually returned. Fewer images than the application minimum removes generated capacity rather than assuming headroom.
7. Builds the required scaling and Frame Generation resources. Scaling-context failure rolls back creation because presenting the smaller source rectangle directly would be visibly incorrect. Frame-Generation-only failure retains a valid native swapchain.

Scaling factor is clamped to 1–2, dimensions greater than one are rounded down to even values, and aspect ratio is preserved. Managed variable-size Gamescope surfaces require a confirmed output target. Unsupported opaque/protected/layered/shared-present shapes, formats, queues, or multi-swapchain batches fail closed.

For presentation extents up to 2,304,000 pixels, scaling normally reconstructs the real frame before Frame Generation so reconstruction runs once. Above that threshold, Frame Generation runs at source resolution and each delivered image is reconstructed afterward. The threshold is a performance-placement heuristic; both orders use capability and synchronization proofs, and the selected order stays fixed for that swapchain.

LS1 creation failure falls back to the open MAKO Scaler for the affected context. Direct generated output is used only when external-image capabilities prove it safe; otherwise a private output plus copy is used.

### Warm-up and steady presentation

A cold context begins with real history before generated delivery. A known replacement additionally holds real-only presentation for 250 ms and then gathers three real history frames. A null-old scaled replacement is primed by presenting one application image directly before private scaler work, which avoids first-use compositor deadlocks.

On each present, MAKO updates cached policy, filters unsupported presentation extensions, applies an optional base-frame deadline, records cadence, advances private transitions, chooses a Fixed or Adaptive frame plan, performs admission, schedules backend work, delivers generated images, and finally presents the original real image. When scaling is active, “native” recovery still means the real frame passes through the required scaler; it does not expose an incomplete source rectangle.

Any exception or failed generated path returns to the real frame. If a failure occurs after generated images were acquired, MAKO still initializes and retires those images before the real present so image ownership remains valid.

### Recreation

Applications naturally recreate swapchains for resize, mode changes, out-of-date results, and other WSI events. MAKO can request recreation only when all of the following are true:

- the requested setting genuinely changes swapchain-owned resources;
- the presentation role is allowed to own that change;
- swapchain-maintenance retirement fences are supported;
- a successful or suboptimal lower present has supplied retirement evidence; and
- the same request has not already been signalled.

Managed Gamescope geometry changes are requested only by the upper spatial owner. A compatible non-Gamescope boundary may also request recreation when generated capacity exceeds current WSI headroom. The request is one `OUT_OF_DATE` result; the application remains responsible for creating the replacement. Repeated polls do not loop the signal, while a distinct request can arm a new one.

Explicit old-swapchain linkage is preferred. When an application omits it, MAKO recognizes a replacement only from a unique exact same-device/surface live or retained context. This constrained inference prevents unrelated swapchains from donating temporal history or retirement state.

### Destruction and retirement

Application destruction immediately removes a context from live updates, but lower WSI destruction may be deferred. When maintenance fences and the default allocator permit it, MAKO retains the lower swapchain for at least 50 ms and until its image fences prove retirement. Later presents on the same surface advance this work without blocking.

If a matching null-old replacement arrives while an exact retained swapchain remains, MAKO completes and destroys the retained object before creating the replacement, but does not pass that retained handle back through Gamescope. Surface destruction is terminal and waits for same-surface retirement. Custom allocation callbacks require synchronous destruction because their callback data cannot safely outlive the application call.

Device destruction waits up to 250 ms across deferred retirements and then force-finalizes remaining lower swapchains because the parent device is itself going away. Instance destruction joins the display-feedback monitor before unload. The private backend Vulkan device is intentionally retained for process lifetime to avoid loader-destruction hazards in mixed layer stacks; the operating system reclaims it at process exit. This is retained memory, not periodic work.

## Frame pacing and generation policies

### Fixed

Fixed 2x–5x requests one to four evenly spaced generated frames between real frames. With confirmed refresh feedback, the refresh budget suppresses outputs the display cannot consume; without refresh evidence, Fixed keeps the requested multiplier. The refresh threshold pauses generation when confirmed refresh is at or below the configured value, and missing refresh evidence fails open.

Eligible ordered SDR paths also contain an always-on collapse guard. It establishes one second of healthy baseline, requires 250 ms of degraded evidence, tests three native-only samples that must be at least 25% faster, and verifies the restored policy for one second. Rejection backs off for 2, 5, 15, then 30 seconds. This is a performance heuristic, but its only authority is to remove synthetic load temporarily; it cannot relax resource or ownership checks.

### Adaptive

Adaptive observes real-frame cadence and chooses a deterministic per-present plan under the target FPS and configured 2x–5x ceiling. It starts with three history frames, uses three seconds of startup stabilization and one second after normal discontinuities, raises load one generated level at a time, and rolls back levels that reduce base throughput without enough output gain.

The scheduler uses bounded evidence windows and backoffs for ramping, strict load shedding, stable-cadence entry, near-target native preference, recovery, and efficiency probes. Fractional demand is accumulated as output credit and re-spaced after partial admission. A cadence drop requires three consecutive intervals at least twice the smoothed baseline; a transient burst above the larger of three times baseline or twice target is treated as native activity and does not advance policy clocks. These are performance heuristics whose failure mode is temporary under-generation, over-generation, or extra native-only time, not unsafe Vulkan use.

[Adaptive validation](ADAPTIVE-VALIDATION.md) owns the complete threshold matrix and required game/hardware evidence. The lifecycle rule is that every higher-load experiment has a measured baseline, a short evaluation, a rollback path, and a longer retry delay after failure.

### Base cap, Smooth Cadence, and Dynamic Cadence Recovery

The real-frame cap uses an absolute per-present deadline. A late frame rebases the deadline instead of causing a catch-up burst. The intentional sleep can be as long as the selected cap interval.

Smooth Cadence is an opt-in Adaptive policy that looks for a constant integer multiplier which can satisfy demand without excessive overshoot. It requires stable evidence before entry, evaluates load for one second, and exits after sustained loss of eligibility. Its special ordered 2x handoff is allowed only when the target matches confirmed refresh within the tighter of one hertz or two percent.

When a 3x–5x multiplier is already delivery-validated and the source cadence lies between integer target rungs, the automatic base cap may select exact target divided by multiplier after one second of evidence. It cannot activate an unvalidated multiplier. A user-selected manual cap remains authoritative, while the automatic half-target cap may be released if ordered presentation proves that it is sustaining a severe combined-workload collapse.

Dynamic Cadence Recovery is an opt-in native-only probe with a configurable 0.1–3 second interval, defaulting to two seconds. It is restricted to ordered SDR with usable target/refresh capacity. A probe starts with one native frame and requires three samples at least 25% faster before changing policy. Enabling it disables manual and automatic base caps so the probe measures an uncapped source.

## Pressure and recovery policies

MAKO uses a ladder rather than one global health timer:

1. **Pre-admission:** remove synthetic outputs before backend work when headroom, policy, or nonblocking image acquisition says they cannot be delivered.
2. **Single-frame guard:** one slow generated-image acquisition arms a zero-wait guard for the next present. A guard miss produces one native relief frame and refreshes history.
3. **Native drain:** timeout, repeated slowness, severe acquisition, or budget exhaustion stops generated acquisition for 250, 500, 1000, then 2000 ms.
4. **Bounded probe:** after drain, one generated image is tested within an 8–25 ms refresh-derived budget and any tighter user budget. Success still requires 250 ms real-only stabilization and three history frames.
5. **Target-aware deferral:** if native cadence already reaches at least 95% of target for 200 ms, acquisition probes stay deferred; they resume only after cadence remains below 90% for 100 ms.
6. **Backend recovery:** a 150 ms render-fence miss or backend failure switches to real frames while readiness is polled with zero-time waits. Recovery resets temporal history before generation resumes.
7. **Lower-present quarantine:** a returned lower present slower than the larger of 50 ms or four refresh periods quarantines generation for 2, 10, 30, then 60 seconds.

Normal ordered generated-image acquisition is unbounded when no compatibility timeout is configured. This avoids false timeouts on healthy WSI paths with reserved headroom, but it is the largest MAKO-controlled blocking exposure: a stuck driver/compositor acquire can hold that present call until the driver returns. A configured acquire budget bounds the cumulative waits; headroom-tight paths use nonblocking admission regardless. The lower queue-present call itself is always driver-controlled and cannot be bounded by MAKO.

## Heuristic audit

| Heuristic or inference | Why it exists | Guardrails and failure mode | Assessment |
| --- | --- | --- | --- |
| 2,304,000-pixel scaling placement threshold | Balance reconstruction cost against Frame Generation resolution | Immutable per swapchain; both paths retain full capability and synchronization checks | **Reasonable but tuneable.** Performance can be suboptimal near the boundary, but correctness does not depend on it |
| Conservative memory admission | Avoid committing source, presentation, and model resources into already pressured heaps | Uses live budget when available, reserves non-MAKO headroom, reduces scale or fails closed | **Reasonable and safety-biased.** It can produce false negatives because budget is an estimate, not a reservation |
| 750 ms HDR stability | Prevent transient compositor properties from rebuilding color resources | Unknown cancels the candidate; old confirmed pipeline remains active; construction is atomic | **Reasonable.** Transition latency is preferable to color-pipeline thrash |
| Exact same-device/surface null-old replacement inference | Recover lifecycle continuity from applications that omit explicit old-swapchain linkage | Requires a unique exact match and never reuses an unrelated context | **Reasonable and constrained.** Ambiguity falls back to cold creation |
| 50 ms retirement grace | Cover compositor observation beyond the application's destroy call | Grace is not the proof: image retirement fences and terminal waits remain authoritative | **Reasonable only because fences remain decisive.** Time alone would be unsafe |
| Fixed collapse and Dynamic Cadence native-only probes | Detect when generated load is reducing total output | Multiple faster samples, verification, immediate rejection, and escalating backoff; can only remove synthetic work | **Reasonable but workload-sensitive.** Present timing is an indirect proxy for scanout and GPU pressure |
| Adaptive ramp/load/steady-cadence thresholds | Find a useful multiplier without a GPU utilization API | Baselines, bounded evaluations, rollback, stabilization, and long retry delays | **Reasonable operational heuristics.** They require hardware/game matrices because thresholds can misclassify irregular workloads |
| Near-target native preference | Avoid fractional generation when native cadence is already smooth enough | One second of confirming evidence and asymmetric decay | **Reasonable quality preference.** A misclassification changes pacing, not resource safety |
| Unbounded normal ordered acquisition | Avoid rejecting healthy but variably delayed compositor acquisition | Reserved headroom, slow-call detection after return, next-frame guard, and optional explicit timeout | **Review-worthy.** It preserves compatibility but cannot protect the current call from a driver stall |
| Returned lower-present stall quarantine | Stop adding work after the driver/compositor has demonstrably stalled | Absolute quarantine deadline, escalating durations, 30-second healthy reset, and history warm-up | **Reasonable reactive protection.** It cannot prevent the first blocking call because the evidence arrives afterward |

The first five rows affect placement, admission, state confirmation, or lifecycle association. The cadence rows affect quality and throughput. None is allowed to assert Vulkan capability, completion, or image ownership.

## What MAKO does not probe

- MAKO does not poll GPU utilization, temperature, power, or driver-wide memory pressure on a background timer. Memory budget is sampled for resource admission, and cadence is the runtime performance signal.
- MAKO does not observe physical scanout timestamps. Present/acquire duration and application cadence are proxies, so VRR, compositor buffering, and unrelated GPU work can complicate interpretation.
- MAKO does not continuously rescan Vulkan capabilities. Device and surface facts are cached for their owning lifetime and current swapchain limits are rechecked at creation.
- MAKO does not infer safety from average frame rate. Capability, return-code, queue, fence, and ownership checks remain separate from performance decisions.
- MAKO does not support managed multi-swapchain present batches by guessing semaphore ownership; those shapes fail closed before synchronization is consumed.
- MAKO cannot preempt a blocking Vulkan driver call. Recovery begins after control returns.

These limits are deliberate, but they define where diagnostics and real-hardware evidence matter most. Any new probe should document its owner lifetime, trigger, worst-case foreground cost, cache invalidation, failure behavior, and whether it supplies safety evidence or only policy evidence.
