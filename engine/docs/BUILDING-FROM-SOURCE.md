# Building MAKO Renderer from Source

This guide provides step-by-step instructions on how to build the MAKO Renderer project from source code.

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

### Prerequisites

Before you begin, ensure you have the required packages installed on your system.

You will need the following dependencies:

- Typical build tools, such as `git`, `curl`, etc.
- A C++ compiler that supports C++20 or later
- CMake (version 3.10 or higher)
- Ninja build system (other build systems may work, but Ninja is recommended)
- Vulkan SDK
- X11 headers (`libx11` and `xorgproto` on Arch/SteamOS)
- A multilib C++ toolchain when building the 32-bit Vulkan layer
- Qt 6.2 or newer and Qt6Quick (only needed when building `mako-ui`)

The list of required packages may vary depending on your operating system. Below are the installation commands for some common Linux distributions.

```bash
# On Debian/Ubuntu, use:
sudo apt-get install -y \
    git curl \
    llvm clang clang-tools clang-tidy \
    cmake ninja-build pkg-config g++-multilib \
    libvulkan-dev \
    mesa-common-dev \
    qt6-base-dev qt6-base-dev-tools \
    qt6-tools-dev qt6-tools-dev-tools \
    qt6-declarative-dev qt6-declarative-dev-tools

# On Arch Linux, use:
sudo pacman -S --needed \
    git curl \
    llvm clang ccache lib32-glibc \
    cmake ninja \
    vulkan-headers vulkan-icd-loader libx11 xorgproto \
    qt6-base qt6-declarative
```

The release packager builds the normal 64-bit application, CLI, UI, launcher, and layer, then builds a second layer with `-m32`. It installs the two layer libraries in `lib` and `lib32` with architecture-tagged Vulkan manifests. Direct CMake builds produce one layer for the compiler architecture selected for that build.

### Choose the right build path

| Need | Entry point | Result |
| --- | --- | --- |
| Incremental native Renderer work on SteamOS | `engine/scripts/build-steamos-dev.sh` | Reuses a development tree and builds the 64-bit layer and CLI; no distributable archive |
| Standalone Renderer archive | `engine/scripts/package-local.sh` | Tests and packages the host Renderer payload; does not build MAKO Decky or publish |
| Complete MAKO Decky tester package from current Renderer source | `pnpm --dir plugin run package:local-engine` from the repository root | Builds and embeds the native and Flatpak Renderer payloads in a self-contained local ZIP |
| SteamOS/AMD release candidate | `scripts/run-steamos-hardware-validation.sh --deploy-to-decky` from the repository root | Rebuilds a clean, pushed commit and optionally deploys the already-verified ZIP; publishes nothing |
| Matched public release | `scripts/publish-release.sh X.Y.Z` from the repository root | Publishes MAKO Renderer first, pins it by checksum, then publishes MAKO Decky |

Use the fast paths for iteration, the complete local package for testers, and the dedicated hardware workflow for the release candidate. Publication is a separate, explicitly invoked cycle described in [How to release MAKO](../../HOW_TO_RELEASE.md).

### Reusable SteamOS release-build SDK

`scripts/package-local.sh` normally uses the host Qt development installation. On a Pacman-based SteamOS host where Qt appears installed but its headers or CMake files are missing, the packager automatically downloads the exact `qt6-base`, `qt6-declarative`, and `libglvnd` packages selected by Pacman into `engine/build/cache/native-sdk/`. It extracts and reuses that isolated SDK on later release builds, without Docker, Podman, root access, or changes to the SteamOS installation. The first fallback build needs network access; later builds reuse the cached files.

This fallback applies only to `scripts/package-local.sh`. Manual CMake builds still use the system development packages. Set `MAKO_NATIVE_SDK_DIR` to place that SDK somewhere else, or set `MAKO_BUILD_CACHE_ROOT` to relocate all MAKO build caches together.

Native Linux packaging does not require a container. The packager verifies that the resulting `mako-ui` does not require a Qt ABI newer than 6.4, which keeps published host archives compatible with Ubuntu 24.04. If the host distribution only provides a newer Qt, rerun with `MAKO_PORTABLE_PACKAGE=1`; that optional mode uses Docker or Podman and builds the UI against Ubuntu 22.04's Qt 6.2 baseline. Non-Linux packaging continues to require one of those container runtimes.

### Fast SteamOS development build

For native Steam-game iteration, use the persistent incremental build instead of the release packager:

```bash
./scripts/build-steamos-dev.sh
```

It builds the 64-bit Vulkan layer and CLI and keeps `build/steamos-dev` between runs. Real AMD image-quality orchestration is intentionally separate in the private sibling MAKO Gym checkout; after building, run `./scripts/run-mako-gym.sh --suite quality --cli build/steamos-dev/mako-cli/mako-cli` from `engine/` when that checkout is available. The SteamOS release workflow requires Gym's complete 74-case LSFG, spatial, and combined visual matrix, 18 repeated LSFG performance workloads with explicit 5× coverage, 36 exact-resolution pixel-qualified timestamp-query spatial-performance rows, 14 paired live runtime-overhead rows, eight synchronization-validation paths, and nine three-run deterministic-output sentinels, including FP32 and FP16 through 5120×2160. To retain a second incremental tree for genuine 32-bit games, run:

```bash
./scripts/build-steamos-dev.sh --with-32-bit
```

The 32-bit tree defaults to `build/steamos-dev-32`. Both commands intentionally skip the Qt UI, Flatpak extensions, general test suite, archives, Decky ZIP, and private hardware QA. MAKO Gym accepts `--dll` for a nonstandard Lossless Scaling installation and `--gpu` for an exact AMD device name. Subsequent builds compile only changed source and its dependants. When available, `ccache` is enabled automatically and stored under `build/cache/ccache`; install it with the rest of the Arch build prerequisites to retain compiler results across larger rebuilds.

### SteamOS Flatpak development cache

MAKO keeps all reusable build data under `build/cache` by default. The plugin's `dev:flatpaks` and `dev:e2e` commands retain their isolated Flatpak SDK/runtime downloads under `build/cache/flatpak` and use `build/work/flatpak` for self-cleaning staging. Nothing in these locations is installed into `/root` or the host system. To inspect all repository-local build storage safely, run:

```bash
./scripts/prune-build-cache.sh
```

That command is a dry run. Add `--confirm` only when you want to remove all reusable Qt, Flatpak, and compiler caches plus disposable work trees. Native incremental builds, release artifacts, installed MAKO data, and profiles remain untouched.

To inspect or remove only the Flatpak portion, run:

```bash
./scripts/prune-steamos-flatpak-cache.sh
```

To delete only the Flatpak cache and work directory, add the explicit confirmation flag:

```bash
./scripts/prune-steamos-flatpak-cache.sh --confirm
```

For safety, both pruners target only the default repo-local locations. If you set `MAKO_BUILD_CACHE_ROOT`, `MAKO_BUILD_WORK_ROOT`, or a component-specific override, manage that custom location yourself.

Use `MAKO_BUILD_JOBS=4` to cap parallelism on a memory-constrained Deck, or `MAKO_BUILD_DIR=/path/to/build` to keep the build tree elsewhere. This is a native host-test workflow only; use `scripts/package-local.sh` for a distributable archive and `scripts/package-flatpaks.sh` for Flatpak runtime bundles.

### Building & Installing MAKO Renderer

1. **Clone the Repository**

Clone the MAKO monorepo and enter the engine package:

```bash
git clone https://github.com/eugeniosegala/MAKO.git
cd MAKO/engine
```

Optionally, you can check out a specific Renderer release tag:

```bash
git checkout tags/render-vX.Y.Z
```

2. **Configure the build with CMake**

The recommended way to configure MAKO Renderer is this:

```bash
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DMAKO_BUILD_UI=On \
    -DMAKO_INSTALL_XDG_FILES=On
```

However, MAKO Renderer provides several CMake options to customize the build process:

- `CMAKE_BUILD_TYPE`: Set to `Release` for optimized builds or `Debug` for debugging builds.
- `CMAKE_INSTALL_PREFIX`: Specify the installation directory (default is `/usr/local`).
- `MAKO_BUILD_VK_LAYER`: Set to `On` to build the Vulkan layer (default is `On`).
- `MAKO_BUILD_UI`: Set to `On` to build the user interface (default is `Off`).
- `MAKO_BUILD_CLI`: Set to `On` to build the command-line interface (default is `On`).
- `MAKO_INSTALL_DEVELOP`: Set to `On` to install development files like headers and libraries (default is `Off`).
- `MAKO_INSTALL_XDG_FILES`: Set to `On` to install XDG desktop files and icons (default is `Off`).
- `MAKO_LAYER_LIBRARY_PATH`: Override the path to the Vulkan layer library (by default, Vulkan will search the systems library path).
- `MAKO_LAYER_MANIFEST_SUFFIX`: Add a suffix to the installed manifest filename when packaging multiple architectures.

Please keep in mind that installing to non-system paths will require `MAKO_LAYER_LIBRARY_PATH` to be set accordingly (e.g. `../../../lib/libmako-render.so`).

3. **Build the Project**

Build the project using Ninja:

```bash
cmake --build build
```

4. **Install the Project**

Install the built files to the specified installation prefix:

```bash
sudo cmake --install build
```

Keep track of the installed files, in order to uninstall them later if needed.

The installed manifests are launch-scoped. Start a native game with `mako-launch <command>`. The helper selects the install prefix's private MAKO-only implicit-layer directory, activates MAKO for that process, prevents Steam's Vulkan hooks or another installed LSFG-VK frame-generation layer from bypassing the same swapchain, suppresses Gamescope WSI's conflicting presentation policy for that child, and selects the supported SDR boundary. It is installed alongside `mako-cli` whenever the Vulkan layer is installed. Read [WSI isolation](WSI-ISOLATION.md) before changing manifests, discovery paths, or Gamescope variables.
