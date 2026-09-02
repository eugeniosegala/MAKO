# Compatibility cleanup ledger

This document owns MAKO's transitional compatibility and cleanup policy. It is not a general backlog. Add an entry when a change temporarily accepts, imports, rewrites, or rejects an older persisted representation and remove the entry when the compatibility code is removed.

## Policy

Generated files and user data have different lifecycles:

- Generated launch wrappers are disposable cache. MAKO supports the current wrapper in place and atomically regenerates every stale, incomplete, or contaminated wrapper from canonical profile and configuration data. Do not add a chain of format-specific wrapper transforms or retain tests for an arbitrary number of old format numbers.
- Persisted values that do not exist anywhere else are user data. Keep a small, idempotent migration until MAKO has explicitly ended the direct-upgrade path from the affected release. Regenerating a wrapper is not a substitute for first rescuing those values.
- Compatibility deny-lists and fail-closed host guards are current safety invariants, not historical implementations. Keep them while a stale artifact could otherwise reactivate unsupported or conflicting behavior.

MAKO supports direct upgrades from the first public MAKO release, MAKO 2.0.0, and does not import state from the differently named experimental predecessor plugins or pre-public wrapper formats. MAKO does not impose a maximum skipped-version gap within that supported release line. Therefore "keep the last three releases" is not a safe deletion rule for MAKO data migrations: users may still update directly from MAKO 2.0.0. A data migration can be removed only after the project declares a newer minimum supported direct-upgrade baseline than its source release, or provides an equivalent state-preserving replacement.

When adding transitional compatibility, update this ledger in the same change with its owner, reason, removal gate, and regression test. Prefer one generic contract test over one test per historical version when the implementation does not branch on version.

## Current obligations

| Compatibility path | Owner | Why it remains | Safe-removal gate | Required evidence |
| --- | --- | --- | --- | --- |
| Unsupported native AArch64 wrapper passthrough | `wrapper_generation.HOST_COMPATIBILITY_MARKER` and host guard generation, orchestrated by `ConfigurationService` | Older or copied wrappers must not enable an x86-only Renderer on Armada/AArch64, while the platform game launcher must remain intact. | Native AArch64 packaging and activation are explicitly supported and pass the gates in `plugin/docs/ARMADA.md`. | Incompatible-host startup and wrapper tests remain fail-closed and preserve the Armada launcher exactly once. |
| Unsupported native AArch64 Flatpak override cleanup | `FlatpakService.disable_incompatible_host_overrides()`, invoked by `Plugin._main()` | Older plugin builds could persist per-application MAKO filesystem, wrapper, or environment overrides before the native-host architecture boundary; refusing new activation alone would leave those x86-only paths and variables active. | The minimum supported direct-upgrade baseline postdates native-host enforcement and no supported installation can retain a pre-boundary MAKO-owned Flatpak override, or native AArch64 activation owns an explicit compatible migration. | Unsupported-host fixtures remove only MAKO-owned application overrides, preserve competitor-only LSFG state, clear every MAKO-managed layer variable, and surface partial cleanup failures. |
| Historical diagnostic prefixes and operation IDs | `scripts/mako-diagnostics` | Reports from already-installed older releases still need to be readable even though current logs use MAKO branding and renamed recovery operations. | A separately declared diagnostics support window expires and release/support guidance accepts losing those old inputs. | Collector fixtures cover current operations plus the retained `resume-generated-frames`, `generated-image-recovered`, and `swapchain-recreation-suppressed` inputs. |
| Legacy split-layer execution contract | `engine/mako-render/src/layer_role.hpp` | Decky 2.2-era or developer launchers may export `MAKO_SPLIT_LAYER_CHAIN=1`, whose lower spatial role owns post-Frame Generation reconstruction. Current launchers export mode `2`, preserving the same Frame Generation → Gamescope WSI → Spatial Scaling loader order while moving all reconstruction resources to one upper combined owner and selecting pre/post placement from the immutable presentation extent. The legacy mode remains recognized so an older installed wrapper does not silently acquire incompatible cross-DSO ownership semantics. | The minimum supported direct-upgrade baseline guarantees wrapper regeneration to the current split contract before Renderer launch, and no supported standalone integration exports mode `1`. | Layer-role tests prove both legacy and current ownership independently; current wrapper tests require mode `2` and exact loader order; policy tests prove low/high-resolution placement and FG extent; MAKO Gym proves lower capability/extent ownership, both upper placements, and healthy generated throughput on real Gamescope WSI. |
| Released Decky 2.2 Gamescope WSI selector split | `profile_storage.normalize_wrapper_settings()` | Decky 2.2 stored `gamescope-wsi` in the mutually exclusive `external_vulkan_layer` field. Current profiles store Gamescope WSI independently so MangoHud or vkBasalt can follow MAKO without losing the WSI requirement. This migration concerns the released compatibility selector, not the unreleased spatial-scaling fields. | The minimum supported direct-upgrade baseline is newer than plugin 2.2, or an equivalent versioned importer preserves that selection. | Upgrade fixture proves `gamescope-wsi` becomes `gamescope_wsi_compatibility = true` with no post-process selection; current validation rejects it as a post-process value. |
| Pre-active-state native Renderer installs | `InstallationService.check_installation()` | Older MAKO Decky installations record only the bundled archive identity and older standalone installs do not identify which managed workflow last selected the shared native Renderer. The current active-state record is written by either installer; until then Decky must retain its installed-engine fallback, inspect the shared manifest owner so an older standalone override cannot hide behind stale Decky metadata, adopt a complete standalone layout for Decky launch workflows, and offer its bundled update when that standalone version is unknown. | The minimum supported direct-upgrade baseline guarantees that either managed installer has written `active-renderer.json`, or an equivalent binary identity probe replaces both records. | Backend fixtures prove a current standalone active version drives Decky's update status, a legacy standalone manifest override forces Decky's update path, a matching standalone install is adopted without replacement, and a legacy Decky installed-engine record remains readable. |

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
