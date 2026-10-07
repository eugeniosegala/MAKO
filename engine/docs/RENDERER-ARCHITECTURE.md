# MAKO Renderer code map

This map identifies code owners and call boundaries. The behavior contracts remain in [Adaptive validation](ADAPTIVE-VALIDATION.md), [Runtime transitions](RUNTIME-TRANSITIONS.md), [Spatial scaling](SCALING.md), [WSI isolation](WSI-ISOLATION.md), and [Memory management](MEMORY-MANAGEMENT.md).

## Ownership from launch to present

| Concern | Owner | Boundary |
| --- | --- | --- |
| Profile parsing and defaults | `mako-common/src/configuration/` | Converts saved settings into validated Renderer configuration. |
| Process and Vulkan layer dispatch | `mako-render/src/entrypoint.cpp`, `instance.cpp` | Selects layer roles, devices, profiles, and application-owned Vulkan objects. |
| Gamescope observations | `mako-render/src/gamescope_hdr_feedback.cpp` and `instance.cpp` | Samples compositor state; a feedback change cannot silently become a profile edit. |
| Sustained runtime health | `mako-render/src/runtime_health.*`, registered by `instance.cpp` under the existing presentation-diagnostics flag | Samples bounded proc/sysfs fields and canonical atomic memory counters on a worker; no frame requests, Vulkan calls, or recovery transitions. |
| Fixed and Adaptive decisions | `mako-render/src/adaptive_scheduler.*`, `presentation_policy.hpp`, and `generated_frame_plan.hpp` | Chooses generated work, output timestamps, and pacing ownership before Vulkan submission. |
| Swapchain state and transitions | `mako-render/src/swapchain/` | Owns the applied profile, private resources, recovery state, and game-owned swapchain boundary. |
| Spatial reconstruction | `mako-render/src/spatial_scaler.*` and `spatial_scaling_policy.hpp` | Chooses geometry and records private scaling work without changing scheduler evidence. |
| Model execution | `mako-backend/` | Owns LSFG and LS1 resources and compute work behind the Renderer context. |
| Shader effects | The optional vkBasalt layer selected by `mako-launch` and MAKO Decky wrappers | Runs as a separate Vulkan layer; Renderer scaling and Frame Generation do not own its effect chain. |
| Native Steam Remote Play | `scripts/mako_remote_play/`, `scripts/mako-remote-play`, and `mako-ui/src/remote_play.cpp` | One Python owner handles Steam file safety. Qt invokes the packaged command asynchronously; Decky bundles byte-identical bindings. Existing launchers and Renderer configuration owners retain feature and live-update policy. |

## Presentation files

`Swapchain` remains the sole state owner. Its presentation methods are grouped by the work they perform:

| File | Responsibility |
| --- | --- |
| `mako-render/src/swapchain/present.cpp` | Orchestrates one application present, including live feedback, pacing selection, and routing to real or generated delivery. |
| `mako-render/src/swapchain/present/admission.cpp` | Prepares the Fixed or Adaptive frame plan, checks pipeline readiness, and handles generated-image admission and recovery. |
| `mako-render/src/swapchain/present/generated_frames.cpp` | Submits and presents generated frames while preserving the requested, admitted, scheduled, and delivered counts. |
| `mako-render/src/swapchain/present/real_frames.cpp` | Presents native, scaled, and history-only real frames and records source-copy work. |
| `mako-render/src/swapchain/present/retirement.cpp` | Preserves upstream present fences and retires acquired images after a failed generation path. |
| `mako-render/src/swapchain/present/internal.hpp` | Shares only presentation-local diagnostics, timing, and image-barrier helpers. It owns no mutable state or policy. |

Keep new policy decisions in their existing policy or scheduler owner, and let `Swapchain::present()` consume the result. Keep live resource replacement in `swapchain/resources.cpp`, profile transitions in `swapchain/profile.cpp`, and runtime status in `swapchain/status.cpp`. A change that crosses those boundaries needs its corresponding contract test and the validation described in [Testing MAKO](../../TESTING.md).

## Deliberate large-file boundaries

`adaptive_scheduler.cpp` keeps cadence observation, workload probes, promotion, and output planning in one scheduler state owner. Its stage methods are forced inline into `planFrame()` in optimized builds. Splitting them across translation units would need the same hot-path property or measured CPU evidence. The scheduler returns a plan; it does not acquire images or own Gamescope feedback.

`entrypoint.cpp` holds the Vulkan loader entrypoints and the per-layer registries for instances, devices, surfaces, queues, swapchains, and retirement. Both compiled layer roles use that dispatch boundary. A future split must first give those registries an explicit lifetime owner and preserve lower-layer forwarding, surface provenance, and retirement under the existing WSI contracts. Splitting the file by function length while sharing anonymous global state would obscure those invariants. Keep optional vkBasalt shader configuration in the launcher and wrapper boundary; effect ordering and execution belong to that layer.
