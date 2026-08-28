/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/vulkan/device_memory_accounting.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
    void expect(const bool condition, const std::string_view message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main() {
    vk::DeviceMemoryAccounting accounting;

    accounting.recordAllocation(vk::DeviceMemoryKind::Internal, 1024);
    accounting.recordAllocation(vk::DeviceMemoryKind::Internal, 4096);
    accounting.recordAllocation(vk::DeviceMemoryKind::Imported, 8192);
    accounting.recordAllocation(vk::DeviceMemoryKind::Exported, 2048);

    auto snapshot = accounting.snapshot();
    expect(snapshot.internal.bytes == 5120 && snapshot.internal.allocations == 2,
        "internal allocations must be totaled independently");
    expect(snapshot.imported.bytes == 8192 && snapshot.imported.allocations == 1,
        "imported mappings must not be folded into MAKO-owned memory");
    expect(snapshot.exported.bytes == 2048 && snapshot.exported.allocations == 1,
        "exported allocations must retain their own category");
    expect(snapshot.peakInternal.bytes == 5120 &&
            snapshot.peakInternal.allocations == 2,
        "internal high-water marks must record concurrent live allocations");

    accounting.recordFree(vk::DeviceMemoryKind::Internal, 1024);
    accounting.recordFree(vk::DeviceMemoryKind::Imported, 8192);
    snapshot = accounting.snapshot();
    expect(snapshot.internal.bytes == 4096 && snapshot.internal.allocations == 1,
        "freeing one internal handle must reduce only current internal totals");
    expect(snapshot.imported.bytes == 0 && snapshot.imported.allocations == 0,
        "freeing imported memory must clear only the imported mapping totals");
    expect(snapshot.peakInternal.bytes == 5120 &&
            snapshot.peakInternal.allocations == 2,
        "freeing handles must not lower the high-water mark");

    accounting.recordFree(vk::DeviceMemoryKind::Internal, 4096);
    accounting.recordFree(vk::DeviceMemoryKind::Exported, 2048);
    snapshot = accounting.snapshot();
    expect(snapshot.internal.bytes == 0 && snapshot.internal.allocations == 0 &&
            snapshot.exported.bytes == 0 && snapshot.exported.allocations == 0,
        "balanced allocation lifetimes must return current totals to zero");

    std::cout << "device memory accounting tests passed\n";
    return 0;
}
