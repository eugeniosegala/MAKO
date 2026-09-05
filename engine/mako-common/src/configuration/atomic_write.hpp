/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace ls::detail {

    // Configuration writes happen during creation or explicit edits, never for frame pacing.
    // Keep the old file intact until every byte has reached the staged file.
    inline void writeConfigurationAtomically(
            const std::filesystem::path& requestedPath,
            const std::string_view content) {
        auto path = requestedPath;
        for (size_t links = 0; std::filesystem::is_symlink(path); ++links) {
            if (links == 40)
                throw std::system_error(ELOOP, std::generic_category(), requestedPath.string());
            const auto target = std::filesystem::read_symlink(path);
            path = target.is_absolute() ? target : path.parent_path() / target;
        }
        const auto directory = path.has_parent_path() ? path.parent_path()
            : std::filesystem::path{"."};
        std::filesystem::create_directories(directory);

        mode_t mode = 0600;
        struct stat existing {};
        if (::stat(path.c_str(), &existing) == 0) {
            if (!S_ISREG(existing.st_mode) || (existing.st_mode & 0222) == 0)
                throw std::system_error(EACCES, std::generic_category(),
                    "configuration is not a writable regular file: " + path.string());
            mode = existing.st_mode & 0777;
        } else if (errno != ENOENT) {
            throw std::system_error(errno, std::generic_category(), path.string());
        }

        struct Staging {
            std::string path;
            int descriptor{-1};
            ~Staging() {
                if (descriptor >= 0) {
                    ::close(descriptor);
                    ::unlink(path.c_str());
                }
            }
        } staging{(directory / ("." + path.filename().string() + ".XXXXXX")).string()};
        staging.descriptor = ::mkstemp(staging.path.data());
        if (staging.descriptor < 0)
            throw std::system_error(errno, std::generic_category(), path.string());
        if (::fcntl(staging.descriptor, F_SETFD, FD_CLOEXEC) != 0)
            throw std::system_error(errno, std::generic_category(), path.string());
        if (mode != 0600 && ::fchmod(staging.descriptor, mode) != 0)
            throw std::system_error(errno, std::generic_category(), path.string());

        size_t written = 0;
        while (written < content.size()) {
            const auto count = ::write(staging.descriptor,
                content.data() + written, content.size() - written);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                throw std::system_error(count < 0 ? errno : EIO,
                    std::generic_category(), path.string());
            written += static_cast<size_t>(count);
        }
        if (::fsync(staging.descriptor) != 0 ||
                ::rename(staging.path.c_str(), path.c_str()) != 0)
            throw std::system_error(errno, std::generic_category(), path.string());
    }

}
