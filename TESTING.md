# Testing MAKO

MAKO uses separate gates for deterministic product behavior and hardware behavior. A passing CPU-only suite is required for every change, but it is not treated as evidence that a Vulkan driver, Gamescope, or a game presents generated frames correctly.

## Repository ownership split

| Test boundary | Owner | Examples |
| --- | --- | --- |
| Deterministic implementation and public command contracts | MAKO | Scene generation, reference images, masks, scoring, scheduler/policy tests, CLI parsing, generated-shader freshness, package layout, and synthetic bridge behavior |
| Individual production-path diagnostic executables | MAKO | `mako-cli` LSFG, spatial-scaling, and combined quality commands, because they compile against the production backend and scaler rather than reimplementing either in QA code |
| Declarative hardware scenarios and licensed execution | Private MAKO Gym | Vulkan/Gamescope feature, procedural-quality, LSFG/spatial/runtime performance, repeatability, synchronization-validation, scripted recovery, real Gamescope WSI, D3D11/DXVK plus D3D12/VKD3D-Proton end-to-end, and tiered Proton-family compatibility manifests; DLL discovery; AMD requirements; complete matrix orchestration; runtime/parameter/phase assertions; and PPM artifact validation |
| Hardware evidence and sanitization | Private MAKO Gym | Per-case logs, comparison images, summaries, and sanitization markers beneath ignored Gym output directories |
| Release-gate dispatch and exact-package selection | MAKO | The optional/required Gym bridge, contract-version handshake, disposable SteamOS runner workflow, and selection of the exact package or source-built CLI under test |
| Comparative real-game sessions | Private MAKO Traces | Reviewed game captures, schemas, checksums, and append-only evidence history |

Portable MAKO tests must not require MAKO Gym, a licensed DLL, an AMD GPU, Gamescope, or a compositor. MAKO Gym must consume MAKO's public executable/diagnostic boundary instead of copying production C++, shaders, thresholds, scene code, or configuration owners. A suite that primarily proves package construction or release dispatch remains in MAKO even when its required release workflow delegates real-hardware scenarios to Gym.

## Validation cycles

Each cycle answers a different question and the later cycles do not erase evidence required by the earlier ones:

| Cycle | What it proves | What it does not prove |
| --- | --- | --- |
| Portable change gates | Deterministic Renderer, backend, frontend, schema, packaging-policy, and sanitizer behavior | Real Vulkan presentation, AMD image quality, or installation |
| Direct device iteration | The selected change works quickly on the current SteamOS development installation | Archive layout, clean-checkout reproducibility, 32-bit or Flatpak behavior unless explicitly included |
| Full local tester ZIP | The complete local source can be packaged, installed, and upgraded as one self-contained build | A clean remote commit, the release hardware gate, or public assets |
| SteamOS hardware release gate | The pushed commit rebuilds in a clean checkout and passes the real AMD, Vulkan-loader, MAKO Gym, dual-bitness, Flatpak, package, and optional exact-ZIP deployment boundaries | The full game/runtime matrix or a published release |
| Manual release-candidate matrix | Selected real games survive the presentation, focus, overlay, hitch, and swapchain scenarios in scope | The public download and production release page |
| Published-package check | The exact GitHub asset downloads and installs through the normal user path | Compatibility with every untested game or hardware configuration |

Use [MAKO Decky packaging](plugin/docs/PACKAGING.md) for direct and tester cycles, [How to release MAKO](HOW_TO_RELEASE.md) for the release candidate and publication sequence, and the sections below for the required gates.

## Pull-request gates

The `Tests` GitHub Actions workflow runs on every pull request and push to `main`:

- **MAKO Decky:** the Python backend suite, focused frontend behavior tests with coverage thresholds, and the production Decky bundle build;
- **MAKO Renderer:** the complete non-hardware CTest suite, including the optional Qt UI and its localization contract, with both GCC and Clang on Linux;
- **Renderer sanitizers:** the portable scheduling, generated-frame-plan, presentation-policy, spatial-scaling policy, profile, transition, and colour-math boundaries under AddressSanitizer and UndefinedBehaviorSanitizer; and
- **Trace producer and protected inputs:** safe capture staging, sanitization, metadata, checksum, containment, rollback, and concurrent no-clobber behavior, plus an index-level rejection gate for licensed DLL/model content and disguised binary, shader, dump, or archive payloads, on Linux and macOS.

The Renderer suite also exercises the standalone `mako-launch` contract: deterministic implicit-layer selection, loader activation, LSFG-VK conflict guards, the Gamescope WSI/HDR process-start boundary, strict fail-closed launcher settings, Zink/ALSA environment application, advanced environment forwarding, argument quoting, input validation, and child exit-status propagation. The portable `run-mako-gym.sh` contract separately proves optional absence, required fail-closed behavior, exact argument forwarding, centralized eleven-suite dispatch, version mismatch rejection, and runner validation without needing the private checkout. The packaged hardware smoke test proves instance/device insertion with `vulkaninfo` and, when a graphical compositor and `vkcube` are available, covers finite swapchain creation and presentation too. Presentation changes must preserve the invariants and expanded matrices in [WSI isolation](engine/docs/WSI-ISOLATION.md), [HDR pipeline architecture](engine/docs/HDR-PIPELINE.md), and [spatial scaling architecture](engine/docs/SCALING.md).

The frontend suite intentionally tests operations where a UI/backend disagreement can damage or misrepresent user state: Renderer installation, configuration persistence, typed profile-field patches, centralized burst coalescing, single-flight writes, close-panel flushing, profile runtime-session transitions, out-of-order profile loads, profile switching, default-profile protection, persistent section state, supported Steam focus-flow values, and Decky RPC method names. It does not use snapshots or test static labels and layout.

The backend suite characterizes the exact generated wrapper and profile-sidecar bytes at the pure-module/service boundary. These tests ensure refactoring cannot silently change wrapper ordering, safety exports, profile metadata, or the allowlisted Decky-only settings that are merged with Renderer TOML.

The Decky binding freshness gate is `npm --prefix plugin run check:generated-config`. It is read-only and fails when the tracked TypeScript or Python binding differs from `plugin/shared_config.py` and its generators; normal Decky build, watch, and backend-test commands invoke the same check automatically.

The Decky localization gate is `npm --prefix plugin run check:i18n`. It verifies the ordered string-only dictionary contract, canonical regional language codes, language metadata, Steam aliases, named-placeholder parity, static call-site keys and English fallbacks, exact replacement fields, complete template usage, and the tracked `src/i18n/languages.json` bundle without writing files. `plugin/tests/test_localization_language_contract.py` also locks Decky and the Renderer desktop UI to the same ordered supported-language inventory, native display names, and complete Renderer catalog shape. Intentional dictionary changes regenerate that tracked bundle with `npm --prefix plugin run generate:i18n`; frontend tests exercise language normalization, regional Portuguese selection, translated replacement, and safe English/unsupported-language fallbacks.

The Renderer portable suite also verifies the adjacent GLSL source hashes and embedded payload hashes recorded for HDR colour-conversion SPIR-V and both spatial-scaling SDR variants. Regenerate `engine/mako-backend/src/shaders/color_conversion_spirv.hpp` and its hash manifest with `engine/scripts/generate-color-conversion-spirv.py` when an owned conversion shader changes. Regenerate `engine/mako-render/src/shaders/spatial_scaling_spirv.hpp` and its hash manifest with `engine/scripts/generate-spatial-scaling-spirv.py` when the spatial compute shader changes. Both generators require `glslangValidator`; never edit the embedded outputs independently.

The Decky job also runs the repository Markdown formatting check. Install the tracked local hook once with `just install-hooks`; it formats only staged Markdown through lint-staged, preserves partially staged work, and leaves JS/TS, Python, C++, generated output, and vendored code untouched.

The backend suite treats Armada as an unsupported-host safety boundary: native host detection must survive a translated Decky process, stale x86 files cannot report a successful install, old wrappers must exit before any MAKO exports, and persisted MAKO Flatpak activation must be removed without touching competitor-only settings. These deterministic checks prove fail-closed behavior only. Native Renderer support still requires the real-hardware evidence listed in [Armada and native AArch64 support](plugin/docs/ARMADA.md).

Run the same gates locally with:

```bash
just check-markdown-format
./scripts/test-protected-inputs.sh
./scripts/test-capture-trace.sh
npm --prefix plugin run check:generated-config
npm --prefix plugin run check:i18n
npm --prefix plugin test
npm --prefix plugin run test:frontend:typecheck
npm --prefix plugin run test:frontend:coverage
npm --prefix plugin run build
(cd engine && ./scripts/test-adaptive-scheduler.sh)
(cd engine && MAKO_ENABLE_SANITIZERS=ON ./scripts/test-adaptive-scheduler.sh)
```

The full Renderer suite additionally requires the Vulkan and X11 development headers:

```bash
cmake -S engine -B engine/build/local -DBUILD_TESTING=ON -DMAKO_BUILD_UI=OFF
cmake --build engine/build/local
ctest --test-dir engine/build/local --output-on-failure
```

The portable local command keeps the optional Qt UI disabled. When Qt 6 Base and Declarative development packages are installed, configure with `-DMAKO_BUILD_UI=ON` to compile `mako-ui` and add `ui-localization-unit`; the normal GCC/Clang CI matrix always uses that path.

## Spatial scaling validation

The portable Renderer suite verifies scaling configuration, fixed and variable surface policy including echoed-presentation suppression, Vulkan-maximum clamping and device-local-memory presentation envelopes, opaque single-array-layer/unprotected/non-shared-present shape policy, immediate discrete method recreation plus debounced numeric/model/capacity recreation, Fixed display-budget admission, lower-present stall recovery, and embedded shader freshness. These checks do not prove that a driver accepts the required transfer, blit, sampled-image, and storage-image operations, that an application handles the injected out-of-date result, that the application presents on the required queue, that batch semaphores remain untouched on rejection, or that the reconstructed result is visually correct.

Every spatial-scaling change needs the proportionate real-Vulkan matrix in [Spatial scaling architecture](engine/docs/SCALING.md). At minimum, exercise a fresh process with Scaling Engine provisioned at the Native Resolution default and Frame Generation Off while proving that LSFG interop/backend/private resources are retained but no per-frame generation or spatial work is submitted; turn Frame Generation On and Off live in that same process; exercise MAKO Scaler plus LS1 Quality and Performance independently and with Fixed and Adaptive Frame Generation; and verify missing DLL/translator/backend startup fails closed without blocking Native Resolution or MAKO Scaler. Cover fixed Gamescope with HDR exposure disabled, variable desktop surfaces including a compositor-echoed presentation extent, Vulkan-maximum clamping, device-memory admission and rejection at 1080p, 1440p, 4K, ultrawide, 5K and 8K presentation sizes, and non-widescreen source preservation, standard and high-precision SDR for every method including LS1's RGBA8 model-boundary conversion, every LS1 sharpness variant, ordinary queue 0 from an application-created graphics-and-compute family that presents to the surface, supported and rejected swapchain shapes, natural swapchain recreation, a process started with Scaling Engine on while switching live between Native Resolution and each scaler plus factor/sharpness/Flow Scale/Lighter FG Model changes, live Fixed/Adaptive changes that grow generated-frame capacity, immediate method switching, rapid numeric-edit debounce and coalescing, a non-null Gamescope-owned final present fence or one MAKO-owned maintenance1 fence before the out-of-date signal, replacement-context flow/model/capacity evidence, Fixed above-half-refresh admission, injected lower-present stalls and recovery, and fail-closed HDR/unsupported-format behavior. Resolution-churn coverage must prove one deferred and one completed lower retirement per game-requested recreation, completion only after a later replacement present plus the compositor grace and every associated MAKO fence, no duplicate lower destruction, no synchronization-validation error, and continued presentation. Scaling Engine itself remains process-static because its WSI chain is fixed at instance creation; only Native Resolution/scaler activity and the documented tuning controls are live inside a provisioned process. Flow Scale, model, and capacity edits without retained FG resources must remain deferred. An echoed variable extent must stop scaling rather than be multiplied again. A present batch containing multiple swapchains and any active scaler must return before consuming application wait semaphores. Combined frame-generation runs still require separate FP32 and FP16 evidence. Record requested/active method plus source and presentation dimensions, present-retirement mode and deferred/completed records, device-local heap and presentation-pixel budget from Renderer policy logs, preserve Vulkan-loader evidence that the intended development layer was active, and compare image quality and GPU cost against a native-resolution reference.

MAKO Gym's 74-case procedural visual matrix measures LSFG, the production spatial scalers, and their real exported-image/timeline-semaphore handoff against vector-rendered references. It includes FP32/FP16, every spatial method, every scene, near-endpoint interpolation, near-native scaling, and representative Flow Scale/model/factor/sharpness interactions. A separate nine-case three-run sentinel requires byte-identical output across independent initialization, a 17-row repeated LSFG benchmark enforces practical throughput and sample-variance budgets through 5120×2160, 36 exact-resolution rows score spatial pixels and measure complete production graphs with Vulkan timestamps, 12 paired live rows enforce present/frame p95/p99 plus CPU/RSS budgets, and eight canonical paths run under Khronos synchronization validation. These lanes still do not measure subjective game quality, input-to-photon latency, power, or scanout cost. Likewise, a successful `vulkaninfo` or one-frame `vkcube` smoke test proves only the loader or basic presentation boundary. A release that changes spatial reconstruction must retain visual, deterministic, performance, synchronization, lifecycle, and relevant game/runtime evidence rather than treating any one gate as a substitute.

### MAKO Gym advanced native-Vulkan smoke matrix

The private sibling [MAKO Gym](https://github.com/eugeniosegala/MAKO-Gym) repository owns the real-hardware scenario inventory, Vulkan/Gamescope runner, assertions, and run artifacts. MAKO retains only `engine/scripts/run-mako-gym.sh`, its contract version, and a hardware-independent bridge test. This keeps licensed-model QA and expanding hardware scenarios outside the product repository without allowing the release gate to skip them silently.

#### Select the smallest sufficient Gym scope

Do not run every hardware inventory after every edit. Run the portable MAKO and Gym contracts first, then select the smallest hardware suite and regex that exercise the changed boundary. Omitting `--filter` runs the complete selected single-runtime suite; running every complete suite is reserved for the SteamOS release gate, broad changes spanning scheduling, scaling, backend pixels, synchronization, determinism and cost, or an explicit final validation request. The native Gamescope E2E lane owns production and reversed WSI order plus cross-layer live/recreation behavior; the Proton E2E lane owns D3D11/DXVK and D3D12/VKD3D-Proton game-like scenes through that production chain; the Proton compatibility lane repeats a stratified subset across provenance-checked Official, Experimental, Hotfix, and GE runtimes. Run focused E2E rows only when their boundary changed and all seven native, twelve translated, plus four-family extended compatibility coverage before release.

| Change boundary | Development hardware selection |
| --- | --- |
| Documentation, website, Decky-only UI/backend, or portable schema work | No Gym run unless the change alters a Renderer-facing contract; use the owning portable tests. |
| Fixed/Adaptive configuration, resource construction, option combinations, scaler selection or fallback | Feature suite with the narrowest matching labels. |
| LSFG output, spatial reconstruction, shaders, colour conversion, precision, Flow Scale, model selection or scaling-to-FG handoff | Quality suite filtered by affected pipeline, method, scene or parameter; add relevant feature rows when lifecycle or presentation also changed. |
| LSFG dispatch cost, precision/model/Flow throughput, or generated-frame capacity | Performance suite filtered by affected resolution, model, precision, multiplier, or Flow workload. |
| Spatial scaler, copy, factor, resolution, or frame-generation handoff GPU cost | Spatial-performance suite filtered by affected method, resolution, factor, or handoff workload. |
| Game-thread present/frame duration, Renderer process CPU, peak RSS, or reports that MAKO feels heavy | Runtime-overhead suite at `--tier fast`, widened by resolution or tier when a shared presentation/backend owner changed. |
| Missing barriers, access hazards, image transitions, command recording, or exported-resource synchronization | Synchronization-validation suite filtered by the affected canonical quality label; widen to all eight for a shared owner. |
| Initialization, shader selection, or unexplained pixel instability | Repeatability suite filtered by the affected sentinel; widen to all nine for a shared owner. |
| Cadence transitions, Steady Adaptive, Dynamic Cadence Recovery, stalls, scheduler recovery or swapchain lifecycle | Recovery suite filtered by the affected recovery family; add relevant feature rows when configuration or construction also changed. |
| Proton, DXVK, VKD3D-Proton, D3D11/D3D12 presentation, or translated game-like scene behavior | Proton E2E suite filtered by translation, scene, scheduler, or scaler; widen to all twelve for a shared translation/WSI owner. |
| Wine/Proton, DXVK, or VKD3D-Proton version behavior | Proton compatibility core/fast only for focused runtime work; extended/extended across at least four distinct runtime families only before release or after a cross-cutting runtime change. |
| Shared Vulkan synchronization, presentation, backend resource ownership or cross-cutting Renderer changes | Run every affected suite completely; run every suite only when the boundary genuinely spans them. |
| Release candidate | The required SteamOS workflow runs all 47 feature, 74 quality, 17 LSFG-performance, 36 spatial-performance, 12 runtime-overhead, eight synchronization-validation, nine repeatability-sentinel, 30 recovery, seven native Gamescope E2E, twelve Proton E2E, and ten Proton compatibility sentinels across at least four runtime families against the exact source/package boundary. |

Examples:

```bash
just test-engine-gym-feature --filter '^(fixed-|adaptive-)'
just test-engine-gym-quality --filter '^spatial-mako-'
just test-engine-gym-quality --filter '^combined-ls1-performance-.*traffic'
just test-engine-gym-performance --filter '800p-90'
just test-engine-gym-spatial-performance --filter '^deck-800p-mako-'
just test-engine-gym-runtime-overhead --tier fast
just test-engine-gym-sync-validation --filter '^spatial-'
just test-engine-gym-repeatability --filter '^combined-'
just test-engine-gym-recovery --filter '(stall|cadence-drop)$'
just test-engine-gym-recovery --filter 'recreate$'
just test-engine-gym-recovery --filter 'near-target|four-x'
just test-engine-gym-recovery --filter 'scaling-live'
just test-engine-gym-gamescope-e2e --filter '^gamescope-live-'
just test-engine-gym-proton-e2e --filter '^proton-vkd3d-hud-'
just test-engine-gym-proton-compatibility --runtime-tier core --case-tier fast
```

Every filtered result is evidence only for its selected rows. Before merging a production change, widen from the iteration filter to the complete affected suite when the change touches a shared owner used by multiple rows. Portable-only changes and isolated scenario/assertion edits do not acquire an unrelated full-Gym requirement.

Use the bridge from a workspace where `MAKO/` and `MAKO-Gym/` are siblings:

```bash
just test-engine-gym-feature --list
just test-engine-gym-feature --filter '^ls1-quality-'
just test-engine-gym-feature
```

Set `MAKO_GYM_REPO` or pass `--gym-repo <path>` for a non-sibling checkout. Local use is optional: when Gym is absent, the bridge prints an explicit skip and exits successfully. Automation that requires GPU evidence passes `--require`; absence, an incompatible `GYM_CONTRACT_VERSION`, or a missing runner is then fatal. `just test-engine-gym` remains the default feature-suite command for compatibility, `just test-engine-gym-feature` makes that selection explicit, and `just test-engine-gym-required` applies required-checkout behavior to the selected suite. The `spatial-performance` suite scores exact-resolution scaler output and timestamps the complete production scaler graph without changing the live Renderer, `runtime-overhead` compares identical layer-idle and active native-Vulkan workloads through Gamescope, and `sync-validation` requires proven `VK_LAYER_KHRONOS_validation` activation plus a record-only `SYNC-HAZARD` canary before accepting clean production logs.

MAKO Gym's current 47-case manifest covers native passthrough with Scaling Engine both absent and provisioned, every MAKO and LS1 scaling model/variant, intentional fallback, Fixed, Adaptive, Steady/Smooth, Dynamic Cadence Recovery configuration, Ultra Performance, FP32/FP16 configuration, Flow Scale, Lighter FG Model, and representative scaling-plus-frame-generation combinations. Its steady finite-cube rows prove configuration and construction, not a recovery transition. Its own portable gate validates the declarative inventory and assertion contract without a GPU or DLL. The complete lane table, duration classes, pass criteria, outputs, and limitations are authoritative in `MAKO-Gym/docs/VULKAN-FEATURE-MATRIX.md`.

Select the scripted recovery matrix through the bridge:

```bash
./engine/scripts/run-mako-gym.sh --suite recovery --list
./engine/scripts/run-mako-gym.sh --suite recovery --filter 'recreate$'
./engine/scripts/run-mako-gym.sh --suite recovery
```

That manifest has 30 rows. Its small native-Vulkan moving workload controls cadence rise, false-probe rejection, 150/210 ms hitches, 300/500 ms and repeated stalls, sustained cadence drop, single and chained game-owned swapchain replacement, steady and noisy near-target cadence, a 60→100→60 high-base round trip, Fixed 100→120 display-budget admission, ordered 4× health, 2×/3×/4× target-rate control, and live scaling lifecycle/coalescing changes. It covers Fixed, Fractional Adaptive, Steady Adaptive, Ultra Performance, MAKO scaling, LS1 scaling, method/factor/sharpness/Flow/model/capacity switching, Native bypass and scaler restoration inside an isolated MAKO presentation chain, compatible re-query activation, variable source/presentation scaling, and fixed-extent rejection. Every row enforces its source-cadence validity floor, exact ordered diagnostics, and the expectation-specific recovery, cadence, lifecycle, or delivery threshold. Its authoritative phase and evidence contract is in `MAKO-Gym/docs/RUNTIME-RECOVERY-MATRIX.md`.

Select the release-only real Gamescope end-to-end lane when a change crosses the compositor, WSI, swapchain, live-transition, or generated-presentation boundary:

```bash
./engine/scripts/run-mako-gym.sh --suite gamescope-e2e --list
./engine/scripts/run-mako-gym.sh --suite gamescope-e2e --filter '^gamescope-live-'
./engine/scripts/run-mako-gym.sh --suite gamescope-e2e
```

This separate seven-row matrix automatically constructs a controlled chain from the installed 64-bit Gamescope WSI and exact MAKO package manifests. It proves loader membership and order, MAKO's allowed WSI presentation policy, Fixed and Adaptive generated delivery, Native Resolution passthrough, variable 640×360-to-960×540 scaling, every live scaler method, live FG Off/On without an FG-only recreation, maintenance1-fenced scaler replacements, deferred/completed lower retirement, natural recreation, transport health, and the deliberately reversed fail-closed layer order. It is slower and environment-specific, so normal edits retain unit and integration coverage and run focused E2E rows only when the affected boundary warrants them; the complete E2E matrix is required before release. Its authoritative contract is in `MAKO-Gym/docs/GAMESCOPE-END-TO-END.md`.

Select the release-only Proton translation lane when a change crosses D3D11/D3D12, DXVK/VKD3D-Proton, translated presentation, or the game-like WSI boundary:

```bash
./engine/scripts/run-mako-gym.sh --suite proton-e2e --list
./engine/scripts/run-mako-gym.sh --suite proton-e2e --filter '^proton-dxvk-traffic-'
./engine/scripts/run-mako-gym.sh --suite proton-e2e --filter '^proton-vkd3d-hud-'
./engine/scripts/run-mako-gym.sh --suite proton-e2e
```

This separate twelve-row matrix locally builds a source-only Windows workload and runs motion-boundary, traffic, crowd, camera-motion, HUD/disocclusion/particle, and mixed-stress scenes through each of D3D11/DXVK and D3D12/VKD3D-Proton. Each translation retains Fixed and Adaptive generation plus Native Resolution, MAKO Scaler, LS1, and LS1 Performance. A pass requires exact translation-engine proof, the real Gamescope WSI-before-MAKO chain, production MAKO activation, the requested scaler or Native passthrough, at least three positive generated-frame windows, all 360 bounded source frames, no unhealthy recovery/delivery record, and clean process exit. VKD3D-Proton's deterministic destroy-before-create startup cycle must produce exactly one retained-lower `oldSwapchain` handoff, classify the new context as a replacement, and complete without a native-window-in-use retry or swapchain-creation failure. The runner owns a disposable Proton prefix and hard deadline; generated PE/DLL/cache data stays ignored and is never package content. Run focused rows while iterating and all twelve only before release or after shared translation/WSI changes. Its authoritative contract is in `MAKO-Gym/docs/PROTON-END-TO-END.md`.

Select the release-only multi-runtime compatibility lane only when investigating a Proton-version boundary or validating a release candidate:

```bash
./engine/scripts/run-mako-gym.sh --suite proton-compatibility --runtime-tier core --case-tier fast
./engine/scripts/run-mako-gym.sh --suite proton-compatibility --runtime-tier extended --case-tier extended --minimum-runtimes 4
```

The compatibility manifest references canonical Proton E2E cases instead of duplicating their settings. Fast selects six stratified DXVK/VKD3D-Proton, Fixed/Adaptive, Native/MAKO/LS1/LS1 Performance sentinels per runtime; extended selects ten by adding camera, crowd, mixed-stress, and VKD3D-Proton Ultra coverage. The execution runtime is distinct from the modern runtime used once to build the workload, and every case log records both identities. Current Official, Experimental, and Hotfix form the core tier; extended adds the newest installed GE-Proton. The SteamOS release gate requires all ten cases on at least four families. Older installed runtimes remain an explicit exploratory boundary because the modern SDL3 test workload can fail on an older DXGI `SetRotation` implementation before MAKO receives a swapchain. Its authoritative tier, evidence, and claim boundary is in `MAKO-Gym/docs/PROTON-COMPATIBILITY.md`.

The complete validation hierarchy is portable unit invariants, small real-Vulkan construction tests, deterministic multi-object scenes, real WSI/Gamescope lifecycle tests, distinct DXVK and VKD3D-Proton lanes, and sanitized real-game traces as final compatibility evidence. MAKO Gym owns the hardware inventories and orchestration through the Proton tier; MAKO Traces owns stored real-game evidence. A synthetic pass must never be presented as proof for an untested commercial game.

The replacement rows use Vulkan's real `oldSwapchain` handoff rather than destroying first. They require the new Renderer context to be classified as a replacement while both contexts are live, the old context to retire afterward, Adaptive's one-second replacement settling guard to remain distinct from the three-second cold-start guard, and native plus LS1 Adaptive generation to resume within a three-second post-replacement source phase.

Select the separate procedural visual matrix through the same bridge:

```bash
./engine/scripts/run-mako-gym.sh --suite quality --list
./engine/scripts/run-mako-gym.sh --suite quality --filter '^combined-.*traffic' \
  --cli "$PWD/engine/build/mako-cli/mako-cli"
./engine/scripts/run-mako-gym.sh --suite quality \
  --cli "$PWD/engine/build/mako-cli/mako-cli"
```

That manifest contains 20 frame-generation, 31 spatial-scaling, and 23 combined cases across five procedural game-like scenes. Gym validates every row and PPM artifact, records one complete log per case, and sanitizes the final summary. Its authoritative catalog and evidence boundary are in `MAKO-Gym/docs/AMD-QUALITY-REGRESSION.md`.

Select the LSFG performance, spatial GPU-time, live runtime-overhead, synchronization-validation, and repeatability lanes through the same bridge:

```bash
./engine/scripts/run-mako-gym.sh --suite performance --cli "$PWD/engine/build/mako-cli/mako-cli"
./engine/scripts/run-mako-gym.sh --suite spatial-performance --cli "$PWD/engine/build/mako-cli/mako-cli"
./engine/scripts/run-mako-gym.sh --suite runtime-overhead --tier fast --launcher /path/to/mako-launch
./engine/scripts/run-mako-gym.sh --suite sync-validation --cli "$PWD/engine/build/mako-cli/mako-cli"
./engine/scripts/run-mako-gym.sh --suite repeatability --cli "$PWD/engine/build/mako-cli/mako-cli"
```

LSFG performance runs 17 warmed, repeated workloads from 720p through 5120×2160 and enforces conservative practical generated-FPS plus coefficient-of-variation budgets. Spatial performance runs 36 every-scaler exact-resolution pixel/timestamp workloads through 5120×2160 and enforces practical median/p95 plus variance budgets. Runtime overhead runs 12 paired layer-idle/active workloads and enforces present/full-frame p95/p99, CPU/RSS, and variance budgets. All three performance lanes provide fast/extended/all selection and signature-checked baselines valid only for a controlled same-host comparison. Synchronization validation runs eight canonical quality paths under the Khronos layer with `validate_sync` enabled and fails closed on missing activation or any finding. Repeatability runs nine cross-pipeline sentinels three times from independent initialization and requires byte-identical generated PPMs. Their authoritative claim boundaries are in `MAKO-Gym/docs/RENDER-PERFORMANCE.md`, `MAKO-Gym/docs/SPATIAL-PERFORMANCE.md`, `MAKO-Gym/docs/RUNTIME-OVERHEAD.md`, `MAKO-Gym/docs/SYNCHRONIZATION-VALIDATION.md`, and `MAKO-Gym/docs/QUALITY-REPEATABILITY.md`.

These remain bounded hardware contracts, not a release-compatibility verdict. The finite-cube pass proves real swapchain construction, selected scaler/context activation, actual Fixed/Adaptive delivery diagnostics, and clean finite presentation. The recovery pass additionally proves ordered response to controlled cadence, stall, and swapchain events. Neither proves subjective spatial quality, actual selected LSFG shader precision from logs alone, DXVK or VKD3D-Proton game behavior, arbitrary live configuration mutation in a title, generated-image starvation, device-loss recovery, 32-bit Vulkan presentation, Flatpak runtime behavior, Steam Deck/RDNA2, another driver, HDR scaling, or compositor scanout timing. Retain those rows as not tested until their owning hardware, capture, lifecycle, and game matrices run.

Current measured development evidence is limited to one RADV NAVI33 host: the spatial shader reported 56→40 VGPR, 18→24 subgroups per SIMD, 365→320 instructions, 264→229 inverse throughput, and zero spills. A live direct-Wayland run reconstructed 500×500 to 750×750 and then safely suppressed an echoed 750×750 recreation instead of compounding it; bounded fixed-extent Gamescope/X11 runs activated 332×332-to-500×500 scaling before both Fixed and Adaptive frame generation. Record this as NAVI33 evidence only, not Steam Deck/RDNA2 validation.

## SteamOS hardware release gate

Before publishing a release candidate, run the `SteamOS hardware validation` workflow for that commit. On the dedicated SteamOS/AMD test machine, the recommended entry point creates a verified one-job runner, dispatches the selected clean branch, waits for the result, and removes the runner registration, credentials, checkout, staging, generated packages, and local runner logs:

```bash
./scripts/run-steamos-hardware-validation.sh
```

The launcher refuses a dirty or remote-divergent branch, an existing queued hardware run, a non-SteamOS/non-AMD host, `/tmp`-backed runner storage, or less than 30 GiB of free work space. It downloads the latest official Linux x64 Actions runner and verifies the archive against the SHA-256 published in the same official release. The workflow still requires the normal 64-bit and 32-bit Renderer/Flatpak build prerequisites, `vulkaninfo`, `vkcube`, Gamescope, access to the licensed `Lossless.dll` used only for validation, and an authorized MAKO Gym checkout discoverable through `MAKO_GYM_REPO` or the sibling default. Gym's own contract gate must pass before the runner is registered.

The workflow:

1. runs MAKO Gym's complete 74-case procedural render-quality matrix with the source-built CLI, including FP32/FP16 LSFG, every spatial method, edge parameters, and combined handoffs;
2. runs 17 repeated LSFG performance workloads, 36 exact-resolution pixel-qualified timestamp-query spatial-performance workloads, eight synchronization-validation paths, and nine three-run deterministic-output sentinels with the same source-built CLI;
3. builds and tests the native 64-bit and 32-bit Renderer payloads;
4. builds and installs each supported Flatpak runtime extension for verification;
5. builds the complete Decky ZIP from the same source tree;
6. extracts the packaged Renderer and proves that the real Vulkan loader activates `VK_LAYER_MAKO_render` on the runner's AMD GPU;
7. passes that exact extracted Renderer launcher to MAKO Gym and requires all 47 native-Vulkan feature scenarios to pass;
8. builds Gym's test-only native-Vulkan workload and requires all 30 scripted recovery, cadence, live-transition, target-rate, and lifecycle scenarios plus 12 paired idle/active runtime-overhead rows to pass against the same extracted launcher;
9. requires all seven native Gamescope E2E rows to load the host's real WSI layer above that exact packaged MAKO layer, prove variable scaling, live FG/scaler transitions, fenced retirement, natural recreation, and the reversed-order fail-closed path;
10. locally builds Gym's source-only Windows workload and requires all twelve D3D11/DXVK and D3D12/VKD3D-Proton scene rows to prove the translation engine, production WSI/MAKO chain, scaling, generated delivery, bounded completion, and clean shutdown;
11. repeats ten canonical translation sentinels across current Official, Experimental, Hotfix, and GE-Proton families with exact build/execution provenance; and
12. retains the complete verified Decky ZIP, sanitized environment evidence, procedural GPU comparison images, performance/repeatability/overhead evidence, and MAKO Gym feature/recovery/native-E2E/Proton-E2E/Proton-compatibility logs and summaries for 14 days under the tested commit.

Pass `--deploy-to-decky` only on a dedicated device with an existing MAKO Decky development installation. That option safely synchronizes the already-verified ZIP into the existing plugin, asks Decky Loader to reload it, and invokes MAKO Decky's normal installer against that exact bundled Renderer. The production installer owns host libraries, manifests, wrappers, engine state, diagnostics, and refreshes of already-installed Flatpak runtime branches; no component is rebuilt or installed through a second CI-only implementation. It intentionally does not run by default because it changes the installed test device.

The launcher reuses the repository's existing ignored `engine/build/cache` tree for compiler, Flatpak, native SDK, pnpm, and Actions tool caches instead of creating a second large cache under the user's home directory; the location can be changed with `MAKO_HARDWARE_CI_ROOT`. Inspect the retained data without deleting anything with `./scripts/prune-hardware-ci-cache.sh`, or add `--confirm` when the space is more valuable than the next build's warm cache. The workflow itself removes large staging and generated outputs even when a manually configured persistent runner is used.

This gate is a fresh MAKO checkout on the real SteamOS/AMD host, not a fresh virtual SteamOS installation. The separately versioned private Gym checkout is a required host QA dependency and its contract version is checked before execution. The gate validates the physical GPU, driver, Vulkan loader, Gym matrix, Flatpak, packaging, and optional Decky deployment boundaries. The GitHub-hosted pull-request jobs independently validate a clean portable Ubuntu environment; neither gate replaces the other.

## Runtime compatibility matrix

The hardware workflow establishes packaging, loader activation, and deterministic image-quality boundaries. Game presentation still requires the release-candidate matrix in [Adaptive validation](engine/docs/ADAPTIVE-VALIDATION.md): DXVK and VKD3D-Proton under Gamescope, overlay and focus transitions, hitches, swapchain recreation, and the supported desktop GPU paths. Spatial-scaling changes additionally require the surface, queue, batch, format, startup, and quality matrix in [Spatial scaling architecture](engine/docs/SCALING.md). Record unavailable rows as **not tested** rather than treating the automated smoke test as equivalent coverage.

## Versioned game traces

Use `scripts/capture-trace.sh` after a completed game session to archive comparative evidence into a sibling private `MAKO-Traces` checkout. Read the [trace extractor guide](TRACES.md) first. Keep the private repository outside the MAKO worktree so licensed paths, large runtime logs, and subjective notes never become product artifacts; it is not required for normal MAKO builds, tests, packaging, installation, or releases.

The public producer contract test is private-data-free and runs in pull-request CI:

```bash
./scripts/test-capture-trace.sh
```

It characterizes safe component normalization and containment, exact offset-aware timestamp ordering, common credential redaction, stable metadata identity fields, canonical checksums, aligned notes headings, validator rollback, concurrent no-clobber publication, and the single-path success output without requiring a MAKO Traces checkout containing real evidence.

The capture command requires a game name, explicit archive version label, and session start time. Development builds must include their source identity rather than masquerading as a released version. The script stores sanitized diagnostics and configuration, optionally clips Decky and Steam logs to the session window, records the exact MAKO source revision and host identity, generates an event index and notes template, rejects likely credentials, and writes checksums without overwriting an existing capture.

```bash
./scripts/capture-trace.sh \
  --game "Resident Evil 4" \
  --game-id 2050650 \
  --version "2.0.0-dev-f1f6a1c" \
  --label "fixed-adaptive-fifo-pressure" \
  --run-index 1 \
  --session-start "2026-08-20T12:30:52+01:00" \
  --session-end "2026-08-20T12:36:25+01:00" \
  --decky-log "$HOME/homebrew/logs/Mako/<decky-log>.log" \
  --steam-log "$HOME/.steam/steam/logs/console-linux.txt"
```

Close the game first so buffered records are complete. The development wrapper keeps the latest three diagnostics-enabled sessions as `present-diagnostics.log`, `.1`, and `.2`; the extractor defaults to the latest, while `--diagnostics` can select one exact rotated raw session before a fourth launch replaces it. Never archive a combined `mako-diagnostics --session all` report as one trace. The producer invokes the sibling validator when it is available; review the resulting private diff and run `./scripts/check.sh` from the MAKO Traces checkout before committing it (`just check` is the equivalent convenience alias). A stored trace supports repeatable comparison; a single run does not by itself prove a performance or image-quality regression.
