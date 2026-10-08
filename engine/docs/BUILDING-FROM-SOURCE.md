# Building MAKO Renderer from source

This guide covers local development, source installation, and distributable Renderer packages.

<!-- prettier-ignore -->
> [!IMPORTANT]
> If you are planning on compiling MAKO Renderer on SteamOS, check the direct-build prerequisites from the repository root, then run the installer if anything is missing:
>
> ```bash
> ./engine/scripts/install-steamos-build-tools.sh --check
> ./engine/scripts/install-steamos-build-tools.sh --install
> ```
>
> The installer restores the original SteamOS read-only state, initializes the official Arch and Holo keyrings, and reinstalls the native build packages without `--needed`. Some SteamOS images preserve Pacman's installed-package records while removing development files or the local keyring. The script prompts before the Pacman transaction and does not build or deploy MAKO.

SteamOS image updates can also leave X11, XCB, Wayland, Vulkan loader, and Qt packages registered with Pacman while their development headers or metadata are absent. The installer restores these alongside the compiler, 32-bit headers, shader tools, and Flatpak development tools; its `--check` mode reports missing commands and headers without changing the host.

## Prerequisites

Install:

- Build tools such as `git` and `curl`
- A C++ compiler that supports C++20 or later
- CMake (version 3.10 or higher)
- Ninja (recommended; other CMake generators may work)
- Vulkan headers and loader development files
- X11 and XCB headers (`libx11`, `libxcb`, and `xorgproto` on Arch/SteamOS)
- Python 3 when building the registered tests
- A multilib C++ toolchain when building the 32-bit Vulkan layer
- Qt 6.2 or newer and Qt6Quick (only needed when building `mako-ui`)

Package names vary by distribution. These commands cover common Debian/Ubuntu and Arch installations:

```bash
# On Debian/Ubuntu, use:
sudo apt-get install -y \
    git curl python3 \
    llvm clang clang-tools clang-tidy \
    cmake ninja-build pkg-config g++-multilib \
    libvulkan-dev libx11-dev libxcb1-dev \
    mesa-common-dev \
    qt6-base-dev qt6-base-dev-tools \
    qt6-tools-dev qt6-tools-dev-tools \
    qt6-declarative-dev qt6-declarative-dev-tools

# On writable Arch Linux, use (SteamOS users should run the installer above):
sudo pacman -S --needed \
    git curl python \
    llvm clang ccache lib32-glibc \
    cmake ninja \
    vulkan-headers vulkan-icd-loader libx11 libxcb xorgproto \
    qt6-base qt6-declarative
```

The release packager builds the 64-bit CLI, UI, launcher, and both Renderer roles, then builds both roles again with `-m32`. It installs the libraries in `lib` and `lib32` with architecture-tagged manifests. A direct CMake build targets only the selected compiler architecture.

## Choose the right build path

| Need | Entry point | Result |
| --- | --- | --- |
| Incremental native Renderer work on SteamOS | `engine/scripts/build-steamos-dev.sh` | Reuses a development tree and builds the 64-bit layer and CLI; no distributable archive |
| Standalone Renderer archive | `engine/scripts/package-local.sh` | Tests and packages the host Renderer payload; does not build MAKO Decky or publish |
| Complete local Renderer release-shaped set | `engine/scripts/package-local-release.sh` | Builds the portable host archive, Flatpak archive, verified Arch package, and `SHA256SUMS` without changing release pins or publishing |
| Complete MAKO Decky tester package from current Renderer source | `pnpm --dir plugin run package:local-engine` from the repository root | Builds and embeds the native and Flatpak Renderer payloads in a self-contained local ZIP |
| Complete local release candidate | `MAKO_PORTABLE_PACKAGE=1 pnpm --dir plugin run package:local-engine`, then `scripts/check-release-candidate.sh PATH-TO-ZIP` from the repository root | Builds once from a clean pushed commit and checks the existing complete Decky ZIP without a CI rebuild or installation |
| Matched public release | `scripts/publish-release.sh X.Y.Z` from the repository root | Publishes MAKO Renderer first, pins it by checksum, then publishes MAKO Decky |

Use the incremental script for iteration, the standalone package for one host archive, and the complete local set to exercise all three public package formats. The complete local Decky ZIP serves as the release candidate; MAKO Gym runs remain targeted hardware work when needed. `package-local-release.sh` writes its artifacts under `engine/out/` by default and synchronizes only a disposable copy of the Arch recipe to the local host archive. Publication remains a separate workflow that applies version and pin commits and rebuilds the public artifacts, as described in [How to release MAKO](../../HOW_TO_RELEASE.md).

For an explicitly requested maintainer exception, `MAKO_RELEASE_SKIP_TESTS=1` omits test compilation, CTest, and launcher tests while retaining build, ABI, archive, and checksum verification. See the [hotfix exception](../../HOW_TO_RELEASE.md#maintainer-directed-hotfix-without-automated-validation). Publication always uses the portable builders: Ubuntu 22.04, Clang 14, Qt 6.2, and the pinned Vulkan headers for the native archive, plus Ubuntu 24.04 driving each declared Flatpak runtime SDK. Use `MAKO_PORTABLE_PACKAGE=1` for complete tester packages too; the scripts select Docker or Podman automatically. Without that flag, Flatpak packaging uses host `flatpak-builder` when available and falls back to the container builder when it is absent. Follow the [build-alignment and evidence requirements](../../HOW_TO_RELEASE.md#keep-tester-and-release-builds-aligned) when comparing tester and release artifacts.

Native and Flatpak archive builds and `scripts/build-steamos-dev.sh` enable `MAKO_REQUIRE_NATIVE_PACKAGE_HEADERS=ON` for both applicable architectures; the option retains its historical name for compatibility. CMake compiles the actual presentation-filter header and rejects headers older than the pinned revision or missing `VK_KHR_present_id2` or `VK_EXT_present_timing`, including when automated tests are disabled. The timing-node ABI checks therefore compile against the official `VkPresentTimingsInfoEXT` declaration. This prevents SDK selection from silently removing presentation compatibility. It is a build-time requirement, not a higher Vulkan driver/API requirement. Manual CMake builds retain their default host-header compatibility unless this option is enabled. Flatpak builds use the shared pinned headers with each runtime's existing compiler and libraries and still need runtime-specific evidence.

[`engine/vulkan-headers-revision.txt`](../vulkan-headers-revision.txt) is the single owner of the native and Flatpak Vulkan-Headers build pin and minimum header version. It contains one upstream SDK branch (`vulkan-sdk-X.Y.Z`) or release tag (`vX.Y.Z`). The portable packager, fast SteamOS development builder, and main Renderer CI builds fetch that ref, and CMake derives its minimum version from the same file. The fast builder caches a release tag under `engine/build/cache/vulkan-headers/`, resolves a movable SDK branch on each run, and logs the selected commit. `scripts/generate-flatpak-vulkan-headers.py` derives the shared Flatpak module from the pin; regenerate after pin changes, then run its read-only `--check` gate. Every generated Flatpak manifest includes that generated module before its two Renderer builds. The headers are removed during Flatpak cleanup and are not a runtime dependency. An SDK branch can advance, so retain the resolved header commit with the build evidence. MAKO Decky delegates native source builds to the owning Renderer builder and has no separate header pin. The sanitizer CI job retains its host headers for distribution SDK coverage. To update the baseline, change this file and qualify the resulting packages under the tester/release build-alignment requirements above. The Vulkan layer manifests' `api_version` describes the layer's supported API and is maintained separately; it must not automatically follow header updates.

[`engine/vkbasalt-release.json`](../vkbasalt-release.json) owns MAKO's runtime vkBasalt dependency. The pin identifies an immutable tag and source commit in MAKO's maintained fork, the exact dual-architecture release asset, its upstream baseline, and its SHA-256. `scripts/manage-vkbasalt-release.py` validates the pin, release provenance, internal checksums, both ELF classes, Vulkan layer entry points, manifests, and activation gates. Native packaging stages the verified libraries under `lib/vkbasalt` and `lib32/vkbasalt`; the generated Flatpak module stages the same release inside each runtime extension. Neither path installs vkBasalt system-wide. The cached download lives under `build/cache/vkbasalt` and can be relocated with `MAKO_BUILD_CACHE_ROOT` through the package scripts.

To update vkBasalt, merge the intended upstream baseline into the maintained fork, increment the fork release revision, run its dual-architecture release workflow, and retain the successful workflow and artifact attestations. Then update `vkbasalt-release.json`, regenerate `dist/flatpak/mako-render/vkbasalt-module.json` with `just generate-vkbasalt-release`, and run `just check-vkbasalt-release`. Never retag or replace a pinned asset: publish a new fork tag and change the MAKO pin. A released MAKO Renderer archive contains the verified libraries, manifests, licenses, source record, and pin, so an installed or downloaded MAKO release does not depend on the upstream vkBasalt repository remaining available. Rebuilding the same MAKO source from an empty cache still requires the pinned fork release asset.

Distributable archives and Flatpak extensions include the project license, third-party notices, and asset-provenance record. Packaging fails if those files or another required payload entry is missing. The standalone installer also rewrites its desktop entries to the selected installation prefix.

## Reusable SteamOS release-build SDK

`scripts/package-local.sh` normally uses the host Qt development installation. On Pacman-based SteamOS, if the Qt headers or CMake files are unavailable, it caches an isolated SDK under `build/cache/native-sdk/`. The first fallback build needs network access; later builds reuse the cache without changing the host installation.

This fallback applies only to `scripts/package-local.sh`. Manual CMake builds still use the system development packages. Set `MAKO_NATIVE_SDK_DIR` to place that SDK somewhere else, or set `MAKO_BUILD_CACHE_ROOT` to relocate all MAKO build caches together.

Local native Linux packaging can use the host SDK when it passes the Vulkan header check and Qt ABI guard. The package check rejects a UI that needs symbols newer than Qt 6.4. If either check fails, set `MAKO_PORTABLE_PACKAGE=1` to use the portable baseline with Docker or Podman. Non-Linux packaging and all public native releases require one of those runtimes.

The Ubuntu 22.04 portable builder installs the Qt QML runtime modules required by offscreen UI tests alongside the Qt development packages. Its system Python 3.10 exercises disabled Remote Play status and refusal to activate on an unsupported interpreter; stream activation still requires system Python 3.11 or newer.

## Fast SteamOS development build

For native Steam-game iteration, use the persistent incremental build instead of the release packager:

```bash
./scripts/build-steamos-dev.sh
```

This native host build uses the shared Vulkan-Headers pin and package presentation check while retaining its incremental CMake tree. Its first run needs network access to cache the pinned headers; later runs reuse a pinned release tag offline. It still needs the host C++ toolchain and X11/XCB development headers. Use the portable package builder when the output itself must match the release toolchain and packaging checks.

It builds both 64-bit Renderer roles and the CLI, retaining `build/steamos-dev` between runs. For real AMD validation with a sibling MAKO Gym checkout, run `./scripts/run-mako-gym.sh --suite quality --cli build/steamos-dev/mako-cli/mako-cli`. To retain a second tree for genuine 32-bit games, run:

```bash
./scripts/build-steamos-dev.sh --with-32-bit
```

The 32-bit tree defaults to `build/steamos-dev-32`. This development path skips the UI, Flatpak extensions, tests, archives, Decky ZIP, and hardware QA. It uses `ccache` under `build/cache/ccache` when available.

### Experimental forced LSFG FP16

For visual testing of an FP32-only LSFG model, build a separate native candidate with the existing FP16 setting controlling conversion:

```bash
./scripts/build-steamos-dev.sh --experimental-lsfg-fp16 --with-32-bit \
    --build-dir build/lsfg-fp16-experimental \
    --build-32-dir build/lsfg-fp16-experimental-32 --jobs 4
```

This build requires CMake 3.22.1 or newer. `scripts/prepare-lsfg-fp16-tools.sh` downloads checksum-pinned official SPIRV-Tools `33e02568181e3312f49a3cf33df470bf96ef293a` and its matching SPIRV-Headers `2a611a970fdbc41ac2e3e328802aed9985352dca` into `build/cache/lsfg-fp16-tools/`. The optimizer is linked statically for each architecture, with no additional runtime library or executable requirement. Direct CMake builds can use `-DMAKO_EXPERIMENTAL_LSFG_FP16=ON -DMAKO_LSFG_FP16_TOOLS_SOURCE=PATH` with the prepared source directory. Ordinary development and release builds leave conversion disabled by default; the build fingerprint identifies experimental binaries explicitly.

There is no additional UI or runtime toggle. With FP16 on, native FP16 takes priority; otherwise a supported native FP32 or translated DirectX graph is converted once and cached in memory. With FP16 off, the ordinary FP32 path remains selected. Conversion requires valid SPIR-V, preserved descriptor contracts, and actual half arithmetic. It cannot manufacture support for an unknown model layout or an unsupported GPU. Shader inputs, converted modules, and model resources never leave process memory.

Forced conversion remains unqualified for game image quality. The runtime reports `experimental_fp16=forced; quality=unqualified` when this path is selected. Earlier blanket conversion failed the moving-edge quality threshold; protecting image preparation, reconstruction, sampling coordinates, and bit-pattern dependencies corrected the tested procedural failures. Check moving edges, ghosting, flicker, and fine detail in actual games; passing compilation, procedural checks, or Vulkan validation does not qualify game image quality. These native builds do not update an installed Renderer, Decky plugin, or Flatpak extension. Follow the existing deployment workflow only when installation is requested.

The forced path keeps shared mipmap preparation and final image reconstruction at their original FP32 precision and converts eligible feature arithmetic to FP16. Sampling coordinates, LODs, offsets, and bit-pattern calculations retain FP32, including dependencies passed through shader memory; rounding them before expanding back to FP32 corrupts motion estimates. Protection is conservative across writes to one variable, so some feature stages remain entirely FP32. The tested beta graph contains 28 stages with actual half arithmetic; this is reported from each converted graph rather than assumed from its size. This mixed precision avoids the observed beta brightness loss and procedural motion errors without modifying a DLL's native FP16 model. Compare generated-frame brightness and motion with FP32 as well as real-frame passthrough; do not hide a conversion error with a brightness multiplier.

## SteamOS Flatpak development cache

Reusable package data lives under `build/cache`; disposable staging lives under `build/work`. Inspect both without deleting anything:

```bash
./scripts/prune-build-cache.sh
```

Add `--confirm` to remove those two trees. Native incremental builds, release artifacts, installed MAKO files, and profiles are not removed.

To inspect only the Flatpak cache and staging area, run:

```bash
./scripts/prune-steamos-flatpak-cache.sh
```

Add `--confirm` to remove only those Flatpak directories:

```bash
./scripts/prune-steamos-flatpak-cache.sh --confirm
```

Both pruners target only the default repository-local paths. They do not remove custom locations selected through environment overrides.

For `scripts/build-steamos-dev.sh`, use `MAKO_BUILD_JOBS=4` to cap parallelism on a memory-constrained Deck or `MAKO_BUILD_DIR=/path/to/build` to keep the build tree elsewhere. That script is a native development workflow only; use `scripts/package-local.sh` for a distributable archive and `scripts/package-flatpaks.sh` for Flatpak runtime bundles.

## Build and install MAKO Renderer

1. **Clone the repository**

Clone the MAKO monorepo and enter the engine package:

```bash
git clone https://github.com/eugeniosegala/MAKO.git
cd MAKO/engine
```

To build a specific Renderer release, check out its tag:

```bash
git checkout tags/render-vX.Y.Z
```

2. **Configure with CMake**

```bash
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DMAKO_BUILD_UI=On \
    -DMAKO_INSTALL_XDG_FILES=On
```

Useful CMake options:

- `CMAKE_BUILD_TYPE`: Set to `Release` for optimized builds or `Debug` for debugging builds.
- `CMAKE_INSTALL_PREFIX`: Specify the installation directory (default is `/usr/local`).
- `MAKO_BUILD_VK_LAYER`: Set to `On` to build the Vulkan layer (default is `On`).
- `MAKO_BUILD_UI`: Set to `On` to build the user interface (default is `Off`).
- `MAKO_BUILD_CLI`: Set to `On` to build the command-line interface (default is `On`).
- `MAKO_INSTALL_DEVELOP`: Set to `On` to install development files like headers and libraries (default is `Off`).
- `MAKO_INSTALL_XDG_FILES`: Set to `On` to install XDG desktop files and icons (default is `Off`).
- `MAKO_REQUIRE_NATIVE_PACKAGE_HEADERS`: Require the shared native/Flatpak Vulkan header baseline (default is `Off` for direct CMake builds; all package builds set it to `On`).
- `MAKO_VULKAN_HEADERS_INCLUDE_DIR`: Prefer an explicit Vulkan-Headers `include/` directory for compilation and the required-header check; the fast SteamOS builder sets this to its cache of the shared pin.
- `MAKO_LAYER_LIBRARY_PATH`: Override the frame-generation role library path stored in its manifest.
- `MAKO_SCALING_LAYER_LIBRARY_PATH`: Override the spatial role library path stored in its manifest.
- `MAKO_LAYER_MANIFEST_SUFFIX`: Add a suffix to the installed manifest filename when packaging multiple architectures.

For a non-system prefix, set both paths relative to their installed manifests, for example `../../../lib/libmako-render.so` and `../../../lib/libmako-render-scaling.so`.

3. **Build**

```bash
cmake --build build
```

The default build produces both split-chain Vulkan DSOs. A targeted `--target mako-render` build also refreshes `mako-render-scaling`, because the upper and lower roles share one versioned runtime contract and must never be staged from different source generations.

The default and Vulkan-layer builds check `mako-render/src/**/*.cpp` against the explicit production source lists in `mako-render/CMakeLists.txt`. When adding or moving an implementation file, assign it to its owning target there. The inventory check runs on incremental builds too, so an unlisted new file fails the build instead of silently disappearing from the host, 32-bit, Flatpak, and Arch artifacts. CTest runs the same check as `renderer-source-inventory` in the portable policy set.

4. **Install**

```bash
sudo cmake --install build
```

Start a native game with `mako-launch <command>`. The helper selects the install prefix's private manifests, activates MAKO for that child, excludes competing frame generation and Gamescope WSI, and selects the supported SDR boundary. A directly installed CMake build does not fetch the optional vkBasalt payload; use `scripts/package-local.sh` for the complete archive that supports `ENABLE_VKBASALT=1 mako-launch <command>`. Read [WSI isolation](WSI-ISOLATION.md) and [Optional graphics integrations](LAYER-CHAINING.md#standalone-mako-renderer-with-vkbasalt) before changing manifests or launch variables.
