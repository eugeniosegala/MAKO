# Building MAKO Renderer from Source
This guide provides step-by-step instructions on how to build the MAKO Renderer project from source code.

>[!IMPORTANT]
>If you are planning on compiling MAKO Renderer on SteamOS, you need to temporarily
>disable read-only protection and restore the base C/C++ headers first:
> ```bash
> sudo steamos-readonly disable
> sudo pacman -S glibc linux-api-headers lib32-glibc
> sudo steamos-readonly enable
> ```
>
>The command intentionally does not use `--needed`: some SteamOS images report
>`glibc`, `linux-api-headers`, or `lib32-glibc` as installed while a required
>header is absent. Reinstalling those packages restores the headers. Only run
>`pacman-key --init` and `pacman-key --populate` if Pacman specifically reports
>a keyring or signature error.

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
- Qt6 and Qt6Quick (only needed when building mako-ui)

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

The release packager builds the normal 64-bit application, CLI, UI, and layer,
then builds a second layer with `-m32`. It installs the two layer libraries in
`lib` and `lib32` with architecture-tagged Vulkan manifests. Direct CMake builds
produce one layer for the compiler architecture selected for that build.

### Reusable SteamOS release-build SDK

`scripts/package-local.sh` normally uses the host Qt development installation.
On a Pacman-based SteamOS host where Qt appears installed but its headers or
CMake files are missing, the packager automatically downloads the exact
`qt6-base`, `qt6-declarative`, and `libglvnd` packages selected by Pacman into
`engine/build/cache/native-sdk/`. It extracts and reuses that isolated SDK on later
release builds, without Docker, Podman, root access, or changes to the SteamOS
installation. The first fallback build needs network access; later builds reuse
the cached files.

This fallback applies only to `scripts/package-local.sh`. Manual CMake builds
still use the system development packages. Set `MAKO_NATIVE_SDK_DIR` to place
that SDK somewhere else, or set `MAKO_BUILD_CACHE_ROOT` to relocate all MAKO
build caches together.

### Fast SteamOS development build

For native Steam-game iteration, use the persistent incremental build instead
of the release packager:

```bash
./scripts/build-steamos-dev.sh
```

It builds only the 64-bit Vulkan layer and keeps `build/steamos-dev` between
runs. To retain a second incremental tree for genuine 32-bit games, run:

```bash
./scripts/build-steamos-dev.sh --with-32-bit
```

The 32-bit tree defaults to `build/steamos-dev-32`. Both commands intentionally
skip the CLI, Qt UI, Flatpak extensions, tests, archives, and Decky ZIP.
Subsequent builds compile only changed source and its dependants. When
available, `ccache` is enabled automatically and stored under
`build/cache/ccache`; install it with the rest of the Arch build prerequisites
to retain compiler results across larger rebuilds.

### SteamOS Flatpak development cache

MAKO keeps all reusable build data under `build/cache` by default. The plugin's
`dev:flatpaks` and `dev:e2e` commands retain their isolated Flatpak SDK/runtime
downloads under `build/cache/flatpak` and use `build/work/flatpak` for
self-cleaning staging. Nothing in these locations is installed into `/root` or
the host system. To inspect all repository-local build storage safely, run:

```bash
./scripts/prune-build-cache.sh
```

That command is a dry run. Add `--confirm` only when you want to remove all
reusable Qt, Flatpak, and compiler caches plus disposable work trees. Native
incremental builds, release artifacts, installed MAKO data, and profiles remain
untouched.

To inspect or remove only the Flatpak portion, run:

```bash
./scripts/prune-steamos-flatpak-cache.sh
```

To delete only the Flatpak cache and work directory, add the explicit
confirmation flag:

```bash
./scripts/prune-steamos-flatpak-cache.sh --confirm
```

For safety, both pruners target only the default repo-local locations. If you
set `MAKO_BUILD_CACHE_ROOT`, `MAKO_BUILD_WORK_ROOT`, or a component-specific
override, manage that custom location yourself.

Use `MAKO_BUILD_JOBS=4` to cap parallelism on a memory-constrained Deck, or
`MAKO_BUILD_DIR=/path/to/build` to keep the build tree elsewhere. This is a
native host-test workflow only; use `scripts/package-local.sh` for a
distributable archive and `scripts/package-flatpaks.sh` for Flatpak runtime
bundles.

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

The installed manifest is wrapper-scoped. Start a direct game with `ENABLE_MAKO=1`.
