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
available, `ccache` is enabled automatically; install it with the rest of the
Arch build prerequisites to retain compiler results across larger rebuilds.

### SteamOS Flatpak development cache

The plugin's `dev:flatpaks` and `dev:e2e` commands retain their isolated Flatpak
SDK/runtime downloads under `build/steamos-flatpak-cache` in the engine tree and
use `build/steamos-flatpak-tmp` for temporary staging. The cache persists until
you remove it, so later Flatpak development builds avoid downloading the same
runtime dependencies. To inspect it safely, run:

```bash
./scripts/prune-steamos-flatpak-cache.sh
```

That command is a dry run. To delete only those two development-cache
directories, with native builds and installed MAKO artifacts preserved, add
the explicit confirmation flag:

```bash
./scripts/prune-steamos-flatpak-cache.sh --confirm
```

For safety, this pruner targets only the default repo-local locations. If you
set a custom Flatpak cache or temporary-directory environment variable, manage
that custom location yourself.

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

Optionally, you can checkout a specific release tag:
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

The installed manifest is wrapper-scoped. Start a direct game with
`ENABLE_MAKO=1`; if either public LSFG implementation is installed alongside it, also set
`DISABLE_LSFGVK=1 DISABLE_LSFG=1` for that game. MAKO Decky manages these guards automatically.
