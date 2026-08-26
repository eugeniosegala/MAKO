/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <array>
#include <cstddef>
#include <cstring>

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

    /// Build a lower-facing VkPresentInfoKHR pNext chain without modifying
    /// any caller-owned input node. Only the prefix through the final removed
    /// node needs to be copied; the untouched suffix remains shared and
    /// immutable. An unknown prefix node fails closed because its full size is
    /// unknowable and copying only VkBaseInStructure would truncate it.
    class FilteredPresentPNextChain {
    public:
        FilteredPresentPNextChain(const void* original,
                const bool removePresentMode,
                const bool removePresentRegions) : filteredHead(original) {
            if (!removePresentMode && !removePresentRegions)
                return;

            const auto selectedForRemoval = [=](
                    const VkStructureType type) {
                return (removePresentMode && type ==
                        VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT) ||
                    (removePresentRegions && type ==
                        VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR);
            };

            const VkBaseInStructure* finalRemoved = nullptr;
            for (auto* node = reinterpret_cast<const VkBaseInStructure*>(
                    original); node; node = node->pNext) {
                if (selectedForRemoval(node->sType))
                    finalRemoved = node;
            }
            if (!finalRemoved)
                return;

            for (auto* node = reinterpret_cast<const VkBaseInStructure*>(
                    original); node; node = node->pNext) {
                if (!selectedForRemoval(node->sType)) {
                    const size_t byteSize = presentStructureSize(node->sType);
                    if (byteSize == 0 ||
                            this->ownedNodeCount == maximumOwnedNodes) {
                        this->validChain = false;
                        this->unsupportedType = node->sType;
                        this->filteredHead = original;
                        this->ownedNodeCount = 0;
                        return;
                    }
                    std::memcpy(
                        this->ownedNodes.at(this->ownedNodeCount).data(),
                        node, byteSize
                    );
                    this->ownedNodeCount++;
                }
                if (node == finalRemoved) {
                    this->filteredHead = node->pNext;
                    break;
                }
            }

            const void* next = this->filteredHead;
            for (size_t index = this->ownedNodeCount; index > 0; --index) {
                auto* base = reinterpret_cast<VkBaseOutStructure*>(
                    this->ownedNodes.at(index - 1).data()
                );
                base->pNext = const_cast<VkBaseOutStructure*>(
                    reinterpret_cast<const VkBaseOutStructure*>(next)
                );
                next = base;
            }
            this->filteredHead = next;
        }

        [[nodiscard]] bool valid() const {
            return this->validChain;
        }
        [[nodiscard]] const void* head() const {
            return this->filteredHead;
        }
        [[nodiscard]] VkStructureType unsupportedStructureType() const {
            return this->unsupportedType;
        }

    private:
        static constexpr size_t maximumOwnedNodes = 16;
        static constexpr size_t maximumNodeBytes = 128;

        struct alignas(std::max_align_t) NodeStorage {
            [[nodiscard]] void* data() { return this->bytes.data(); }
            std::array<std::byte, maximumNodeBytes> bytes;
        };

        static_assert(sizeof(VkDeviceGroupPresentInfoKHR) <= maximumNodeBytes);
        static_assert(sizeof(VkDisplayPresentInfoKHR) <= maximumNodeBytes);
        static_assert(sizeof(VkPresentIdKHR) <= maximumNodeBytes);
        static_assert(sizeof(VkPresentRegionsKHR) <= maximumNodeBytes);
        static_assert(sizeof(VkPresentTimesInfoGOOGLE) <= maximumNodeBytes);
        static_assert(sizeof(VkSwapchainPresentFenceInfoEXT) <= maximumNodeBytes);
        static_assert(sizeof(VkSwapchainPresentModeInfoEXT) <= maximumNodeBytes);
#if defined(VK_EXT_frame_boundary)
        static_assert(sizeof(VkFrameBoundaryEXT) <= maximumNodeBytes);
#endif
#if defined(VK_KHR_present_id2)
        static_assert(sizeof(VkPresentId2KHR) <= maximumNodeBytes);
#endif
#if defined(VK_ENABLE_BETA_EXTENSIONS) && \
    defined(VK_NV_present_metering)
        static_assert(sizeof(VkSetPresentConfigNV) <= maximumNodeBytes);
#endif
#if defined(VK_ARM_tensors)
        static_assert(sizeof(VkFrameBoundaryTensorsARM) <= maximumNodeBytes);
#endif
#if defined(VK_USE_PLATFORM_GGP)
        static_assert(sizeof(VkPresentFrameTokenGGP) <= maximumNodeBytes);
#endif

        [[nodiscard]] static size_t presentStructureSize(
                const VkStructureType type) noexcept {
            switch (type) {
                case VK_STRUCTURE_TYPE_DEVICE_GROUP_PRESENT_INFO_KHR:
                    return sizeof(VkDeviceGroupPresentInfoKHR);
                case VK_STRUCTURE_TYPE_DISPLAY_PRESENT_INFO_KHR:
                    return sizeof(VkDisplayPresentInfoKHR);
                case VK_STRUCTURE_TYPE_PRESENT_ID_KHR:
                    return sizeof(VkPresentIdKHR);
                case VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR:
                    return sizeof(VkPresentRegionsKHR);
                case VK_STRUCTURE_TYPE_PRESENT_TIMES_INFO_GOOGLE:
                    return sizeof(VkPresentTimesInfoGOOGLE);
                case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT:
                    return sizeof(VkSwapchainPresentFenceInfoEXT);
                case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT:
                    return sizeof(VkSwapchainPresentModeInfoEXT);
#if defined(VK_EXT_frame_boundary)
                case VK_STRUCTURE_TYPE_FRAME_BOUNDARY_EXT:
                    return sizeof(VkFrameBoundaryEXT);
#endif
#if defined(VK_KHR_present_id2)
                case VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR:
                    return sizeof(VkPresentId2KHR);
#endif
#if defined(VK_ENABLE_BETA_EXTENSIONS) && \
    defined(VK_NV_present_metering)
                case VK_STRUCTURE_TYPE_SET_PRESENT_CONFIG_NV:
                    return sizeof(VkSetPresentConfigNV);
#endif
#if defined(VK_ARM_tensors)
                case VK_STRUCTURE_TYPE_FRAME_BOUNDARY_TENSORS_ARM:
                    return sizeof(VkFrameBoundaryTensorsARM);
#endif
#if defined(VK_USE_PLATFORM_GGP)
                case VK_STRUCTURE_TYPE_PRESENT_FRAME_TOKEN_GGP:
                    return sizeof(VkPresentFrameTokenGGP);
#endif
                default:
                    return 0;
            }
        }

        const void* filteredHead{nullptr};
        bool validChain{true};
        VkStructureType unsupportedType{VK_STRUCTURE_TYPE_MAX_ENUM};
        size_t ownedNodeCount{0};
        std::array<NodeStorage, maximumOwnedNodes> ownedNodes;
    };

}
