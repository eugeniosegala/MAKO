# Armada and native AArch64 support

## Current release status

MAKO Decky can identify Armada and other native AArch64 hosts, but the current
MAKO Renderer payload is **not supported or activated there**. Published host
and Flatpak packages are built for an x86_64 native host; their 64-bit and
32-bit entries describe Vulkan client-process coverage, not native AArch64
host compatibility.

This is an intentional release boundary, not a claim that Armada support is
complete. Games continue through Armada's normal FEX launch path without MAKO
frame generation.

| Situation | Current behavior |
| --- | --- |
| Native x86_64 host | Installation and activation proceed normally. |
| Armada, including Decky running through FEX | The root-owned `device-env` marker identifies the native AArch64 host even if Python reports x86_64. Installation is disabled. |
| Other native AArch64 host | PID 1's ELF identity and the native machine name provide the same fail-closed boundary. |
| Wrapper left by an older MAKO build | Startup replaces it with a minimal passthrough before parsing profiles or exporting Vulkan settings. On Armada, `armada-game-launch` is applied exactly once. |
| x86 Renderer files left on disk | They remain visible for cleanup, but installation status is unsupported and the wrapper never enables them. |
| Flatpak app prepared by an older build | MAKO-owned activation paths and variables are removed. Shared runtime extensions may remain installed, but cannot be newly installed, refreshed, or activated. |

## Compatibility boundary

Native host architecture is a package property, separate from Vulkan process
bitness:

```text
package.json host_architectures
        |
        +-- native host detection (Armada marker, PID 1 ELF, machine name)
        |       |
        |       +-- compatible: normal installation and migrations
        |       |
        |       +-- incompatible: no install, early wrapper passthrough,
        |                         remove persisted MAKO Flatpak activation
        |
        +-- archive architectures: 64-bit/32-bit Vulkan client payloads
```

The boundary is centralized in `host_environment.py`. Host installation,
startup migration, generated wrappers, Flatpak activation, backend status, and
the Decky install button all consume the same decision. Do not add a second
Armada detector or infer native support solely from `platform.machine()`.

The generated wrapper checks the host before setting `ENABLE_MAKO`, Vulkan
manifest paths, WSI/HDR policy, diagnostics, profile settings, or competitor
guards. This ordering is a safety invariant: an unsupported process must leave
through the passthrough branch without inheriting a partial MAKO environment.

## What was retained from the upstream work

The useful, bounded parts of the original Armada integration remain:

- a stable root-owned Armada marker instead of trusting a translated process;
- deterministic user-owned staging and permissions rather than crossing a
  translated `/tmp` boundary;
- `armada-game-launch` preservation with double-wrapping protection; and
- an explicit native-host declaration in package metadata.

MAKO does not carry the unfinished global platform/overlay framework proposed
upstream, does not rewrite FEX's global `Config.json`, and does not download an
opaque AArch64 layer binary. Those approaches expand the failure surface and
do not establish that the Vulkan layer is reproducible or correct on Armada.

## Requirements for enabling Armada

Armada must remain disabled until one release candidate satisfies all of these
as a coherent path:

1. Produce the native AArch64 Renderer from reviewable source in MAKO's normal
   build and release workflow, with immutable version and checksum metadata.
2. Define the required native-host and Vulkan client-process payload matrix;
   do not assume the existing x86_64 `64`/`32` archive labels prove FEX or
   Turnip compatibility.
3. Prove Vulkan loader activation and clean teardown on Armada without global
   FEX configuration edits or an always-on system layer.
4. Validate `armada-game-launch`, direct Steam/Proton games, relevant Flatpak
   launchers, and one-launch bypass behavior.
5. Run FP32 and FP16 image-quality comparisons, fixed and adaptive scheduling,
   swapchain recreation, overlays, focus changes, and representative games on
   real Armada/Turnip hardware.
6. Ship a tested rollback: an unsupported or failed native package must return
   to the same passthrough behavior documented above.

Only after those gates pass should an AArch64 package declare
`host_architectures: ["aarch64"]` and make the install control available.

## Code ownership and regression checks

- Native-host detection: `py_modules/mako_plugin/host_environment.py`
- Package validation and installation status: `installation.py`
- Startup boundary: `plugin.py`
- Wrapper passthrough and Armada launcher: `configuration.py`
- Flatpak activation and old-override cleanup: `flatpak_service.py`
- Deterministic coverage: `tests/test_dual_arch_installation.py`,
  `tests/test_wrapper_environment.py`, `tests/test_plugin_lifecycle.py`, and
  `tests/test_flatpak_runtime_detection.py`

Any future Armada change must update this document and those boundary tests.
Native Armada hardware evidence is required before a release can describe the
Renderer as supported there.
