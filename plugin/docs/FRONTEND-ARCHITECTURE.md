# MAKO Decky frontend code map

MAKO Decky's panel composes independently owned state and view modules. Keep profile persistence in the existing hooks and backend RPC boundary; settings components render those values and send changes through the supplied callbacks.

| Concern | Owner | Boundary |
| --- | --- | --- |
| Panel composition and modal entry points | `src/components/Content.tsx` | Wires installation, session, configuration, and status hooks to views. |
| Current game and selected editing profile | `src/hooks/useProfileSession.ts` | Selects the editor profile without changing the running game's profile. |
| Profile lists and matching | `src/hooks/useProfileManagement.ts` | Calls backend profile RPCs and synchronizes the selected profile. |
| Configuration loading and live status | `src/hooks/useMakoHooks.ts` | Reads backend state and supplies the current configuration. |
| Debounced configuration writes | `src/hooks/useProfileConfigWriter.ts` | Serializes and applies editor changes; controls should not create a second save queue. |
| Main feature grouping | `src/components/FeatureSettings.tsx` and `src/components/ModalityTabs.tsx` | Selects Frame Generation, Scaling, or Shaders while retaining shared controls. |
| Feature controls | `src/components/FpsMultiplierControl.tsx`, `ScalingControl.tsx`, and `src/components/settings/` | Sends typed configuration changes through the editor callbacks. |
| Shader effect selection | `src/components/settings/shaders/EffectsChecklist.tsx` | Owns effect order, paging, Steam focus, and serialized live saves; `ShadersConfigurationGroup.tsx` owns the surrounding settings, custom file picker, catalog labels, and profile-keyed reset. `useMakoConfig()` reads the catalog with the profile; `Content.tsx` drains the existing save queue before add/refresh and guards against late profile reloads. |
| Flatpak setup | `src/components/FlatpaksModal.tsx` | Owns extension and application operations and their status display. |
| Native Remote Play controls | `src/components/RemotePlaySection.tsx` | Polls typed override status without overlapping requests, drains queued profile writes before mutations, rejects stale poll results, and refreshes the existing profile list after successful installation/removal. Groups the action, optional help, and compact status panel with shared section spacing; checking, working, unavailable, and recovery states stay visible when info is hidden. |
| Shared presentation | `src/components/MakoUi.tsx`, `ContentNotices.tsx`, and `RuntimeStatusCard.tsx` | Renders controls, notices, and status without persisting profile state. |

`ModelWarning.tsx` owns the single Lossless Scaling warning, with status-dependent bullets and a compact header action. `ScalingControl.tsx` retains scaling controls and surface guidance without duplicating DLL/model warnings.

`InfoVisibility.tsx` owns panel focus scrolling and R1 position restoration. Ordinary settings centre on navigation; controls inside `data-mako-focus-scroll="nearest"` scroll only as needed to remain visible. The effects selector uses this policy and disables Steam's enclosing Field scroll-on-child-focus option, so moving between visible rows does not recenter the panel. Its controls own their focus outlines, clear stale hover on navigation, and ignore blur events from an older focus target.

`PowerProfileControls.tsx` renders the shared/native power-profile toggle and editing-set selector. `useMakoHooks.ts` retains the backend's resolved editing set and guards pending loads; `useProfileConfigWriter.ts` keys queued patches by both profile and power set, so a source change cannot move an old edit to the new set. `Content.tsx` routes editor reloads through the existing writer's queue drain: loading locks edits before flushing, then reads canonical state after every queued or in-flight save finishes. This keeps shared shader values current across manual power-set selection and automatic source changes. Failed-write reconciliation loads directly, without waiting on its own in-flight write. The toggle flushes pending edits before cloning or removing power sets. `useProfileSession.ts` refreshes the editor after a live game's confirmed AC/battery change.

The schema source is `shared_config.py`; `src/config/generatedConfigSchema.ts` is generated. Decky's translation source is `defaults/i18n/`; `src/i18n/languages.json` is generated. Follow [testing](../../TESTING.md) after moving a component boundary, and preserve keyboard, gamepad, focus, selected-order, and profile-switch behavior in the focused frontend tests.

Native Remote Play extends `useProfileSession.ts` with a backend-confirmed native-client session; it does not fabricate a local Steam app record. Sessions without an AppID follow the dedicated process profile, lock offline profile changes, and use the existing power-source reload and write-queue behavior. On exit the editor returns to Default once. `useProfileEditorModel.ts` reloads its catalog after override changes and runtime profile selection so the newly created profile becomes selectable.
