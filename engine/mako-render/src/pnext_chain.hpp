/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <array>
#include <cstddef>
#include <cstring>

#include <vulkan/vulkan_core.h>

namespace mako::layer {

    /// Build a lower-facing VkSwapchainCreateInfoKHR pNext chain without
    /// modifying any caller-owned input node. Gamescope normally puts the
    /// maintenance1 present-mode node at the head, but a legal caller may put
    /// known swapchain-create structures before it in immutable storage. Copy
    /// that prefix and share the untouched suffix. An unknown prefix fails
    /// closed because copying only VkBaseInStructure would truncate it.
    class FilteredSwapchainCreatePNextChain {
    public:
        FilteredSwapchainCreatePNextChain(const void* original,
                const VkStructureType target, const bool enabled = true) :
                filteredHead(original) {
            if (!enabled)
                return;

            const VkBaseInStructure* removed = nullptr;
            for (auto* node = reinterpret_cast<const VkBaseInStructure*>(
                    original); node; node = node->pNext) {
                if (node->sType == target) {
                    removed = node;
                    break;
                }
            }
            if (!removed)
                return;

            for (auto* node = reinterpret_cast<const VkBaseInStructure*>(
                    original); node != removed; node = node->pNext) {
                const size_t byteSize = swapchainCreateStructureSize(
                    node->sType
                );
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

            const void* next = removed->pNext;
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

        static_assert(sizeof(VkDeviceGroupSwapchainCreateInfoKHR) <=
            maximumNodeBytes);
        static_assert(sizeof(VkImageFormatListCreateInfo) <= maximumNodeBytes);
        static_assert(sizeof(VkImageSwapchainCreateInfoKHR) <= maximumNodeBytes);
        static_assert(sizeof(VkSwapchainCounterCreateInfoEXT) <=
            maximumNodeBytes);
        static_assert(sizeof(VkSwapchainDisplayNativeHdrCreateInfoAMD) <=
            maximumNodeBytes);
        static_assert(sizeof(VkImageCompressionControlEXT) <= maximumNodeBytes);
        static_assert(sizeof(VkSwapchainPresentScalingCreateInfoKHR) <=
            maximumNodeBytes);
#if defined(VK_NV_present_barrier)
        static_assert(sizeof(VkSwapchainPresentBarrierCreateInfoNV) <=
            maximumNodeBytes);
#endif
#if defined(VK_NV_low_latency2)
        static_assert(sizeof(VkSwapchainLatencyCreateInfoNV) <=
            maximumNodeBytes);
#endif

        [[nodiscard]] static size_t swapchainCreateStructureSize(
                const VkStructureType type) noexcept {
            switch (type) {
                case VK_STRUCTURE_TYPE_DEVICE_GROUP_SWAPCHAIN_CREATE_INFO_KHR:
                    return sizeof(VkDeviceGroupSwapchainCreateInfoKHR);
                case VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO:
                    return sizeof(VkImageFormatListCreateInfo);
                case VK_STRUCTURE_TYPE_IMAGE_SWAPCHAIN_CREATE_INFO_KHR:
                    return sizeof(VkImageSwapchainCreateInfoKHR);
                case VK_STRUCTURE_TYPE_SWAPCHAIN_COUNTER_CREATE_INFO_EXT:
                    return sizeof(VkSwapchainCounterCreateInfoEXT);
                case VK_STRUCTURE_TYPE_SWAPCHAIN_DISPLAY_NATIVE_HDR_CREATE_INFO_AMD:
                    return sizeof(VkSwapchainDisplayNativeHdrCreateInfoAMD);
                case VK_STRUCTURE_TYPE_IMAGE_COMPRESSION_CONTROL_EXT:
                    return sizeof(VkImageCompressionControlEXT);
                case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_SCALING_CREATE_INFO_KHR:
                    return sizeof(VkSwapchainPresentScalingCreateInfoKHR);
#if defined(VK_NV_present_barrier)
                case VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_BARRIER_CREATE_INFO_NV:
                    return sizeof(VkSwapchainPresentBarrierCreateInfoNV);
#endif
#if defined(VK_NV_low_latency2)
                case VK_STRUCTURE_TYPE_SWAPCHAIN_LATENCY_CREATE_INFO_NV:
                    return sizeof(VkSwapchainLatencyCreateInfoNV);
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
