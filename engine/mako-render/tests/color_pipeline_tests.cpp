/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "color_pipeline.hpp"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace mako;

namespace {
    void expect(const bool condition, const std::string_view message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main() {
    // Native 8-bit and 10-bit sRGB are both SDR. This guards against treating
    // "more bits" as HDR and producing the washed-out transfer-function bug.
    const auto sdr = layer::classifySwapchainColor(
        VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
    );
    expect(sdr.generationSupported, "8-bit sRGB should be supported");
    expect(sdr.encoding == backend::FrameEncoding::Sdr8,
        "8-bit sRGB should select the SDR transport");
    expect(!sdr.hdr, "8-bit sRGB should not be HDR");

    const auto sdr10 = layer::classifySwapchainColor(
        VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
    );
    expect(sdr10.generationSupported, "10-bit SDR should remain supported");
    expect(sdr10.encoding == backend::FrameEncoding::SdrHighPrecision,
        "10-bit SDR must not activate HDR model semantics");
    expect(!sdr10.hdr, "10-bit SDR should not be classified as HDR");

    // Below Gamescope the colour space can already be normalized to sRGB.
    // Confirmed compositor feedback plus an HDR-capable format recovers PQ;
    // the later 8-bit case proves feedback cannot leak HDR into normal SDR.
    const auto gamescopeHdr10 = layer::classifySwapchainColor(
        VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        true
    );
    expect(gamescopeHdr10.generationSupported,
        "Gamescope-normalized HDR10 should remain supported below WSI");
    expect(gamescopeHdr10.encoding == backend::FrameEncoding::Hdr10Pq,
        "Gamescope-normalized packed 10-bit should recover PQ semantics");
    expect(gamescopeHdr10.hdr,
        "Gamescope-normalized packed 10-bit should be classified as HDR");
    expect(gamescopeHdr10.gamescopeColorSpaceRecovered,
        "Gamescope-normalized HDR10 should identify its recovered source");

    // Compression is allowed only across the device boundary and only when
    // both Vulkan devices support the packed format. Float remains the safe
    // fallback and the internal model representation.
    auto packedGamescopeHdr10 = gamescopeHdr10;
    expect(!layer::enablePackedHdr10Transport(
            packedGamescopeHdr10, true, false),
        "Packed HDR10 must not activate when the backend cannot import it");
    expect(packedGamescopeHdr10.encoding == backend::FrameEncoding::Hdr10Pq,
        "An unsupported packed path must preserve float HDR10 transport");
    expect(layer::enablePackedHdr10Transport(
            packedGamescopeHdr10, true, true),
        "Packed HDR10 should activate when both Vulkan devices support it");
    expect(packedGamescopeHdr10.encoding ==
            backend::FrameEncoding::Hdr10PqPacked,
        "Packed HDR10 selected the wrong backend encoding");
    expect(packedGamescopeHdr10.exchangeFormat ==
            VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        "Packed HDR10 must use the canonical 32-bit exchange format");
    expect(packedGamescopeHdr10.packedHdr10Transport,
        "Packed HDR10 should be visible in runtime diagnostics");
    expect(layer::transportBytesPerPixel(
            backend::FrameEncoding::Hdr10PqPacked) == 4 &&
            layer::transportBytesPerPixel(
                backend::FrameEncoding::Hdr10Pq) == 8,
        "Packed HDR10 must halve nominal transport bytes per pixel");

    constexpr size_t steamDeckPixels = 1280 * 800;
    constexpr size_t adaptiveThreeXTransportImages = 2 + 2;
    const size_t packedBytes = steamDeckPixels *
        adaptiveThreeXTransportImages *
        layer::transportBytesPerPixel(
            backend::FrameEncoding::Hdr10PqPacked
        );
    const size_t floatBytes = steamDeckPixels *
        adaptiveThreeXTransportImages *
        layer::transportBytesPerPixel(backend::FrameEncoding::Hdr10Pq);
    expect(floatBytes - packedBytes == 16'384'000,
        "Steam Deck Adaptive 3x transport should save 16.384 MB nominally");

    // scRGB is already linear float HDR and therefore does not use the PQ pack
    // conversion used by HDR10.
    const auto gamescopeScrgb = layer::classifySwapchainColor(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        true
    );
    expect(gamescopeScrgb.encoding == backend::FrameEncoding::ScRgbLinear,
        "Gamescope-normalized float swapchains should recover scRGB semantics");
    expect(gamescopeScrgb.gamescopeColorSpaceRecovered,
        "Gamescope-normalized scRGB should identify its recovered source");

    const auto gamescopeSdr8 = layer::classifySwapchainColor(
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        true
    );
    expect(gamescopeSdr8.encoding == backend::FrameEncoding::Sdr8,
        "Gamescope HDR feedback must not reclassify an 8-bit SDR swapchain");
    expect(!gamescopeSdr8.hdr,
        "Gamescope HDR feedback must preserve 8-bit SDR semantics");

    // Classification is deliberately stateless. An SDR -> HDR -> SDR
    // feedback transition for the same normalized 10-bit format must restore
    // the high-precision SDR pipeline rather than retaining PQ semantics from
    // the preceding frame-generation context.
    const auto recoveredSdr10 = layer::classifySwapchainColor(
        VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
        false
    );
    expect(recoveredSdr10.generationSupported,
        "SDR recovery after Gamescope HDR should remain supported");
    expect(recoveredSdr10.encoding == backend::FrameEncoding::SdrHighPrecision,
        "SDR recovery retained HDR10/PQ transport semantics");
    expect(!recoveredSdr10.hdr &&
            !recoveredSdr10.gamescopeColorSpaceRecovered &&
            !recoveredSdr10.packedHdr10Transport,
        "HDR state leaked into the recovered SDR pipeline");

    for (const auto format : {
            VK_FORMAT_A2B10G10R10_UNORM_PACK32,
            VK_FORMAT_A2R10G10B10_UNORM_PACK32}) {
        const auto hdr10 = layer::classifySwapchainColor(
            format, VK_COLOR_SPACE_HDR10_ST2084_EXT
        );
        expect(hdr10.generationSupported, "Gamescope HDR10 formats should be supported");
        expect(hdr10.encoding == backend::FrameEncoding::Hdr10Pq,
            "HDR10 should select PQ-to-scRGB conversion");
        expect(hdr10.hdr, "HDR10 should be classified as HDR");
    }

    const auto scrgb = layer::classifySwapchainColor(
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT
    );
    expect(scrgb.generationSupported, "Gamescope scRGB should be supported");
    expect(scrgb.encoding == backend::FrameEncoding::ScRgbLinear,
        "scRGB should use the native linear HDR model path");

    const auto badScrgb = layer::classifySwapchainColor(
        VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT
    );
    expect(!badScrgb.generationSupported,
        "invalid packed scRGB combinations should fail safely");

    const auto hlg = layer::classifySwapchainColor(
        VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_HDR10_HLG_EXT
    );
    expect(!hlg.generationSupported, "HLG should pass through until implemented");
    expect(hlg.hdr, "unsupported HLG should still be diagnosed as HDR");

    std::cout << "All swapchain colour-pipeline tests passed.\n";
    return 0;
}
