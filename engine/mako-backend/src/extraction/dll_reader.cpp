/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "dll_reader.hpp"

#include "content_hash.hpp"
#include "mako-common/helpers/errors.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <sys/stat.h>

namespace {
    constexpr uint64_t maximumDllSize = 1024ULL * 1024ULL * 1024ULL;
    constexpr uint32_t peSignature = 0x00004550U;
    constexpr uint16_t dosMagic = 0x5a4dU;
    constexpr uint16_t pe32Magic = 0x010bU;
    constexpr uint16_t pe32PlusMagic = 0x020bU;
    constexpr uint32_t resourceDataType = 10U;
    constexpr uint32_t directoryFlag = 0x80000000U;
    constexpr uint32_t offsetMask = 0x7fffffffU;

    struct FileIdentity {
        std::string normalizedPath;
        uint64_t device{0};
        uint64_t inode{0};
        uint64_t size{0};
        int64_t modifiedSeconds{0};
        int64_t modifiedNanoseconds{0};
        int64_t changedSeconds{0};
        int64_t changedNanoseconds{0};

        [[nodiscard]] bool operator==(const FileIdentity&) const = default;
    };

    struct CachedArchive {
        FileIdentity identity;
        std::shared_ptr<const mako::backend::DllResourceArchive> archive;
    };

    [[nodiscard]] size_t checkedAdd(
            const size_t lhs, const size_t rhs, const std::string& context) {
        if (rhs > std::numeric_limits<size_t>::max() - lhs)
            throw ls::error(context + " overflows the file address space");
        return lhs + rhs;
    }

    [[nodiscard]] size_t checkedMultiply(
            const size_t lhs, const size_t rhs, const std::string& context) {
        if (lhs != 0 && rhs > std::numeric_limits<size_t>::max() / lhs)
            throw ls::error(context + " overflows the file address space");
        return lhs * rhs;
    }

    void requireRange(const size_t offset, const size_t length,
            const size_t limit, const std::string& context) {
        if (offset > limit || length > limit - offset)
            throw ls::error(context + " points outside its containing range");
    }

    template<typename Integer>
    [[nodiscard]] Integer readInteger(
            const std::span<const uint8_t> data, const size_t offset,
            const std::string& context) {
        requireRange(offset, sizeof(Integer), data.size(), context);
        Integer result{};
        std::memcpy(&result, data.data() + offset, sizeof(result));
        return result;
    }

    [[nodiscard]] FileIdentity fileIdentity(
            const std::filesystem::path& path) {
        std::error_code error;
        auto normalized = std::filesystem::weakly_canonical(path, error);
        if (error) {
            error.clear();
            normalized = std::filesystem::absolute(path, error);
            if (error)
                normalized = path;
        }

        struct stat status{};
        if (::stat(path.c_str(), &status) != 0)
            throw ls::error("failed to stat dll file");
        if (!S_ISREG(status.st_mode))
            throw ls::error("dll path is not a regular file");
        if (status.st_size < 0)
            throw ls::error("dll file has an invalid size");
        const auto size = static_cast<uint64_t>(status.st_size);
        if (size == 0 || size > maximumDllSize)
            throw ls::error("dll file size is outside the supported range");

        return {
            .normalizedPath = normalized.string(),
            .device = static_cast<uint64_t>(status.st_dev),
            .inode = static_cast<uint64_t>(status.st_ino),
            .size = size,
            .modifiedSeconds = status.st_mtim.tv_sec,
            .modifiedNanoseconds = status.st_mtim.tv_nsec,
            .changedSeconds = status.st_ctim.tv_sec,
            .changedNanoseconds = status.st_ctim.tv_nsec,
        };
    }

    [[nodiscard]] std::vector<uint8_t> readFile(
            const std::filesystem::path& path,
            const FileIdentity& identity) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
            throw ls::error("failed to open dll file");
        std::vector<uint8_t> data(static_cast<size_t>(identity.size));
        if (!file.read(
                reinterpret_cast<char*>(data.data()),
                static_cast<std::streamsize>(data.size()))) {
            throw ls::error("failed to read dll file");
        }
        if (file.peek() != std::ifstream::traits_type::eof())
            throw ls::error("dll file changed size while it was being read");
        return data;
    }

    struct ResourceRegion {
        size_t fileOffset{0};
        size_t size{0};
        uint32_t virtualAddress{0};
    };

    [[nodiscard]] size_t resourceOffset(
            const ResourceRegion& region, const uint32_t relative,
            const size_t length, const size_t fileSize,
            const std::string& context) {
        requireRange(relative, length, region.size, context);
        const size_t result = checkedAdd(
            region.fileOffset, static_cast<size_t>(relative), context
        );
        requireRange(result, length, fileSize, context);
        return result;
    }

    struct DirectoryCounts {
        uint16_t named{0};
        uint16_t identified{0};
    };

    [[nodiscard]] DirectoryCounts directoryCounts(
            const std::span<const uint8_t> data,
            const ResourceRegion& region, const uint32_t relative,
            const std::string& context) {
        const size_t offset = resourceOffset(
            region, relative, 16U, data.size(), context
        );
        return {
            .named = readInteger<uint16_t>(data, offset + 12U, context),
            .identified = readInteger<uint16_t>(data, offset + 14U, context),
        };
    }

    struct DirectoryEntry {
        uint32_t id{0};
        uint32_t target{0};
    };

    [[nodiscard]] std::vector<DirectoryEntry> directoryEntries(
            const std::span<const uint8_t> data,
            const ResourceRegion& region, const uint32_t relative,
            const std::string& context) {
        const DirectoryCounts counts = directoryCounts(
            data, region, relative, context
        );
        const size_t count = static_cast<size_t>(counts.named) + counts.identified;
        const size_t bytes = checkedMultiply(count, 8U, context);
        const size_t entriesRelative = checkedAdd(relative, 16U, context);
        if (entriesRelative > std::numeric_limits<uint32_t>::max())
            throw ls::error(context + " entry offset is not representable");
        const size_t entriesOffset = resourceOffset(
            region, static_cast<uint32_t>(entriesRelative), bytes,
            data.size(), context
        );
        std::vector<DirectoryEntry> result;
        result.reserve(count);
        for (size_t index = 0; index < count; ++index) {
            const size_t entry = entriesOffset + index * 8U;
            result.push_back({
                .id = readInteger<uint32_t>(data, entry, context),
                .target = readInteger<uint32_t>(data, entry + 4U, context),
            });
        }
        return result;
    }

    [[nodiscard]] ResourceRegion findResourceRegion(
            const std::span<const uint8_t> data) {
        if (readInteger<uint16_t>(data, 0, "DOS header") != dosMagic)
            throw ls::error("dos header magic number is incorrect");
        const int32_t signedPeOffset = readInteger<int32_t>(
            data, 60U, "DOS PE offset"
        );
        if (signedPeOffset < 0)
            throw ls::error("DOS PE offset is negative");
        const size_t peOffset = static_cast<size_t>(signedPeOffset);
        if (readInteger<uint32_t>(data, peOffset, "PE header") != peSignature)
            throw ls::error("pe header signature is incorrect");
        const uint16_t sectionCount = readInteger<uint16_t>(
            data, checkedAdd(peOffset, 6U, "PE section count"),
            "PE section count"
        );
        if (sectionCount == 0)
            throw ls::error("PE file has no sections");
        const uint16_t optionalSize = readInteger<uint16_t>(
            data, checkedAdd(peOffset, 20U, "PE optional header size"),
            "PE optional header size"
        );
        const size_t optionalOffset = checkedAdd(
            peOffset, 24U, "PE optional header"
        );
        requireRange(optionalOffset, optionalSize, data.size(), "PE optional header");
        const uint16_t optionalMagic = readInteger<uint16_t>(
            data, optionalOffset, "PE optional header"
        );
        size_t numberOfDirectoriesOffset = 0U;
        size_t resourceDirectoryOffset = 0U;
        if (optionalMagic == pe32Magic) {
            numberOfDirectoriesOffset = 92U;
            resourceDirectoryOffset = 112U;
        } else if (optionalMagic == pe32PlusMagic) {
            numberOfDirectoriesOffset = 108U;
            resourceDirectoryOffset = 128U;
        } else {
            throw ls::error("PE optional header is neither PE32 nor PE32+");
        }
        if (optionalSize < numberOfDirectoriesOffset + sizeof(uint32_t) ||
                optionalSize < resourceDirectoryOffset + 8U) {
            throw ls::error("PE optional header is too small for resources");
        }
        if (readInteger<uint32_t>(
                data, optionalOffset + numberOfDirectoriesOffset,
                "PE data-directory count") < 3U) {
            throw ls::error("PE optional header does not declare a resource directory");
        }
        const uint32_t resourceRva = readInteger<uint32_t>(
            data, optionalOffset + resourceDirectoryOffset,
            "PE resource directory"
        );
        const uint32_t resourceSize = readInteger<uint32_t>(
            data, optionalOffset + resourceDirectoryOffset + 4U,
            "PE resource directory"
        );
        if (resourceRva == 0 || resourceSize == 0)
            throw ls::error("PE resource directory is empty");

        const size_t sectionTable = checkedAdd(
            optionalOffset, optionalSize, "PE section table"
        );
        const size_t sectionBytes = checkedMultiply(
            sectionCount, 40U, "PE section table"
        );
        requireRange(sectionTable, sectionBytes, data.size(), "PE section table");
        for (size_t index = 0; index < sectionCount; ++index) {
            const size_t section = sectionTable + index * 40U;
            const uint32_t virtualSize = readInteger<uint32_t>(
                data, section + 8U, "PE section"
            );
            const uint32_t virtualAddress = readInteger<uint32_t>(
                data, section + 12U, "PE section"
            );
            const uint32_t rawSize = readInteger<uint32_t>(
                data, section + 16U, "PE section"
            );
            const uint32_t rawOffset = readInteger<uint32_t>(
                data, section + 20U, "PE section"
            );
            const uint64_t virtualEnd = static_cast<uint64_t>(virtualAddress) +
                std::max(virtualSize, rawSize);
            if (resourceRva < virtualAddress || resourceRva >= virtualEnd)
                continue;
            const uint64_t relative =
                static_cast<uint64_t>(resourceRva) - virtualAddress;
            if (relative > rawSize || resourceSize > rawSize - relative)
                throw ls::error("resource directory exceeds its raw PE section");
            const size_t fileOffset = checkedAdd(
                rawOffset, static_cast<size_t>(relative),
                "PE resource directory"
            );
            requireRange(
                fileOffset, resourceSize, data.size(), "PE resource directory"
            );
            return {
                .fileOffset = fileOffset,
                .size = resourceSize,
                .virtualAddress = resourceRva,
            };
        }
        throw ls::error("unable to locate resource section");
    }

    [[nodiscard]] std::unordered_map<uint32_t, std::vector<uint8_t>>
    parseResources(const std::span<const uint8_t> data) {
        const ResourceRegion region = findResourceRegion(data);
        const auto rootEntries = directoryEntries(
            data, region, 0U, "resource root directory"
        );
        std::optional<uint32_t> dataDirectory;
        for (const DirectoryEntry& entry : rootEntries) {
            if ((entry.id & directoryFlag) != 0 || entry.id != resourceDataType)
                continue;
            if ((entry.target & directoryFlag) == 0)
                throw ls::error("RT_RCDATA entry is not a directory");
            if (dataDirectory)
                throw ls::error("PE file contains duplicate RT_RCDATA directories");
            dataDirectory = entry.target & offsetMask;
        }
        if (!dataDirectory)
            throw ls::error("unable to locate RT_RCDATA directory");

        const auto resourceEntries = directoryEntries(
            data, region, *dataDirectory, "RT_RCDATA directory"
        );
        std::unordered_map<uint32_t, std::vector<uint8_t>> resources;
        for (const DirectoryEntry& resourceEntry : resourceEntries) {
            if ((resourceEntry.id & directoryFlag) != 0)
                continue;
            if ((resourceEntry.target & directoryFlag) == 0)
                throw ls::error("RT_RCDATA resource entry is not a directory");
            const uint32_t languageRelative = resourceEntry.target & offsetMask;
            const auto languageEntries = directoryEntries(
                data, region, languageRelative, "resource language directory"
            );
            if (languageEntries.empty())
                throw ls::error("resource language directory is empty");

            const DirectoryEntry* selectedLanguage = nullptr;
            for (const DirectoryEntry& language : languageEntries) {
                if ((language.target & directoryFlag) != 0)
                    continue;
                if (!selectedLanguage || language.id == 0U)
                    selectedLanguage = &language;
                if (language.id == 0U)
                    break;
            }
            if (!selectedLanguage)
                throw ls::error("resource language directory has no data entry");
            const uint32_t dataRelative = selectedLanguage->target & offsetMask;
            const size_t dataEntry = resourceOffset(
                region, dataRelative, 16U, data.size(), "resource data entry"
            );
            const uint32_t payloadRva = readInteger<uint32_t>(
                data, dataEntry, "resource data entry"
            );
            const uint32_t payloadSize = readInteger<uint32_t>(
                data, dataEntry + 4U, "resource data entry"
            );
            if (payloadRva < region.virtualAddress)
                throw ls::error("resource data precedes the resource section");
            const uint64_t payloadRelative =
                static_cast<uint64_t>(payloadRva) - region.virtualAddress;
            if (payloadRelative > region.size ||
                    payloadSize > region.size - payloadRelative) {
                throw ls::error("resource data exceeds the resource section");
            }
            const size_t payloadOffset = resourceOffset(
                region, static_cast<uint32_t>(payloadRelative), payloadSize,
                data.size(), "resource payload"
            );
            std::vector<uint8_t> payload(payloadSize);
            std::copy_n(data.data() + payloadOffset, payloadSize, payload.data());
            if (!resources.emplace(resourceEntry.id, std::move(payload)).second)
                throw ls::error("PE file contains a duplicate numeric resource ID");
        }
        if (resources.empty())
            throw ls::error("RT_RCDATA directory contains no numeric resources");
        return resources;
    }

    [[nodiscard]] std::string layoutFingerprint(
            const std::unordered_map<uint32_t, std::vector<uint8_t>>& resources) {
        std::vector<uint32_t> ids;
        ids.reserve(resources.size());
        for (const auto& [id, payload] : resources) {
            static_cast<void>(payload);
            ids.push_back(id);
        }
        std::ranges::sort(ids);
        std::vector<uint8_t> manifest;
        for (const uint32_t id : ids) {
            const auto& payload = resources.at(id);
            const std::string digest = mako::backend::detail::sha256Hex(payload);
            const uint64_t size = payload.size();
            for (int shift = 0; shift < 32; shift += 8)
                manifest.push_back(static_cast<uint8_t>(id >> shift));
            for (int shift = 0; shift < 64; shift += 8)
                manifest.push_back(static_cast<uint8_t>(size >> shift));
            manifest.insert(manifest.end(), digest.begin(), digest.end());
        }
        return mako::backend::detail::sha256Hex(manifest);
    }

    [[nodiscard]] std::shared_ptr<const mako::backend::DllResourceArchive>
    parseArchive(const std::filesystem::path& path,
            const FileIdentity& before) {
        const std::vector<uint8_t> data = readFile(path, before);
        const FileIdentity after = fileIdentity(path);
        if (!(before == after))
            throw ls::error("dll file changed while it was being inspected");
        auto resources = parseResources(data);
        const std::string layout = layoutFingerprint(resources);
        return std::make_shared<const mako::backend::DllResourceArchive>(
            mako::backend::DllResourceArchive{
                .resources = std::move(resources),
                .fileSha256 = mako::backend::detail::sha256Hex(data),
                .resourceLayoutSha256 = layout,
                .fileSize = before.size,
            }
        );
    }
}

std::shared_ptr<const mako::backend::DllResourceArchive>
mako::backend::loadDllResourceArchive(const std::filesystem::path& dll) {
    const FileIdentity identity = fileIdentity(dll);
    static std::mutex cacheMutex;
    static std::unordered_map<std::string, CachedArchive> cache;
    {
        const std::scoped_lock lock(cacheMutex);
        const auto found = cache.find(identity.normalizedPath);
        if (found != cache.end() && found->second.identity == identity)
            return found->second.archive;
    }

    auto parsed = parseArchive(dll, identity);
    const std::scoped_lock lock(cacheMutex);
    cache.insert_or_assign(
        identity.normalizedPath,
        CachedArchive{.identity = identity, .archive = parsed}
    );
    return parsed;
}

std::unordered_map<uint32_t, std::vector<uint8_t>>
mako::backend::extractResourcesFromDLL(const std::filesystem::path& dll) {
    return loadDllResourceArchive(dll)->resources;
}
