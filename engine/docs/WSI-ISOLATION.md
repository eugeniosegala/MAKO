# Gamescope WSI isolation

This document defines MAKO Renderer's Vulkan-layer isolation and presentation ownership. Read [HDR pipeline architecture](HDR-PIPELINE.md) for colour policy and [Runtime configuration transitions](RUNTIME-TRANSITIONS.md) for process-start and live-setting lifetimes.

## What is being isolated

Gamescope the compositor and Gamescope's Vulkan WSI layer are different components:

- **Gamescope compositor:** owns the display session, scanout, Game Mode UI, focus, refresh information, and final composition. It remains active.
- **Gamescope WSI Vulkan layer:** joins the game's Vulkan dispatch chain and applies swapchain/presentation policy. Managed MAKO launches exclude it from the game process by default; MAKO Decky can admit it through one per-profile experimental compatibility lane or the narrowly ordered scaling-at-process-start lane documented below.

Disabling Gamescope WSI does not disable Gamescope, Steam, or Game Mode. It changes only the implicit Vulkan layers visible inside the launched application.

## Why MAKO needs one presentation owner

MAKO turns one application present into a sequence of generated presents plus the original frame. A single combined MAKO layer below Gamescope WSI is unsafe: the upper WSI policy sees only the application's real present while the lower driver receives every injected present, so generated calls never re-enter WSI for pacing. The observed result was repeated lower FIFO stalls, frame generation that appeared inactive, and freezes during live scaler or resolution transitions.

The validated split solution gives frame generation the upper layer, lets every injected present traverse Gamescope WSI, and gives spatial reconstruction an independent lower layer:

```text
Game / Proton / DXVK or VKD3D-Proton
                  |
                  v
    VK_LAYER_MAKO_render
        frame generation
                  |
      generated frame(s) + original
                  |
                  v
 VK_LAYER_FROG_gamescope_wsi_x86_64
                  |
                  v
VK_LAYER_MAKO_spatial_scaling
       spatial reconstruction
                  |
                  v
             Vulkan driver
                  |
                  v
       Gamescope compositor remains active
```

## Default managed launch contract

The host `mako-launch` helper and MAKO Decky's generated wrapper establish the contract before the Vulkan loader creates an instance:

1. Select the private MAKO manifest directory with `VK_IMPLICIT_LAYER_PATH`.
2. Remove `VK_ADD_IMPLICIT_LAYER_PATH` so inherited implicit layers cannot rejoin the chain.
3. Set `ENABLE_MAKO=1`.
4. Disable known LSFG-VK identities with `DISABLE_LSFG=1` and `DISABLE_LSFGVK=1`.
5. Set `DISABLE_GAMESCOPE_WSI=1` and remove `ENABLE_GAMESCOPE_WSI`.
6. Select the supported SDR lane with `MAKO_DISABLE_HDR_EXPOSURE=1` and remove `DXVK_HDR`.

The private manifest is installed under:

```text
<prefix>/share/mako-render/vulkan/implicit_layer.d
```

MAKO Decky uses the equivalent per-user path below `~/.local/share/mako-render/`. Packages include architecture-correct 64-bit and 32-bit manifests. Flatpak applications use only the matching mounted MAKO runtime-extension directory because the host launcher cannot cross the sandbox boundary.

`VK_IMPLICIT_LAYER_PATH` must be set before process startup. The Renderer cannot repair layer order after `vkCreateInstance`, so this behavior belongs in launchers, manifests, packaging checks, and Flatpak overrides as well as in C++ policy tests.

The bounded managed scaling exception is selected before process startup. An eligible native host profile with Scaling enabled stages the exact `VK_LAYER_MAKO_render -> VK_LAYER_FROG_gamescope_wsi_x86_64 -> VK_LAYER_MAKO_spatial_scaling` chain. The upper MAKO role owns only frame generation; the lower role owns only spatial reconstruction. A selected MangoHud or vkBasalt layer may follow the spatial role. Profiles without Scaling retain the isolated top-only MAKO contract unless the explicit WSI compatibility option is selected. Layer membership itself is not live.

## Experimental MAKO Decky Gamescope WSI exception

MAKO Decky exposes **Experimental Gamescope WSI (Restart)** under **Compatibility Settings** for FG-only games that need the Gamescope WSI presentation path. It is off by default, stored per profile, and requires a game restart. Scaling automatically provides and locks the same requirement. The WSI switch is independent from the mutually exclusive MangoHud/vkBasalt post-process selector.

Installation validates the host's 64-bit Gamescope WSI manifest, including identity, absolute available library, and activation gates, then copies it into MAKO's managed compatibility directory. It also stages MAKO's lower spatial-role manifest. The wrapper admits only those exact role manifests, retains competing-FG and Mesa guards, and fails closed to top-only MAKO with scaling suppressed when WSI or lower-role evidence is invalid.

This exception remains SDR-only: `MAKO_DISABLE_HDR_EXPOSURE=1` stays set and inherited `DXVK_HDR` is removed. It is implemented for direct native-host launch contracts, including the validated 64-bit Steam/Proton path where Proton runs inside its matching Steam Linux Runtime Pressure Vessel. MAKO Gym's Proton E2E lane asserts from inside that container that Pressure Vessel imported all three role manifests before accepting the exact instance/device order, translated-scene completion, selected scaler, generated delivery, and clean shutdown. Flatpak, Heroic/UMU, 32-bit presentation, non-SteamOS layouts, HDR, commercial-title pacing, and every Gamescope limiter state remain outside the current evidence. Loader discovery alone is not a compatibility result; verify final generated FPS, image quality, and pacing before expanding this lane.

Manifest order and role isolation are decisive. MAKO Gym's `gamescope-e2e` suite owns the complete split, top-only isolation, missing-dependency failure, delivery, private scaler changes, FG Off/On, natural recreation and retirement, resolution-envelope transitions, compound journeys, and shutdown evidence. Current rows and values belong in Gym's manifests, not this architecture guide. Layer membership cannot change live; methods and sharpness rebuild only the lower private scaler, while Scale Factor waits for natural recreation.

Natural resolution changes negotiate KHR swapchain maintenance1, or its EXT predecessor, only when the device exposes both extension and feature. MAKO preserves upstream present-fence ownership or attaches preallocated per-image fences. It removes a destroyed context from live updates immediately but retains the lower swapchain until a same-surface replacement present, 50 ms compositor grace, and all fences complete. A null lower `oldSwapchain` may receive the exact retained same-device, same-surface handle once. Surface destruction is the terminal same-surface boundary and drains the same proof before forwarding. No worker thread, steady-state allocation, or guessed completion is introduced. [Runtime configuration transitions](RUNTIME-TRANSITIONS.md) owns the full lifecycle contract; Gym's Gamescope and Proton lanes prove it end to end.

Issue reports show that successful Vulkan presents do not prove image correctness: the curated WSI path can change motion artifacts even when both paths report successful presentation. WSI validation therefore needs a visual A/B check as well as loader and delivery evidence.

## What remains and what is excluded

| Component | Default managed launch result | Reason |
| --- | --- | --- |
| MAKO Renderer frame-generation role | Included and gated by `ENABLE_MAKO=1` | Owns generated-frame scheduling and injects every generated present above WSI |
| Gamescope compositor | Active | It is outside the application's implicit-layer chain |
| Steam/Game Mode interface | Active | Compositor UI is not the Vulkan WSI layer |
| Gamescope WSI Vulkan layer | Excluded by default; ordered below the frame-generation role and above the spatial role for the bounded scaling-at-start lane or below the frame-generation role for the explicit FG-only compatibility lane | Paces every generated and original present while exposing the validated variable surface to the lower scaler |
| MAKO Renderer spatial-scaling role | Included only by a complete split Scaling launch and gated by `ENABLE_MAKO_SPATIAL_SCALING=1` | Owns source-to-presentation reconstruction below WSI and cannot schedule frame generation |
| Steam Fossilize/implicit overlay hooks | Excluded | Prevents dispatch-chain bypass and ordering changes |
| System-wide implicit layers | Excluded | Makes swapchain ownership deterministic; MAKO Decky copies only the validated architecture-specific MangoHud or vkBasalt manifests for the selected tool into one dedicated managed directory rather than admitting the host directory |
| Known LSFG-VK frame-generation layers | Disabled | Two frame generators cannot own one swapchain |
| Explicit application layers | Not selected by this implicit-path policy | Vulkan explicit-layer behavior remains separate |
| Game-local integrations such as OptiScaler | Files are not removed | Use only one frame-generation implementation per game |

The current contract intentionally prefers deterministic presentation over compatibility with arbitrary implicit overlays or capture layers.

## Engine-side policy

`PresentationEnvironmentPolicy` in `mako-render/src/presentation_policy.hpp` centralizes the process-start facts:

- whether Gamescope WSI is disabled; and
- whether HDR exposure is available.

WSI isolation automatically closes the experimental Gamescope HDR bridge. The root resolves that process-start decision once and passes the same immutable snapshot to compositor feedback and swapchain transport selection, so those paths cannot disagree when only one environment variable was set.

At swapchain creation, `selectPresentationTransport()` chooses one immutable transport:

- `OrderedSdr` for the managed, isolated path; or
- `GamescopeHdr` only when Gamescope is detected, WSI is present, HDR exposure is allowed, and the swapchain is HDR-capable.

The selected transport is stored in `SwapchainInfo::privateOrderedTransport`. Later feedback may rebuild private colour resources, but cannot reinterpret the game-owned swapchain's present mode or pNext compatibility.

Per present, policy produces an inline requested plan, transport records admission, and the scheduled timestamps reach the backend unchanged under full admission. The experimental HDR bridge may admit fewer images and evenly re-space them. Adaptive changes policy, not presentation ownership; [Adaptive validation](ADAPTIVE-VALIDATION.md) owns the detailed plan contract.

## Why HDR cannot be a live toggle

The experimental HDR lane depends on Gamescope WSI, enabled HDR exposure, normalized colour-space recovery, metadata feedback, and a different presentation contract. The default SDR lane excludes WSI; the guarded scaling lane admits it with HDR exposure still disabled. Vulkan cannot insert a layer or change that process-start exposure policy in an existing instance, so neither SDR path can switch to HDR after launch.

A future supported HDR control must therefore be a **process-start choice** and must require a game restart. A safe design needs a per-game launch policy with at least these explicit outcomes:

- **Validated SDR:** private MAKO-only discovery, Gamescope WSI disabled, HDR exposure disabled.
- **Validated HDR:** a deliberately constructed and tested WSI/MAKO chain, HDR exposure enabled, native-first generated-image admission.
- **Native fallback:** MAKO inactive when neither lane is safe.

Do not implement this by removing the SDR guards globally, guessing from output HDR capability, or trying to mutate environment variables after Vulkan starts.

## Downsides and compatibility risks

Isolation is a strong boundary and has intentional tradeoffs:

- Steam's implicit shader-cache and Vulkan overlay hooks do not join the game process. Steam and Game Mode remain usable, but a Vulkan-hook feature may be absent.
- MangoHud, capture tools, post-processing layers, or vendor tools installed as implicit layers do not load through the normal managed path. The [optional graphics integrations guide](LAYER-CHAINING.md) documents MAKO Decky's independent Gamescope WSI requirement, mutually exclusive MangoHud/vkBasalt post-process selection, and manual expert path; each is an opt-in compatibility lane rather than the default presentation contract.
- A game that relied on another implicit layer for compatibility may behave differently. Compare against a native launch before assuming MAKO's scheduler is responsible.
- Current managed launches intentionally do not expose HDR frame generation.
- An incorrectly packaged private manifest can make MAKO appear active at the instance level while missing device or swapchain interception. Both architecture manifests and their relative library paths must be verified.
- Over-broad changes to the path variables can break Flatpak, 32-bit Proton, Heroic/UMU, or emulators even when a native 64-bit Steam game passes.

[Optional graphics integrations](LAYER-CHAINING.md) owns Decky controls, manual commands, guards, ordering, evidence, and unsupported boundaries. Do not generalize an exception without package and real-hardware evidence.

Signals that the boundary regressed include:

- MAKO logs instance/profile activation but never logs backend GPU selection or swapchain colour-pipeline creation;
- Gamescope WSI appears in loader logs for a default managed SDR launch or for a profile whose compatibility toggle is off;
- target FPS collapses identically in Fixed and Adaptive mode;
- the game runs at accelerated speed or the generated/original sequence loses cadence;
- frame generation appears inactive after a focus, display, or settings change;
- HDR/10-bit games show washed-out, purple, green, or pixelated motion output;
- Steam/Heroic works while Flatpak or 32-bit Proton does not.

## Diagnostics

For a managed SDR launch, verify the child environment and loader evidence:

```text
VK_IMPLICIT_LAYER_PATH=<private MAKO directory>
VK_ADD_IMPLICIT_LAYER_PATH unset
DISABLE_GAMESCOPE_WSI=1
ENABLE_GAMESCOPE_WSI unset
MAKO_DISABLE_HDR_EXPOSURE=1
DXVK_HDR unset
ENABLE_MAKO=1
```

Expected Renderer evidence includes:

- `MAKO Renderer: render layer active`;
- `presentation policy: gamescope_wsi=isolated; hdr_exposure=disabled`;
- the selected profile and backend GPU;
- `swapchain colour pipeline`;
- `Gamescope SDR presentation transport: mode=fifo-ordered` when Gamescope is detected; and
- generated/original delivery records when presentation diagnostics are explicitly enabled.

For the explicit compatibility exception, loader diagnostics must show `VK_LAYER_MAKO_render` above `VK_LAYER_FROG_gamescope_wsi_x86_64`, and the frame-generation role must report `gamescope_wsi=allowed` while `hdr_exposure=disabled`. For automatic native scaling, diagnostics must additionally show `VK_LAYER_MAKO_spatial_scaling` below WSI, a `role=spatial-scaling` build marker, and `spatial scaling active ... pipeline=post-frame-generation`. Positive spatial-scaling evidence requires `inactive_reason=none`, `source_presentation_split=1`, and differing selected source/presentation extents. A fixed surface must also report a matching nonzero advertised source/presentation contract; a variable surface reports its enlarged lower presentation extent directly. A selected method or recreation request without those records is not evidence that a scaler ran.

Use `VK_LOADER_DEBUG=layer` only for a focused reproduction because loader logs are verbose. Presentation diagnostics are also opt-in; synchronous logging can distort the timing problem under investigation. Follow [Collect diagnostics](COLLECT_DIAGNOSTICS.md) and retain the `layers`, `startup`, `performance`, and `recovery` presets.

## Change and validation checklist

Any change to discovery, Gamescope variables, present modes, pNext filtering, or HDR transport must cover the applicable launcher/wrapper/Flatpak contracts, 64-bit and 32-bit package manifests, loader instance/device order, finite Vulkan presentation, Fixed and Adaptive delivery, native Gamescope and Proton E2E, Proton-family compatibility when runtime behavior changes, and real-game traces. Include native Steam/Pressure Vessel, Heroic/UMU, supported Flatpak runtimes, available refresh rates, focus/overlay/resolution/recreation, HDR-capable hardware even for SDR, and low-end Steam Deck-class hardware. MAKO Gym owns exact scenario counts and release tiers.

A skipped GPU test proves no GPU behavior. A unit test cannot prove layer ordering, and `vkcube` cannot prove real-game pacing.

## Code and test ownership

| Responsibility | Source of truth |
| --- | --- |
| Host launch environment | `scripts/mako-launch` |
| Private manifest generation/install | `mako-render/CMakeLists.txt` |
| Package layout verification | `scripts/package-local.sh` |
| Process-start policy and transport choice | `mako-render/src/presentation_policy.hpp` |
| Ordered-SDR generated-image starvation quarantine and retry policy | `mako-render/src/presentation_policy.hpp`, `mako-render/src/swapchain_present.cpp` |
| Swapchain creation/pNext filtering | `mako-render/src/instance.cpp`, `mako-render/src/swapchain.cpp` |
| Generated-frame plan representation and partial-admission policy | `mako-render/src/generated_frame_plan.hpp` |
| Adaptive requested/accepted delivery-window policy | `mako-render/src/generated_frame_delivery.hpp`, `mako-render/src/adaptive_scheduler.*` |
| Requested/admitted/scheduled handoff and generated/original presentation | `mako-render/src/swapchain_present.cpp` |
| Standalone deterministic test | `scripts/test-mako-launch.sh` |
| Renderer transport and plan tests | `mako-render/tests/presentation_policy_tests.cpp`, `mako-render/tests/generated_frame_plan_tests.cpp` |
| Decky wrapper and Flatpak equivalents | `../plugin/py_modules/mako_plugin/` and `../plugin/tests/` |
