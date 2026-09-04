# Compatibility cleanup ledger

This document owns MAKO's transitional compatibility and cleanup policy. It is not a general backlog. Add an entry when a change temporarily accepts, imports, rewrites, or rejects an older persisted representation and remove the entry when the compatibility code is removed.

## Policy

Generated files and user data have different lifecycles:

- Launch wrappers are disposable cache. Regenerate any stale, incomplete, or contaminated wrapper from canonical data instead of adding format-specific transforms.
- Persisted values that exist nowhere else are user data. Keep an idempotent migration until MAKO ends the direct-upgrade path from the affected release.
- Deny-lists and fail-closed host guards remain current safety rules while stale artifacts could reactivate unsupported behavior.

MAKO supports direct upgrades from MAKO 2.0.0, with no maximum skipped-version gap, but does not import state from differently named experimental predecessors or pre-public wrapper formats. A data migration can be removed only after MAKO declares a newer minimum direct-upgrade baseline than its source release or provides an equivalent state-preserving replacement.

When adding transitional compatibility, update this ledger in the same change with its owner, reason, removal gate, and regression test. Prefer one generic contract test over one test per historical version when the implementation does not branch on version.

## Current obligations

| Compatibility path | Owner | Why it remains and removal gate | Regression evidence |
| --- | --- | --- | --- |
| Unsupported native AArch64 wrapper passthrough | `wrapper_generation.HOST_COMPATIBILITY_MARKER`, orchestrated by `ConfigurationService` | Copied wrappers must preserve the Armada launcher without enabling the x86-only Renderer. Remove after native AArch64 packaging and activation pass `plugin/docs/ARMADA.md`. | Incompatible-host startup and wrapper tests stay fail-closed and preserve one launcher invocation. |
| Unsupported native AArch64 Flatpak override cleanup | `FlatpakService.disable_incompatible_host_overrides()`, called by `Plugin._main()` | Old builds may have left MAKO-owned x86 Flatpak overrides active. Remove after the supported upgrade baseline cannot contain them, or a native AArch64 migration replaces them. | Flatpak fixtures remove only MAKO-owned state, preserve competitor state, clear managed variables, and report partial failure. |
| Historical diagnostic prefixes and operation IDs | `scripts/mako-diagnostics` | Older reports remain readable after log branding and operation names changed. Remove only after a declared diagnostics support window expires. | `test_diagnostics_helper.py` covers current input plus `resume-generated-frames`, `generated-image-recovered`, and `swapchain-recreation-suppressed`. |
| Runtime-status filenames without process start ticks | `engine/mako-render/src/runtime_status.cpp` startup pruning | Abnormal exits from older releases can leave `<pid>-<role>-<context>.json`. Remove after the upgrade baseline cannot retain one. | Runtime-status tests remove only stale current and legacy records while preserving active locks and unrelated or unsafe entries. |
| Split-layer mode `1` | `engine/mako-render/src/layer_role.hpp` | Old launchers may export `MAKO_SPLIT_LAYER_CHAIN=1`; mode `2` keeps the same loader order but changes reconstruction ownership. Remove after every supported launcher regenerates before launch and no standalone integration exports mode `1`. | Layer-role and wrapper tests cover both modes, exact mode `2` order, placement, and extent; MAKO Gym covers the real Gamescope WSI path. |
| Decky 2.2 Gamescope WSI selector split | `profile_storage.normalize_wrapper_settings()` | Decky 2.2 stored `gamescope-wsi` in `external_vulkan_layer`; current profiles store WSI separately from MangoHud or vkBasalt. Remove after the upgrade baseline moves beyond 2.2 or an equivalent importer replaces it. | Upgrade tests move the token to `gamescope_wsi_compatibility` and reject it as a current post-process value. |
| Native Renderer installs without active-state metadata | `InstallationService.check_installation()` | Older installs lack `active-renderer.json`, so Decky must inspect legacy metadata and shared manifest ownership before adopting or updating the Renderer. Remove after all supported installs have active-state metadata or an identity probe replaces both records. | Installation tests cover current standalone state, legacy manifest ownership, matching-layout adoption, unknown-version updates, and legacy Decky metadata. |

## Completed cleanups

| Cleanup | Result | Completed |
| --- | --- | --- |
| Pre-public Decky compatibility paths | Declared MAKO 2.0.0 as the minimum direct-upgrade baseline. Removed formats 27–31 wrapper-state and DXVK cap imports, differently named predecessor package migration, pre-public unsafe-process repair, the 0.13 experimental DLL placeholder, obsolete-export contamination checks, the unreleased scaling listing alias, and their historical fixtures. Current MAKO wrappers remain disposable cache and still regenerate from canonical state. | 2026-09-02 |
| Pre-Steady-default Adaptive profile override | Removed the missing-field exception so every profile without an explicit choice adopts the current Steady Base Cap default. Profiles that explicitly saved Fractional remain unchanged. | 2026-08-21 |
| Numbered generated-wrapper history in migration documentation and tests | Replaced format-by-format descriptions and tests with the actual invariant: any non-current format is regenerated from canonical state. Unique user-state migrations and contamination guards remain separately tested. | 2026-08-20 |
| Legacy vkBasalt wrapper-export scrubber | Removed the one-off line editor. Non-current wrappers are regenerated from canonical profile state, and current vkBasalt activation is now owned by the per-profile External Tools selector. | 2026-08-20 |

## Removal checklist

Before deleting a current obligation:

1. Confirm whether the old representation contains unique user data or is only generated output.
2. Identify the published release that first wrote the canonical replacement and compare it with the declared minimum direct-upgrade baseline.
3. Verify that install, startup, rollback, and skipped-version upgrades cannot encounter the removed path within the supported contract.
4. Remove the compatibility code, its ledger entry, and only the tests that no longer express a current invariant.
5. Run the boundary-specific suite in `TESTING.md`; package/install changes also require local archive verification.
6. Mention user-visible compatibility removals in the component release notes.

Diagnostic input compatibility, stable package identifiers, and published archive names have their own contracts in `AGENTS.md`; do not treat them as generated-wrapper cleanup.
