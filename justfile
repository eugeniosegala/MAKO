set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

# List the monorepo commands.
default:
    @just --list

# Install the Decky plugin dependencies from the lockfile.
install-plugin:
    pnpm --dir plugin install --frozen-lockfile

# Enable the tracked Markdown pre-commit hook for this checkout.
install-hooks:
    ./scripts/install-git-hooks.sh

# Format all Markdown without artificial prose wrapping.
format-markdown:
    pnpm --dir plugin run format:markdown

# Check repository Markdown formatting without changing files.
check-markdown-format:
    pnpm --dir plugin run format:markdown:check

# Build MAKO Renderer and the MAKO Decky plugin.
build: build-engine build-plugin

# Run the engine, plugin, and trace-producer test suites.
test: test-engine test-plugin test-trace-producer

# Exercise the private-archive producer without requiring private evidence.
test-trace-producer:
    ./scripts/test-capture-trace.sh

# Configure and build MAKO Renderer in release mode.
build-engine:
    cmake -S engine -B engine/build -DCMAKE_BUILD_TYPE=Release
    cmake --build engine/build

# Build and run the MAKO Renderer tests.
test-engine: build-engine
    ctest --test-dir engine/build --output-on-failure

# Run the portable Renderer policy tests without a Vulkan SDK or GPU.
test-engine-portable:
    cd engine && ./scripts/test-adaptive-scheduler.sh

# Run the portable Renderer policy tests under ASan and UBSan.
test-engine-sanitized:
    cd engine && MAKO_ENABLE_SANITIZERS=ON ./scripts/test-adaptive-scheduler.sh

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

# Run the release gate on a verified one-job SteamOS/AMD runner.
validate-steamos-hardware:
    ./scripts/run-steamos-hardware-validation.sh

# Inspect the scoped caches retained between SteamOS hardware jobs.
inspect-hardware-cache:
    ./scripts/prune-hardware-ci-cache.sh
