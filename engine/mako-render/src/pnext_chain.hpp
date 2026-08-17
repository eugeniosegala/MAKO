/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <vulkan/vulkan_core.h>

namespace mako::layer {

    /// Temporarily remove one optional structure from a Vulkan pNext chain.
    ///
    /// Gamescope prepends maintenance1 present-mode nodes advertising its
    /// driver-facing MAILBOX transport. When MAKO deliberately creates the
    /// private ordered SDR transport, forwarding that node beside a FIFO base
    /// mode would describe an inconsistent swapchain. We filter the node from
    /// the lower-facing copy instead of modifying Gamescope's mode array, which
    /// may be immutable. The HDR transport leaves the node and array untouched.
    ///
    /// Gamescope's node is normally the head, so only the caller-owned head
    /// pointer changes. Nested removal exists for defensive chain composition;
    /// its predecessor link is restored exactly on scope exit.
    class ScopedPNextRemoval {
    public:
        ScopedPNextRemoval(const void*& head, const VkStructureType target,
                const bool enabled = true) :
                head(head) {
            if (!enabled)
                return;

            auto* current = reinterpret_cast<const VkBaseInStructure*>(head);
            const VkBaseInStructure* previous = nullptr;
            while (current && current->sType != target) {
                previous = current;
                current = current->pNext;
            }
            if (!current)
                return;

            this->removed = current;
            if (!previous) {
                this->removedFromHead = true;
                this->head = current->pNext;
                return;
            }

            this->predecessor = const_cast<VkBaseOutStructure*>(
                reinterpret_cast<const VkBaseOutStructure*>(previous)
            );
            this->predecessor->pNext = const_cast<VkBaseOutStructure*>(
                reinterpret_cast<const VkBaseOutStructure*>(current->pNext)
            );
        }

        ~ScopedPNextRemoval() {
            if (!this->removed)
                return;
            if (this->removedFromHead) {
                this->head = this->removed;
                return;
            }
            this->predecessor->pNext = const_cast<VkBaseOutStructure*>(
                reinterpret_cast<const VkBaseOutStructure*>(this->removed)
            );
        }

        ScopedPNextRemoval(const ScopedPNextRemoval&) = delete;
        ScopedPNextRemoval& operator=(const ScopedPNextRemoval&) = delete;
        ScopedPNextRemoval(ScopedPNextRemoval&&) = delete;
        ScopedPNextRemoval& operator=(ScopedPNextRemoval&&) = delete;

    private:
        const void*& head;
        const VkBaseInStructure* removed{nullptr};
        VkBaseOutStructure* predecessor{nullptr};
        bool removedFromHead{false};
    };

}
