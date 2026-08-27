# Runtime configuration transitions

This document is the architectural source of truth for how MAKO Renderer turns a saved configuration change into running process state. It defines which values can change live, which values request or wait for a game-owned swapchain recreation, which values require a game restart, how mixed updates are partially applied, and which Ultra Performance resource decisions remain process-static.

This is not the owner of every subsystem affected by a transition. [Configuration](CONFIGURATION.md) defines the settings and user-facing semantics, [Adaptive validation](ADAPTIVE-VALIDATION.md) owns cadence policy and generated-frame plans, [HDR pipeline architecture](HDR-PIPELINE.md) owns colour feedback and private colour-resource transitions, and [WSI isolation](WSI-ISOLATION.md) owns process-start layer discovery and presentation transport. This document owns the lifecycle contract that keeps those boundaries consistent when configuration changes.

## Lifetime boundaries

A setting belongs to the earliest lifetime boundary that can safely establish all state it affects. A convenient UI toggle does not make its implementation live-safe.

| Boundary | State established there | Completion event |
| --- | --- | --- |
| Process-start discovery | Scaling Engine enablement, implicit-layer membership and order, Gamescope WSI isolation, HDR exposure, launcher compatibility environment | Start a new game process |
| Process-wide backend | DLL, effective FP16 permission, GPU selection, Ultra Performance backend policy | First backend construction, or a new game process once a backend already exists |
| Game-owned swapchain and private context | Spatial-scaling shape, Flow Scale, model selection, generated-image capacity, presentation shape | A natural game-owned recreation, or one `VK_ERROR_OUT_OF_DATE_KHR` after a maintenance1-fenced lower present; discrete scaler methods request it immediately and numeric/model/capacity edits first coalesce |
| Live context policy | Generation enable, refresh threshold, Fixed/Adaptive selection within reserved capacity, target, caps, cadence policy | The next successful configuration reload and application-present boundary |
| Compositor safety feedback | Confirmed refresh rate and HDR application state | A stabilized runtime feedback sample, independently of profile reload |
| Stored metadata or dormant policy | Values that do not alter current profile selection and values belonging only to an inactive mode | Saved immediately; no running-state reset until the value becomes active |

The process-wide backend is created lazily for the first managed swapchain. Before it exists, the latest configuration can still become its construction input. After construction, the Renderer compares requested DLL, FP16, GPU, and Ultra state against that actual baseline rather than against the previous file contents, so a pending process-static change remains pending across later saves.

## Runtime update pipeline

`Root::update()` runs from the application-present path, but the expensive parts are bounded and change-driven:

1. Gamescope refresh and HDR feedback are sampled and stabilized as runtime safety inputs. They are not user-profile reloads.
2. The watched configuration is checked at most once every 250 ms. An unchanged file causes no profile planning, including in an Ultra Performance process.
3. A changed file is parsed, the process is identified again, and the latest matching profile becomes Root's requested profile.
4. Toggling Ultra Performance or Scaling Engine is a process-restart transition, but compatible scheduler and scaler-method fields from the same write can still apply at their normal boundaries.
5. Every existing swapchain context calls `planProfileUpdate()` with its actually applied profile, the requested profile, its reserved generated-image capacity, and current resource availability.
6. `Swapchain::updateProfile()` applies the live-safe merged profile and resets only the runtime state invalidated by those applied fields. Deferred values remain requested at Root for their later boundary.
7. Native/scaler method edits arm a recreation request for the next lower present that accepts MAKO's maintenance1 retirement fence. Spatial factor/sharpness, Flow Scale, FG model, or generated-capacity edits retain a 500 ms quiet period so continuous or grouped controls coalesce before requesting recreation. Scaling Engine and pacing wait for process and natural-recreation boundaries respectively. Diagnostics report live application, recreation deferral/request, and process-restart deferral independently. One update may have all three outcomes.

The layer never initiates application swapchain destruction. In a Scaling Engine process whose device negotiated `VK_KHR_swapchain_maintenance1` or its EXT predecessor, MAKO preallocates one present fence per lower swapchain image. It attaches the image's fence to lower presentations without allocating in the steady-state path. For live scaler/model/capacity controls, it returns `VK_ERROR_OUT_OF_DATE_KHR` once only after the final lower present accepted one of those retirement fences, leaving the game responsible for requesting destruction and recreation. When the game destroys the retired swapchain, MAKO immediately removes its context from live updates but defers the lower WSI destruction until a later presentation on the exact same creating surface has occurred, the 50 ms compositor grace has elapsed, and every associated fence has signaled; a different window on the same device cannot release it. Some upper WSI implementations destroy their application-visible swapchain before recreating and consequently pass a null lower `oldSwapchain`; while MAKO's fence-protected lower object is retained, MAKO supplies that exact same-device, same-surface lower handle once so Vulkan can retire it during creation instead of returning `VK_ERROR_NATIVE_WINDOW_IN_USE_KHR`. This lower-only handoff never invents an application-visible owner, never bypasses the fence or grace, and is consumed even if creation fails because Vulkan retires `oldSwapchain` on the attempt. A resize, display change, or other natural recreation uses the same deferred lower-retirement path. Terminal application teardown may have no later replacement present; MAKO therefore records each deferred swapchain's creating surface and treats application-owned surface destruction as the terminal boundary, synchronously draining the same maintenance1 proof and releasing the lower swapchain before forwarding `vkDestroySurfaceKHR`. Root substitutes process-static backend values used by the existing process, so a recreation cannot falsely apply GPU or Ultra Performance changes.

For a provisioned Scaling Engine process, MAKO queries swapchain-maintenance1 support before application-device creation and requests the promoted KHR spelling, or the EXT predecessor, only when the physical device exposes both the extension and feature. The Gamescope WSI layer provisioned above MAKO also enables maintenance1 before calling MAKO, so the managed scaling lane preserves the same contract when the upper layer already owns negotiation. If Gamescope supplies `VkSwapchainPresentFenceInfoKHR`, MAKO preserves its chain and ownership unchanged and treats a non-null fence on the final lower present as the specification-defined retirement proof for the one-shot result. When no upstream owner exists, MAKO attaches its own preallocated per-image fence. If maintenance1 is unsupported or was not enabled, fence allocation failed, or the upstream entry is invalid, resource changes wait for a natural recreation. A caller-supplied allocation callback also cannot be retained beyond `vkDestroySwapchainKHR`, so that rare path waits for MAKO-owned retirement fences synchronously and forwards destruction with the original callback. Surface-terminal collection likewise never bypasses an unsignaled fence and requires no layer-owned worker thread. Device teardown retains its final bounded drain and terminal cleanup.

## Requested, applied, and pending state

The transition engine deliberately keeps three facts separate:

| Fact | Owner | Meaning |
| --- | --- | --- |
| Requested profile | `Root::active_profile` | Latest normal-mode profile selected from configuration |
| Applied context profile | `Swapchain::profile` | Values that are true for that already-created context |
| Backend construction baseline | `Root::backendGlobal` and `Root::backendProfile` | DLL, FP16, GPU, and Ultra inputs actually used by the process-wide backend |

`ProfileUpdatePlan::appliedProfile` is a merge, not a copy of either endpoint. It starts with the requested profile, restores every field that cannot cross the current boundary, and then classifies the remaining effective differences. This prevents a pending field from overwriting the context's actual state while allowing unrelated live-safe work to proceed.

For example, if one save changes Flow Scale from 1.0 to 0.75 and Base FPS Cap from Off to 45, the cap applies live while Flow Scale remains pending for natural recreation. A later Frame Generation Off change still applies immediately, the Flow Scale request remains pending, and reverting Flow Scale to 1.0 clears that deferral. If the same save also changes GPU, the transition reports both swapchain-recreation and process-restart deferrals instead of collapsing them into one ambiguous status.

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
| Scaling Engine enable (`scaling_enabled`) | Process restart | Existing process retains its actual engine and WSI membership; Decky provisions or removes the WSI lane only for the next launch |
| Native/scaler method | Requested recreation when Scaling Engine and maintenance1 retirement are provisioned; otherwise natural recreation | Existing context retains its actual surface/pipeline state; the next retirement-fenced lower present produces one out-of-date result so the game recreates promptly; Native allocates and dispatches no spatial scaler |
| Scaling factor and sharpness | Requested recreation when Scaling Engine is provisioned | Existing context retains its actual surface/pipeline state; a 500 ms quiet period coalesces numeric edits before one out-of-date result asks the game to recreate |
| Flow Scale and Lighter FG Model | Requested recreation when retained generation resources exist | Existing private context retains its actual model construction; a debounced one-shot out-of-date result asks the game to recreate |
| Pacing shape | Natural recreation | Existing game-owned swapchain and presentation transport remain unchanged |
| Generated-image capacity growth | Requested recreation when retained generation resources exist | Current active policy remains within available capacity; unrelated targets, caps, or switches still apply live |
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

Every matched process negotiates the application-device interop and constructs the backend, private source images, generated images, and synchronization objects needed by Frame Generation, even when the saved switch starts Off. The native presentation branch bypasses those objects while Off: it schedules no model work, copies no generation input, and presents no generated image. The retained allocation is the deliberate memory/startup tradeoff that makes Off→On immediate instead of restart-bound. If extension negotiation, backend startup, or private-resource construction failed, the context remains native or independently scaled and reports the enable as restart-pending. A stabilized HDR colour transition may update retained private colour resources so a later live enable uses the correct encoding.

Ultra Performance no longer removes the live-switch foundation when Frame Generation starts Off. It retains the same interop and active-policy-sized private resources as an On startup, while the live Off branch performs no generation work. The preset's effective FP16, Flow Scale, lighter-model, and capacity policy is still selected at process construction, so toggling Ultra Performance itself waits for restart.

If a normal running profile stops matching, existing contexts disable generation immediately and retain their resources. A newly started process with no matching profile remains dormant and does not activate MAKO's swapchain path.

## Ultra Performance invariants

Ultra Performance is a process-start resource policy, not a faster branch inside the live scheduler:

- the preset's FP16 choice and resource-sizing policy remain fixed until restart;
- generated-output capacity is sized only for the startup active Fixed or Adaptive policy;
- explicit Frame Generation Off retains private generation resources and generation-specific application Vulkan modifications but performs no per-frame generation work;
- compatible scheduler fields continue applying live, and scaler/model/capacity edits continue through the game-owned recreation path;
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
- Do not initiate game-owned swapchain destruction. Preserve the one-shot, retirement-fenced out-of-date request, remove a game-destroyed context from live updates immediately, and defer its lower WSI destruction until the replacement-present, grace-period, and fence conditions are all satisfied; only explicit destruction of that swapchain's creating surface may complete fence-proven terminal retirement without a later present.
- Do not mark a requested field applied unless the running context or backend actually uses it.
- Do not let a deferred field block Frame Generation Off or another independent live-safe field.
- Do not run generation work when the effective Frame Generation state is Off.
- Do not turn compositor safety feedback into a user-profile reload or suppress it under Ultra Performance.
- Keep diagnostics opt-in and avoid repeated per-frame formatting in the normal path.

## Diagnostics

With presentation diagnostics enabled, `operation=runtime-transition-pending` records the individual scaling/model/capacity flags and distinguishes `action=signal-out-of-date-after-retirement-fenced-present`, `action=wait-for-natural-swapchain-recreation`, and `action=wait-for-process-restart`. `operation=swapchain-context-create present_retirement=maintenance1-fence|natural-only`, `operation=swapchain-retirement-deferred`, `operation=swapchain-retirement-handoff reason=null-upper-old-swapchain`, and `operation=swapchain-retirement-complete trigger=same-surface-present|surface-destroy|device-destroy` expose the lower lifetime decision, null-old replacement bridge, and completion. Normal and surface-terminal collection share the same completion record because both require the same fence and compositor grace; the trigger identifies whether a same-surface replacement present occurred or a terminal owner boundary drained it. A mixed request may report live application and more than one pending boundary for one context and state revision.

`operation=runtime-state-applied transition=live` records the merged context state after a live-safe subset applies. The configuration summary separately reports the number of live-updated contexts, contexts with each pending boundary, and whether profile or global process-static state remains pending. `operation=swapchain-context-create` proves a natural context boundary occurred; it does not prove a process-wide GPU, DLL, or FP16 request was reconstructed.

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

These tests do not exercise Vulkan presentation. The full Renderer suite additionally runs `swapchain-retirement`, which verifies maintenance1 negotiation, pNext discovery, queued-result classification, and the compositor grace without a GPU. Changes to swapchain mutation, resource construction, synchronization, private colour transitions, or device features also require real Vulkan evidence. Exercise Frame Generation Off/On, mixed live/deferred saves, natural swapchain recreation, process restart, profile loss/reselection, Ultra Performance On/Off across restart, refresh-threshold transitions, focus/overlay behavior, and the applicable DXVK, VKD3D-Proton, native Vulkan, Gamescope, desktop, FP32, and FP16 rows in [Adaptive validation](ADAPTIVE-VALIDATION.md). A retirement change specifically requires a Gamescope scaling-live resolution-churn row that proves one deferred and one completed retirement for each game-requested recreation, no duplicate lower destruction, no validation error, and clean continued presentation, plus the Proton E2E rows that prove surface-terminal completion and translated-client/Gamescope shutdown without forced pending retirement. Mark unavailable hardware rows not tested.

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
