/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-backend/mako.hpp"

#include <cstddef>
#include <string_view>

#include <vulkan/vulkan_core.h>

namespace mako::layer {

    /// Colour handling selected from the complete Vulkan surface-format pair.
    /// `hdr` describes transfer-function semantics, not component bit depth:
    /// a 10-bit UNORM/sRGB pair is still SDR, while the same packed format with
    /// confirmed PQ semantics is HDR10. Keeping those concepts separate avoids
    /// washed-out output from interpreting SDR code values as PQ (or reverse).
    struct SwapchainColorPipeline {
        backend::FrameEncoding encoding{backend::FrameEncoding::Sdr8};
        VkFormat exchangeFormat{VK_FORMAT_R8G8B8A8_UNORM};
        bool generationSupported{true};
        bool hdr{false};
        bool gamescopeColorSpaceRecovered{false};
        bool packedHdr10Transport{false};
        std::string_view name{"sdr-8-bit"};
        std::string_view reason{};
    };

    /// Spatial reconstruction is currently restricted to validated, non-HDR
    /// SDR pipelines. Unsupported colour pairs retain the default enum value,
    /// so callers must test the complete classification rather than encoding
    /// alone.
    [[nodiscard]] constexpr bool spatialScalingColorSupported(
            const SwapchainColorPipeline& pipeline) noexcept {
        return pipeline.generationSupported && !pipeline.hdr &&
            (pipeline.encoding == backend::FrameEncoding::Sdr8 ||
             pipeline.encoding == backend::FrameEncoding::SdrHighPrecision);
    }

    /// Classify a swapchain without relying on VkFormat enum ordering.
    /// Gamescope may consume the application's HDR colour space before lower
    /// layers see VkSwapchainCreateInfoKHR, so confirmed application feedback
    /// is used only to recover semantics for an HDR-capable 10/16-bit format.
    /// It never promotes an ordinary 8-bit SDR swapchain to HDR.
    [[nodiscard]] SwapchainColorPipeline classifySwapchainColor(
        VkFormat format, VkColorSpaceKHR colorSpace,
        bool gamescopeHdrActive = false
    );

    /// Select the compact HDR10 boundary representation only when the game
    /// device can export it and the backend device can import/write it. This is
    /// boundary transport compression: model/intermediate images remain linear
    /// 16-bit float so interpolation quality is not reduced.
    [[nodiscard]] bool enablePackedHdr10Transport(
        SwapchainColorPipeline& pipeline,
        bool applicationDeviceSupported,
        bool backendDeviceSupported
    );

    [[nodiscard]] size_t transportBytesPerPixel(
        backend::FrameEncoding encoding
    );

}
