/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "color_pipeline.hpp"

using namespace mako;

namespace {
    bool isEightBitSdrFormat(const VkFormat format) {
        switch (format) {
            case VK_FORMAT_R8G8B8A8_UNORM:
            case VK_FORMAT_R8G8B8A8_SRGB:
            case VK_FORMAT_B8G8R8A8_UNORM:
            case VK_FORMAT_B8G8R8A8_SRGB:
                return true;
            default:
                return false;
        }
    }

    bool isHighPrecisionSdrFormat(const VkFormat format) {
        switch (format) {
            case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
            case VK_FORMAT_R16G16B16A16_SFLOAT:
                return true;
            default:
                return false;
        }
    }

    bool isHdr10Format(const VkFormat format) {
        switch (format) {
            case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
                return true;
            default:
                return false;
        }
    }
}

layer::SwapchainColorPipeline layer::classifySwapchainColor(
        const VkFormat format, const VkColorSpaceKHR colorSpace,
        const bool gamescopeHdrActive) {
    if (colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) {
        if (isHdr10Format(format)) {
            return {
                .encoding = backend::FrameEncoding::Hdr10Pq,
                .exchangeFormat = VK_FORMAT_R16G16B16A16_SFLOAT,
                .generationSupported = true,
                .hdr = true,
                .name = "hdr10-pq",
            };
        }
        return {
            .generationSupported = false,
            .hdr = true,
            .name = "unsupported-hdr10-format",
            .reason = "HDR10/PQ requires an A2R10G10B10 or A2B10G10R10 swapchain",
        };
    }

    if (colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT) {
        if (format == VK_FORMAT_R16G16B16A16_SFLOAT) {
            return {
                .encoding = backend::FrameEncoding::ScRgbLinear,
                .exchangeFormat = VK_FORMAT_R16G16B16A16_SFLOAT,
                .generationSupported = true,
                .hdr = true,
                .name = "scrgb-linear",
            };
        }
        return {
            .generationSupported = false,
            .hdr = true,
            .name = "unsupported-scrgb-format",
            .reason = "linear scRGB requires an R16G16B16A16_SFLOAT swapchain",
        };
    }

    if (colorSpace == VK_COLOR_SPACE_HDR10_HLG_EXT ||
            colorSpace == VK_COLOR_SPACE_DOLBYVISION_EXT) {
        return {
            .generationSupported = false,
            .hdr = true,
            .name = "unsupported-hdr-colorspace",
            .reason = "the selected HDR transfer function is not supported",
        };
    }

    // Gamescope WSI consumes the application's HDR colour space and sends an
    // sRGB-normalized copy of VkSwapchainCreateInfoKHR to lower layers. MAKO
    // must remain below Gamescope so Wine's WSI bridge can translate its
    // dispatchable handles before they reach us. Recover the transfer
    // function only when Gamescope's application feedback confirms that the
    // active game requested HDR and the format is one of its HDR formats. Bit
    // depth alone is insufficient: without confirmed feedback the identical
    // packed 10-bit/sRGB pair below remains high-precision SDR.
    if (gamescopeHdrActive &&
            colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        if (isHdr10Format(format)) {
            return {
                .encoding = backend::FrameEncoding::Hdr10Pq,
                .exchangeFormat = VK_FORMAT_R16G16B16A16_SFLOAT,
                .generationSupported = true,
                .hdr = true,
                .gamescopeColorSpaceRecovered = true,
                .name = "hdr10-pq",
            };
        }
        if (format == VK_FORMAT_R16G16B16A16_SFLOAT) {
            return {
                .encoding = backend::FrameEncoding::ScRgbLinear,
                .exchangeFormat = VK_FORMAT_R16G16B16A16_SFLOAT,
                .generationSupported = true,
                .hdr = true,
                .gamescopeColorSpaceRecovered = true,
                .name = "scrgb-linear",
            };
        }
    }

    if (colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
        return {
            .generationSupported = false,
            .name = "unsupported-wide-color",
            .reason = "the swapchain uses an unvalidated non-sRGB colour space",
        };
    }

    if (isEightBitSdrFormat(format)) {
        return {
            .encoding = backend::FrameEncoding::Sdr8,
            .exchangeFormat = VK_FORMAT_R8G8B8A8_UNORM,
            .generationSupported = true,
            .name = "sdr-8-bit",
        };
    }

    if (isHighPrecisionSdrFormat(format)) {
        return {
            .encoding = backend::FrameEncoding::SdrHighPrecision,
            .exchangeFormat = VK_FORMAT_R16G16B16A16_SFLOAT,
            .generationSupported = true,
            .name = "sdr-high-precision",
        };
    }

    return {
        .generationSupported = false,
        .name = "unsupported-sdr-format",
        .reason = "the swapchain format has no validated MAKO transport",
    };
}

bool layer::enablePackedHdr10Transport(
        SwapchainColorPipeline& pipeline,
        const bool applicationDeviceSupported,
        const bool backendDeviceSupported) {
    if (pipeline.encoding != backend::FrameEncoding::Hdr10Pq ||
            !applicationDeviceSupported || !backendDeviceSupported)
        return false;

    pipeline.encoding = backend::FrameEncoding::Hdr10PqPacked;
    pipeline.exchangeFormat = VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    pipeline.packedHdr10Transport = true;
    return true;
}

size_t layer::transportBytesPerPixel(
        const backend::FrameEncoding encoding) {
    switch (encoding) {
        case backend::FrameEncoding::Sdr8:
        case backend::FrameEncoding::Hdr10PqPacked:
            return 4;
        case backend::FrameEncoding::SdrHighPrecision:
        case backend::FrameEncoding::ScRgbLinear:
        case backend::FrameEncoding::Hdr10Pq:
            return 8;
    }
    return 8;
}
