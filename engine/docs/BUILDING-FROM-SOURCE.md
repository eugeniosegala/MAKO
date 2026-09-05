# Building MAKO Renderer from source

This guide covers local development, source installation, and distributable Renderer packages.

<!-- prettier-ignore -->
> [!IMPORTANT]
> If you are planning on compiling MAKO Renderer on SteamOS, you need to temporarily disable read-only protection and restore the base C/C++ headers first:
>
> ```bash
> sudo steamos-readonly disable
> sudo pacman -S glibc linux-api-headers lib32-glibc
> sudo steamos-readonly enable
> ```
>
> The command intentionally does not use `--needed`: some SteamOS images report `glibc`, `linux-api-headers`, or `lib32-glibc` as installed while a required header is absent. Reinstalling those packages restores the headers. Only run `pacman-key --init` and `pacman-key --populate` if Pacman specifically reports a keyring or signature error.

## Prerequisites

Install:

- Build tools such as `git` and `curl`
- A C++ compiler that supports C++20 or later
- CMake (version 3.10 or higher)
- Ninja (recommended; other CMake generators may work)
- Vulkan headers and loader development files
- X11 headers (`libx11` and `xorgproto` on Arch/SteamOS)
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
    libvulkan-dev libx11-dev \
    mesa-common-dev \
    qt6-base-dev qt6-base-dev-tools \
    qt6-tools-dev qt6-tools-dev-tools \
    qt6-declarative-dev qt6-declarative-dev-tools

# On Arch Linux, use:
sudo pacman -S --needed \
    git curl python \
    llvm clang ccache lib32-glibc \
    cmake ninja \
    vulkan-headers vulkan-icd-loader libx11 xorgproto \
    qt6-base qt6-declarative
```

The release packager builds the 64-bit CLI, UI, launcher, and both Renderer roles, then builds both roles again with `-m32`. It installs the libraries in `lib` and `lib32` with architecture-tagged manifests. A direct CMake build targets only the selected compiler architecture.

## Choose the right build path

| Need | Entry point | Result |
| --- | --- | --- |
| Incremental native Renderer work on SteamOS | `engine/scripts/build-steamos-dev.sh` | Reuses a development tree and builds the 64-bit layer and CLI; no distributable archive |
| Standalone Renderer archive | `engine/scripts/package-local.sh` | Tests and packages the host Renderer payload; does not build MAKO Decky or publish |
| Complete MAKO Decky tester package from current Renderer source | `pnpm --dir plugin run package:local-engine` from the repository root | Builds and embeds the native and Flatpak Renderer payloads in a self-contained local ZIP |
| SteamOS/AMD release candidate | `scripts/run-steamos-hardware-validation.sh --gym-suite <affected-suite> --gym-reason '<why>' --deploy-to-decky` from the repository root | Rebuilds a clean, pushed commit, runs explicitly selected Gym hardware coverage, and optionally deploys the gate-built ZIP; publishes nothing and does not promote that ZIP into the later release |
| Matched public release | `scripts/publish-release.sh X.Y.Z` from the repository root | Publishes MAKO Renderer first, pins it by checksum, then publishes MAKO Decky |

Use the incremental script for iteration, the local package for testers, and the hardware workflow for a release candidate. Publication is a separate workflow that applies version and pin commits and rebuilds the public artifacts, as described in [How to release MAKO](../../HOW_TO_RELEASE.md).

Distributable archives and Flatpak extensions include the project license, third-party notices, and asset-provenance record. Packaging fails if those files or another required payload entry is missing. The standalone installer also rewrites its desktop entries to the selected installation prefix.

## Reusable SteamOS release-build SDK

`scripts/package-local.sh` normally uses the host Qt development installation. On Pacman-based SteamOS, if the Qt headers or CMake files are unavailable, it caches an isolated SDK under `build/cache/native-sdk/`. The first fallback build needs network access; later builds reuse the cache without changing the host installation.

This fallback applies only to `scripts/package-local.sh`. Manual CMake builds still use the system development packages. Set `MAKO_NATIVE_SDK_DIR` to place that SDK somewhere else, or set `MAKO_BUILD_CACHE_ROOT` to relocate all MAKO build caches together.

Native Linux packaging does not require a container. The package check rejects a UI that needs symbols newer than Qt 6.4. If the host provides only a newer Qt, set `MAKO_PORTABLE_PACKAGE=1` to build against Ubuntu 22.04's Qt 6.2 baseline with Docker or Podman. Non-Linux packaging also requires one of those runtimes.

## Fast SteamOS development build

For native Steam-game iteration, use the persistent incremental build instead of the release packager:

```bash
./scripts/build-steamos-dev.sh
```

It builds both 64-bit Renderer roles and the CLI, retaining `build/steamos-dev` between runs. For real AMD validation with a sibling MAKO Gym checkout, run `./scripts/run-mako-gym.sh --suite quality --cli build/steamos-dev/mako-cli/mako-cli`. To retain a second tree for genuine 32-bit games, run:

```bash
./scripts/build-steamos-dev.sh --with-32-bit
```

The 32-bit tree defaults to `build/steamos-dev-32`. This development path skips the UI, Flatpak extensions, tests, archives, Decky ZIP, and hardware QA. It uses `ccache` under `build/cache/ccache` when available.

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
- `MAKO_LAYER_LIBRARY_PATH`: Override the frame-generation role library path stored in its manifest.
- `MAKO_SCALING_LAYER_LIBRARY_PATH`: Override the spatial role library path stored in its manifest.
- `MAKO_LAYER_MANIFEST_SUFFIX`: Add a suffix to the installed manifest filename when packaging multiple architectures.

For a non-system prefix, set both paths relative to their installed manifests, for example `../../../lib/libmako-render.so` and `../../../lib/libmako-render-scaling.so`.

3. **Build**

```bash
cmake --build build
```

The default build produces both split-chain Vulkan DSOs. A targeted `--target mako-render` build also refreshes `mako-render-scaling`, because the upper and lower roles share one versioned runtime contract and must never be staged from different source generations.

4. **Install**

```bash
sudo cmake --install build
```

Start a native game with `mako-launch <command>`. The helper selects the install prefix's private manifests, activates MAKO for that child, excludes competing frame generation and Gamescope WSI, and selects the supported SDR boundary. Read [WSI isolation](WSI-ISOLATION.md) before changing manifests or launch variables.
