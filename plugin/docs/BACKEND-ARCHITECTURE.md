# MAKO Decky backend code map

MAKO Decky's Python backend translates UI requests into canonical profile data, generated launch wrappers, installed files, and runtime status. The modules have distinct ownership even where a service orchestrator remains large.

| Concern | Owner | Boundary |
| --- | --- | --- |
| Async RPC and Decky lifecycle | `py_modules/mako_plugin/plugin.py` | Validates request shape, calls services, and returns typed responses; it does not own profile serialization or file replacement. |
| Public response mappings | `py_modules/mako_plugin/types.py` and owning service types | Keep Python RPC payloads aligned with `src/api/makoApi.ts` and the RPC contract tests. |
| Renderer TOML and profile transactions | `py_modules/mako_plugin/configuration.py` | Orchestrates profile and wrapper-sidecar changes, locks configuration field writes, and regenerates dependent files. |
| Profile metadata and wrapper sidecars | `py_modules/mako_plugin/profile_storage.py` | Normalizes persisted Decky-only values and owns profile identity, vkBasalt content merging, FX discovery, and pure file-reference registration. `ConfigurationService.add_profile_shader()` validates the profile and writes its file atomically under the existing configuration lock; FX source files remain external. |
| Pure launcher generation | `py_modules/mako_plugin/wrapper_generation.py` | Emits the current wrapper format from normalized inputs; no runtime import from MAKO Renderer. |
| Atomic replacement and rollback | `py_modules/mako_plugin/managed_files.py` and `installation.py` | The helper owns file primitives; installation owns the native payload transaction and selected Renderer identity. |
| Flatpak setup | `py_modules/mako_plugin/flatpak_service.py` | Detects runtime extensions and manages application overrides outside the native installation transaction. |
| Runtime status | `py_modules/mako_plugin/runtime_state.py` | Validates and reads MAKO Renderer's atomic status records; it does not apply profile changes. |
| Paths and stable identifiers | `py_modules/mako_plugin/base_service.py`, `constants.py`, and `package_paths.py` | Resolve the Decky user's paths, packaged payloads, and compatibility names. |

`config_schema.py` preserves and normalizes the Renderer's optional per-profile `handheld` and `docked` TOML tables through capture, clone, rename, selection, deletion, and installation. Mode-aware field patches keep native settings isolated while writing globals and wrapper settings to their existing shared owners. `host_environment.py` reports power for editor selection only; the Renderer independently owns actual startup and live selection. `wrapper_generation.py` retains layer discovery for every saved power set, leaving actual resource provisioning to the Renderer. Focused power-profile tests and RPC contracts guard this independently deployed boundary.

`shared_config.py` owns the cross-language configuration schema. Generate its Python and TypeScript bindings through the owning script; never edit generated bindings directly. A profile write must keep Renderer TOML, Decky sidecars, the selected shader configuration, and the generated wrapper coherent. `InstallationService._renderer_file_inventory()` owns archive destinations and the file set used for native rollback and cleanup; the standalone installer's independent cleanup list must match it through the cross-component test. Installation and Flatpak preparation are separate transactions with the limits recorded in [native installation transactions](../../INSTALLATION-TRANSACTIONS.md).

Native base and power-table serialization derives its field set from that schema's `toml`/`profile` locations, excluding the shared process match. Adding a native setting does not require another Decky writer or power-mode mapping. The power-profile contract checks every schema-owned native key in both tables; shader and launcher sidecars remain shared across power modes.

Keep orchestration in `ConfigurationService` and `InstallationService` when an operation must coordinate several files. A shorter file is not a reason to create a second state owner or to move only half of a transaction. Follow [testing](../../TESTING.md) and the focused backend contract tests whenever an RPC, profile, wrapper, installation, or Flatpak boundary changes.

## Flatpak VRR discovery lifecycle

`plugin.py` starts and stops the installed Renderer `mako-vrr-lease --watch-flatpak` host monitor on plugin load/unload and Renderer install/uninstall. The helper owns bounded runtime-status discovery, namespace process validation, compositor verification, and detached game leases. Decky does not implement a second VRR writer; detached leases survive plugin reload to preserve exit restoration. Missing helpers or a different user identity leave discovery disabled.
