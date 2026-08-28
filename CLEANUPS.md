# Compatibility cleanup ledger

This document owns MAKO's transitional compatibility and cleanup policy. It is not a general backlog. Add an entry when a change temporarily accepts, imports, rewrites, or rejects an older persisted representation and remove the entry when the compatibility code is removed.

## Policy

Generated files and user data have different lifecycles:

- Generated launch wrappers are disposable cache. MAKO supports the current wrapper in place and atomically regenerates every stale, incomplete, or contaminated wrapper from canonical profile and configuration data. Do not add a chain of format-specific wrapper transforms or retain tests for an arbitrary number of old format numbers.
- Persisted values that do not exist anywhere else are user data. Keep a small, idempotent migration until MAKO has explicitly ended the direct-upgrade path from the affected release. Regenerating a wrapper is not a substitute for first rescuing those values.
- Compatibility deny-lists and fail-closed host guards are current safety invariants, not historical implementations. Keep them while a stale artifact could otherwise reactivate unsupported or conflicting behavior.

MAKO does not currently impose a maximum skipped-version upgrade gap. Therefore "keep the last three releases" is not a safe deletion rule for data migrations: users may update directly from an older published build. A data migration can be removed only after the project declares a minimum supported direct-upgrade baseline that is newer than its source release, or provides an equivalent state-preserving replacement.

When adding transitional compatibility, update this ledger in the same change with its owner, reason, removal gate, and regression test. Prefer one generic contract test over one test per historical version when the implementation does not branch on version.

## Current obligations

| Compatibility path | Owner | Why it remains | Safe-removal gate | Required evidence |
| --- | --- | --- | --- | --- |
| Pre-profile wrapper-only settings import | `ConfigurationService.migrate_wrapper_profile_settings_if_needed()` | Older releases stored some selected-profile settings only in the generated launcher. | The minimum supported direct-upgrade baseline includes the profile settings store, or an equivalent importer preserves those values. | Upgrade fixture with legacy wrapper-only settings; current multi-profile wrapper must never be reverse-imported as one profile. |
| Legacy DXVK Base FPS Cap import | `ConfigurationService.migrate_legacy_base_fps_caps_if_needed()` | Wrapper format 27 stored the cap as `DXVK_FRAME_RATE`; dropping it changes users' configured pacing and can leave a double cap. | The minimum supported direct-upgrade baseline is newer than format 27 and release guidance no longer supports that direct path. | Upgrade fixture proves the value moves to engine `base_fps_cap` and the obsolete export is removed. |
| Legacy Decky package locations | `Plugin._migration()` | The published predecessor used `decky-lossless-scaling-vk` log, settings, and runtime paths that Decky must move before MAKO starts. | The minimum supported direct-upgrade baseline no longer includes an installation under the predecessor package identity. | Lifecycle fixture verifies all three Decky migration calls retain their exact source and destination paths. |
| Unsafe captured helper-process cleanup | `ConfigurationService.sanitize_captured_processes_if_needed()` | Older capture logic could persist shared launcher and Wine helper names that match unrelated games and select the wrong profile. | The minimum supported direct-upgrade baseline postdates the safe capture filter and no supported profile store can contain those helper names. | Idempotent profile fixture proves unsafe names are removed from metadata and `active_in`, safe names remain, and the wrapper is regenerated once. |
| Obsolete Lossless.dll placeholder cleanup | `InstallationService._merge_config_with_defaults()` | Releases before 0.13.0-experimental.2 persisted `/games/Lossless Scaling/Lossless.dll` when discovery failed, preventing current automatic discovery. | The minimum supported direct-upgrade baseline is newer than 0.13.0-experimental.2, or an equivalent canonical-config migration preserves automatic discovery. | Merge fixture proves a nonexistent placeholder is cleared while a real path and every user-selected non-placeholder path remain unchanged. |
| Obsolete wrapper export deny-list | `wrapper_generation.OBSOLETE_WRAPPER_EXPORTS`, consumed by `ConfigurationService` regeneration | A wrapper carrying a current marker but a retired or unsafe export must still be regenerated. | Review entries individually after every supported installed wrapper containing that export is outside the declared upgrade window and current generation cannot reintroduce it. | A structurally current wrapper contaminated by each denied export is regenerated. |
| Unsupported native AArch64 wrapper passthrough | `wrapper_generation.HOST_COMPATIBILITY_MARKER` and host guard generation, orchestrated by `ConfigurationService` | Older or copied wrappers must not enable an x86-only Renderer on Armada/AArch64, while the platform game launcher must remain intact. | Native AArch64 packaging and activation are explicitly supported and pass the gates in `plugin/docs/ARMADA.md`. | Incompatible-host startup and wrapper tests remain fail-closed and preserve the Armada launcher exactly once. |
| Unsupported native AArch64 Flatpak override cleanup | `FlatpakService.disable_incompatible_host_overrides()`, invoked by `Plugin._main()` | Older plugin builds could persist per-application MAKO filesystem, wrapper, or environment overrides before the native-host architecture boundary; refusing new activation alone would leave those x86-only paths and variables active. | The minimum supported direct-upgrade baseline postdates native-host enforcement and no supported installation can retain a pre-boundary MAKO-owned Flatpak override, or native AArch64 activation owns an explicit compatible migration. | Unsupported-host fixtures remove only MAKO-owned application overrides, preserve competitor-only LSFG state, clear every MAKO-managed layer variable, and surface partial cleanup failures. |
| Historical diagnostic prefixes and operation IDs | `scripts/mako-diagnostics` | Reports from already-installed older releases still need to be readable even though current logs use MAKO branding and renamed recovery operations. | A separately declared diagnostics support window expires and release/support guidance accepts losing those old inputs. | Collector fixtures cover current operations plus the retained `resume-generated-frames`, `generated-image-recovered`, and `swapchain-recreation-suppressed` inputs. |
| Released Decky 2.2 Gamescope WSI selector split | `profile_storage.normalize_wrapper_settings()` | Decky 2.2 stored `gamescope-wsi` in the mutually exclusive `external_vulkan_layer` field. Current profiles store Gamescope WSI independently so MangoHud or vkBasalt can follow MAKO without losing the WSI requirement. This migration concerns the released compatibility selector, not the unreleased spatial-scaling fields. | The minimum supported direct-upgrade baseline is newer than plugin 2.2, or an equivalent versioned importer preserves that selection. | Upgrade fixture proves `gamescope-wsi` becomes `gamescope_wsi_compatibility = true` with no post-process selection; current validation rejects it as a post-process value. |
| Pre-scaling Decky listing name | `plugin/scripts/deploy-dev.sh` and `plugin/scripts/deploy-validated-package.py` | Developer-mode installations can still declare `MAKO - Frame Generation`; they must be accepted while the manifest changes to `MAKO - Scaling & Frame Generation` so the shared `Mako/` directory is updated in place. | The first public MAKO Decky release using the new name is outside the supported developer-build upgrade path. | Package-deployment fixture upgrades an old manifest in the `Mako/` directory; path-package contract proves both deployers retain the legacy name. |

## Completed cleanups

| Cleanup | Result | Completed |
| --- | --- | --- |
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
