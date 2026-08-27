# Testing MAKO

MAKO uses separate gates for deterministic product behavior and hardware behavior. A passing CPU-only suite is required for every change, but it is not treated as evidence that a Vulkan driver, Gamescope, or a game presents generated frames correctly.

## Repository ownership split

| Test boundary | Owner | Examples |
| --- | --- | --- |
| Deterministic implementation and public command contracts | MAKO | Scene generation, reference images, masks, scoring, scheduler/policy tests, CLI parsing, generated-shader freshness, package layout, and synthetic bridge behavior |
| Individual production-path diagnostic executables | MAKO | `mako-cli` LSFG, spatial-scaling, and combined quality commands, because they compile against the production backend and scaler rather than reimplementing either in QA code |
| Declarative hardware scenarios and licensed execution | Private MAKO-Gym | Vulkan/Gamescope feature, procedural-quality, and scripted recovery manifests; DLL discovery; AMD requirements; complete matrix orchestration; runtime/parameter/phase assertions; and PPM artifact validation |
| Hardware evidence and sanitization | Private MAKO-Gym | Per-case logs, comparison images, summaries, and sanitization markers beneath ignored Gym output directories |
| Release-gate dispatch and exact-package selection | MAKO | The optional/required Gym bridge, contract-version handshake, disposable SteamOS runner workflow, and selection of the exact package or source-built CLI under test |
| Comparative real-game sessions | Private MAKO-Traces | Reviewed game captures, schemas, checksums, and append-only evidence history |

Portable MAKO tests must not require MAKO-Gym, a licensed DLL, an AMD GPU, Gamescope, or a compositor. MAKO-Gym must consume MAKO's public executable/diagnostic boundary instead of copying production C++, shaders, thresholds, scene code, or configuration owners. A suite that primarily proves package construction or release dispatch remains in MAKO even when its required release workflow delegates real-hardware scenarios to Gym.

## Validation cycles

Each cycle answers a different question and the later cycles do not erase evidence required by the earlier ones:

| Cycle | What it proves | What it does not prove |
| --- | --- | --- |
| Portable change gates | Deterministic Renderer, backend, frontend, schema, packaging-policy, and sanitizer behavior | Real Vulkan presentation, AMD image quality, or installation |
| Direct device iteration | The selected change works quickly on the current SteamOS development installation | Archive layout, clean-checkout reproducibility, 32-bit or Flatpak behavior unless explicitly included |
| Full local tester ZIP | The complete local source can be packaged, installed, and upgraded as one self-contained build | A clean remote commit, the release hardware gate, or public assets |
| SteamOS hardware release gate | The pushed commit rebuilds in a clean checkout and passes the real AMD, Vulkan-loader, MAKO-Gym, dual-bitness, Flatpak, package, and optional exact-ZIP deployment boundaries | The full game/runtime matrix or a published release |
| Manual release-candidate matrix | Selected real games survive the presentation, focus, overlay, hitch, and swapchain scenarios in scope | The public download and production release page |
| Published-package check | The exact GitHub asset downloads and installs through the normal user path | Compatibility with every untested game or hardware configuration |

Use [MAKO Decky packaging](plugin/docs/PACKAGING.md) for direct and tester cycles, [How to release MAKO](HOW_TO_RELEASE.md) for the release candidate and publication sequence, and the sections below for the required gates.

## Pull-request gates

The `Tests` GitHub Actions workflow runs on every pull request and push to `main`:

- **MAKO Decky:** the Python backend suite, focused frontend behavior tests with coverage thresholds, and the production Decky bundle build;
- **MAKO Renderer:** the complete non-hardware CTest suite, including the optional Qt UI and its localization contract, with both GCC and Clang on Linux;
- **Renderer sanitizers:** the portable scheduling, generated-frame-plan, presentation-policy, spatial-scaling policy, profile, transition, and colour-math boundaries under AddressSanitizer and UndefinedBehaviorSanitizer; and
- **Trace producer and protected inputs:** safe capture staging, sanitization, metadata, checksum, containment, rollback, and concurrent no-clobber behavior, plus an index-level rejection gate for licensed DLL/model content and disguised binary, shader, dump, or archive payloads, on Linux and macOS.

The Renderer suite also exercises the standalone `mako-launch` contract: deterministic implicit-layer selection, loader activation, LSFG-VK conflict guards, the Gamescope WSI/HDR process-start boundary, strict fail-closed launcher settings, Zink/ALSA environment application, advanced environment forwarding, argument quoting, input validation, and child exit-status propagation. The portable `run-mako-gym.sh` contract separately proves optional absence, required fail-closed behavior, exact argument forwarding, version mismatch rejection, and runner validation without needing the private checkout. The packaged hardware smoke test proves instance/device insertion with `vulkaninfo` and, when a graphical compositor and `vkcube` are available, covers finite swapchain creation and presentation too. Presentation changes must preserve the invariants and expanded matrices in [WSI isolation](engine/docs/WSI-ISOLATION.md), [HDR pipeline architecture](engine/docs/HDR-PIPELINE.md), and [spatial scaling architecture](engine/docs/SCALING.md).

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

The portable Renderer suite verifies scaling configuration, fixed and variable surface policy including echoed-presentation suppression, opaque single-array-layer/unprotected/non-shared-present shape policy, debounced one-shot live-recreation state for scaling and LSFG context inputs, and embedded shader freshness. These checks do not prove that a driver accepts the required transfer, blit, sampled-image, and storage-image operations, that an application handles the injected out-of-date result, that the application presents on the required queue, that batch semaphores remain untouched on rejection, or that the reconstructed result is visually correct.

Every spatial-scaling change needs the proportionate real-Vulkan matrix in [Spatial scaling architecture](engine/docs/SCALING.md). At minimum, exercise a fresh MAKO scaling-only process that negotiates no external-memory/semaphore/timeline interop, does not load `Lossless.dll`, and allocates no frame-generation resources; exercise LS1 Quality and Performance scaling-only while proving they read the DLL without creating LSFG interop; prove that enabling Frame Generation then requires process restart rather than swapchain recreation; and test every method with Fixed and Adaptive paths. Cover fixed Gamescope with HDR exposure disabled, variable desktop surfaces including a compositor-echoed presentation extent, standard and high-precision SDR for every method including LS1's RGBA8 model-boundary conversion, every LS1 sharpness variant, missing DLL/translator/resources, ordinary queue 0 from an application-created graphics-and-compute family that presents to the surface, supported and rejected swapchain shapes, natural swapchain recreation, live enable/disable/method/factor/sharpness/Flow Scale/Lighter FG Model changes, live Fixed/Adaptive changes that grow generated-frame capacity, rapid-edit debounce and coalescing, one successful lower present before the out-of-date signal, replacement-context flow/model/capacity evidence, and fail-closed HDR/unsupported-format behavior. Flow Scale, model, and capacity edits without retained FG resources must remain deferred. An echoed variable extent must stop scaling rather than be multiplied again. A present batch containing multiple swapchains and any active scaler must return before consuming application wait semaphores. Combined frame-generation runs still require separate FP32 and FP16 evidence. Record requested/active method plus source and presentation dimensions from Renderer policy logs, preserve Vulkan-loader evidence that the intended development layer was active, and compare image quality and GPU cost against a native-resolution reference.

MAKO-Gym's 66-case procedural visual matrix measures LSFG, the production spatial scalers, and their real exported-image/timeline-semaphore handoff against vector-rendered references. It provides deterministic pixel evidence, including FP32/FP16, every spatial method, every scene, and representative Flow Scale/model/factor/sharpness/timestamp interactions. It does not measure WSI presentation, subjective game quality, latency, or GPU cost. Likewise, a successful `vulkaninfo` or one-frame `vkcube` smoke test proves only the loader or basic presentation boundary. A release that changes spatial reconstruction must retain visual, performance, lifecycle, and relevant game/runtime evidence rather than treating any one gate as a substitute.

### MAKO-Gym advanced native-Vulkan smoke matrix

The private sibling [MAKO-Gym](https://github.com/eugeniosegala/MAKO-Gym) repository owns the real-hardware scenario inventory, Vulkan/Gamescope runner, assertions, and run artifacts. MAKO retains only `engine/scripts/run-mako-gym.sh`, its contract version, and a hardware-independent bridge test. This keeps licensed-model QA and expanding hardware scenarios outside the product repository without allowing the release gate to skip them silently.

#### Select the smallest sufficient Gym scope

Do not run all 130 default hardware cases after every edit. Run the portable MAKO and Gym contracts first, then select the smallest hardware suite and regex that exercise the changed boundary. Omitting `--filter` runs the complete selected suite; running all three complete suites is reserved for the SteamOS release gate, broad changes spanning scheduling, scaling and backend pixels, or an explicit final validation request. Two additional Gamescope WSI order rows remain explicit because they require reviewed positive and deliberately reversed two-layer launchers.

| Change boundary | Development hardware selection |
| --- | --- |
| Documentation, website, Decky-only UI/backend, or portable schema work | No Gym run unless the change alters a Renderer-facing contract; use the owning portable tests. |
| Fixed/Adaptive configuration, resource construction, option combinations, scaler selection or fallback | Feature suite with the narrowest matching labels. |
| LSFG output, spatial reconstruction, shaders, colour conversion, precision, Flow Scale, model selection or scaling-to-FG handoff | Quality suite filtered by affected pipeline, method, scene or parameter; add relevant feature rows when lifecycle or presentation also changed. |
| Cadence transitions, Steady Adaptive, Dynamic Cadence Recovery, stalls, scheduler recovery or swapchain lifecycle | Recovery suite filtered by the affected recovery family; add relevant feature rows when configuration or construction also changed. |
| Shared Vulkan synchronization, presentation, backend resource ownership or cross-cutting Renderer changes | Run every affected suite completely; run all three when the boundary genuinely spans them. |
| Release candidate | The required SteamOS workflow runs all 44 feature, 66 quality and 20 default recovery cases against the exact package. |

Examples:

```bash
just test-engine-gym-feature --filter '^(fixed-|adaptive-)'
just test-engine-gym-quality --filter '^spatial-mako-'
just test-engine-gym-quality --filter '^combined-ls1-performance-.*traffic'
just test-engine-gym-recovery --filter '(stall|cadence-drop)$'
just test-engine-gym-recovery --filter 'recreate$'
just test-engine-gym-recovery --filter 'near-target|four-x'
just test-engine-gym-recovery --filter 'scaling-live'
```

Every filtered result is evidence only for its selected rows. Before merging a production change, widen from the iteration filter to the complete affected suite when the change touches a shared owner used by multiple rows. Portable-only changes and isolated scenario/assertion edits do not acquire an unrelated full-Gym requirement.

Use the bridge from a workspace where `MAKO/` and `MAKO-Gym/` are siblings:

```bash
just test-engine-gym-feature --list
just test-engine-gym-feature --filter '^ls1-quality-'
just test-engine-gym-feature
```

Set `MAKO_GYM_REPO` or pass `--gym-repo <path>` for a non-sibling checkout. Local use is optional: when Gym is absent, the bridge prints an explicit skip and exits successfully. Automation that requires GPU evidence passes `--require`; absence, an incompatible `GYM_CONTRACT_VERSION`, or a missing runner is then fatal. `just test-engine-gym` remains the default feature-suite command for compatibility, `just test-engine-gym-feature` makes that selection explicit, and `just test-engine-gym-required` applies required-checkout behavior to the selected suite.

MAKO-Gym's current 44-case manifest covers native passthrough, every MAKO and LS1 scaling model/variant, intentional fallback, Fixed, Adaptive, Steady/Smooth, Dynamic Cadence Recovery configuration, Ultra Performance, FP32/FP16 configuration, Flow Scale, Lighter FG Model, and representative scaling-plus-frame-generation combinations. Its steady finite-cube rows prove configuration and construction, not a recovery transition. Its own portable gate validates the declarative inventory and assertion contract without a GPU or DLL. The complete lane table, duration classes, pass criteria, outputs, and limitations are authoritative in `MAKO-Gym/docs/VULKAN-FEATURE-MATRIX.md`.

Select the scripted recovery matrix through the bridge:

```bash
./engine/scripts/run-mako-gym.sh --suite recovery --list
./engine/scripts/run-mako-gym.sh --suite recovery --filter 'recreate$'
./engine/scripts/run-mako-gym.sh --suite recovery
```

That manifest has 20 default rows plus two explicit Gamescope WSI order rows. Its small native-Vulkan moving workload controls cadence rise, false-probe rejection, isolated hitch, hard stall, sustained cadence drop, game-owned swapchain replacement, steady and noisy near-target cadence, ordered 4× health, 4× target-rate control, and live scaling lifecycle changes. It covers Fixed, Fractional Adaptive, Steady Adaptive, Ultra Performance, MAKO scaling, LS1 scaling, method/factor switching, disable/re-enable, compatible re-query activation, and fixed-extent rejection. Every default row enforces its source-cadence validity floor, exact ordered diagnostics, and the expectation-specific recovery, cadence, lifecycle, or delivery threshold. Its authoritative phase and evidence contract is in `MAKO-Gym/docs/RUNTIME-RECOVERY-MATRIX.md`.

The replacement rows use Vulkan's real `oldSwapchain` handoff rather than destroying first. They require the new Renderer context to be classified as a replacement while both contexts are live, the old context to retire afterward, Adaptive's one-second replacement settling guard to remain distinct from the three-second cold-start guard, and native plus LS1 Adaptive generation to resume within a three-second post-replacement source phase.

Select the separate procedural visual matrix through the same bridge:

```bash
./engine/scripts/run-mako-gym.sh --suite quality --list
./engine/scripts/run-mako-gym.sh --suite quality --filter '^combined-.*traffic' \
  --cli "$PWD/engine/build/mako-cli/mako-cli"
./engine/scripts/run-mako-gym.sh --suite quality \
  --cli "$PWD/engine/build/mako-cli/mako-cli"
```

That manifest contains 18 frame-generation, 28 spatial-scaling, and 20 combined cases across five procedural game-like scenes. Gym validates every row and PPM artifact, records one complete log per case, and sanitizes the final summary. Its authoritative catalog and evidence boundary are in `MAKO-Gym/docs/AMD-QUALITY-REGRESSION.md`.

These remain bounded hardware contracts, not a release-compatibility verdict. The finite-cube pass proves real swapchain construction, selected scaler/context activation, actual Fixed/Adaptive delivery diagnostics, and clean finite presentation. The recovery pass additionally proves ordered response to controlled cadence, stall, and swapchain events. Neither proves subjective spatial quality, actual selected LSFG shader precision from logs alone, DXVK or VKD3D-Proton game behavior, arbitrary live configuration mutation in a title, generated-image starvation, device-loss recovery, 32-bit Vulkan presentation, Flatpak runtime behavior, Steam Deck/RDNA2, another driver, HDR scaling, or compositor scanout timing. Retain those rows as not tested until their owning hardware, capture, lifecycle, and game matrices run.

Current measured development evidence is limited to one RADV NAVI33 host: the spatial shader reported 56→40 VGPR, 18→24 subgroups per SIMD, 365→320 instructions, 264→229 inverse throughput, and zero spills. A live direct-Wayland run reconstructed 500×500 to 750×750 and then safely suppressed an echoed 750×750 recreation instead of compounding it; bounded fixed-extent Gamescope/X11 runs activated 332×332-to-500×500 scaling before both Fixed and Adaptive frame generation. Record this as NAVI33 evidence only, not Steam Deck/RDNA2 validation.

## SteamOS hardware release gate

Before publishing a release candidate, run the `SteamOS hardware validation` workflow for that commit. On the dedicated SteamOS/AMD test machine, the recommended entry point creates a verified one-job runner, dispatches the selected clean branch, waits for the result, and removes the runner registration, credentials, checkout, staging, generated packages, and local runner logs:

```bash
./scripts/run-steamos-hardware-validation.sh
```

The launcher refuses a dirty or remote-divergent branch, an existing queued hardware run, a non-SteamOS/non-AMD host, `/tmp`-backed runner storage, or less than 30 GiB of free work space. It downloads the latest official Linux x64 Actions runner and verifies the archive against the SHA-256 published in the same official release. The workflow still requires the normal 64-bit and 32-bit Renderer/Flatpak build prerequisites, `vulkaninfo`, `vkcube`, Gamescope, access to the licensed `Lossless.dll` used only for validation, and an authorized MAKO-Gym checkout discoverable through `MAKO_GYM_REPO` or the sibling default. Gym's own contract gate must pass before the runner is registered.

The workflow:

1. runs MAKO-Gym's complete 66-case procedural render-quality matrix with the source-built CLI, including FP32/FP16 LSFG, every spatial method, and combined handoffs;
2. builds and tests the native 64-bit and 32-bit Renderer payloads;
3. builds and installs each supported Flatpak runtime extension for verification;
4. builds the complete Decky ZIP from the same source tree;
5. extracts the packaged Renderer and proves that the real Vulkan loader activates `VK_LAYER_MAKO_render` on the runner's AMD GPU;
6. passes that exact extracted Renderer launcher to MAKO-Gym and requires all 44 native-Vulkan feature scenarios to pass;
7. builds Gym's test-only native-Vulkan workload and requires all 20 default scripted recovery, cadence, and lifecycle scenarios to pass against the same extracted launcher; and
8. retains the complete verified Decky ZIP, sanitized environment evidence, procedural GPU comparison images, and MAKO-Gym feature/recovery logs and summaries for 14 days under the tested commit.

Pass `--deploy-to-decky` only on a dedicated device with an existing MAKO Decky development installation. That option safely synchronizes the already-verified ZIP into the existing plugin, asks Decky Loader to reload it, and invokes MAKO Decky's normal installer against that exact bundled Renderer. The production installer owns host libraries, manifests, wrappers, engine state, diagnostics, and refreshes of already-installed Flatpak runtime branches; no component is rebuilt or installed through a second CI-only implementation. It intentionally does not run by default because it changes the installed test device.

The launcher reuses the repository's existing ignored `engine/build/cache` tree for compiler, Flatpak, native SDK, pnpm, and Actions tool caches instead of creating a second large cache under the user's home directory; the location can be changed with `MAKO_HARDWARE_CI_ROOT`. Inspect the retained data without deleting anything with `./scripts/prune-hardware-ci-cache.sh`, or add `--confirm` when the space is more valuable than the next build's warm cache. The workflow itself removes large staging and generated outputs even when a manually configured persistent runner is used.

This gate is a fresh MAKO checkout on the real SteamOS/AMD host, not a fresh virtual SteamOS installation. The separately versioned private Gym checkout is a required host QA dependency and its contract version is checked before execution. The gate validates the physical GPU, driver, Vulkan loader, Gym matrix, Flatpak, packaging, and optional Decky deployment boundaries. The GitHub-hosted pull-request jobs independently validate a clean portable Ubuntu environment; neither gate replaces the other.

## Runtime compatibility matrix

The hardware workflow establishes packaging, loader activation, and deterministic image-quality boundaries. Game presentation still requires the release-candidate matrix in [Adaptive validation](engine/docs/ADAPTIVE-VALIDATION.md): DXVK and VKD3D-Proton under Gamescope, overlay and focus transitions, hitches, swapchain recreation, and the supported desktop GPU paths. Spatial-scaling changes additionally require the surface, queue, batch, format, startup, and quality matrix in [Spatial scaling architecture](engine/docs/SCALING.md). Record unavailable rows as **not tested** rather than treating the automated smoke test as equivalent coverage.

## Versioned game traces

Use `scripts/capture-trace.sh` after a completed game session to archive comparative evidence into a sibling **private** `MAKO-Traces` checkout. Read the [trace extractor guide](TRACES.md) first. Keep the private repository outside the MAKO worktree so licensed paths, large runtime logs, and subjective notes never become product artifacts; it is not required for normal MAKO builds, tests, packaging, installation, or releases.

The public producer contract test is private-data-free and runs in pull-request CI:

```bash
./scripts/test-capture-trace.sh
```

It characterizes safe component normalization and containment, exact offset-aware timestamp ordering, common credential redaction, stable metadata identity fields, canonical checksums, aligned notes headings, validator rollback, concurrent no-clobber publication, and the single-path success output without requiring a MAKO-Traces checkout containing real evidence.

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

Close the game first so buffered records are complete. The development wrapper keeps the latest three diagnostics-enabled sessions as `present-diagnostics.log`, `.1`, and `.2`; the extractor defaults to the latest, while `--diagnostics` can select one exact rotated raw session before a fourth launch replaces it. Never archive a combined `mako-diagnostics --session all` report as one trace. The producer invokes the sibling validator when it is available; review the resulting private diff and run `./scripts/check.sh` from MAKO-Traces before committing it (`just check` is the equivalent convenience alias). A stored trace supports repeatable comparison; a single run does not by itself prove a performance or image-quality regression.
