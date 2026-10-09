/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mako-common/vulkan/descriptor_pool.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {
    template<typename T> T handle() {
        if constexpr (std::is_pointer_v<T>) return reinterpret_cast<T>(1);
        else return static_cast<T>(1);
    }
    void require(bool value, const char* message) {
        if (!value) throw std::runtime_error(message);
    }
    std::array<uint32_t, 4> observed{};
    uint32_t entries{}, sets{}, destroyed{};
    bool fail{};
    vk::Vulkan makeVulkan() {
        vk::VulkanInstanceFuncs fi{};
        fi.GetPhysicalDeviceQueueFamilyProperties = [](VkPhysicalDevice, uint32_t* count, VkQueueFamilyProperties* properties) {
            *count = 1; if (properties) properties[0].queueFlags = VK_QUEUE_COMPUTE_BIT;
        };
        vk::VulkanDeviceFuncs df{};
        df.GetDeviceQueue = [](VkDevice, uint32_t, uint32_t, VkQueue* queue) { *queue = handle<VkQueue>(); };
        df.CreateCommandPool = [](VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool* pool) {
            *pool = handle<VkCommandPool>(); return VK_SUCCESS;
        };
        df.DestroyCommandPool = [](VkDevice, VkCommandPool, const VkAllocationCallbacks*) {};
        df.CreatePipelineCache = [](VkDevice, const VkPipelineCacheCreateInfo*, const VkAllocationCallbacks*, VkPipelineCache* cache) {
            *cache = handle<VkPipelineCache>(); return VK_SUCCESS;
        };
        df.DestroyPipelineCache = [](VkDevice, VkPipelineCache, const VkAllocationCallbacks*) {};
        df.CreateDescriptorPool = [](VkDevice, const VkDescriptorPoolCreateInfo* info, const VkAllocationCallbacks*, VkDescriptorPool* pool) {
            observed = {}; entries = info->poolSizeCount; sets = info->maxSets;
            for (uint32_t i = 0; i < entries; ++i) {
                const auto& size = info->pPoolSizes[i];
                require(size.descriptorCount > 0, "empty descriptor type passed to Vulkan");
                const auto slot = size.type == VK_DESCRIPTOR_TYPE_SAMPLER ? 0 :
                    size.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ? 1 :
                    size.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ? 2 : 3;
                require(observed[slot] == 0, "duplicate descriptor type");
                observed[slot] = size.descriptorCount;
            }
            if (fail) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
            *pool = handle<VkDescriptorPool>(); return VK_SUCCESS;
        };
        df.DestroyDescriptorPool = [](VkDevice, VkDescriptorPool, const VkAllocationCallbacks*) { ++destroyed; };
        return {handle<VkInstance>(), handle<VkDevice>(), handle<VkPhysicalDevice>(), fi, df, 0, true};
    }
}
int main() {
    try {
        const auto vk = makeVulkan();
        // Every sparse combination, including conversion-only and empty-layout
        // sets, must preserve requested capacity without zero-sized entries.
        for (unsigned mask = 0; mask < 16; ++mask) {
            const vk::Limits limits{
                .sets = 7, .uniform_buffers = (mask & 8) ? 4U : 0U,
                .samplers = (mask & 1) ? 1U : 0U,
                .sampled_images = (mask & 2) ? 2U : 0U,
                .storage_images = (mask & 4) ? 3U : 0U,
            };
            const auto before = destroyed;
            {
                const vk::DescriptorPool pool(vk, limits);
                require(sets == 7 && observed == std::array{
                    limits.samplers, limits.sampled_images, limits.storage_images, limits.uniform_buffers},
                    "pool capacities changed");
                unsigned expected = 0;
                for (auto count : observed) expected += count != 0;
                require(entries == expected, "absent descriptor types were retained");
            }
            require(destroyed == before + 1, "pool was not released exactly once");
        }
        const auto before = destroyed;
        fail = true;
        bool threw = false;
        try { const vk::DescriptorPool pool(vk, {.sets = 1, .storage_images = 1}); }
        catch (const std::exception&) { threw = true; }
        require(threw && destroyed == before, "failed pool creation claimed a resource");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
