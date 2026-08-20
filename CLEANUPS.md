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
| Obsolete wrapper export deny-list | `ConfigurationService._OBSOLETE_WRAPPER_EXPORTS` | A wrapper carrying a current marker but a retired or unsafe export must still be regenerated. | Review entries individually after every supported installed wrapper containing that export is outside the declared upgrade window and current generation cannot reintroduce it. | A structurally current wrapper contaminated by each denied export is regenerated. |
| Unsupported native AArch64 wrapper passthrough | `ConfigurationService._HOST_COMPATIBILITY_MARKER` and host guard generation | Older or copied wrappers must not enable an x86-only Renderer on Armada/AArch64, while the platform game launcher must remain intact. | Native AArch64 packaging and activation are explicitly supported and pass the gates in `plugin/docs/ARMADA.md`. | Incompatible-host startup and wrapper tests remain fail-closed and preserve the Armada launcher exactly once. |
| Historical diagnostic prefixes | `scripts/mako-diagnostics` | Reports from already-installed older releases still need to be readable even though current logs use MAKO branding. | A separately declared diagnostics support window expires and release/support guidance accepts losing those old inputs. | Collector fixtures cover current and retained legacy inputs. |

## Completed cleanups

| Cleanup | Result | Completed |
| --- | --- | --- |
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
