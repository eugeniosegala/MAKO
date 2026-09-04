# MAKO repository guide for coding agents

This is the only repository-wide instruction source for coding agents. Component READMEs, guides, and scripts add detail without overriding it. Do not create nested `AGENTS.md` copies unless a genuinely scoped exception is required. When a workflow changes, update its owning script or guide first, then keep this index aligned.

## Engineering principles

Before changing code, read the applicable repository, component, and boundary guides to find the canonical owner, generated outputs, compatibility requirements, and validation path. MAKO runs on resource-constrained devices, so protect CPU/GPU cost, memory, synchronization, startup, and hot paths. Extend existing owners and abstractions instead of creating parallel implementations. Keep each change small and align its code, tests, schemas or generators, evidence, and documentation.

Keep Markdown prose unwrapped: each paragraph and list item stays on one source line. Preserve line breaks only when they carry Markdown structure, such as blank paragraph boundaries, headings, tables, blockquotes or admonitions, and fenced or indented code.

## Product names and compatibility identifiers

Use these names in user-facing prose, UI text, workflow labels, package output, and new logs:

- **MAKO** for the overall project;
- **MAKO Renderer** for the Vulkan layer, backend, CLI, and optional Qt UI;
- **MAKO Decky** for the Decky Loader plugin;
- **MAKO Gym** for the private real-hardware QA companion; and
- **MAKO Traces** for the private comparative-evidence archive.

The repository and checkout slugs `MAKO-Gym` and `MAKO-Traces` are stable technical identifiers. Preserve them in paths, URLs, schema identifiers, commands, and other literal references while using MAKO Gym and MAKO Traces in prose.

The Decky manifest/listing name is the deliberate exception: `plugin/plugin.json` remains **MAKO - Frame Generation** as its immutable upgrade identity even though the component now includes spatial scaling. Call the component **MAKO Decky** elsewhere.

Renderer log records use the stable `MAKO Renderer:` prefix. MAKO Decky lifecycle logs name `MAKO Decky`. Do not introduce `mako:`, `mako-render:`, `Mako Renderer:`, or `Mako Decky:` as new public prefixes.

These stable compatibility identifiers are not branding errors:

- Decky's `Mako/` install slug, including `~/homebrew/plugins/Mako` and `~/homebrew/logs/Mako`, and its `MAKO - Frame Generation` listing name;
- commands and files such as `mako-run`, `mako-launch`, `mako-ui`, `mako-cli`, `mako-diagnostics`, `libmako-render.so`, and `mako-render/conf.toml`;
- source paths, namespaces, modules, application IDs, package-manager names, and existing environment variables such as `VK_LAYER_MAKO_render`, `ENABLE_MAKO`, and `DISABLE_MAKO`; and
- release tags such as `render-vX.Y.Z` and `plugin-vX.Y.Z`, plus already-published legacy archive names. Packaging scripts own current archive names.

The diagnostics helper deliberately recognizes historical lowercase renderer prefixes so reports from older installations remain readable. Preserve that input compatibility while requiring current source to emit the branded prefix. Do not rename any stable identifier without an explicit migration and backward-compatibility plan.

## Agent working flow

1. Inspect the current branch and worktree before acting, preserve unrelated changes, and read this guide plus the owning component README and boundary-specific document.
2. Trace the existing behavior to its canonical owner, generated outputs, compatibility obligations, and contract tests before editing. Extend that owner rather than creating a parallel path.
3. Make the smallest behavior-appropriate change. Update an owning schema, generator, migration ledger, focused test, and documentation in the same change whenever their contract moves; never patch a generated output independently.
4. Run the read-only freshness gates and the portable tests required by `TESTING.md`, then add package, sanitizer, Vulkan, hardware, or game-matrix evidence in proportion to the boundary affected.
5. Review the complete diff and final worktree state for duplication, stale documentation, accidental generated artifacts, and unrelated edits. Report what passed, what was not testable, and the exact branch/staged/committed/pushed state without claiming unavailable hardware evidence.

## Project map

| Area | Responsibility | Primary locations |
| --- | --- | --- |
| Repository root | Product overview, cross-component orchestration, shared diagnostics, compatibility cleanup, and releases | `README.md`, `justfile`, `TESTING.md`, `CLEANUPS.md`, `HOW_TO_RELEASE.md`, `scripts/` |
| MAKO Renderer layer | Vulkan interception, native spatial reconstruction, deterministic Adaptive scheduling, generated-frame planning, swapchain handling, presentation, recovery, Gamescope integration | `engine/mako-render/` |
| Renderer backend | Lossless Scaling resource extraction and private compute pipeline | `engine/mako-backend/` |
| Renderer common code | Configuration, Vulkan wrappers, device features, and image-quality utilities | `engine/mako-common/` |
| Renderer tools | Validation, benchmarking, debugging, and GPU quality regression | `engine/mako-cli/` |
| Renderer UI | Optional Qt Quick configuration application | `engine/mako-ui/` |
| Renderer distribution | Host manifests, Flatpak definitions, packaging, and build scripts | `engine/dist/`, `engine/scripts/` |
| MAKO Decky frontend | Decky React/TypeScript composition, profile editor/runtime-session state, and RPC consumers | `plugin/src/` |
| MAKO Decky backend | Installation, configuration orchestration, canonical profile sidecars, wrapper generation, Flatpak preparation, and lifecycle RPCs | `plugin/py_modules/mako_plugin/` |
| Decky packaging | Frontend generation, local ZIPs, direct development deployment, reload, and publishing | `plugin/scripts/` |
| Product website | Public MAKO product story, direct component downloads, social metadata, static GitHub Pages output, and Pages deployment | `website/`, `.github/workflows/pages.yml` |
| Cross-component contracts | Static agreement for configuration, RPCs, package layout, paths, runtime versions, and stable identities | `plugin/tests/test_*_contract.py`, `plugin/tests/test_decky_loader_import.py`, `plugin/tests/test_flatpak_runtime_detection.py`, `plugin/tests/frontend/*Contract*.test.ts` |
| Automation | Portable CI and dedicated SteamOS/AMD hardware validation | `.github/workflows/` |
| Private real-hardware QA | Licensed-model feature, quality, performance, synchronization, recovery, Gamescope/Desktop, Flatpak, and Proton matrices with local evidence | Sibling `MAKO-Gym` checkout; MAKO owns only `engine/scripts/run-mako-gym.sh` and its portable bridge contract |

Start with the root `README.md`, then read `engine/README.md` or `plugin/README.md`. The boundary owners are:

| Boundary | Authoritative guide |
| --- | --- |
| Renderer configuration | `engine/docs/CONFIGURATION.md` |
| Live and deferred setting lifetimes | `engine/docs/RUNTIME-TRANSITIONS.md` |
| Spatial scaling | `engine/docs/SCALING.md` |
| Adaptive scheduling and frame plans | `engine/docs/ADAPTIVE-VALIDATION.md` |
| HDR | `engine/docs/HDR-PIPELINE.md` |
| Implicit-layer and Gamescope presentation | `engine/docs/WSI-ISOLATION.md` |
| Optional Vulkan-layer exceptions | `engine/docs/LAYER-CHAINING.md` |
| Decky profile and compatibility UX | `plugin/docs/CONFIGURATION.md` |

## Directory and module placement

- Keep cross-component orchestration at the repository root, Renderer-only work under `engine/`, and Decky-only work under `plugin/`. Do not create root-level utilities, constants, generated files, or tests for component-owned behavior.
- Keep real-hardware scenarios, licensed-DLL orchestration, and run artifacts in the private sibling `MAKO-Gym` repository. This repository owns only the bridge and release-gate integration.
- Renderer interception, scaling, scheduling, presentation, and recovery live in `engine/mako-render/`; shared configuration and Vulkan/quality helpers in `engine/mako-common/`; backend inference and shaders in `engine/mako-backend/`; tools and UI in `engine/mako-cli/` and `engine/mako-ui/`; distribution files in `engine/dist/`; and reusable workflows in `engine/scripts/`. Keep focused tests with their owning CMake component.
- Decky's async RPC surface lives in `plugin/py_modules/mako_plugin/plugin.py`; transactions in `configuration.py`; profile metadata and sidecars in `profile_storage.py`; pure wrapper generation in `wrapper_generation.py`; package-root discovery in `package_paths.py`; stable payload and environment identities in `constants.py`; and response shapes in `types.py` or the owning service. Put build, deployment, validation, and packaging entry points in `plugin/scripts/`.
- Decky frontend bindings belong in `plugin/src/api/`, configuration contracts in `plugin/src/config/`, reusable state in `plugin/src/hooks/`, views in `plugin/src/components/`, translations in `plugin/src/i18n/`, and domain-neutral helpers in `plugin/src/utils/`.
- The public website lives under `website/`: `website/app/` owns page code and styling, `website/public/` owns browser assets, `website/vite.pages.config.ts` owns the Pages build, and `.github/workflows/pages.yml` is the only publishing entry point.
- Put Decky Python tests in `plugin/tests/`, frontend tests in `plugin/tests/frontend/`, and Renderer tests with their CMake component. Cross-component static contracts belong in `plugin/tests/`; independently deployed components must not import each other at runtime.
- When independently deployed components cannot share one runtime owner, keep native declarations and add a focused cross-component contract test instead of duplicating utility or serialization layers.

## Structured mappings and localization contracts

- Give every persisted or public RPC mapping a named Python `TypedDict` or generated schema type at its boundary. Builders and normalizers must populate required keys, copy mutable inputs, and distinguish optional presence from nullability. Use open dictionaries only for untrusted input, then validate and normalize them.
- `plugin/shared_config.py` owns Decky's cross-language schema and its generated Python/TypeScript bindings. `profile_storage.py` owns profile and sidecar keys; `wrapper_generation.py` accepts normalized typed input without persisting it; `constants.py` and `flatpak_service.py` own Flatpak shapes.
- Keep Decky's Python RPC names and `TypedDict` response shapes aligned with `plugin/src/api/makoApi.ts`. Update `plugin/tests/test_rpc_contract.py` and focused frontend response tests when the contract changes.
- Decky and Qt localization use separate catalogs. In `plugin/defaults/i18n/`, `template.json` owns English keys, order, fallbacks, types, and placeholders; `language_metadata.json` owns advertised languages; and `steam_language_map.json` owns Steam aliases. Keep `t()` calls static and exact. Use `npm run check:i18n` or `npm run generate:i18n` from `plugin/`; never edit generated `plugin/src/i18n/languages.json` directly.
- MAKO Renderer's independent Qt catalog is `engine/mako-ui/rsc/i18n/translations.json`. Preserve its English key order and value types and validate it through the Qt localization tests. Do not merge or copy catalogs between components.

## Authoritative workflow index

| Task | Read first | Executable entry points |
| --- | --- | --- |
| Understand validation coverage | `TESTING.md` | `justfile`, `.github/workflows/tests.yml` |
| Format Markdown or enable the commit hook | `AGENTS.md` | `just format-markdown`, `just check-markdown-format`, `just install-hooks` |
| Build Renderer from source | `engine/docs/BUILDING-FROM-SOURCE.md` | `engine/CMakeLists.txt`, `engine/scripts/build-steamos-dev.sh` |
| Run portable Renderer tests | `TESTING.md` | `engine/scripts/test-adaptive-scheduler.sh` |
| Change native spatial scaling | `engine/docs/SCALING.md`, `engine/docs/WSI-ISOLATION.md`, `engine/docs/HDR-PIPELINE.md` | `engine/mako-render/src/spatial_scaler.*`, `engine/mako-render/src/spatial_scaling_policy.hpp`, `engine/scripts/generate-spatial-scaling-spirv.py` |
| Change runtime configuration transitions or setting lifetimes | `engine/docs/RUNTIME-TRANSITIONS.md`, `engine/docs/CONFIGURATION.md` | `engine/mako-render/src/profile_update.hpp`, `engine/mako-render/src/instance.cpp`, `engine/mako-render/src/swapchain.cpp` |
| Change Adaptive scheduling or generated-frame plans | `engine/docs/ADAPTIVE-VALIDATION.md` | `engine/mako-render/src/adaptive_scheduler.*`, `engine/mako-render/src/generated_frame_plan.hpp`, `engine/mako-render/src/generated_frame_delivery.hpp` |
| Build host Renderer archives | `engine/docs/BUILDING-FROM-SOURCE.md` | `engine/scripts/package-local.sh` |
| Build Flatpak runtime extensions | `engine/docs/FLATPAK-GUIDE.md` | `engine/scripts/package-flatpaks.sh` |
| Test AMD image quality | `engine/docs/IMAGE-QUALITY-REGRESSION.md`, sibling `MAKO-Gym/docs/AMD-QUALITY-REGRESSION.md` | `engine/mako-cli/src/tools/quality.cpp`, `engine/scripts/run-mako-gym.sh --suite quality` |
| Test licensed-backend throughput or deterministic output | Sibling `MAKO-Gym/docs/RENDER-PERFORMANCE.md`, sibling `MAKO-Gym/docs/QUALITY-REPEATABILITY.md` | `engine/scripts/run-mako-gym.sh --suite performance`, `engine/scripts/run-mako-gym.sh --suite repeatability` |
| Test spatial GPU cost, live runtime overhead, or synchronization correctness | `TESTING.md`, sibling `MAKO-Gym/docs/` | `engine/scripts/run-mako-gym.sh --suite spatial-performance`, `engine/scripts/run-mako-gym.sh --suite runtime-overhead`, `engine/scripts/run-mako-gym.sh --suite sync-validation` |
| Test repeated private-resource memory or sustained temperature/performance health | `TESTING.md`, sibling `MAKO-Gym/docs/SUSTAINED-HEALTH.md` | `engine/scripts/run-mako-gym.sh --suite sustained-health` |
| Change HDR colour handling | `engine/docs/HDR-PIPELINE.md` | `engine/mako-render/src/color_pipeline.cpp`, `engine/mako-backend/src/mako.cpp` |
| Change WSI/layer isolation | `engine/docs/WSI-ISOLATION.md` | `engine/mako-render/src/presentation_policy.hpp`, `engine/scripts/mako-launch` |
| Add or change an optional Vulkan layer chain | `engine/docs/LAYER-CHAINING.md`, `engine/docs/WSI-ISOLATION.md` | Owning launcher, manifest, package, and focused loader/runtime validation |
| Run native Vulkan feature, recovery, compositor, Proton translation, or multi-runtime compatibility matrices | `TESTING.md`, `engine/docs/SCALING.md`, `engine/docs/ADAPTIVE-VALIDATION.md`, sibling `MAKO-Gym/docs/` | `engine/scripts/run-mako-gym.sh`; sibling Gym owns all manifests and hardware runners |
| Test native or Steam Runtime execution outside Gamescope | `TESTING.md`, sibling `MAKO-Gym/docs/DIRECT-DESKTOP-END-TO-END.md` | `engine/scripts/run-mako-gym.sh --suite direct-desktop-e2e` |
| Validate real SteamOS hardware | `TESTING.md` | `scripts/run-steamos-hardware-validation.sh`, `.github/workflows/steamos-hardware-validation.yml` |
| Exercise the game/runtime matrix | `engine/docs/ADAPTIVE-VALIDATION.md` | Manual DXVK, VKD3D-Proton, native Vulkan, Gamescope, and supported desktop scenarios |
| Change or exercise comparative game capture | `TRACES.md` | `scripts/capture-trace.sh`, `scripts/test-capture-trace.sh`, sibling private `MAKO-Traces` checkout |
| Build or package MAKO Decky | `plugin/docs/PACKAGING.md` | `plugin/package.json`, `plugin/scripts/package-local.sh` |
| Build or publish the product website | `website/README.md` | `website/package.json`, `.github/workflows/pages.yml` |
| Change Decky's shared configuration or runtime contract | `plugin/README.md`, `plugin/docs/CONFIGURATION.md` | `plugin/shared_config.py`, `plugin/scripts/generate_ts_schema.py`, `plugin/scripts/check_generated_config.py` |
| Change Decky's public RPC mappings | `plugin/README.md`, `TESTING.md` | `plugin/py_modules/mako_plugin/types.py`, owning service types, `plugin/src/api/makoApi.ts`, `plugin/tests/test_rpc_contract.py` |
| Change Decky or Qt translations | This file and the owning component README | `plugin/defaults/i18n/`, `plugin/scripts/i18n-contract.mjs`, `plugin/scripts/manage-i18n.mjs`, `engine/mako-ui/rsc/i18n/translations.json`, localization tests |
| Review Armada/native AArch64 behavior | `plugin/docs/ARMADA.md` | `plugin/py_modules/mako_plugin/host_environment.py`, host/wrapper/Flatpak boundary tests |
| Add or remove transitional compatibility | `CLEANUPS.md` | Owning migration/generator and its focused regression tests |
| Deploy/reload a local Decky install | `plugin/docs/PACKAGING.md` | `plugin/scripts/deploy-dev.sh`, `plugin/scripts/reload-decky-plugin.mjs` |
| Collect diagnostics | `COLLECT_DIAGNOSTICS.md` | `scripts/mako-diagnostics` |
| Publish both components | `HOW_TO_RELEASE.md` | `scripts/publish-release.sh` |
| Resume one component publish | `HOW_TO_RELEASE.md` | `engine/scripts/publish-package.sh`, `plugin/scripts/publish-package.sh` |

`just --list` exposes the common non-publishing entry points. Component scripts remain the source of truth for their arguments, prerequisites, cache behavior, and output paths.

## Test selection and evidence

Use `TESTING.md` to choose proportionate evidence. Renderer C++ changes need portable/full CTest; scheduling and presentation work also needs sanitizers and the applicable Adaptive matrix. Vulkan, scaling, synchronization, shader, quality, or resource-lifetime changes need the matching MAKO Gym evidence, including FP32 and FP16 for AMD paths. Decky changes need their backend or frontend gates, website changes need both documented builds, and package-boundary changes need package verification.

The SteamOS release gate must use a compatible MAKO Gym checkout, build and verify the complete dual-bitness/Flatpak package from a clean pushed candidate, smoke-test its packaged native Renderer, retain evidence, and record an explicit risk-based suite selection. That gate package is not the later public byte stream: publication rebuilds after release-owned metadata commits, and the published-asset install check is separate. Run every suite only for a genuinely cross-cutting change or explicit maintainer request. Armada remains fail-closed under `plugin/docs/ARMADA.md`; enabling it requires source-built AArch64 packages and real-hardware evidence, not an opaque binary, second detector, or global FEX mutation.

A skipped GPU test is not evidence, and the automated AMD scene does not replace the game/runtime matrix. State which hardware, driver, architecture, Flatpak runtime, and rows were not tested.

Read `TRACES.md` before archiving a completed session. MAKO owns trace extraction, sanitization, metadata, initial checksums, and producer tests; MAKO Traces owns stored evidence and archive validation. MAKO Gym owns hardware manifests, orchestration, assertions, and local evidence; MAKO owns only its bridge contract and release integration. Update both repositories when either cross-repository contract changes. Never copy private inventories, licensed inputs, or generated evidence into MAKO, and never make portable workflows depend on them.

## Diagnostics map

- `COLLECT_DIAGNOSTICS.md` routes users to Decky or standalone collection.
- `plugin/docs/COLLECT_DIAGNOSTICS.md` owns the managed Decky workflow.
- `engine/docs/COLLECT_DIAGNOSTICS.md` owns standalone Renderer collection.
- `scripts/mako-diagnostics` filters current and legacy Renderer logs into a focused report; its deterministic coverage is in `plugin/tests/test_diagnostics_helper.py`.
- `engine/mako-render/src/present_diagnostics.cpp` and `.hpp` own structured presentation records. Related state is emitted from `instance.cpp`, `swapchain.cpp`, and `swapchain_present.cpp`.
- `plugin/py_modules/mako_plugin/wrapper_generation.py` owns the generated wrapper environment, including opt-in wrapper-side log capture; `configuration.py` orchestrates canonical state, migrations, and atomic regeneration; and `installation.py` installs and migrates the helper.

Keep diagnostic operation names and fields machine-filterable. If a current log format changes, update the collector, its tests, and both user guides together.

## Generated files, artifacts, and protected inputs

- Treat `engine/{build,out}/`, `plugin/{dist,out,coverage,node_modules}/`, `website/{dist,dist-pages,.vinext,.next,node_modules}/`, package-manager stores, `__pycache__/`, and sibling `MAKO-Gym/out/` as generated local data. Do not hand-edit or commit them.
- `.agents/skills/openspec-*` is generated by the OpenSpec CLI for the target recorded in `.agents/skills/.openspec-target`; refresh it with `openspec update` instead of editing individual `SKILL.md` files.
- Renderer SPIR-V headers and hash manifests are generated from adjacent GLSL by `engine/scripts/generate-color-conversion-spirv.py` and `engine/scripts/generate-spatial-scaling-spirv.py`. Portable CTest runs their read-only `--check` modes; regeneration requires `glslangValidator`. Never edit embedded arrays or hashes independently.
- `plugin/shared_config.py` owns the Decky schema and stable identifiers. `plugin/scripts/generate_ts_schema.py` owns its generated Python and TypeScript bindings; use `npm run check:generated-config` from `plugin/` and never edit the outputs independently.
- `plugin/defaults/i18n/` owns Decky's translation sources; `plugin/scripts/manage-i18n.mjs` validates them and generates `plugin/src/i18n/languages.json`. `plugin/src/config/devBuildInfo.generated.ts` is local build metadata. Do not edit either generated output directly.
- `engine/dist/flatpak/mako-render/runtime-versions.txt` owns MAKO Renderer's ordered Flatpak build matrix. Decky's independently deployed runtime list remains in `plugin/shared_config.py`; `plugin/tests/test_flatpak_runtime_detection.py` requires the two owners to remain aligned, and `plugin/scripts/read_flatpak_runtime_contract.py` exposes the Decky contract to shell tooling without duplicating it.
- Treat the Decky schema as the profile allowlist. Unknown keys are inert and removed by the next canonical write; wrapper keys become environment exports only when the schema and generator support them. Preserve changed semantics with an explicit, tested migration.
- Published Renderer URLs, checksums, and pins in `plugin/package.json` are maintained by the release/pinning scripts. Local-engine packaging writes local identity into the ZIP without changing the tracked release pin.
- `Lossless.dll` is a user-supplied licensed input. Never add it to the repository, package it, upload it, or treat its presence as portable CI data.
- MAKO Renderer descends from the GPL-3.0-or-later lsfg-vk version 2 history identified in `LICENSE.md`. The replacement lsfg-vk repository announced on 27 August 2026 is licensed under CC BY-NC-ND 4.0 and is not a source for MAKO; never copy its post-reset code, documentation, or assets into this repository.

## Compatibility cleanup contract

`CLEANUPS.md` tracks temporary migrations, legacy readers, deny-lists, and their removal gates. Update it when adding or removing a compatibility path. Generated wrappers are replaceable cache: keep one current format and regenerate old ones from canonical state. Preserve small idempotent migrations for values stored only in retired locations; skipped-version upgrades mean age alone never makes them safe to remove.

## Qt compatibility contract

Qt is used only by the optional `mako-ui`; the Vulkan layer, backend, and CLI do not depend on it. `engine/mako-ui/CMakeLists.txt` deliberately declares Qt 6.2 as the source-build minimum. That is a minimum, not a pinned version, so Ubuntu 24.04's Qt 6.4 satisfies it.

Published host archives must remain runnable with Ubuntu 24.04's Qt 6.4. The ABI checks in `engine/scripts/package-local.sh` reject Qt 6.5-or-newer symbols and the Qt 6.8 `libQt6QmlMeta` dependency. On hosts with newer Qt, use `MAKO_PORTABLE_PACKAGE=1`; the portable Ubuntu 22.04 builder links against Qt 6.2 and produces an archive that also runs with newer compatible Qt 6 releases. Do not raise the CMake minimum or relax the package ABI guard without updating the build documentation and testing the oldest supported runtime.

## Packaging, deployment, and release boundaries

Local packaging does not publish. Direct development deployment does mutate an installed Decky test environment, and engine replacement requires games using the layer to be closed. Run deployment/reload actions only when the task calls for changing that installed environment.

Publishing is a separate, explicitly authorized workflow. Follow `HOW_TO_RELEASE.md` from a clean `main` checkout, update the two component release-note files as the only manual release copy, require the SteamOS hardware gate, and let the scripts manage versions, pins, checksums, tags, assets, and README release links. The hardware gate uses `scripts/run-steamos-hardware-validation.sh` to create a one-job runner; never register a persistent public-repository runner as a release shortcut. Never move a published tag or replace a published asset.

## Source-control boundaries

Inspect `git status`, preserve unrelated changes, and stay on the selected branch. Inspection, implementation, tests, builds, and packages do not authorize switching branches, pulling, rebasing, staging, committing, pushing, tagging, publishing, or deploying. Perform those mutations only when explicitly requested, then report the final branch and staged, committed, and pushed state.
