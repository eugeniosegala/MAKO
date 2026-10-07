# Testing MAKO

MAKO separates deterministic product checks from real-hardware evidence. Portable tests are required for every change, but they do not prove Vulkan presentation, image quality, Gamescope behavior, or game compatibility.

## Ownership

| Boundary | Owner |
| --- | --- |
| Product code, portable tests, production-path CLI tools, package contracts, and Gym bridge | MAKO |
| Licensed-model execution, declarative hardware scenarios, Vulkan/Gamescope orchestration, runtime assertions, performance budgets, artifact validation, and sanitized local evidence | Private MAKO Gym |
| Comparative real-game sessions, schemas, checksums, and append-only evidence | Private MAKO Traces |

Portable MAKO tests must not require MAKO Gym, `Lossless.dll`, an AMD GPU, Gamescope, or a compositor. MAKO Gym consumes MAKO's public executable and diagnostic boundaries; it must not copy production C++, shaders, thresholds, or configuration owners.

## Validation cycles

| Cycle | Proves | Does not prove |
| --- | --- | --- |
| Portable change gates | Deterministic Renderer, Decky, schema, package-policy, sanitizer, and producer behavior | Real Vulkan presentation, AMD image quality, or installation |
| Direct device iteration | A focused change works on the current development installation | Archive layout, clean-checkout reproducibility, 32-bit, or Flatpak behavior unless selected |
| Complete tester ZIP | Local source packages as one self-contained build | A clean pushed commit, real game behavior, or public asset |
| Local release-candidate check | The already-built complete ZIP matches the clean pushed commit and contains verified native and Flatpak payloads | GPU behavior, installation, or the published download |
| Manual release-candidate matrix | Selected games survive relevant presentation, focus, overlay, hitch, and recreation scenarios | Untested games and hardware |
| Published-package check | The exact GitHub host, Flatpak, and Arch assets match their recorded checksums and install through their user-facing paths | Universal compatibility |

Use [MAKO Decky packaging](plugin/docs/PACKAGING.md) for development and tester builds and [How to release MAKO](HOW_TO_RELEASE.md) for release validation and publication.

## Portable gates

The `Tests` workflow runs on every pull request and push to `main`:

- MAKO Decky backend/frontend tests, type checking, coverage, generated-contract freshness, localization, production bundling, and package-license contracts;
- MAKO Renderer CTest with GCC and Clang using the [native package Vulkan-Headers revision](engine/vulkan-headers-revision.txt) and its required-header check, including Qt, localization, synthetic model inspection, launch policy, generated-SPIR-V freshness, shared Flatpak-header module freshness/mutation checks, and the pinned vkBasalt release/generated-module contract;
- the Arch package release-pin, alternative-package identity, non-mutating lifecycle-hook, automated release-sync, and exact-release build-wrapper contracts;
- portable Renderer policy tests under ASan and UBSan;
- protected-input, trace-producer, and local release-candidate contracts on their supported hosts; and
- Markdown formatting.

The owning component tests remain authoritative for their detailed invariants.

Native Remote Play's portable Decky tests use temporary Steam trees, synthetic ELF headers, and the host's native `/usr/bin/env` as an environment-recording executable through the real generated `mako-run`. They cover install/removal rollback, foreign-client preservation after injected failures, staged ownership failure, system-interpreter preflight, alias/relative/deleted client detection, inherited launch leases, checksum/symlink/special-file conflicts, payload upgrades, lost-state recovery, interrupted removal, Renderer uninstall guards, complete managed-chain passthrough, AppID-free profile selection, AC/battery reloads, and stale frontend polling. Real launcher environment tests cover shaders, spatial scaling, frame generation, combined chains, custom shaders, Gamescope WSI, MangoHud, ALSA, Steam Deck mode, HDR-exposure suppression, and local-only Zink removal; those tests do not load graphics layers or licensed models. They do not prove real stream interpolation, video-decoder behavior, host/client cadence, Gamescope presentation, quality, or input latency; qualify those with native Steam Remote Play on the receiving hardware. Standalone Steam Link, Flatpak Steam, and browser streaming are outside this integration.

The shared Remote Play command tests add standalone install/restore, real `mako-launch` shader activation across sidecar edits, configuration-path preservation, version-1 migration, and stale Decky-launcher refusal. Qt's backend contract exercises the actual asynchronous command against temporary Steam files and preserves an existing AC/battery profile through removal. The standalone installer test guards restoration failures and shared HOME state with alternate XDG paths before cleanup. `remote-play-bindings` and Decky's generated gate check byte-identical shared modules; the cross-component contract checks response fields, shader identity, launch-input consumption, package registration, and read-only generation failure. These are process/file/environment tests without a real stream or graphics-layer load.

When testing the optional forced-FP16 native build, configure `BUILD_TESTING=ON` and `MAKO_EXPERIMENTAL_LSFG_FP16=ON` with the pinned sources described in [Building from source](engine/docs/BUILDING-FROM-SOURCE.md#experimental-forced-lsfg-fp16). Its additional `backend-experimental-lsfg-fp16` test uses original synthetic SPIR-V to check half arithmetic, preserved bindings, FP32 sampling and bit-pattern calculations through memory stores/loads, byte-identical FP32 image preparation/reconstruction, native FP16 priority, FP16-off isolation, selected modes, cache replacement, and validation failures. Run ordinary CTest as well to retain strict-build coverage. GPU quality and synchronization evidence must distinguish forced conversion from native FP16, preserve failing image-quality results, and state which actual DLL branches were available. Add warmed throughput evidence when precision protection changes the amount of half arithmetic; stage counts alone do not establish performance.

Default and Vulkan-layer Renderer builds and portable policy CTest check that every production `mako-render/src/**/*.cpp` file is registered with its CMake target. This catches a newly split file even when an existing build tree does not reconfigure. The complete local release-shaped package build then verifies the host archive, Flatpak bundles, Arch package, direct ELF dependencies, and checksums; a successful direct 64-bit build alone does not establish those package contracts.

Launcher exclusion freshness runs in Renderer CTest and Decky's generated-contract gate; `just check-launcher-exclusions` runs it directly. The shared registry is `engine/mako-common/launcher_exclusions.json`; regenerate both component bindings with `just generate-launcher-exclusions`. Portable tests validate registry entries, reject stale or missing bindings without rewriting them, and cover exclusion and child activation for the registered executables. New exclusions also need focused real-launcher evidence; moving the unchanged list does not establish new compatibility evidence.

Decky's backend suite includes `plugin/tests/test_flatpak_override_integration.py`, which uses the real Linux `flatpak override` command in a temporary `FLATPAK_USER_DIR`. It checks preparation, repeated app-list refreshes, removal, and preservation of unrelated settings for Heroic, Lutris, and Dolphin. Application/runtime inventory is simulated; no installed apps, runtime downloads, licensed inputs, or GPU are required. The test skips when Linux or Flatpak is unavailable locally; the Decky CI job installs Flatpak before running it. Actual sandbox launches and rendering remain MAKO Gym evidence.

Run `just test` for protected-input and local release-candidate contracts, Renderer CTest, Decky backend/frontend tests, and the trace producer. Add the checks below for the complete local portable gate:

```bash
just check-markdown-format
just test
pnpm --dir plugin run test:frontend:typecheck
pnpm --dir plugin run test:frontend:coverage
MAKO_LOCAL_RELEASE_BUILD=1 pnpm --dir plugin run build
just test-engine-sanitized
```

The local frontend build uses the prepared release-note version while `plugin/package.json` still carries the published version. The local package script and CI set the same flag; publication later aligns the versioned manifests and validates the exact release heading.

The focused Renderer policy script compiles Vulkan-facing policies and the optional Gamescope surface adapter against a local client fixture, so it needs Vulkan, X11, and XCB headers even though it does not need a Vulkan device, compositor, or Wayland development package. The full Renderer suite additionally needs the Vulkan loader:

```bash
cmake -S engine -B engine/build/local -DBUILD_TESTING=ON -DMAKO_BUILD_UI=OFF
cmake --build engine/build/local
ctest --test-dir engine/build/local --output-on-failure
```

Use `-DMAKO_BUILD_UI=ON` when Qt 6 Base and Declarative development packages are installed. Shader changes must regenerate their adjacent embedded payloads with the owning generator; never edit generated SPIR-V arrays or hashes directly.

AC/battery profile coverage includes native parsing/serialization, system-supply detection, selection by explicit profile, power-only watcher changes, transient-read retention, and Qt editor persistence in `watched-configuration` and `ui-backend-contract`. `power-profile-update` joins native serialization and every matching route to the production live-update planner, including same-name source switches, inactive edits, mixed live/private/restart requests, pending extents, reversion, and missing startup resources. Decky's power-profile, game-profile, RPC, editor-session, and save-queue tests cover independently deployed contracts, every schema-owned native field in both power tables, shared live shader edits, lifecycle preservation, layer discovery, and delayed edits across mode changes. `contentPowerReload.test.tsx` exercises the panel with its real configuration, session, and writer hooks to verify queued saves before manual selection, in-flight saves before automatic power changes, and failed-write reconciliation. Frontend build metadata uses test-only virtual fixtures so these checks run before any build in a clean checkout. These temporary supply fixtures do not establish actual unplug/replug behavior, Flatpak sysfs visibility, or game transition behavior; qualify those separately on hardware and retain the existing restart-boundary limits.

Adaptive display-refresh matching is covered by `profile-update`, native configuration round trips, Qt backend persistence, Decky power-profile and runtime-status contracts, and frontend controls. Portable tests cover disabled defaults, display-rate changes, the Renderer’s 10–1000 FPS bounds, fallback restoration, Fixed isolation, target-dependent caps, private-resource growth into Adaptive, newer live edits during preparation, and older runtime records. Real handheld/TV/monitor hotplug and Steam refresh changes still require game evidence under Gamescope; non-Gamescope sessions intentionally retain the saved fallback.

Custom shader UI coverage includes native FX discovery, registration, and deletion in `standalone-launch-configuration`, Qt file-URL import, deletion, and persistence in `ui-backend-contract`, and Decky game-profile, schema/RPC, catalog loading, and effect-selector tests. Fixtures cover quoted paths, comments, duplicate assignments, alias collisions, case-sensitive IDs, legacy chain preservation, mixed ordering, clearing without removing definitions, profile copies, and invalid imports. Both UIs' deletion tests cover selected-only removal, missing definitions, bundled/reserved protection, source-file preservation, shared power settings, rollback after a sidecar failure, catalog refresh, and saving pending edits before deletion. Qt also rejects deletion after a failed pending save and retries that edit; Decky locks effect selection while deletion runs. These use synthetic local files and do not prove shader compilation, pixel output, Flatpak filesystem access, or real game compatibility.

The maintained vkBasalt fork owns portable live-selection, configuration-snapshot, sampler-free descriptor, and ReShade compilation tests. They cover custom activation/removal/reordering, same-alias path and option changes, malformed/missing shaders, missing includes and textures, invalid texture bytes, and recovery. Its dependency release workflow runs those tests before building the dual-architecture archive. Runtime qualification still requires live graph replacement and failed-edit recovery in a Vulkan workload, with separate evidence for Frame Generation/scaling combinations, Gamescope, 32-bit, Flatpak, and game compatibility.

Optional Lossless Scaling coverage includes file and environment profile parsing in `watched-configuration`, Qt missing-path detection and model-free setting persistence in `ui-backend-contract`, and Decky discovery, RPC, panel access, warning visibility, and availability-refresh tests. Synthetic files cover removal, configured-path precedence, directories, stale replies, and unknown discovery failures without loading licensed models. These tests do not prove actual MAKO Scaler or shader rendering, Gamescope, or Flatpak launches without Lossless Scaling; qualify those separately on hardware.

Frame Generation diagnostic profiling adds `cli-profile-timing-contract` coverage for queue timestamp rollover, unsupported timestamp properties and summary statistics, plus CLI parsing/default/range checks in `cli-precision-contract`. The shared query owner remains used by the spatial profiler. MAKO Gym's opt-in `scripts/renderer_profile.py` validates effective model/precision, every raw sample, containment of CPU boundaries, GPU span consistency and recomputed summaries. Run a filtered hardware profile before collecting canonical FG configurations; retain ordinary capacity evidence separately because query readback changes iteration spacing. These checks do not qualify generated-frame display pacing or game image quality.

## Selecting MAKO Gym coverage

MAKO Gym is an optional sibling checkout for targeted hardware work. The bridge skips clearly when Gym is absent unless `--require` is used; required mode also rejects a missing runner or incompatible `GYM_CONTRACT_VERSION`.

Gym contract version 19 adds the `pacing` suite for explicit Gamescope VRR feedback, pacing-owner transitions, and independent compositor-completion evidence while retaining defined-content benchmark inputs, performance baseline schema 4, and explicit CLI precision selection. Use a matching Gym checkout; older runners must not silently omit VRR pacing coverage or reinterpret an FP32 workload through the CLI default.

Before any hardware run, validate Gym's portable contracts with `(cd ../MAKO-Gym && ./scripts/check.sh)` or `just check` from its checkout.

The bridge exposes one selection pattern. `--list-suites` discovers suites, `--all-suites --validate` validates their inventories without hardware, `--suite NAME --list` discovers rows, and `--filter REGEX` runs a subset. Omitting `--filter` runs the complete selected suite:

```bash
./engine/scripts/run-mako-gym.sh --list-suites
./engine/scripts/run-mako-gym.sh --require --all-suites --validate
./engine/scripts/run-mako-gym.sh --suite recovery --list
./engine/scripts/run-mako-gym.sh --suite recovery
```

Run the smallest suite and filter that can observe a change. When claiming complete hardware qualification for a selected boundary, include Gym's bounded `constraints` suite with the relevant candidate CLI and launcher; Gym's compact hardware runner applies its own case mapping. A filtered pass is evidence only for its selected rows. Widen to the complete affected suite when a shared production owner changes. A release does not automatically select every suite; run all suites only for a genuinely cross-cutting change or an explicit maintainer request.

Retained evidence may be reused when the exact Renderer/package identity and source commit, host and driver, Gym commit, configuration, and relevant rows match the candidate. Record the prior run identifier. A code, package, driver, scenario, or assertion change invalidates the affected evidence.

| Change boundary | Start with |
| --- | --- |
| Documentation, website, Decky-only UI/backend, or portable schema | Owning portable tests; no Gym unless a Renderer-facing contract changed |
| Fixed/Adaptive configuration, construction, option combinations, scaler selection, or fallback | `vulkan` feature matrix with matching labels |
| LSFG/spatial pixels, shaders, colour, precision, Flow Scale, model selection, or combined handoff | `quality`; add `vulkan` for lifecycle changes |
| LSFG dispatch cost, model/precision/Flow throughput, or generated capacity | `performance` |
| Spatial graph, copy, factor, resolution, or combined GPU cost | `spatial-performance` |
| Present/frame tails, process CPU/RSS, Vulkan allocations, or “feels heavy” reports | `runtime-overhead` |
| Barriers, image transitions, command recording, or exported-resource synchronization | `sync-validation` |
| Initialization or unexplained pixel instability | `repeatability` |
| Fixed/Steady/Fractional cadence, Gamescope VRR feedback, pacing-owner transitions, or completion timing | `pacing`; add `recovery` when recovery, stalls, or swapchain lifetime changed |
| Recovery, stalls, acquire pressure, or swapchain lifetime | `recovery`; add `vulkan` if construction changed |
| External full-cover overlay pause/throttle or workload-proven source-return recovery | `external-recovery` |
| Native compositor, WSI, cross-layer live transition, or resolution lifecycle | `gamescope-e2e` |
| Native direct scaling/Frame Generation or Steam Desktop Proton Frame Generation and extent-override fallback | `direct-desktop-e2e` |
| Repeated private-resource memory plateau or sustained thermal/performance health | `sustained-health` |
| D3D11/DXVK or D3D12/VKD3D-Proton translation | `proton-e2e` |
| Proton-family/version behavior | `proton-compatibility` |

`just test-engine-gym-*` provides equivalent shortcuts. Set `MAKO_GYM_REPO` or pass `--gym-repo <path>` when the checkout is not a sibling.

MAKO Gym's manifests and guides are authoritative for current rows, thresholds, prerequisites, artifacts, and claim limits. Portable validation proves inventory correctness only. A GPU suite does not prove subjective quality, input-to-photon latency, power, scanout timing, arbitrary games, other GPUs or drivers, 32-bit presentation, Flatpak behavior, or HDR unless a selected row covers that boundary. Record unselected and unavailable coverage as **not tested**.

Spatial changes must also follow the surface, extent, queue, format, startup, live-transition, synchronization, and quality matrix in [Spatial scaling architecture](engine/docs/SCALING.md). Scheduler and presentation changes must follow [Adaptive validation](engine/docs/ADAPTIVE-VALIDATION.md). A successful `vulkaninfo` or finite `vkcube` run proves only its narrow loader or presentation boundary.

## Local release-candidate check

Build one complete portable MAKO Decky ZIP from the clean pushed candidate and pass its printed path to `scripts/check-release-candidate.sh`. The check reuses the ZIP and verifies the package contract, source commit, portable native builder identity, 64-bit/32-bit host layers, and every supported Flatpak bundle. It does not create a GitHub runner, rebuild, install, or run MAKO Gym. [How to release MAKO](HOW_TO_RELEASE.md#prepare-the-release) gives the commands.

Run MAKO Gym separately when a changed boundary needs targeted hardware evidence. Use the table above to choose the suite and retain the package identity, source commit, host/driver, Gym commit, configuration, and results. Direct-desktop coverage must start from a graphical session outside Gamescope. Record unavailable or unselected rows as not tested; local game tests and the final published-package installation check remain separate.

## Real-game traces

After a completed game session, `scripts/capture-trace.sh` can archive sanitized comparative evidence into a sibling private MAKO Traces checkout. [The trace extractor guide](TRACES.md) owns setup, commands, privacy, checksums, and contract changes. Run the producer test without private data using:

```bash
./scripts/test-capture-trace.sh
```

A stored trace supports comparison; one playthrough does not prove a performance or image-quality regression.
