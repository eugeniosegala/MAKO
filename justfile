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

# Update the README gallery from this branch's commit and co-author history.
update-contributors:
    python3 scripts/update-contributors.py

# Check that the README gallery matches this branch's contributor history.
check-contributors:
    python3 scripts/update-contributors.py --check

# Generate Renderer and Decky launcher lists from the documented shared registry.
generate-launcher-exclusions:
    python3 scripts/generate-launcher-exclusions.py

# Check launcher exclusion bindings without changing files.
check-launcher-exclusions:
    python3 scripts/generate-launcher-exclusions.py --check

# Generate Flatpak's build-only Vulkan-Headers module from the shared pin.
generate-flatpak-headers:
    python3 engine/scripts/generate-flatpak-vulkan-headers.py

# Reject a stale Flatpak header dependency without rewriting it.
check-flatpak-headers:
    python3 engine/scripts/generate-flatpak-vulkan-headers.py --check

# Generate Flatpak's runtime vkBasalt module from the pinned MAKO fork release.
generate-vkbasalt-release:
    python3 engine/scripts/manage-vkbasalt-release.py --check --generate-flatpak-module engine/dist/flatpak/mako-render/vkbasalt-module.json

# Reject an invalid vkBasalt pin or stale generated Flatpak module.
check-vkbasalt-release:
    python3 engine/scripts/manage-vkbasalt-release.py --check --check-flatpak-module engine/dist/flatpak/mako-render/vkbasalt-module.json

# Sync the Arch package recipe to the checksum-pinned Renderer release.
sync-arch-package:
    python3 engine/dist/arch/sync-release-pin.py plugin/package.json engine/dist/arch/PKGBUILD

# Reject a stale or unsafe Arch package recipe and lifecycle script.
check-arch-package:
    engine/dist/arch/check-release-pin.sh
    engine/dist/arch/test-package-contract.sh

# Build MAKO Renderer and the MAKO Decky plugin.
build: build-engine build-plugin

# Run the protected-input, release-candidate, engine, plugin, and trace-producer tests.
test: test-protected-inputs test-release-candidate test-engine test-plugin test-trace-producer

# Reject licensed inputs and disguised binary/model/archive payloads from Git.
test-protected-inputs:
    ./scripts/test-protected-inputs.sh

# Verify the local release-candidate checker with complete and incomplete ZIP fixtures.
test-release-candidate:
    ./scripts/test-release-candidate.sh

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

# Run the portable Renderer policy tests with Vulkan headers but without a Vulkan device.
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

# Exercise Fixed/Steady/Fractional pacing across synthetic Gamescope VRR feedback.
test-engine-gym-pacing *args:
    ./engine/scripts/run-mako-gym.sh --suite pacing {{args}}

# Run selected scripted runtime-recovery rows; omit --filter to run the default matrix.
test-engine-gym-recovery *args:
    ./engine/scripts/run-mako-gym.sh --suite recovery {{args}}

# Exercise full-cover overlay pause/throttle recovery after workload-proven source return.
test-engine-gym-external-recovery *args:
    ./engine/scripts/run-mako-gym.sh --suite external-recovery {{args}}

# Run the high-cost real Gamescope compositor + WSI + MAKO end-to-end lane.
test-engine-gym-gamescope-e2e *args:
    ./engine/scripts/run-mako-gym.sh --suite gamescope-e2e {{args}}

# Run native Vulkan and Steam Runtime/Proton through the combined Renderer outside Gamescope.
test-engine-gym-direct-desktop-e2e *args:
    ./engine/scripts/run-mako-gym.sh --suite direct-desktop-e2e {{args}}

# Run focused private-resource memory and sustained thermal/performance health soaks.
test-engine-gym-sustained-health *args:
    ./engine/scripts/run-mako-gym.sh --suite sustained-health {{args}}

# Run the high-cost D3D11/DXVK and D3D12/VKD3D-Proton end-to-end lane.
test-engine-gym-proton-e2e *args:
    ./engine/scripts/run-mako-gym.sh --suite proton-e2e {{args}}

# Repeat curated translation sentinels across installed Proton runtime families.
test-engine-gym-proton-compatibility *args:
    ./engine/scripts/run-mako-gym.sh --suite proton-compatibility {{args}}

# Qualify a bounded set of Renderer cases under mandatory resource constraints.
test-engine-gym-constraints *args:
    ./engine/scripts/run-mako-gym.sh --suite constraints {{args}}

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

# Build the complete release-shaped Renderer artifact set without publishing.
package-renderer-local-release:
    ./engine/scripts/package-local-release.sh

# Build a complete Decky ZIP from the local engine checkout.
package-plugin:
    pnpm --dir plugin run package:local-engine

# Build a fast native-only, 64-bit Decky development ZIP.
package-plugin-fast:
    pnpm --dir plugin run package:local-engine-fast

# Check an already-built complete local Decky ZIP against the pushed source commit.
check-release-candidate archive:
    ./scripts/check-release-candidate.sh {{archive}}
