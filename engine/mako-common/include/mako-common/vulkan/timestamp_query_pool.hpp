/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "vulkan.hpp"
#include "../helpers/errors.hpp"
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// Opt-in diagnostic queries only; the owner must establish GPU completion
// before reading or destroying the pool. No blocking query-result reads.
namespace vk {
    [[nodiscard]] inline double timestampElapsedMicroseconds(
            uint64_t start, uint64_t end, uint32_t validBits, float periodNs) {
        if (validBits == 0 || validBits > 64 || !std::isfinite(periodNs) || periodNs <= 0.0F)
            throw ls::error("invalid Vulkan timestamp properties");
        const uint64_t mask = validBits == 64 ? UINT64_MAX : (uint64_t{1} << validBits) - 1;
        return static_cast<double>((end - start) & mask) * static_cast<double>(periodNs) / 1000.0;
    }
    template<typename Function>
    [[nodiscard]] Function deviceFunction(
            const vk::Vulkan& vk, const char* name) {
        const auto function = reinterpret_cast<Function>(
            vk.fi().GetDeviceProcAddr(vk.dev(), name)
        );
        if (!function)
            throw ls::vulkan_error(
                "failed to get device proc addr for " + std::string(name)
            );
        return function;
    }

    class TimestampQueryPool {
    public:
        TimestampQueryPool(const vk::Vulkan& vk, const uint32_t queryCount) :
            device(vk.dev()),
            create(deviceFunction<PFN_vkCreateQueryPool>(vk, "vkCreateQueryPool")),
            destroy(deviceFunction<PFN_vkDestroyQueryPool>(vk, "vkDestroyQueryPool")),
            resetFunction(deviceFunction<PFN_vkCmdResetQueryPool>(
                vk, "vkCmdResetQueryPool"
            )),
            writeFunction(deviceFunction<PFN_vkCmdWriteTimestamp>(
                vk, "vkCmdWriteTimestamp"
            )),
            resultsFunction(deviceFunction<PFN_vkGetQueryPoolResults>(
                vk, "vkGetQueryPoolResults"
            )),
            count(queryCount) {
            if (queryCount == 0 || queryCount % 2 != 0)
                throw ls::error("timestamp query count must be positive and even");
            uint32_t familyCount{};
            vk.fi().GetPhysicalDeviceQueueFamilyProperties(
                vk.physdev(), &familyCount, nullptr
            );
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vk.fi().GetPhysicalDeviceQueueFamilyProperties(
                vk.physdev(), &familyCount, families.data()
            );
            if (vk.queueFamilyIndex() >= families.size())
                throw ls::vulkan_error("timestamp queue family is out of range");
            this->validBits = families.at(
                vk.queueFamilyIndex()
            ).timestampValidBits;
            if (this->validBits == 0)
                throw ls::vulkan_error(
                    "selected Vulkan queue does not support timestamps"
                );
            if (this->validBits > 64)
                throw ls::vulkan_error(
                    "selected Vulkan queue reports an invalid timestamp width"
                );

            VkPhysicalDeviceProperties properties{};
            vk.fi().GetPhysicalDeviceProperties(vk.physdev(), &properties);
            this->periodNanoseconds = properties.limits.timestampPeriod;
            if (!std::isfinite(this->periodNanoseconds) ||
                    this->periodNanoseconds <= 0.0F) {
                throw ls::vulkan_error(
                    "selected Vulkan device reports an invalid timestamp period"
                );
            }

            const VkQueryPoolCreateInfo info{
                .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                .queryType = VK_QUERY_TYPE_TIMESTAMP,
                .queryCount = queryCount,
            };
            const auto result = this->create(
                this->device, &info, nullptr, &this->pool
            );
            if (result != VK_SUCCESS)
                throw ls::vulkan_error(result, "vkCreateQueryPool() failed");
        }

        ~TimestampQueryPool() {
            if (this->pool != VK_NULL_HANDLE)
                this->destroy(this->device, this->pool, nullptr);
        }

        TimestampQueryPool(const TimestampQueryPool&) = delete;
        TimestampQueryPool& operator=(const TimestampQueryPool&) = delete;

        void reset(const VkCommandBuffer commandBuffer) const {
            this->resetFunction(commandBuffer, this->pool, 0, this->count);
        }

        void write(const VkCommandBuffer commandBuffer,
                const VkPipelineStageFlagBits stage,
                const uint32_t query) const {
            if (query >= this->count)
                throw ls::error("timestamp write exceeds query capacity");
            this->writeFunction(commandBuffer, stage, this->pool, query);
        }

        [[nodiscard]] std::vector<uint64_t> timestamps(uint32_t usedCount = 0) const {
            if (usedCount == 0) usedCount = this->count;
            if (usedCount > this->count)
                throw ls::error("timestamp read exceeds query capacity");
            std::vector<uint64_t> timestamps(usedCount);
            const auto result = this->resultsFunction(
                this->device, this->pool, 0, usedCount,
                timestamps.size() * sizeof(uint64_t), timestamps.data(),
                sizeof(uint64_t), VK_QUERY_RESULT_64_BIT
            );
            if (result != VK_SUCCESS)
                throw ls::vulkan_error(
                    result, "vkGetQueryPoolResults() failed"
                );

            return timestamps;
        }

        [[nodiscard]] double elapsedMicroseconds(uint64_t start, uint64_t end) const {
            return timestampElapsedMicroseconds(start, end, this->validBits, this->periodNanoseconds);
        }

        [[nodiscard]] std::vector<double> microseconds() const {
            const auto values = this->timestamps();
            std::vector<double> samples;
            samples.reserve(this->count / 2);
            for (uint32_t query = 0; query < this->count; query += 2)
                samples.push_back(this->elapsedMicroseconds(values.at(query), values.at(query + 1)));
            return samples;
        }

        [[nodiscard]] uint32_t timestampValidBits() const {
            return this->validBits;
        }

        [[nodiscard]] float timestampPeriodNanoseconds() const {
            return this->periodNanoseconds;
        }

    private:
        VkDevice device{VK_NULL_HANDLE};
        PFN_vkCreateQueryPool create{};
        PFN_vkDestroyQueryPool destroy{};
        PFN_vkCmdResetQueryPool resetFunction{};
        PFN_vkCmdWriteTimestamp writeFunction{};
        PFN_vkGetQueryPoolResults resultsFunction{};
        VkQueryPool pool{VK_NULL_HANDLE};
        uint32_t count{};
        uint32_t validBits{};
        float periodNanoseconds{};
    };

}
