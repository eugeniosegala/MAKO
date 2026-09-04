# Runtime configuration transitions

This guide defines how a saved profile becomes running Renderer state. [Configuration](CONFIGURATION.md) defines fields, [Adaptive validation](ADAPTIVE-VALIDATION.md) owns scheduling, [Spatial scaling architecture](SCALING.md) owns extent policy, [HDR pipeline architecture](HDR-PIPELINE.md) owns colour transitions, and [WSI isolation](WSI-ISOLATION.md) owns process-start discovery.

## Lifetime boundaries

A setting belongs to the earliest boundary that can safely establish all state it affects.

| Boundary | State | Completion |
| --- | --- | --- |
| Process start | Scaling enablement, Game Swapchain Images compatibility, layer membership and order, Gamescope WSI isolation, HDR exposure, Zink, and audio compatibility | Start a new game process |
| Process-wide backend | DLL, FP16 permission, GPU, and Ultra Performance policy | Construct a new backend, normally by restarting the process |
| Game-owned swapchain | Spatial extents and pacing shape | Natural recreation, or one eligible maintenance1-backed extent request |
| Private spatial context | Scaling method and sharpness | Prepare, drain MAKO-owned work, and atomically replace |
| Private FG context | Flow Scale, lighter model, and generated-output capacity | Prepare, drain MAKO-owned work, and atomically replace |
| Live policy | Frame Generation switch, refresh guard, Fixed/Adaptive policy, target, caps, and cadence controls | Next successful reload and application present |
| Compositor feedback | Confirmed refresh and application HDR state | Stabilized sample, independent of profile reload |
| Dormant value | A setting for an inactive mode or unavailable private resource | Save now; apply when its owning mode or resource becomes active |

The process-wide backend is created lazily when the first active swapchain needs it. Once built, pending DLL, FP16, GPU, and Ultra changes are compared with the actual construction baseline, not merely the previous file.

## Update flow

`Root::update()` is reached from application presentation, but work is bounded and change-driven:

1. Compositor refresh and HDR feedback update safety state independently.
2. Configuration is checked at most every 250 ms; an unchanged file is not reparsed or replanned.
3. A changed file is parsed and profile matching is repeated.
4. Process-static fields are projected back to their applied values while compatible fields continue.
5. Each live swapchain receives a `ProfileUpdatePlan` based on its applied profile, private resources, generated capacity, and current extent support.
6. The live-safe merge is applied with only the necessary state resets; other values stay pending at their owning boundary.

Every application-owned swapchain creation forces one configuration freshness check so a replacement cannot combine new upper-layer policy with a stale lower-layer snapshot.

## Requested, applied, and pending state

The transition engine keeps three facts distinct:

| Fact | Owner | Meaning |
| --- | --- | --- |
| Requested profile | `Root::active_profile` | Latest matching saved profile |
| Applied profile | `Swapchain::profile` | Values true for this live context |
| Backend baseline | `Root::backendGlobal`, `Root::backendProfile` | Process-wide inputs used to construct the backend |

`ProfileUpdatePlan::appliedProfile` is a merge. It starts from the requested profile, restores values that cannot cross the current boundary, then classifies the remaining effective differences. A pending restart or recreation must not block an independent live update.

For example, a write that changes Base FPS Cap and Flow Scale applies the cap while preparing a private FG replacement. The old Flow Scale remains applied until handoff. A later Frame Generation Off still applies immediately, and reverting Flow Scale before handoff cancels the replacement.

## Transition matrix

| Setting or input | Boundary | Effect |
| --- | --- | --- |
| Frame Generation Off | Live | Releases the effective base cap, resets relevant scheduler and recovery state, and takes the real-frame path without LSFG work. |
| Frame Generation On | Live if startup provisioning succeeded; otherwise restart | Reuses retained interop and private resources, then warms temporal history where required. |
| Refresh threshold and Gamescope refresh | Live | Re-evaluates effective enablement and refresh-targeted scheduling. |
| Fixed/Adaptive mode or multiplier | Live within current capacity; otherwise private FG replacement or recreation | Dormant mode values are saved without resetting the active mode. |
| Adaptive target, ceiling, Smooth Cadence, and Dynamic Cadence Recovery | Live within capacity | Rebuilds only the scheduler state whose assumptions changed. |
| Dynamic Cadence probe interval | Live | Reschedules an inactive probe without discarding validated cadence or an active confirmation. |
| Base FPS Cap and Adaptive auto-cap | Live while generation is active; dormant while Off | Resets the real-frame pacer and affected scheduler policy. |
| Scaling enable | Restart | Existing and naturally recreated contexts retain process-start scaling and layer membership. |
| Game Swapchain Images compatibility | Restart | Existing contexts retain the process-start WSI image-count policy. |
| Scaling method | Private spatial replacement when active; dormant otherwise | Applies at the next present, retains extents and WSI objects, and keeps the old method on failure. |
| Scaling sharpness | Private spatial replacement when active; dormant otherwise | Coalesces edits for 500 ms before replacement. |
| Scale Factor | Extent no-op or game-owned recreation | Applies immediately when effective extents stay identical. Otherwise, an eligible spatial owner may request one recreation after a retirement-fenced present; other paths wait for natural recreation. |
| Quality Supersampling | Extent no-op or game-owned recreation | Uses the same extent boundary as Scale Factor on a variable managed Gamescope surface; fixed and direct geometry are unaffected. |
| Flow Scale and Lighter FG Model | Private FG replacement | Coalesces for 500 ms, prepares a complete candidate, drains MAKO-owned work, switches atomically, and warms history. |
| Generated-output capacity | Private FG replacement when the current WSI pool fits; otherwise recreation | Keeps the old active policy until enough resources and WSI headroom exist. Managed Gamescope waits for natural recreation; a compatible non-Gamescope maintenance1 context may request one. |
| Pacing | Natural recreation | Does not change a live swapchain or its transport. |
| DLL, FP16, GPU, and Ultra Performance | Restart | Existing contexts retain the actual backend baseline. |
| WSI compatibility, external layer, HDR exposure, Zink, and ALSA | Restart | These affect discovery or application initialization. |
| Stable HDR application feedback | Private colour transition | May rebuild MAKO-owned colour and backend resources, never the game-owned transport. |
| Active profile match | Live for compatible fields | Losing the match disables generation immediately; static differences remain pending. |

Normal profiles reserve capacity for the larger configured Fixed or Adaptive ceiling. Ultra Performance reserves only its startup-active policy. A later capacity increase can still use private replacement when the lower WSI pool has `application minimum + generated capacity` images; otherwise it remains pending for recreation.

## Private replacement contract

Private spatial, FG, and colour changes use one last-value-wins coordinator:

1. Coalesce the request when required.
2. Construct a complete candidate while the old resources remain usable.
3. Poll only MAKO-owned work; never call `vkDeviceWaitIdle`.
4. Commit atomically after drain proof.
5. Warm temporal history when the replacement affects Frame Generation.
6. Retain the old resources and retry later if construction or drain fails.

Private transitions never destroy the application swapchain or replace its WSI-facing synchronization. During a drain, presentation uses native or reconstructed real frames. A superseded candidate is discarded rather than partially applied.

## Recreation and retirement

On the managed Gamescope split, only a Scale Factor or Quality Supersampling extent change may request application-visible recreation, and only from the upper spatial-resource owner. A compatible non-Gamescope maintenance1 context may also request recreation for another private-resource change that cannot be rebuilt in place, such as generated-capacity growth beyond current WSI headroom. Either path may return `VK_ERROR_OUT_OF_DATE_KHR` once, only after the lower present succeeded with retirement proof. MAKO never destroys the game-owned swapchain itself.

Natural and requested recreation remove the old context from live updates immediately. Lower WSI destruction waits for same-surface replacement progress, a 50 ms compositor grace, and retirement fences. If Gamescope WSI supplies a null lower `oldSwapchain`, MAKO completes the exact retained same-device, same-surface retirement before forwarding replacement creation. Surface destruction is the terminal same-surface boundary.

A scaled null-old replacement presents its first acquired application image directly before private spatial work. A known replacement with Frame Generation active then presents real frames for 250 ms and warms three new history frames inside scheduler stabilization. A replacement created while Frame Generation is Off does not arm that FG-only interval.

The immutable pre/post-FG spatial placement is selected from source and presentation extents at swapchain creation. A live method, sharpness, mode, multiplier, Flow Scale, or model change cannot alter that geometry.

## State resets

Reset only state whose assumptions changed:

- enable/disable clears affected pacing, admission, acquire recovery, and history state;
- mode, target, ceiling, Smooth Cadence, or recovery-policy changes rebuild scheduler policy;
- effective cap changes reset the real-frame pacer and scheduler observations;
- probe-interval-only changes update only the timer;
- private-resource, mode, multiplier, refresh, or transport-recovery changes clear Fixed collapse evidence when its baseline is no longer valid; and
- dormant or deferred values reset nothing until they apply.

A broad “configuration changed” reset would discard validated cadence after unrelated edits and is not allowed.

## Frame Generation Off and Ultra Performance

Frame Generation Off submits no LSFG model work, generated-image acquisition, or generated presents. The saved cap is dormant. A matched process still provisions interop, backend, private images, and synchronization when startup succeeds so Off can turn On live. Failed provisioning leaves real-frame or independent scaling active and reports restart pending.

Ultra Performance remains a process-start policy: effective FP16, Flow Scale 0.75, lighter model, active-policy-sized capacity, and LS1 Performance when scaling is enabled. It never enables scaling. Compatible live controls still work, but changing Ultra itself waits for restart and cannot partially mutate the active backend.

## Runtime status and diagnostics

With diagnostics enabled, `runtime-transition-pending`, `runtime-transition-prepared`, `runtime-transition-failed`, `runtime-transition-applied`, and `runtime-transition-recreation-requested` identify the boundary and result. Swapchain create, replacement, retirement, WSI-prime, backend-stabilization, and memory records provide lifecycle evidence.

Each context also publishes an atomic schema-5 requested-versus-applied record under the configuration directory's `runtime-state/`. It includes process identity, transition phase, pending boundaries, effective generation and scaling state, extents, method, factor, placement, fallback, and active constraint. Context teardown removes its own record and lock.

Before the first primary context publishes, one process-wide pass removes only unlocked stale MAKO runtime files from that exact directory. It is non-recursive and best-effort; held locks, unrelated names, symlinks, ownership uncertainty, and failures are preserved and never block startup. Status I/O is observational and cannot change presentation.

Use the `config`, `startup`, `recovery`, or `performance` presets in [Collect diagnostics](COLLECT_DIAGNOSTICS.md).

## Adding a setting

Before adding or changing a field, define:

1. its schema and validation owner;
2. the earliest safe lifetime boundary;
3. requested, applied, and construction-baseline state;
4. behavior in mixed writes and inactive modes;
5. the smallest scheduler, pacing, history, recovery, or resource reset;
6. behavior inside an active Ultra Performance process;
7. diagnostics that prove application; and
8. portable, sanitizer, Vulkan, hardware, and package evidence.

Extend `ProfileUpdatePlan` and the existing transition owner rather than adding a second reload path.

## Validation

Run portable and sanitizer coverage from `engine/`:

```bash
scripts/test-adaptive-scheduler.sh
MAKO_ENABLE_SANITIZERS=ON scripts/test-adaptive-scheduler.sh
```

`profile_update_tests.cpp` must cover a field alone, mixed with live and deferred fields, superseded or reverted requests, insufficient resources, inactive modes, and process-static baselines as applicable. Vulkan-facing changes also need the MAKO Gym suites selected by [Testing MAKO](../../TESTING.md). Mark unavailable hardware rows **not tested**.

## Code and test ownership

| Responsibility | Source of truth |
| --- | --- |
| Parsing and watched configuration | `mako-common/src/configuration/config.cpp`, `mako-common/include/mako-common/configuration/config.hpp` |
| Polling, profile selection, and backend baselines | `mako-render/src/instance.*` |
| Merge and transition classification | `mako-render/src/profile_update.hpp` |
| Private transitions and live application | `mako-render/src/runtime_transition.hpp`, `mako-render/src/swapchain.cpp` |
| Real-frame and generated presentation | `mako-render/src/swapchain_present.cpp` |
| Process-start transport policy | `mako-render/src/presentation_policy.hpp`, `scripts/mako-launch` |
| Status and diagnostics | `mako-render/src/runtime_status.*`, `mako-render/src/present_diagnostics.*` |
| Deterministic tests | `mako-render/tests/profile_update_tests.cpp`, `runtime_transition_tests.cpp`, `runtime_status_tests.cpp` |
