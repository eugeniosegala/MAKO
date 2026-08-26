/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "vulkan/command_buffer_submit_storage.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {
    void require(const bool condition, const std::string& message) {
        if (!condition)
            throw std::runtime_error(message);
    }

    template<typename Handle>
    Handle fakeHandle(const uintptr_t value) {
        if constexpr (std::is_pointer_v<Handle>) {
            return reinterpret_cast<Handle>(value);
        } else {
            return static_cast<Handle>(value);
        }
    }

    void testInlineTimelineComposition() {
        const std::array waits{
            fakeHandle<VkSemaphore>(1), fakeHandle<VkSemaphore>(2)
        };
        const std::array signals{fakeHandle<VkSemaphore>(4)};
        const vk::detail::CommandBufferSubmitStorage storage{
            waits, fakeHandle<VkSemaphore>(3), 17,
            signals, fakeHandle<VkSemaphore>(5), 23
        };

        require(storage.waits().size() == 3,
            "inline wait count is wrong");
        require(storage.waits()[0] == waits[0] &&
                storage.waits()[1] == waits[1] &&
                storage.waits()[2] == fakeHandle<VkSemaphore>(3),
            "timeline wait was not appended after binary waits");
        require(storage.waitValues()[0] == 0 &&
                storage.waitValues()[1] == 0 &&
                storage.waitValues()[2] == 17,
            "inline wait values are not aligned with semaphores");
        require(storage.signals().size() == 2 &&
                storage.signals()[0] == signals[0] &&
                storage.signals()[1] == fakeHandle<VkSemaphore>(5),
            "timeline signal was not appended after binary signals");
        require(storage.signalValues()[0] == 0 &&
                storage.signalValues()[1] == 23,
            "inline signal values are not aligned with semaphores");
        require(storage.usesTimelineSemaphores(),
            "timeline submission did not request timeline submit info");
        for (const auto stage : storage.stages()) {
            require(stage == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                "wait stage changed");
        }
    }

    void testOverflowAndEmptyComposition() {
        const std::array waits{
            fakeHandle<VkSemaphore>(10), fakeHandle<VkSemaphore>(11),
            fakeHandle<VkSemaphore>(12), fakeHandle<VkSemaphore>(13),
            fakeHandle<VkSemaphore>(14)
        };
        const vk::detail::CommandBufferSubmitStorage overflow{
            waits, fakeHandle<VkSemaphore>(15), 29,
            {}, VK_NULL_HANDLE, 0
        };
        require(overflow.waits().size() == waits.size() + 1,
            "overflow wait count is wrong");
        for (size_t i = 0; i < waits.size(); ++i) {
            require(overflow.waits()[i] == waits[i],
                "overflow storage changed a binary wait semaphore");
            require(overflow.waitValues()[i] == 0,
                "overflow binary wait received a timeline value");
        }
        require(overflow.waits().back() == fakeHandle<VkSemaphore>(15) &&
                overflow.waitValues().back() == 29,
            "overflow storage lost the appended timeline wait");
        require(overflow.stages().size() == overflow.waits().size(),
            "overflow wait stage count is misaligned");
        for (const auto stage : overflow.stages()) {
            require(stage == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                "overflow wait stage changed");
        }
        require(overflow.signals().empty() &&
                overflow.signalValues().empty(),
            "empty signals unexpectedly created storage entries");

        const vk::detail::CommandBufferSubmitStorage empty{
            {}, VK_NULL_HANDLE, 0, {}, VK_NULL_HANDLE, 0
        };
        require(empty.waits().empty() && empty.waitValues().empty() &&
                empty.stages().empty() && empty.signals().empty() &&
                empty.signalValues().empty(),
            "empty submission storage is not empty");
        require(!empty.usesTimelineSemaphores(),
            "empty submission requested timeline submit info");

        const std::array binarySignals{fakeHandle<VkSemaphore>(16)};
        const vk::detail::CommandBufferSubmitStorage binaryOnly{
            waits, VK_NULL_HANDLE, 0,
            binarySignals, VK_NULL_HANDLE, 0
        };
        require(!binaryOnly.usesTimelineSemaphores(),
            "binary-only submission requested timeline submit info");
    }

    void testTimelineOnlyComposition() {
        const auto wait = fakeHandle<VkSemaphore>(20);
        const auto signal = fakeHandle<VkSemaphore>(21);
        const vk::detail::CommandBufferSubmitStorage storage{
            {}, wait, 31, {}, signal, 37
        };

        require(storage.waits().size() == 1 && storage.waits()[0] == wait &&
                storage.waitValues()[0] == 31 &&
                storage.stages()[0] == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            "timeline-only wait composition is wrong");
        require(storage.signals().size() == 1 &&
                storage.signals()[0] == signal &&
                storage.signalValues()[0] == 37,
            "timeline-only signal composition is wrong");
        require(storage.usesTimelineSemaphores(),
            "timeline-only submission did not request submit info");
    }
}

int main() {
    try {
        testInlineTimelineComposition();
        testOverflowAndEmptyComposition();
        testTimelineOnlyComposition();
    } catch (const std::exception& error) {
        std::cerr << "Command submit storage test failed: "
                  << error.what() << '\n';
        return 1;
    }
    return 0;
}
