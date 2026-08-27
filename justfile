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

# Run the protected-input, engine, plugin, and trace-producer test suites.
test: test-protected-inputs test-engine test-plugin test-trace-producer

# Reject licensed inputs and disguised binary/model/archive payloads from Git.
test-protected-inputs:
    ./scripts/test-protected-inputs.sh

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

# Run MAKO Gym's feature suite when its sibling checkout is available; pass --filter for focused development.
test-engine-gym *args:
    ./engine/scripts/run-mako-gym.sh {{args}}

# Explicit feature-suite alias for focused development commands.
test-engine-gym-feature *args:
    ./engine/scripts/run-mako-gym.sh --suite vulkan {{args}}

# Run a selected MAKO Gym suite and fail when its sibling checkout is unavailable; vulkan remains the default.
test-engine-gym-required *args:
    ./engine/scripts/run-mako-gym.sh --require {{args}}

# Run selected procedural render-quality rows; no --filter runs all 74 cases.
test-engine-gym-quality *args:
    ./engine/scripts/run-mako-gym.sh --suite quality {{args}}

# Require byte-identical output across independent executions of curated quality sentinels.
test-engine-gym-repeatability *args:
    ./engine/scripts/run-mako-gym.sh --suite repeatability {{args}}

# Run repeated warmed LSFG throughput samples with practical budgets and variance checks.
test-engine-gym-performance *args:
    ./engine/scripts/run-mako-gym.sh --suite performance {{args}}

# Measure each production spatial scaler with Vulkan timestamp queries.
test-engine-gym-spatial-performance *args:
    ./engine/scripts/run-mako-gym.sh --suite spatial-performance {{args}}

# Compare MAKO-idle and active live presentation overhead on the same workload.
test-engine-gym-runtime-overhead *args:
    ./engine/scripts/run-mako-gym.sh --suite runtime-overhead {{args}}

# Run selected production GPU paths under Khronos synchronization validation.
test-engine-gym-sync-validation *args:
    ./engine/scripts/run-mako-gym.sh --suite sync-validation {{args}}

# Run selected scripted runtime-recovery rows; no --filter runs all 30 default cases.
test-engine-gym-recovery *args:
    ./engine/scripts/run-mako-gym.sh --suite recovery {{args}}

# Run the release-only real Gamescope compositor + WSI + MAKO end-to-end lane.
test-engine-gym-gamescope-e2e *args:
    ./engine/scripts/run-mako-gym.sh --suite gamescope-e2e {{args}}

# Run the release-only D3D11/DXVK and D3D12/VKD3D-Proton end-to-end lane.
test-engine-gym-proton-e2e *args:
    ./engine/scripts/run-mako-gym.sh --suite proton-e2e {{args}}

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
