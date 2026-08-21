# Gamescope WSI isolation

This document is the architectural source of truth for MAKO Renderer's Vulkan layer isolation and presentation ownership. Read [HDR pipeline architecture](HDR-PIPELINE.md) alongside it: the supported SDR boundary and the future HDR transport are deliberately interconnected.

## What is being isolated

Gamescope the compositor and Gamescope's Vulkan WSI layer are different components:

- **Gamescope compositor:** owns the display session, scanout, Game Mode UI, focus, refresh information, and final composition. It remains active.
- **Gamescope WSI Vulkan layer:** joins the game's Vulkan dispatch chain and applies swapchain/presentation policy. Managed MAKO launches exclude it from the game process by default; MAKO Decky can admit it through one per-profile experimental compatibility lane.

Disabling Gamescope WSI does not disable Gamescope, Steam, or Game Mode. It changes only the implicit Vulkan layers visible inside the launched application.

## Why MAKO needs one presentation owner

MAKO turns one application present into a sequence of generated presents plus the original frame. When Gamescope WSI is above MAKO, its upper policy sees the single application present, while the lower driver receives every MAKO-injected present. Those injected calls do not re-enter the upper WSI layer individually.

In the observed regression, Gamescope's upper FIFO policy and MAKO's lower generated/original sequence applied backpressure to each other. A nominal 60 FPS game fell to roughly 13 FPS; Fixed and Adaptive modes behaved alike because the conflict was below the scheduler.

The validated SDR solution gives MAKO one ordered lower swapchain and one presentation clock:

```text
Game / Proton / DXVK or VKD3D-Proton
                  |
                  v
        MAKO Vulkan layer only
                  |
      generated frame(s) + original
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

## Experimental MAKO Decky Gamescope WSI exception

MAKO Decky exposes **Experimental Gamescope WSI (Restart)** under **Compatibility Settings** for games affected by coloured or pixelated motion artifacts when the default WSI-isolated path is active. It is off by default, stored per profile, requires a game restart, and shares one mutually exclusive optional-layer selector with MangoHud and vkBasalt.

MAKO Renderer installation validates the host's `/usr/share/vulkan/implicit_layer.d/VkLayer_FROG_gamescope_wsi.x86_64.json`: the layer identity, absolute available library, and enable/disable gates must match the expected 64-bit Gamescope WSI contract. A valid manifest is copied into MAKO's managed `~/.local/share/mako-render/vulkan/gamescope_wsi_compatibility.d` directory. The wrapper admits only MAKO's private manifests plus that staged WSI manifest, sets `ENABLE_GAMESCOPE_WSI=1`, removes `DISABLE_GAMESCOPE_WSI`, and keeps the known LSFG-VK, Mesa device-selection, and Mesa anti-lag guards. Missing or invalid host evidence leaves the default isolated path active.

This exception remains SDR-only: `MAKO_DISABLE_HDR_EXPOSURE=1` stays set and inherited `DXVK_HDR` is removed. It is implemented only for direct host launches; Flatpak, Heroic/UMU, 32-bit validation, non-SteamOS layouts, HDR, and stable layer ordering remain outside the current evidence. Loader discovery is not a compatibility result. Verify the actual instance and device order, final generated FPS, image quality, and pacing before expanding this lane.

## What remains and what is excluded

| Component | Default managed launch result | Reason |
| --- | --- | --- |
| MAKO Renderer | Included and gated by `ENABLE_MAKO=1` | Owns the swapchain sequence |
| Gamescope compositor | Active | It is outside the application's implicit-layer chain |
| Steam/Game Mode interface | Active | Compositor UI is not the Vulkan WSI layer |
| Gamescope WSI Vulkan layer | Excluded | Avoids competing presentation policy |
| Steam Fossilize/implicit overlay hooks | Excluded | Prevents dispatch-chain bypass and ordering changes |
| System-wide implicit layers | Excluded by default | Makes swapchain ownership deterministic; MAKO Decky may admit the guarded host directory for one selected External Tool |
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

## Why HDR cannot be a live toggle

The experimental HDR lane depends on Gamescope WSI for its normalized colour space, metadata feedback, and lower presentation contract. The release SDR lane excludes that layer. Vulkan does not allow an implicit layer to be inserted into an existing instance, so changing a Decky or UI setting after launch cannot switch between the lanes.

A future supported HDR control must therefore be a **process-start choice** and must require a game restart. A safe design needs a per-game launch policy with at least these explicit outcomes:

- **Validated SDR:** private MAKO-only discovery, Gamescope WSI disabled, HDR exposure disabled.
- **Validated HDR:** a deliberately constructed and tested WSI/MAKO chain, HDR exposure enabled, native-first generated-image admission.
- **Native fallback:** MAKO inactive when neither lane is safe.

Do not implement this by removing the SDR guards globally, guessing from output HDR capability, or trying to mutate environment variables after Vulkan starts.

## Downsides and compatibility risks

Isolation is a strong boundary and has intentional tradeoffs:

- Steam's implicit shader-cache and Vulkan overlay hooks do not join the game process. Steam and Game Mode remain usable, but a Vulkan-hook feature may be absent.
- MangoHud, capture tools, post-processing layers, or vendor tools installed as implicit layers do not load through the normal managed path. The [optional graphics integrations guide](LAYER-CHAINING.md) documents MAKO Decky's mutually exclusive experimental Gamescope WSI, MangoHud, and experimental vkBasalt selections plus the manual expert path; each is an opt-in compatibility lane rather than the default presentation contract.
- A game that relied on another implicit layer for compatibility may behave differently. Compare against a native launch before assuming MAKO's scheduler is responsible.
- Current managed launches intentionally do not expose HDR frame generation.
- An incorrectly packaged private manifest can make MAKO appear active at the instance level while missing device or swapchain interception. Both architecture manifests and their relative library paths must be verified.
- Over-broad changes to the path variables can break Flatpak, 32-bit Proton, Heroic/UMU, or emulators even when a native 64-bit Steam game passes.

The dedicated integrations guide owns the Decky controls, manual commands, customization boundary, guard meanings, observed order, verification procedure, evidence, and unsupported matrix. Do not remove those guards or generalize an exception without the corresponding package and real-hardware evidence.

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

For the experimental compatibility exception, the process policy must instead report `gamescope_wsi=allowed` while `hdr_exposure=disabled`, and loader diagnostics must show both `VK_LAYER_MAKO_render` and `VK_LAYER_FROG_gamescope_wsi_x86_64`. That policy record proves only that MAKO did not set its isolation guard; loader evidence is still required to prove the WSI layer actually joined.

Use `VK_LOADER_DEBUG=layer` only for a focused reproduction because loader logs are verbose. Presentation diagnostics are also opt-in; synchronous logging can distort the timing problem under investigation. Follow [Collect diagnostics](COLLECT_DIAGNOSTICS.md) and retain the `layers`, `startup`, `performance`, and `recovery` presets.

## Change and validation checklist

Any change to private discovery, Gamescope variables, present modes, pNext filtering, or HDR transport must cover all applicable rows:

- `mako-launch` contract test;
- wrapper and Flatpak override tests;
- 64-bit and 32-bit manifest/package validation;
- Vulkan loader instance/device activation and finite `vkcube` presentation;
- Fixed and Adaptive frame generation;
- DXVK, VKD3D-Proton, and native Vulkan;
- native Steam/Pressure Vessel, Heroic/UMU, and supported Flatpak runtimes;
- 40, 60, 90, and 120 Hz where hardware permits;
- focus, overlay, resolution, display-mode, and natural swapchain recreation;
- HDR-capable hardware even when validating the SDR lane;
- real games on low-end Steam Deck-class hardware.

A skipped GPU test proves no GPU behavior. A unit test cannot prove layer ordering, and `vkcube` cannot prove real-game pacing.

## Code and test ownership

| Responsibility | Source of truth |
| --- | --- |
| Host launch environment | `scripts/mako-launch` |
| Private manifest generation/install | `mako-render/CMakeLists.txt` |
| Package layout verification | `scripts/package-local.sh` |
| Process-start policy and transport choice | `mako-render/src/presentation_policy.hpp` |
| Swapchain creation/pNext filtering | `mako-render/src/instance.cpp`, `mako-render/src/swapchain.cpp` |
| Generated/original presentation | `mako-render/src/swapchain_present.cpp` |
| Standalone deterministic test | `scripts/test-mako-launch.sh` |
| Renderer transport tests | `mako-render/tests/presentation_policy_tests.cpp` |
| Decky wrapper and Flatpak equivalents | `../plugin/py_modules/mako_plugin/` and `../plugin/tests/` |
