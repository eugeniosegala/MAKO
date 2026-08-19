/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/quality/image_quality.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace mako::quality;

namespace {
    struct Color {
        uint8_t red;
        uint8_t green;
        uint8_t blue;
        uint8_t alpha{255};
    };

    constexpr uint32_t sceneWidth = 321;
    constexpr uint32_t sceneHeight = 181;

    size_t pixelOffset(const uint32_t x, const uint32_t y) {
        return (static_cast<size_t>(y) * sceneWidth + x) * 4;
    }

    void setPixel(std::vector<uint8_t>& image, const int x, const int y,
            const Color color) {
        if (x < 0 || y < 0 || x >= static_cast<int>(sceneWidth) ||
                y >= static_cast<int>(sceneHeight))
            return;
        const size_t offset = pixelOffset(
            static_cast<uint32_t>(x), static_cast<uint32_t>(y)
        );
        image.at(offset) = color.red;
        image.at(offset + 1) = color.green;
        image.at(offset + 2) = color.blue;
        image.at(offset + 3) = color.alpha;
    }

    void drawRect(std::vector<uint8_t>& image, const int left, const int top,
            const int width, const int height, const Color color) {
        for (int y = top; y < top + height; ++y)
            for (int x = left; x < left + width; ++x)
                setPixel(image, x, y, color);
    }

    void markRect(std::vector<uint8_t>& mask, const int left, const int top,
            const int width, const int height) {
        for (int y = std::max(top, 0);
                y < std::min(top + height, static_cast<int>(sceneHeight)); ++y) {
            for (int x = std::max(left, 0);
                    x < std::min(left + width, static_cast<int>(sceneWidth)); ++x) {
                mask.at(static_cast<size_t>(y) * sceneWidth +
                    static_cast<size_t>(x)) = 1;
            }
        }
    }

    std::vector<uint8_t> renderScene(const float time,
            std::vector<uint8_t>* detailMask = nullptr) {
        std::vector<uint8_t> image(
            static_cast<size_t>(sceneWidth) * sceneHeight * 4, 255
        );
        for (uint32_t y = 0; y < sceneHeight; ++y) {
            for (uint32_t x = 0; x < sceneWidth; ++x) {
                const bool grid = ((x / 11) + (y / 13)) % 2 == 0;
                const Color background{
                    .red = static_cast<uint8_t>(18 + (x * 37 / sceneWidth) + (grid ? 8 : 0)),
                    .green = static_cast<uint8_t>(24 + (y * 43 / sceneHeight)),
                    .blue = static_cast<uint8_t>(34 + ((x + y) % 29)),
                };
                setPixel(image, static_cast<int>(x), static_cast<int>(y), background);
            }
        }

        // Static one-pixel geometry catches history instability without motion.
        drawRect(image, 258, 20, 1, 119, Color{.red = 72, .green = 211, .blue = 148});
        drawRect(image, 231, 88, 62, 1, Color{.red = 72, .green = 211, .blue = 148});

        const int occluderX = static_cast<int>(std::lround(96.0F + 38.0F * time));
        drawRect(image, occluderX, 58, 55, 54,
            Color{.red = 35, .green = 101, .blue = 224});
        drawRect(image, occluderX + 4, 62, 47, 3,
            Color{.red = 238, .green = 147, .blue = 41});

        // A tiny high-contrast feature attached to the moving surface exposes
        // motion blur and duplicate-image trails.
        drawRect(image, occluderX + 43, 69, 3, 3,
            Color{.red = 250, .green = 250, .blue = 250});

        const int filamentX = static_cast<int>(std::lround(43.0F + 23.0F * time));
        drawRect(image, filamentX, 37, 2, 73,
            Color{.red = 244, .green = 52, .blue = 83});

        // Partially off-screen motion stresses image reads at the left edge.
        const int edgeX = static_cast<int>(std::lround(-7.0F + 14.0F * time));
        drawRect(image, edgeX, 145, 11, 9,
            Color{.red = 241, .green = 224, .blue = 62});

        if (detailMask != nullptr) {
            markRect(*detailMask, occluderX + 41, 67, 7, 7);
            markRect(*detailMask, filamentX - 1, 35, 4, 77);
            markRect(*detailMask, edgeX - 1, 143, 13, 13);
        }
        return image;
    }

    std::vector<uint8_t> makeFocusMask(
            const std::vector<uint8_t>& previous,
            const std::vector<uint8_t>& current) {
        std::vector<uint8_t> changed(static_cast<size_t>(sceneWidth) * sceneHeight);
        for (uint32_t y = 0; y < sceneHeight; ++y) {
            for (uint32_t x = 0; x < sceneWidth; ++x) {
                const size_t offset = pixelOffset(x, y);
                for (size_t channel = 0; channel < 3; ++channel) {
                    if (previous.at(offset + channel) != current.at(offset + channel)) {
                        changed.at(static_cast<size_t>(y) * sceneWidth + x) = 1;
                        break;
                    }
                }
            }
        }

        std::vector<uint8_t> focus(changed.size());
        constexpr int radius = 6;
        for (uint32_t y = 0; y < sceneHeight; ++y) {
            for (uint32_t x = 0; x < sceneWidth; ++x) {
                if (changed.at(static_cast<size_t>(y) * sceneWidth + x) == 0)
                    continue;
                markRect(focus, static_cast<int>(x) - radius,
                    static_cast<int>(y) - radius, radius * 2 + 1, radius * 2 + 1);
            }
        }
        return focus;
    }

    double normalizedMean(const double total, const size_t samples) {
        return samples == 0 ? 0.0 : total /
            (static_cast<double>(samples) * 255.0);
    }
}

RegressionScene mako::quality::makeAmdImageQualityRegressionScene() {
    RegressionScene scene{
        .width = sceneWidth,
        .height = sceneHeight,
        .previous = renderScene(0.0F),
        .current = renderScene(1.0F),
        .reference = {},
        .focusMask = {},
        .detailMask = std::vector<uint8_t>(
            static_cast<size_t>(sceneWidth) * sceneHeight
        ),
    };
    scene.reference = renderScene(0.5F, &scene.detailMask);
    scene.focusMask = makeFocusMask(scene.previous, scene.current);
    return scene;
}

ImageQualityMetrics mako::quality::evaluateImageQuality(
        const RegressionScene& scene, const std::span<const uint8_t> generated) {
    const size_t pixels = static_cast<size_t>(scene.width) * scene.height;
    const size_t expectedBytes = pixels * 4;
    if (scene.reference.size() != expectedBytes || generated.size() != expectedBytes ||
            scene.focusMask.size() != pixels || scene.detailMask.size() != pixels)
        throw std::invalid_argument("image-quality frame or mask has an invalid size");

    double absoluteError{};
    double focusAbsoluteError{};
    double detailAbsoluteError{};
    size_t focusSamples{};
    size_t detailSamples{};
    size_t severeFocusPixels{};
    size_t focusPixels{};

    for (size_t pixel = 0; pixel < pixels; ++pixel) {
        const size_t offset = pixel * 4;
        uint8_t maximumDifference{};
        for (size_t channel = 0; channel < 3; ++channel) {
            const auto difference = static_cast<uint8_t>(std::abs(
                static_cast<int>(generated[offset + channel]) -
                static_cast<int>(scene.reference[offset + channel])
            ));
            absoluteError += difference;
            maximumDifference = std::max(maximumDifference, difference);
            if (scene.focusMask[pixel] != 0) {
                focusAbsoluteError += difference;
                ++focusSamples;
            }
            if (scene.detailMask[pixel] != 0) {
                detailAbsoluteError += difference;
                ++detailSamples;
            }
        }
        if (scene.focusMask[pixel] != 0) {
            ++focusPixels;
            if (maximumDifference >= 96)
                ++severeFocusPixels;
        }
    }

    return {
        .meanAbsoluteError = normalizedMean(absoluteError, pixels * 3),
        .focusMeanAbsoluteError = normalizedMean(focusAbsoluteError, focusSamples),
        .severeFocusErrorFraction = focusPixels == 0 ? 0.0 :
            static_cast<double>(severeFocusPixels) / static_cast<double>(focusPixels),
        .detailMeanAbsoluteError = normalizedMean(detailAbsoluteError, detailSamples),
    };
}

bool mako::quality::passesImageQualityRegression(
        const ImageQualityMetrics& metrics,
        const ImageQualityThresholds& thresholds) {
    return metrics.meanAbsoluteError <= thresholds.maximumMeanAbsoluteError &&
        metrics.focusMeanAbsoluteError <= thresholds.maximumFocusMeanAbsoluteError &&
        metrics.severeFocusErrorFraction <=
            thresholds.maximumSevereFocusErrorFraction &&
        metrics.detailMeanAbsoluteError <= thresholds.maximumDetailMeanAbsoluteError;
}
