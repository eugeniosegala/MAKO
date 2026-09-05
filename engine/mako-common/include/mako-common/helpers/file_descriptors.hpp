/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cassert>
#include <cstddef>
#include <span>

#include <unistd.h>

namespace ls {
    /// Own descriptors in caller-owned storage until they are handed off.
    /// The storage must outlive this scope and must not move or resize. Slots
    /// may be filled by an exporter while guarded; initialize empty slots to
    /// -1. take() hands one descriptor to a consuming operation, and release()
    /// hands the entire remaining batch to another owner. Neither allocates.
    class FileDescriptorScope {
    public:
        explicit FileDescriptorScope(std::span<const int> descriptors) noexcept
            : descriptors(descriptors) {}

        FileDescriptorScope(const FileDescriptorScope&) = delete;
        FileDescriptorScope& operator=(const FileDescriptorScope&) = delete;

        ~FileDescriptorScope() {
            for (size_t index = next; index < descriptors.size(); ++index) {
                if (descriptors[index] >= 0)
                    static_cast<void>(::close(descriptors[index]));
            }
        }

        [[nodiscard]] size_t size() const noexcept { return descriptors.size(); }

        [[nodiscard]] int take() noexcept {
            assert(next < descriptors.size());
            return descriptors[next++];
        }

        void release() noexcept { next = descriptors.size(); }

    private:
        std::span<const int> descriptors;
        size_t next{0};
    };
}
