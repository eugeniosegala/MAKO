# Gamescope WSI isolation

This guide defines MAKO Renderer's Vulkan-layer discovery and presentation ownership. [HDR pipeline architecture](HDR-PIPELINE.md) owns colour policy; [runtime configuration transitions](RUNTIME-TRANSITIONS.md) owns swapchain replacement and retirement.

## Compositor and Vulkan layer

Gamescope has two separate roles:

- The **Gamescope compositor** owns the display session, Game Mode UI, focus, refresh information, composition, and scanout. It remains active.
- The **Gamescope WSI Vulkan layer** joins the application's Vulkan dispatch chain and changes swapchain and presentation behavior. MAKO excludes it from ordinary managed launches.

Disabling Gamescope WSI does not disable Gamescope, Steam, or Game Mode. It changes only the Vulkan layers visible inside the launched process.

## Why presentation needs one owner

MAKO expands one application present into generated present(s) followed by the real frame. If Gamescope WSI sits above a single combined MAKO layer, it observes only the application's original call while MAKO injects additional calls below it. Gamescope WSI therefore cannot pace every delivered image.

When both Scaling and Gamescope WSI compatibility are enabled, the managed path uses this explicit order:

```text
Application / Proton translation
    -> VK_LAYER_MAKO_render
       reconstruction + Frame Generation
    -> VK_LAYER_FROG_gamescope_wsi_x86_64
       sees every generated and real present
    -> VK_LAYER_MAKO_spatial_scaling
       capabilities + physical lower extent
    -> Vulkan driver
    -> Gamescope compositor
```

The lower MAKO role performs no presentation-time GPU work. Reconstruction stays in the upper role so there is one resource and scheduling owner. An FG-only Gamescope WSI compatibility launch uses the upper MAKO role followed by WSI without the lower spatial role.

## Default managed launch

Standalone `mako-launch` and MAKO Decky's generated wrapper establish the normal boundary before `vkCreateInstance`:

```text
VK_IMPLICIT_LAYER_PATH=<private MAKO manifest directory>
VK_ADD_IMPLICIT_LAYER_PATH unset
ENABLE_MAKO=1
DISABLE_LSFG=1
DISABLE_LSFGVK=1
DISABLE_GAMESCOPE_WSI=1
ENABLE_GAMESCOPE_WSI unset
MAKO_DISABLE_HDR_EXPOSURE=1
DXVK_HDR unset
```

The host manifest directory is `<prefix>/share/mako-render/vulkan/implicit_layer.d`; MAKO Decky uses the matching user-local path. Published host packages contain 64-bit and 32-bit MAKO manifests. A prepared Flatpak uses the matching MAKO runtime extension and bounded application overrides rather than the host launcher.

The private path prevents inherited implicit layers from joining accidentally. Known competing frame-generation layers and the Gamescope HDR bridge are also disabled explicitly. Layer membership cannot be repaired after Vulkan starts, so launchers, manifests, packages, wrapper generation, and Flatpak setup all enforce the same contract.

## Guarded Gamescope WSI paths

MAKO Decky admits a staged 64-bit Gamescope WSI payload only when the per-profile **Gamescope WSI (Restart)** compatibility option was enabled before launch:

- With Scaling enabled, the WSI path requires the complete three-role split.
- With Scaling disabled, the WSI path uses the upper Renderer followed by Gamescope WSI.

Scaling alone keeps WSI isolated and uses the existing combined Renderer. The WSI toggle remains independently editable, defaults off, and is not changed by enabling or disabling Scaling. Wrapper regeneration preserves the canonical saved WSI value; old scaling-implied WSI activation is not imported as an explicit opt-in.

With Scaling on and WSI off, `GamescopeScalingSurface` supplies the Gamescope surface association needed to decouple an X11 game's source size from its output size. It creates a real Wayland Vulkan surface for a verified Gamescope XCB/Xlib window and reasserts that window association at every application present because Xwayland and Steam UI transitions can publish a competing mapping. Each Vulkan swapchain owns a matching Gamescope protocol object and publishes its actual image-count and format feedback at creation; a replacement therefore refreshes the compositor-side swapchain lifetime as well as the Vulkan handle. Combined ordered Frame Generation retains FIFO on the lower Wayland swapchain so every generated and real Vulkan present creates a distinct compositor commit; Gamescope's protocol FIFO annotation alone is too late to prevent lower MAILBOX coalescing. Scaling-only retains its existing lower present mode. The adapter does not load the Gamescope WSI layer or issue its limiter, timing, or HDR control requests. With WSI on, the established WSI chain owns that association and MAKO creates no second connection. With Scaling off and WSI off, neither path is provisioned.

The optional adapter resolves the existing Wayland and X11 client libraries at runtime, requires the active Gamescope session, the version-one `gamescope_swapchain_factory_v2` protocol, and window-root server identity, and bounds registry discovery to 500 ms. Every X11-capable Vulkan instance enables the driver Wayland surface extension, including later Wine instances; headless probes and native Wayland instances require no X11 adapter. Missing prerequisites retain the application's original surface and extent safeguards. Surface objects outlive Vulkan retirement, while protocol objects follow Vulkan swapchain creation and destruction; neither a replacement swapchain nor a replacement surface can take over the window before its first present. The present path drains already-read events only; it performs no socket polling, roundtrip, timing-history collection, or per-frame protocol allocation and adds no worker thread.

The adapter preserves X11's concrete window-size contract at both application-facing surface-capability entrypoints: `currentExtent`, `minImageExtent`, and `maxImageExtent` describe the current XCB/Xlib window, including before the first swapchain and after a resize. Only internal driver queries retain Wayland's variable extent for separate scaling output. This applies to every bridged X11 application without executable, engine-name, or version exceptions; exposing Wayland's variable-size sentinel at this boundary can crash applications such as Dolphin during startup. An unavailable or unrepresentable window returns `VK_ERROR_SURFACE_LOST_KHR` instead of fabricated capabilities. Geometry is queried only when the application requests capabilities, with no added present-path query or worker. The bridge record identifies `application_surface=x11; extent_contract=window`. Native Wayland, unbridged X11, explicit Gamescope WSI, and existing unsupported-scaling fallback policies remain unchanged.

The option is independent from Scaling and the mutually exclusive MangoHud/vkBasalt selection, and both WSI paths remain SDR-only. MAKO Decky validates the host manifest identity, library, architecture, and activation gates, then copies only that payload into its managed compatibility directory. It never exposes the full host implicit-layer directory.

Eligibility follows Gamescope WSI's active-session boundary: `GAMESCOPE_WAYLAND_DISPLAY` must be nonempty, and `WAYLAND_DISPLAY` must be empty or equal to it. Desktop Mode, a mismatched nested Wayland session, or unavailable staged manifests leave WSI disabled and use the combined Renderer. That path retains its own extent checks and can remain native if the game cannot supply a distinct source size. Once a split chain is selected, missing lower-role evidence fails closed; it cannot silently become a combined chain after Vulkan starts.

The managed wrapper sets the semantic prefix through `VK_INSTANCE_LAYERS`; manifest-directory enumeration does not define order. When Scaling and WSI are both selected, the order is render role, Gamescope WSI, spatial role, then any selected post-process tool. Caller-requested layers follow that managed prefix. See [Optional graphics integrations](LAYER-CHAINING.md) for supported exceptions.

Prepared 64-bit Heroic and EmuDeck Flatpak launches can receive the same bounded WSI payload and MAKO extension. Unprepared Flatpaks, 32-bit WSI presentation, HDR, broader host layouts, and other sandboxes are separate validation boundaries.

## Scaling ownership proof

Gamescope WSI may replace the surface handle between upper and lower roles. Fixed-surface capabilities and create decisions therefore cross the two MAKO DSOs through same-thread, one-shot relays tied to the physical device and request. The lower decision is authoritative: a valid source/presentation split activates scaling, while an equal-extent decision creates a native context. Missing, stale, or mismatched evidence rolls back the create.

Call order alone is insufficient. The lower spatial role must observe a surface created through `vkCreateWaylandSurfaceKHR`, proving that Gamescope WSI converted the application's X11 window into the compositor-owned surface. An XCB or Xlib surface at that boundary remains native with `inactive_reason=gamescope-wsi-surface-unproven`; the upper role can still retain Frame Generation. Direct combined Renderer operation has no intervening WSI owner and does not require this split-only proof.

For variable surfaces, scaling through either the managed split chain or the isolated surface adapter also requires the positively identified Gamescope output target. A missing target or a source with no enlargement headroom remains native. [Spatial scaling architecture](SCALING.md) owns the full fixed/variable extent policy.

## Presentation and swapchain policy

`PresentationEnvironmentPolicy` resolves WSI isolation and HDR exposure once at process start. `selectPresentationTransport()` then selects one immutable swapchain transport:

- `OrderedSdr` for the supported managed SDR path.
- `GamescopeHdr` only for the experimental combination of Gamescope, WSI, allowed HDR exposure, and an HDR-capable swapchain.

Current launchers select `OrderedSdr`. It owns FIFO ordering for generated and real frames and removes Gamescope's incompatible present-mode override from the lower create chain. Unknown create-chain prefixes fail before driver creation rather than mutating caller-owned data.

Frame Generation normally reserves lower WSI images for the largest configured generated batch. FG-only ordered presentation also requests its established relief headroom; combined scaling that is active or configured above 1.0 uses a smaller bounded topology for compatibility. An enabled 1.0 scaler remains inactive and retains the FG-only relief topology. Surface limits remain authoritative, and an initialization or memory failure retries once with the application's original minimum.

When the returned pool fits the requested generated batch beyond the application's minimum, MAKO acquires and presents outputs sequentially. An additional relief image is not required. Without that relief image, acquisition has a shared ceiling of 50 ms per application present, reduced by any shorter configured ceiling; the existing per-image deadline and recovery remain active. This delivery policy is shared by all Frame Generation modes and is independent of model execution. An undersized pool still admits generated images opportunistically before backend work; Adaptive can retain a smaller proven capacity and Fixed may skip unavailable output under pressure. The experimental Gamescope HDR bridge remains nonblocking.

**Game Swapchain Images (Restart)** is the per-profile escape hatch for games that reject MAKO's normal reservation. It preserves the application's requested minimum from the first managed create and every replacement. It is off by default, process-static, and never enabled by executable or runtime heuristics. With no reserved headroom, synthetic output can be skipped when the compositor has no free image.

Changes that need a new source/presentation pair use the game-owned recreation and safe-retirement rules in [Runtime configuration transitions](RUNTIME-TRANSITIONS.md). MAKO never destroys an active application swapchain merely to apply a profile setting.

## Supported concurrency boundary

Renderer interception state currently supports one active Vulkan instance dispatch domain per process. Applications must externally synchronize each `VkQueue` as Vulkan requires. Multiple swapchains can exist in that instance, but a `VkPresentInfoKHR` batch containing more than one swapchain is rejected when any entry is managed by MAKO; shared binary waits and per-swapchain extension arrays do not yet have a safe fan-out owner.

Do not claim general multi-instance, multi-queue, or multi-swapchain-batch support until the state is instance/queue-owned and real-hardware coverage exists.

## Isolation results

| Component | Managed behavior |
| --- | --- |
| MAKO render role | Admitted and gated by `ENABLE_MAKO=1` |
| Gamescope compositor and Game Mode | Remain active outside the application layer chain |
| Gamescope WSI | Excluded normally, including for scaling; admitted only by an explicit compatibility profile |
| MAKO spatial role | Admitted only when Scaling and Gamescope WSI select the complete managed split chain |
| Known competing Frame Generation layers | Disabled |
| System implicit overlays, capture, Mesa helpers, and vendor layers | Excluded unless a named guarded exception stages an exact manifest |
| Explicit application layers | Outside this implicit-discovery policy |
| Game-local DLL integrations | Files are untouched, but any other Frame Generation implementation must be disabled |

This boundary intentionally prefers deterministic presentation over arbitrary layer compatibility.

## Diagnostics and validation

### Steam focus feedback

The existing background Gamescope reader samples `GAMESCOPE_FOCUSED_APP` (input recipient) and `GAMESCOPE_FOCUSED_APP_GFX` (displayed application) from the verified same-compositor server-zero root. A strict nonzero 32-bit `SteamAppId`, or `STEAM_COMPAT_APP_ID` when it is absent, identifies this game; Steam UI uses Gamescope's app ID 769. Matching game input/display means gameplay focus, and Steam input over this game or Steam's own display means Steam UI focus. Another app, missing/empty/malformed properties, an unknown launch identity, changed compositor identity, or stale feedback is unknown, never guessed from FPS. Graphics identity is re-read to reject a transition during the sample; focus changes require 250 ms agreement. The monitor records returns even when the game submits no frames. Consumers expire cached focus after one second and perform no X11 queries on the presentation thread.

This observes Steam UI ownership, not a particular menu's identity or the game's own pause screen. Very short visits can be missed by the existing 250 ms sampling cadence. Notifications that leave input on the game do not interrupt generation. Desktop, unsupported launchers, sandbox access restrictions, and unrecognized Steam focus layouts retain ordinary scheduling without menu-authorized rebuilds. Steam, Quick Access, Decky panels, keyboard, full overlay, brief taps, focus switches, and stale/missing feedback still require real-session validation; portable tests do not prove every client version's behavior. No Gamescope WSI layer or per-frame Decky RPC is required.

### VRR and tearing feedback

The same background reader samples Gamescope's explicit server-zero `GAMESCOPE_VRR_ENABLED`, `GAMESCOPE_VRR_CAPABLE`, `GAMESCOPE_VRR_FEEDBACK`, and `GAMESCOPE_ALLOW_TEARING` properties. Requested, connector-capable, and currently active VRR remain separate facts because an overlay can temporarily stop adaptive scanout without changing the user's setting. An explicit active signal, or enabled VRR on a connector not explicitly marked incapable, selects VRR-aware pacing. Missing properties on older Gamescope builds preserve the established fixed-refresh behavior; after a valid value has been observed, a missing or malformed individual read retains that value instead of flapping pacing ownership.

This feedback changes only who owns the pacing clock. It does not select a multiplier, qualify Adaptive work, start recovery, or request swapchain recreation. `GAMESCOPE_ALLOW_TEARING` is diagnostic transport context only: ordered SDR still owns FIFO delivery, so the Steam toggle does not replace MAKO's effective present mode. The incoming application present mode and effective transport mode are both recorded at swapchain creation. Sampling remains off the presentation thread and performs no per-frame X11 or Decky query.

### Presentation evidence

For an ordinary managed launch, loader and Renderer evidence must agree that Gamescope WSI is isolated, HDR exposure is disabled, the render role selected a profile and backend, and ordered SDR presentation owns delivery. The presentation-feedback record must identify the explicit VRR state and selected pacing owner when those properties are available. For an FG-only compatibility launch, `VK_LAYER_MAKO_render` must be above the architecture-correct Gamescope WSI identity. For scaling with WSI, the complete three-role order, lower Wayland provenance, authoritative create relay, active source/presentation split, one upper reconstruction owner, and correct generated/real delivery are all required.

Scaling with WSI off must instead prove a combined Renderer context with WSI isolated, no lower spatial role, and an active source/presentation split. WSI on/off comparisons must retain the same source/output sizes, scaler, FG mode, scene, and refresh rate; a safe native fallback cannot count as improved scaling performance.

For X11/Proton scaling through the isolated surface adapter, additionally require `spatial scaling surface bridge` with `transport=wayland; gamescope_wsi=isolated` and Wayland surface provenance. A lower post-processing layer may return `VK_ERROR_LAYER_NOT_PRESENT` for global instance-extension enumeration; MAKO treats that response as opaque and attempts the same bridge, while a complete extension list without `VK_KHR_wayland_surface` and every other enumeration failure remain fail-closed. MAKO Gym's `run-proton-end-to-end.sh --scaling-surface` exercises this boundary inside Steam Linux Runtime with the existing translation matrix. Its default mode still validates the full WSI split chain. Commercial-game, other-driver, 32-bit, and Flatpak evidence remains separate.

Use `VK_LOADER_DEBUG=layer` only for a short reproduction because it is verbose. Presentation diagnostics are also opt-in and can affect timing. Collect the `layers`, `startup`, `scaling`, `performance`, and `recovery` presets described in [Collect diagnostics](COLLECT_DIAGNOSTICS.md).

Any discovery, ordering, present-mode, pNext, image-reservation, or transport change needs its portable launcher/wrapper/package/Flatpak contracts plus applicable MAKO Gym Gamescope, native Vulkan, Proton, synchronization, recovery, and runtime-family evidence. Loader success or `vkcube` does not prove real-game pacing or image correctness. Record untested architectures, sandboxes, GPUs, drivers, and hardware paths explicitly.

Present-chain filtering also preserves `VkPresentTimingsInfoEXT` (`VK_EXT_present_timing`) when a newer application or Proton runtime places it before a removed maintenance1 mode override or scaling damage region. `FilteredPresentPNextChain` in `mako-render/src/pnext_chain.hpp` copies the complete outer timing node and retains its caller-owned timing array, present IDs, fences, and untouched suffix. The fixed ABI layout remains recognized when MAKO is built with older Vulkan headers, with compile-time layout checks against SDKs that expose the extension. This does not disable the extension, rewrite timing values, change which output owns application-present metadata, or weaken rejection of unknown prefix structures. `pnext-chain` tests cover immutable input, interleaved removals, null timing arrays, passthrough, and unknown-node rejection.

## Code and test ownership

| Responsibility | Source of truth |
| --- | --- |
| Standalone launch environment | `scripts/mako-launch` |
| Renderer manifests and installation | `mako-render/CMakeLists.txt` |
| Host archive verification | `scripts/package-local.sh` |
| Process-start policy, transport, and admission helpers | `mako-render/src/presentation_policy.hpp` |
| Surface and split-role interception | `mako-render/src/entrypoint.cpp` |
| Swapchain creation and pNext filtering | `mako-render/src/instance.cpp`, `mako-render/src/swapchain/create.cpp` |
| Generated/real delivery | `mako-render/src/swapchain/present.cpp` |
| Launcher and portable Renderer tests | `scripts/test-mako-launch.sh`, `mako-render/tests/` |
| MAKO Decky wrapper, manifests, and Flatpak contracts | `../plugin/py_modules/mako_plugin/`, `../plugin/tests/` |
