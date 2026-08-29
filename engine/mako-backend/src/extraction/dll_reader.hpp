/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mako::backend {

    /// Parsed, process-local view of one user-owned DLL. Fingerprints identify
    /// content for caching and diagnostics; they are never compatibility
    /// allowlists and no resource payload is persisted.
    struct DllResourceArchive {
        std::unordered_map<uint32_t, std::vector<uint8_t>> resources;
        std::string fileSha256;
        std::string resourceLayoutSha256;
        uint64_t fileSize{0};
    };

    /// Parse and fingerprint a DLL, reusing a process-local result only while
    /// its path, inode, size, modification time, and change time are stable.
    [[nodiscard]] std::shared_ptr<const DllResourceArchive>
    loadDllResourceArchive(const std::filesystem::path& dll);

    /// extract all resources from a DLL file
    /// @param dll path to the DLL file
    /// @return map of resource IDs to their binary data
    /// @throws ls::error on various failure points
    std::unordered_map<uint32_t, std::vector<uint8_t>> extractResourcesFromDLL(
        const std::filesystem::path& dll);

}
