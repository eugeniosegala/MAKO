/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <array>
#include <cstddef>
#include <cstring>
#include <vulkan/vk_layer.h>

namespace vk {

    /// Read the enabled feature, rather than treating physical support as proof
    /// that an application-owned logical device may execute FP16 arithmetic.
    [[nodiscard]] inline const VkBool32* shaderFloat16Feature(const void* chain) {
        for (auto* node = static_cast<const VkBaseInStructure*>(chain);
                node; node = node->pNext) {
            if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES)
                return &reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(node)->shaderFloat16;
            if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES)
                return &reinterpret_cast<const VkPhysicalDeviceShaderFloat16Int8Features*>(node)->shaderFloat16;
        }
        return nullptr;
    }

    [[nodiscard]] inline bool shaderFloat16Enabled(const void* chain) {
        const auto* feature = shaderFloat16Feature(chain);
        return feature && *feature == VK_TRUE;
    }

    /// Enable only a supported request, preserving the application's other
    /// feature bits and avoiding duplicate promoted/extension structures.
    /// Copy the prefix through an existing feature: input storage may be read
    /// only. Unknown prefixes retain the application's original request rather
    /// than truncating a structure or changing its feature bits.
    class ShaderFloat16FeatureRequest {
    public:
        ShaderFloat16FeatureRequest(VkDeviceCreateInfo& info, const bool enable)
            : info(info), originalChain(info.pNext) {
            if (!enable || shaderFloat16Enabled(info.pNext))
                return;
            const auto* target = static_cast<const VkBaseInStructure*>(info.pNext);
            while (target && target->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES &&
                    target->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES)
                target = target->pNext;
            if (!target) {
                this->features.pNext = const_cast<void*>(info.pNext);
                info.pNext = &this->features;
                return;
            }
            size_t count{};
            for (auto* node = static_cast<const VkBaseInStructure*>(info.pNext);
                    node; node = node->pNext) {
                const auto size = structureSize(node->sType);
                if (!size || size > maximumNodeBytes || count == this->ownedNodes.size())
                    return;
                std::memcpy(this->ownedNodes[count++].bytes.data(), node, size);
                if (node == target)
                    break;
            }
            const void* next = target->pNext;
            for (size_t index = count; index > 0; --index) {
                auto* node = reinterpret_cast<VkBaseOutStructure*>(
                    this->ownedNodes[index - 1].bytes.data());
                node->pNext = const_cast<VkBaseOutStructure*>(
                    static_cast<const VkBaseOutStructure*>(next));
                if (index == count) {
                    if (target->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES)
                        reinterpret_cast<VkPhysicalDeviceVulkan12Features*>(node)->shaderFloat16 = VK_TRUE;
                    else
                        reinterpret_cast<VkPhysicalDeviceShaderFloat16Int8Features*>(node)->shaderFloat16 = VK_TRUE;
                }
                next = node;
            }
            info.pNext = next;
        }
        ~ShaderFloat16FeatureRequest() {
            this->info.pNext = this->originalChain;
        }
        ShaderFloat16FeatureRequest(const ShaderFloat16FeatureRequest&) = delete;
        ShaderFloat16FeatureRequest& operator=(const ShaderFloat16FeatureRequest&) = delete;
    private:
        VkDeviceCreateInfo& info;
        const void* originalChain;
        static constexpr size_t maximumNodeBytes = 512;
        struct alignas(std::max_align_t) NodeStorage {
            std::array<std::byte, maximumNodeBytes> bytes;
        };
        std::array<NodeStorage, 16> ownedNodes;
        [[nodiscard]] static size_t structureSize(const VkStructureType type) {
            switch (type) {
                case VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO: return sizeof(VkLayerDeviceCreateInfo);
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2: return sizeof(VkPhysicalDeviceFeatures2);
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES: return sizeof(VkPhysicalDeviceVulkan11Features);
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES: return sizeof(VkPhysicalDeviceVulkan12Features);
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES: return sizeof(VkPhysicalDeviceVulkan13Features);
#if defined(VK_VERSION_1_4)
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES: return sizeof(VkPhysicalDeviceVulkan14Features);
#endif
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES: return sizeof(VkPhysicalDeviceShaderFloat16Int8Features);
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES: return sizeof(VkPhysicalDeviceTimelineSemaphoreFeatures);
                case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES: return sizeof(VkPhysicalDeviceBufferDeviceAddressFeatures);
                case VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO: return sizeof(VkDeviceGroupDeviceCreateInfo);
                default: return 0;
            }
        }
        VkPhysicalDeviceShaderFloat16Int8Features features{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES,
            .shaderFloat16 = VK_TRUE,
        };
    };

    /// Optional Vulkan features MAKO may enable without changing correctness.
    struct OptionalDeviceFeatures {
        bool robustImageAccess2{};
    };

    /// Select optional features only when both the extension and feature bit exist.
    [[nodiscard]] constexpr OptionalDeviceFeatures selectOptionalDeviceFeatures(
            const bool robustness2Extension,
            const bool robustImageAccess2) {
        return {
            .robustImageAccess2 = robustness2Extension && robustImageAccess2,
        };
    }
}
