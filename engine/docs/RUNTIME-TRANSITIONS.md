# Runtime configuration transitions

This document defines how saved configuration becomes running process state: live updates, private-context rebuilds, game-owned recreation, process restart, mixed-update behavior, and Ultra Performance's static resource policy.

[Configuration](CONFIGURATION.md) defines settings, [Adaptive validation](ADAPTIVE-VALIDATION.md) owns cadence and frame plans, [HDR pipeline architecture](HDR-PIPELINE.md) owns colour transitions, and [WSI isolation](WSI-ISOLATION.md) owns process-start discovery and transport. This guide owns only their shared lifecycle contract.

## Lifetime boundaries

A setting belongs to the earliest lifetime boundary that can safely establish all state it affects. A convenient UI toggle does not make its implementation live-safe.

| Boundary | State established there | Completion event |
| --- | --- | --- |
| Process-start discovery | Scaling enablement, implicit-layer membership and order, Gamescope WSI isolation, HDR exposure, launcher compatibility environment | Start a new game process |
| Process-wide backend | DLL, effective FP16 permission, GPU selection, Ultra Performance backend policy | First backend construction, or a new game process once a backend already exists |
| Game-owned swapchain resources | Spatial-scaling shape, Flow Scale, frame-generation model selection, generated-image capacity | A game-owned recreation; compatible non-Gamescope maintenance1 contexts may request one, while managed Gamescope waits for a natural recreation |
| Game-owned presentation shape | Pacing | A natural game-owned recreation |
| Private spatial context | Native/MAKO/LS1 method and sharpness | Next presentation boundary; method selections apply immediately and sharpness edits coalesce for 500 ms before rebuilding |
| Live context policy | Generation enable, refresh threshold, Fixed/Adaptive selection within reserved capacity, target, caps, cadence policy | The next successful configuration reload and application-present boundary |
| Compositor safety feedback | Confirmed refresh rate and HDR application state | A stabilized runtime feedback sample, independently of profile reload |
| Stored metadata or dormant policy | Values that do not alter current profile selection and values belonging only to an inactive mode | Saved immediately; no running-state reset until the value becomes active |

The process-wide backend is created lazily for the first managed swapchain. Before it exists, the latest configuration can still become its construction input. After construction, the Renderer compares requested DLL, FP16, GPU, and Ultra state against that actual baseline rather than against the previous file contents, so a pending process-static change remains pending across later saves.

## Runtime update pipeline

`Root::update()` runs from the application-present path, but the expensive parts are bounded and change-driven:

1. Gamescope refresh and HDR feedback are sampled and stabilized as runtime safety inputs. They are not user-profile reloads.
2. The watched configuration is checked at most once every 250 ms. An unchanged file causes no profile planning, including in an Ultra Performance process.
3. A changed file is parsed, the process is identified again, and the latest matching profile becomes Root's requested profile.
4. Toggling Ultra Performance or Scaling is a process-restart transition, but compatible scheduler and scaler-method fields from the same write can still apply at their normal boundaries.
5. Every existing swapchain context calls `planProfileUpdate()` with its actually applied profile, the requested profile, its reserved generated-image capacity, and current resource availability. A split spatial role receives Frame Generation Off and therefore has zero active generated-image demand; only the frame-generation owner can defer a capacity change.
6. `Swapchain::updateProfile()` applies the live-safe merged profile and resets only the runtime state invalidated by those applied fields. Deferred values remain requested at Root for their later boundary.
7. Native/MAKO/LS1 method edits queue a private spatial-context rebuild for the next present. Sharpness-only edits retain a 500 ms quiet period so continuous controls coalesce. Spatial factor, Flow Scale, FG model, and generated-capacity edits can request one game-owned recreation after a maintenance1-fenced present on a compatible non-Gamescope context; managed Gamescope waits for natural recreation, as does pacing everywhere. Scaling waits for process restart. Diagnostics distinguish private application, requested or natural recreation, and process restart. One update may have multiple outcomes.

MAKO never initiates application swapchain destruction or synthesizes an out-of-date result for a private scaler change. It builds a replacement scaler first, waits for application-device work only at that explicit transition, swaps it in, warms frame-generation history, and retains the old scaler with bounded retry on failure. Natural game-owned recreation removes the old context from live updates immediately but defers lower WSI destruction until a present on the same creating surface, a 50 ms compositor grace, and all retirement fences complete. If an upper WSI recreates after destroying its visible object and supplies no lower `oldSwapchain`, MAKO may pass the exact retained same-device, same-surface lower handle once; this never changes application ownership or bypasses retirement proof. Surface destruction is the terminal same-surface boundary and drains that proof before forwarding `vkDestroySurfaceKHR`. Natural recreation always substitutes the process's actual backend baseline, so it cannot falsely apply pending GPU or Ultra Performance changes.

Each scaling role negotiates KHR swapchain maintenance1, or its EXT predecessor, only when the device exposes both extension and feature. The split chain preserves this contract across the upper MAKO role, Gamescope WSI, and lower spatial role. MAKO preserves an upstream `VkSwapchainPresentFenceInfoKHR`; otherwise it attaches preallocated per-image fences. These fences prove natural retirement and never trigger model-switch recreation. A caller allocation callback cannot outlive `vkDestroySwapchainKHR`, so that rare path drains synchronously. Surface and device teardown never bypass unsignaled proof or require a worker thread.

## Requested, applied, and pending state

The transition engine deliberately keeps three facts separate:

| Fact | Owner | Meaning |
| --- | --- | --- |
| Requested profile | `Root::active_profile` | Latest normal-mode profile selected from configuration |
| Applied context profile | `Swapchain::profile` | Values that are true for that already-created context |
| Backend construction baseline | `Root::backendGlobal` and `Root::backendProfile` | DLL, FP16, GPU, and Ultra inputs actually used by the process-wide backend |

`ProfileUpdatePlan::appliedProfile` is a merge, not a copy of either endpoint. It starts with the requested profile, restores every field that cannot cross the current boundary, and then classifies the remaining effective differences. This prevents a pending field from overwriting the context's actual state while allowing unrelated live-safe work to proceed.

For example, if one save changes Flow Scale from 1.0 to 0.75 and Base FPS Cap from Off to 45, the cap applies live while Flow Scale remains pending for game-owned recreation; on managed Gamescope that means a natural recreation. A later Frame Generation Off change still applies immediately, the Flow Scale request remains pending, and reverting Flow Scale to 1.0 clears that deferral. If the same save also changes GPU, the transition reports both recreation and process-restart deferrals instead of collapsing them into one status.

## Setting transition matrix

| Setting or input | Normal running process | Runtime effect |
| --- | --- | --- |
| `frame_generation_enabled = false` | Always live | Resets generation pacing/recovery state and takes the native presentation path; no model scheduling, copies, private fences, generated-image acquisition, or generated presents run |
| `frame_generation_enabled = true` | Live when startup provisioning succeeded; otherwise process restart | Every matched process requests interop and private resources at construction, then reuses them, resets admission/recovery state, and starts with fresh temporal history where required |
| Refresh threshold | Live | Updates the stored guard; a change that crosses the effective enable boundary performs the same enable/disable resets |
| Gamescope refresh feedback | Live safety input | Re-evaluates the threshold and refresh-targeted cadence policy independently of profile reload |
| Fixed/Adaptive mode | Live within reserved generated-image capacity | Resets fixed timing, the real-frame pacer, and generation scheduler policy |
| Fixed multiplier | Live when Fixed is or becomes active and capacity is available | Updates the configured generated count and resets affected timing/scheduler state; a dormant Fixed multiplier in Adaptive mode is stored without a false runtime reset |
| Adaptive target, ceiling within capacity, and Smooth Cadence | Live | Resets generation scheduler policy and relevant pacing handoff state |
| Dynamic Cadence Recovery | Live | Rebuilds generation scheduler policy; the UI/schema contract separately owns its cap exclusivity |
| Dynamic Cadence probe interval | Live | Reschedules the inactive probe interval without discarding validated cadence or an active confirmation |
| Base FPS Cap and Adaptive auto-cap | Live | Resets the real-frame pacer, fixed-window timing, and scheduler policy affected by the effective cap |
| Scaling enable (`scaling_enabled`) | Process restart | Existing process retains its actual scaling configuration and WSI membership; Decky provisions or removes the WSI lane only for the next launch |
| Native/MAKO/LS1 method | Live when scaling is provisioned | Rebuilds only the private spatial context at the next present, keeps the WSI objects and extents stable, warms FG history, and retains the old method on failure; Native uses a model-free linear transfer graph |
| Scaling sharpness | Live when scaling is provisioned | Coalesces edits for 500 ms, then uses the same private spatial rebuild without changing WSI ownership |
| Scaling factor | Game-owned recreation | Existing context retains its extent contract; a compatible non-Gamescope maintenance1 context requests one recreation after a fenced present, while managed Gamescope waits for the next natural resolution/surface recreation |
| Flow Scale and Lighter FG Model | Game-owned recreation | Existing LSFG context retains its construction; a compatible non-Gamescope maintenance1 context requests one recreation after the 500 ms quiet period, while managed Gamescope waits for natural recreation |
| Pacing shape | Natural recreation | Existing game-owned swapchain and presentation transport remain unchanged |
| Generated-image capacity growth | Game-owned recreation | Current policy stays within capacity; a compatible non-Gamescope maintenance1 context can request recreation, while managed Gamescope waits for a natural boundary; unrelated live controls still apply |
| GPU | Process restart | Existing and naturally recreated contexts retain the backend's actual GPU identity |
| DLL and FP16 policy | Process restart | Pending status is measured against the constructed backend, not only the previous configuration file |
| Ultra Performance | Process restart | Startup FP16 and resource policy remain active, while unrelated compatible controls from the same write continue through their normal live/recreation boundaries |
| Explicit WSI compatibility, post-process layer, HDR exposure, Zink, ALSA, and other launcher compatibility | Process restart | These values affect discovery, environment, or application initialization before the Renderer can reload a profile |
| Stable HDR application feedback | Live safety transition | May rebuild MAKO's private colour resources after readiness checks, but never changes the immutable game-owned presentation transport |
| Active profile selection or match result | Live for compatible fields | The selected profile is planned through the same merge; losing the match disables generation immediately, while process-static differences remain pending |
| Metadata that does not alter current profile selection or inactive-mode-only values | No immediate runtime work | Values remain available for later selection or activation without resetting unrelated scheduling state |

When a field's effective value is unchanged, its storage representation may still be updated without runtime work. Effective comparisons matter for presets such as Ultra Performance, auto-cap behavior, and dormant Fixed/Adaptive values.

## Frame Generation Off resource contract

Frame Generation Off means no per-frame generation execution. The real frame may still pass through the configured Base FPS Cap because that limiter is an independent requested feature, and compositor safety monitoring remains active.

Every matched process provisions Frame Generation interop, backend, private images, and synchronization even when it starts Off. The Off path bypasses those resources: no model scheduling, input copy, generated acquisition, or generated present runs. Retaining them trades startup memory for immediate Off→On. Failed provisioning leaves native or independent scaling active and reports generation as restart-pending. Stable HDR feedback may still update retained colour resources for a later enable.

Ultra Performance no longer removes the live-switch foundation when Frame Generation starts Off. It retains the same interop and active-policy-sized private resources as an On startup, while the live Off branch performs no generation work. The preset's effective FP16, Flow Scale, lighter-model, and capacity policy is still selected at process construction, so toggling Ultra Performance itself waits for restart.

If a normal running profile stops matching, existing contexts disable generation immediately and retain their resources. A newly started process with no matching profile remains dormant and does not activate MAKO's swapchain path.

## Ultra Performance invariants

Ultra Performance is a process-start resource policy, not a faster branch inside the live scheduler:

- the preset's FP16 choice and resource-sizing policy remain fixed until restart;
- generated-output capacity is sized only for the startup active Fixed or Adaptive policy;
- explicit Frame Generation Off retains private generation resources and generation-specific application Vulkan modifications but performs no per-frame generation work;
- compatible scheduler fields and private spatial model changes continue applying live, while extent, LSFG model, and capacity edits retain their game-owned recreation boundary;
- refresh and HDR feedback, native fallback, bounded waits, recovery, and presentation safety continue because they are compositor/runtime inputs rather than profile toggles; and
- toggling Ultra Performance itself never partially changes the running backend's static policy.

Any new control must explicitly classify both its ordinary lifetime and its behavior inside an already-active Ultra Performance session, with performance evidence on low-end hardware, updated documentation, and focused tests.

## State-reset contract

A live update must reset the smallest state set that can contain assumptions invalidated by the applied field:

- enable/disable transitions clear fixed-window measurements, pacer ownership, generated-image admission, ordered-acquire recovery, and history warm-up state as appropriate;
- mode, target, ceiling, Smooth Cadence, and cadence-recovery policy changes rebuild the generation scheduler policy;
- an effective cap change resets both real-frame pacing and scheduler policy that observes the capped cadence;
- a probe-interval-only change updates the scheduler timer without destroying validated policy;
- a dormant value or metadata-only change performs no scheduler reset; and
- a deferred value performs no reset until its owning boundary applies it.

Do not use a broad "configuration changed" reset. It would discard validated cadence on harmless edits, increase warm-up and stutter, and make mixed live/deferred writes behave differently from equivalent one-field writes.

## Performance and safety invariants

- Do not parse or plan configuration on every present. Preserve the bounded watcher interval and unchanged-file fast path.
- Do not allocate per frame for configuration or generated-frame planning. `ProfileUpdatePlan` is built only for an observed configuration change; `GeneratedFramePlan` remains the separate inline hot-path object.
- Do not initiate game-owned swapchain destruction or synthesize an out-of-date result for a private scaler edit. Remove a game-destroyed context from live updates immediately, and defer its lower WSI destruction until the replacement-present, grace-period, and fence conditions are all satisfied; only explicit destruction of that swapchain's creating surface may complete fence-proven terminal retirement without a later present.
- Do not mark a requested field applied unless the running context or backend actually uses it.
- Do not let a deferred field block Frame Generation Off or another independent live-safe field.
- Do not run generation work when the effective Frame Generation state is Off.
- Do not turn compositor safety feedback into a user-profile reload or suppress it under Ultra Performance.
- Keep diagnostics opt-in and avoid repeated per-frame formatting in the normal path.

## Diagnostics

With presentation diagnostics enabled, `runtime-transition-pending` distinguishes `rebuild-private-scaler`, `signal-out-of-date-after-retirement-fenced-present`, `wait-for-natural-swapchain-recreation`, and `wait-for-process-restart`; matching applied records include requested and active state. `runtime-transition-recreation-requested` records the one-shot request. `swapchain-recreation-observed` identifies whether an out-of-date result came from that guarded request or the upstream game/driver; `swapchain-create-observed` and `swapchain-destroy-observed` bracket the game-owned lifecycle. `swapchain-context-create`, `swapchain-retirement-deferred`, `swapchain-retirement-handoff`, and `swapchain-retirement-complete` expose maintenance1 selection, null-old handoff, and completion by same-surface present, surface destruction, or device destruction. A mixed revision may report live application and multiple pending boundaries.

`runtime-state-applied transition=live` records the merged live subset. The configuration summary counts live and pending contexts. `swapchain-context-create` proves a natural context boundary, not reconstruction of process-wide GPU, DLL, or FP16 state.

Use these records with the `startup`, `performance`, `recovery`, or `config` diagnostics presets described in [Collect diagnostics](COLLECT_DIAGNOSTICS.md). Diagnostics explain a transition but do not replace Vulkan, driver, compositor, and game validation.

## Adding or changing a setting

Every new setting or semantic change must answer these questions before implementation:

1. Which owner defines and validates the value: Renderer configuration, Decky's shared schema, launcher compatibility, compositor feedback, or another subsystem?
2. What is the earliest safe lifetime boundary: live context, private context/game-owned swapchain, process-wide backend, or process-start discovery?
3. What requested, applied, and construction-baseline facts must remain distinct while the value is pending?
4. Can the live-safe subset of a mixed write still apply, including Frame Generation Off?
5. Which exact pacer, scheduler, history, recovery, diagnostics, or resource state becomes invalid when the value actually applies?
6. What happens when the value belongs to an inactive Fixed/Adaptive mode or has the same effective value through a preset?
7. Is it compatible inside an active Ultra Performance session, does it alter the preset's process-static policy, or does it remain active because it is a runtime safety input?
8. Can natural recreation apply it without falsely applying a process-wide backend change?
9. Which diagnostic proves applied versus pending state without adding normal-path overhead?
10. Which deterministic, sanitizer, Vulkan, hardware, package, and game-matrix evidence is required for the affected boundary?

Extend `ProfileUpdatePlan` and the existing owner instead of adding a second reload path or applying fields directly at call sites. Update the Renderer configuration guide, Decky schema/UI restart semantics where applicable, diagnostics contract, and this matrix in the same change.

## Validation

Portable transition coverage belongs in `mako-render/tests/profile_update_tests.cpp`. At minimum, test the setting alone, mixed with a live-safe setting, mixed with each applicable deferral boundary, followed by a later live edit while the first request remains pending, reverted before its boundary, and applied with insufficient resources where relevant. Capacity tests must distinguish active policy from dormant mode values. Process-static tests must compare against the backend construction baseline rather than only consecutive requested profiles.

Run the portable and sanitizer suites from `engine/`:

```bash
scripts/test-adaptive-scheduler.sh
MAKO_ENABLE_SANITIZERS=ON scripts/test-adaptive-scheduler.sh
```

These tests do not exercise Vulkan presentation. The full suite's `swapchain-retirement` test covers maintenance1 negotiation, pNext discovery, result classification, and compositor grace without a GPU. Swapchain, resource, synchronization, colour, or device-feature changes also need real Vulkan evidence across the applicable native Vulkan, Gamescope, DXVK, VKD3D-Proton, FP32, and FP16 lanes in [Adaptive validation](ADAPTIVE-VALIDATION.md). Retirement changes must prove exactly one deferred and completed lower retirement per game recreation, no duplicate destruction or validation error, continued presentation, and clean terminal Proton/Gamescope shutdown. Mark unavailable hardware rows not tested.

## Code and test ownership

| Responsibility | Source of truth |
| --- | --- |
| Configuration parsing, effective presets, and watched-file state | `mako-common/src/configuration/config.cpp`, `mako-common/include/mako-common/configuration/config.hpp` |
| Runtime polling, profile selection, backend baselines, and context fan-out | `mako-render/src/instance.cpp`, `mako-render/src/instance.hpp` |
| Requested/applied merge and transition classification | `mako-render/src/profile_update.hpp` |
| Live context application and minimal state resets | `mako-render/src/swapchain.cpp` |
| Effective Off/native presentation branch | `mako-render/src/swapchain_present.cpp` |
| Process-start layer and transport policy | `mako-render/src/presentation_policy.hpp`, `scripts/mako-launch` |
| HDR safety feedback and private colour transition | `mako-render/src/gamescope_hdr_feedback.cpp`, `mako-render/src/runtime_transition.hpp`, `mako-render/src/color_pipeline.cpp` |
| Runtime transition diagnostics | `mako-render/src/entrypoint.cpp`, `mako-render/src/swapchain.cpp`, `mako-render/src/present_diagnostics.*` |
| Deterministic transition tests | `mako-render/tests/profile_update_tests.cpp`, `mako-render/tests/runtime_transition_tests.cpp` |
| Cross-component schema, wrappers, and UI semantics | `../plugin/shared_config.py`, `../plugin/py_modules/mako_plugin/`, `../plugin/src/`, `../plugin/tests/` |
