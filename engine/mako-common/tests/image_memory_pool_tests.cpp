/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/vulkan/image_memory_pool.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
    void require(const bool condition, const std::string& message) {
        if (!condition)
            throw std::runtime_error(message);
    }

    void testAlignmentAndCapacity() {
        vk::detail::ImageMemoryBlockCursor block{1024};
        require(block.allocate(100, 64) == 0, "first suballocation was not aligned");
        require(block.allocate(100, 256) == 256, "second suballocation was not aligned");
        require(block.used() == 356, "suballocator used-byte accounting is wrong");
        require(!block.allocate(800, 1), "oversized suballocation unexpectedly succeeded");
        require(block.used() == 356, "failed suballocation advanced the cursor");
    }

    void testInvalidAlignmentFailsClosed() {
        vk::detail::ImageMemoryBlockCursor block{1024};
        require(!block.allocate(1, 0), "zero alignment unexpectedly succeeded");
        require(!block.allocate(1, 3), "non-power-of-two alignment unexpectedly succeeded");
        require(block.used() == 0, "invalid alignment advanced the cursor");
    }
}

int main() {
    try {
        testAlignmentAndCapacity();
        testInvalidAlignmentFailsClosed();
    } catch (const std::exception& error) {
        std::cerr << "Image memory pool test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
