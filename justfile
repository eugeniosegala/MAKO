set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

# List the monorepo commands.
default:
    @just --list

# Install the Decky plugin dependencies from the lockfile.
install-plugin:
    pnpm --dir plugin install --frozen-lockfile

# Build MAKO Renderer and the MAKO Decky plugin.
build: build-engine build-plugin

# Run the engine and plugin test suites.
test: test-engine test-plugin

# Configure and build MAKO Renderer in release mode.
build-engine:
    cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release
    cmake --build engine/build

# Build and run the MAKO Renderer tests.
test-engine: build-engine
    ctest --test-dir engine/build --output-on-failure

# Build the Decky frontend.
build-plugin:
    pnpm --dir plugin run build

# Run the Decky backend tests.
test-plugin:
    pnpm --dir plugin run test

# Build the native MAKO Renderer release archive.
package-engine:
    ./engine/scripts/package-local.sh

# Build the MAKO Renderer Flatpak extension archive.
package-flatpaks:
    ./engine/scripts/package-flatpaks.sh

# Build a complete Decky ZIP from the local engine checkout.
package-plugin:
    pnpm --dir plugin run package:local-engine

# Build a fast native-only, 64-bit Decky development ZIP.
package-plugin-fast:
    pnpm --dir plugin run package:local-engine-fast
