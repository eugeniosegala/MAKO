/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/quality/image_quality.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <stdexcept>
#include <string_view>
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

    constexpr uint32_t baseWidth = 321;
    constexpr uint32_t baseHeight = 181;

    constexpr std::array sceneCatalog{
        QualitySceneDescriptor{
            .kind = QualitySceneKind::MotionBoundary,
            .name = "motion-boundary",
            .description = "Thin geometry, partial off-screen motion, and a hard disocclusion boundary",
        },
        QualitySceneDescriptor{
            .kind = QualitySceneKind::Traffic,
            .name = "traffic",
            .description = "Opposing cars, road markings, reflections, and foreground occlusion",
        },
        QualitySceneDescriptor{
            .kind = QualitySceneKind::Crowd,
            .name = "crowd",
            .description = "Articulated people with independent limb motion and fine fence detail",
        },
        QualitySceneDescriptor{
            .kind = QualitySceneKind::CameraPan,
            .name = "camera-pan",
            .description = "Multi-depth parallax, fine cables, foliage, and a moving airborne object",
        },
        QualitySceneDescriptor{
            .kind = QualitySceneKind::HudDisocclusion,
            .name = "hud-disocclusion",
            .description = "Static HUD detail over a moving door, particles, and revealed world geometry",
        },
    };

    [[nodiscard]] float wrap(const float value, const float maximum) {
        const float wrapped = std::fmod(value, maximum);
        return wrapped < 0.0F ? wrapped + maximum : wrapped;
    }

    class Canvas {
    public:
        Canvas(const uint32_t width, const uint32_t height,
                std::vector<uint8_t>* detailMask = nullptr) :
            width_(width), height_(height),
            image_(static_cast<size_t>(width) * height * 4, 255),
            detailMask_(detailMask) {}

        [[nodiscard]] uint32_t width() const { return width_; }
        [[nodiscard]] uint32_t height() const { return height_; }

        [[nodiscard]] int x(const float canonical) const {
            return static_cast<int>(std::lround(
                canonical * static_cast<float>(width_) /
                static_cast<float>(baseWidth)
            ));
        }

        [[nodiscard]] int y(const float canonical) const {
            return static_cast<int>(std::lround(
                canonical * static_cast<float>(height_) /
                static_cast<float>(baseHeight)
            ));
        }

        void pixel(const int xPosition, const int yPosition, const Color color) {
            if (xPosition < 0 || yPosition < 0 ||
                    xPosition >= static_cast<int>(width_) ||
                    yPosition >= static_cast<int>(height_))
                return;
            const size_t offset = (
                static_cast<size_t>(yPosition) * width_ +
                static_cast<size_t>(xPosition)
            ) * 4;
            image_.at(offset) = color.red;
            image_.at(offset + 1) = color.green;
            image_.at(offset + 2) = color.blue;
            image_.at(offset + 3) = color.alpha;
        }

        void rectPixels(const int left, const int top, const int width,
                const int height, const Color color) {
            for (int row = top; row < top + height; ++row)
                for (int column = left; column < left + width; ++column)
                    pixel(column, row, color);
        }

        void rect(const float left, const float top, const float width,
                const float height, const Color color) {
            const int pixelLeft = x(left);
            const int pixelTop = y(top);
            rectPixels(
                pixelLeft,
                pixelTop,
                std::max(1, x(left + width) - pixelLeft),
                std::max(1, y(top + height) - pixelTop),
                color
            );
        }

        void line(const float x0, const float y0, const float x1,
                const float y1, const Color color, const float thickness = 1.0F) {
            int fromX = x(x0);
            int fromY = y(y0);
            const int toX = x(x1);
            const int toY = y(y1);
            const int deltaX = std::abs(toX - fromX);
            const int stepX = fromX < toX ? 1 : -1;
            const int deltaY = -std::abs(toY - fromY);
            const int stepY = fromY < toY ? 1 : -1;
            int error = deltaX + deltaY;
            const int radius = std::max(0, x(thickness) / 2);

            while (true) {
                rectPixels(
                    fromX - radius,
                    fromY - radius,
                    radius * 2 + 1,
                    radius * 2 + 1,
                    color
                );
                if (fromX == toX && fromY == toY)
                    break;
                const int doubledError = error * 2;
                if (doubledError >= deltaY) {
                    error += deltaY;
                    fromX += stepX;
                }
                if (doubledError <= deltaX) {
                    error += deltaX;
                    fromY += stepY;
                }
            }
        }

        void circle(const float centerX, const float centerY, const float radius,
                const Color color) {
            const int pixelCenterX = x(centerX);
            const int pixelCenterY = y(centerY);
            const int radiusX = std::max(1, x(radius));
            const int radiusY = std::max(1, y(radius));
            for (int row = -radiusY; row <= radiusY; ++row) {
                for (int column = -radiusX; column <= radiusX; ++column) {
                    const float normalizedX = static_cast<float>(column) /
                        static_cast<float>(radiusX);
                    const float normalizedY = static_cast<float>(row) /
                        static_cast<float>(radiusY);
                    if (normalizedX * normalizedX + normalizedY * normalizedY <= 1.0F)
                        pixel(pixelCenterX + column, pixelCenterY + row, color);
                }
            }
        }

        void triangle(const std::array<std::pair<float, float>, 3>& points,
                const Color color) {
            std::array<std::pair<int, int>, 3> pixels{};
            for (size_t index = 0; index < pixels.size(); ++index)
                pixels.at(index) = {x(points.at(index).first), y(points.at(index).second)};
            const auto signedArea = [](const std::pair<int, int>& first,
                    const std::pair<int, int>& second, const int pointX,
                    const int pointY) {
                return (pointX - second.first) * (first.second - second.second) -
                    (first.first - second.first) * (pointY - second.second);
            };
            const int minimumX = std::min({pixels[0].first, pixels[1].first, pixels[2].first});
            const int maximumX = std::max({pixels[0].first, pixels[1].first, pixels[2].first});
            const int minimumY = std::min({pixels[0].second, pixels[1].second, pixels[2].second});
            const int maximumY = std::max({pixels[0].second, pixels[1].second, pixels[2].second});
            for (int row = minimumY; row <= maximumY; ++row) {
                for (int column = minimumX; column <= maximumX; ++column) {
                    const int area0 = signedArea(pixels[0], pixels[1], column, row);
                    const int area1 = signedArea(pixels[1], pixels[2], column, row);
                    const int area2 = signedArea(pixels[2], pixels[0], column, row);
                    const bool hasNegative = area0 < 0 || area1 < 0 || area2 < 0;
                    const bool hasPositive = area0 > 0 || area1 > 0 || area2 > 0;
                    if (!(hasNegative && hasPositive))
                        pixel(column, row, color);
                }
            }
        }

        void detailRect(const float left, const float top, const float width,
                const float height) {
            if (detailMask_ == nullptr)
                return;
            const int pixelLeft = x(left);
            const int pixelTop = y(top);
            const int pixelWidth = std::max(1, x(left + width) - pixelLeft);
            const int pixelHeight = std::max(1, y(top + height) - pixelTop);
            for (int row = std::max(pixelTop, 0);
                    row < std::min(pixelTop + pixelHeight, static_cast<int>(height_)); ++row) {
                for (int column = std::max(pixelLeft, 0);
                        column < std::min(pixelLeft + pixelWidth, static_cast<int>(width_)); ++column) {
                    detailMask_->at(
                        static_cast<size_t>(row) * width_ +
                        static_cast<size_t>(column)
                    ) = 1;
                }
            }
        }

        [[nodiscard]] std::vector<uint8_t> release() { return std::move(image_); }

    private:
        uint32_t width_;
        uint32_t height_;
        std::vector<uint8_t> image_;
        std::vector<uint8_t>* detailMask_;
    };

    void renderBackground(Canvas& canvas, const Color top, const Color bottom,
            const bool texture = true) {
        for (uint32_t row = 0; row < canvas.height(); ++row) {
            const float blend = canvas.height() == 1 ? 0.0F :
                static_cast<float>(row) / static_cast<float>(canvas.height() - 1);
            for (uint32_t column = 0; column < canvas.width(); ++column) {
                const uint8_t variation = texture ? static_cast<uint8_t>(
                    ((column / std::max(1U, canvas.width() / 29U)) +
                    (row / std::max(1U, canvas.height() / 17U))) % 8U
                ) : 0;
                canvas.pixel(
                    static_cast<int>(column),
                    static_cast<int>(row),
                    Color{
                        .red = static_cast<uint8_t>(
                            static_cast<float>(top.red) * (1.0F - blend) +
                            static_cast<float>(bottom.red) * blend + variation
                        ),
                        .green = static_cast<uint8_t>(
                            static_cast<float>(top.green) * (1.0F - blend) +
                            static_cast<float>(bottom.green) * blend + variation
                        ),
                        .blue = static_cast<uint8_t>(
                            static_cast<float>(top.blue) * (1.0F - blend) +
                            static_cast<float>(bottom.blue) * blend + variation
                        ),
                    }
                );
            }
        }
    }

    void renderMotionBoundary(Canvas& canvas, const float time) {
        for (uint32_t row = 0; row < canvas.height(); ++row) {
            for (uint32_t column = 0; column < canvas.width(); ++column) {
                const uint32_t canonicalX = column * baseWidth / canvas.width();
                const uint32_t canonicalY = row * baseHeight / canvas.height();
                const bool grid = ((canonicalX / 11) + (canonicalY / 13)) % 2 == 0;
                canvas.pixel(
                    static_cast<int>(column),
                    static_cast<int>(row),
                    Color{
                        .red = static_cast<uint8_t>(18 +
                            (canonicalX * 37 / baseWidth) + (grid ? 8 : 0)),
                        .green = static_cast<uint8_t>(24 +
                            (canonicalY * 43 / baseHeight)),
                        .blue = static_cast<uint8_t>(34 +
                            ((canonicalX + canonicalY) % 29)),
                    }
                );
            }
        }

        canvas.rect(258.0F, 20.0F, 1.0F, 119.0F,
            Color{.red = 72, .green = 211, .blue = 148});
        canvas.rect(231.0F, 88.0F, 62.0F, 1.0F,
            Color{.red = 72, .green = 211, .blue = 148});

        const float occluderX = 96.0F + 38.0F * time;
        canvas.rect(occluderX, 58.0F, 55.0F, 54.0F,
            Color{.red = 35, .green = 101, .blue = 224});
        canvas.rect(occluderX + 4.0F, 62.0F, 47.0F, 3.0F,
            Color{.red = 238, .green = 147, .blue = 41});
        canvas.rect(occluderX + 43.0F, 69.0F, 3.0F, 3.0F,
            Color{.red = 250, .green = 250, .blue = 250});

        const float filamentX = 43.0F + 23.0F * time;
        canvas.rect(filamentX, 37.0F, 2.0F, 73.0F,
            Color{.red = 244, .green = 52, .blue = 83});

        const float edgeX = -7.0F + 14.0F * time;
        canvas.rect(edgeX, 145.0F, 11.0F, 9.0F,
            Color{.red = 241, .green = 224, .blue = 62});

        canvas.detailRect(occluderX + 41.0F, 67.0F, 7.0F, 7.0F);
        canvas.detailRect(filamentX - 1.0F, 35.0F, 4.0F, 77.0F);
        canvas.detailRect(edgeX - 1.0F, 143.0F, 13.0F, 13.0F);
    }

    void drawCar(Canvas& canvas, const float left, const float top,
            const Color body, const bool facingRight) {
        canvas.rect(left + 3.0F, top + 6.0F, 30.0F, 10.0F, body);
        const std::array<std::pair<float, float>, 3> hood{
            facingRight ? std::pair{left + 33.0F, top + 7.0F} :
                std::pair{left + 3.0F, top + 7.0F},
            facingRight ? std::pair{left + 39.0F, top + 12.0F} :
                std::pair{left - 3.0F, top + 12.0F},
            facingRight ? std::pair{left + 33.0F, top + 15.0F} :
                std::pair{left + 3.0F, top + 15.0F},
        };
        canvas.triangle(hood, body);
        canvas.rect(left + 9.0F, top + 1.0F, 17.0F, 7.0F,
            Color{.red = 91, .green = 188, .blue = 228});
        canvas.line(left + 17.5F, top + 1.0F, left + 17.5F, top + 8.0F,
            Color{.red = 230, .green = 241, .blue = 247});
        canvas.rect(left + 4.0F, top + 9.0F, 3.0F, 2.0F,
            Color{.red = 254, .green = 80, .blue = 54});
        canvas.rect(left + 30.0F, top + 9.0F, 3.0F, 2.0F,
            Color{.red = 255, .green = 242, .blue = 165});
        canvas.circle(left + 9.0F, top + 17.0F, 4.0F,
            Color{.red = 18, .green = 21, .blue = 27});
        canvas.circle(left + 29.0F, top + 17.0F, 4.0F,
            Color{.red = 18, .green = 21, .blue = 27});
        canvas.circle(left + 9.0F, top + 17.0F, 1.5F,
            Color{.red = 174, .green = 183, .blue = 190});
        canvas.circle(left + 29.0F, top + 17.0F, 1.5F,
            Color{.red = 174, .green = 183, .blue = 190});
        canvas.detailRect(left + 3.0F, top, 36.0F, 22.0F);
    }

    void renderTraffic(Canvas& canvas, const float time) {
        renderBackground(
            canvas,
            Color{.red = 36, .green = 60, .blue = 91},
            Color{.red = 138, .green = 154, .blue = 164}
        );
        canvas.circle(267.0F, 28.0F, 12.0F,
            Color{.red = 249, .green = 196, .blue = 93});
        for (int building = 0; building < 9; ++building) {
            const float left = static_cast<float>(building * 39 - 8);
            const float height = static_cast<float>(30 + (building * 17) % 37);
            canvas.rect(left, 91.0F - height, 34.0F, height,
                Color{
                    .red = static_cast<uint8_t>(39 + building * 5),
                    .green = static_cast<uint8_t>(47 + building * 4),
                    .blue = static_cast<uint8_t>(61 + building * 3),
                });
            for (int window = 0; window < 3; ++window)
                canvas.rect(left + 6.0F + window * 9.0F, 61.0F, 3.0F, 8.0F,
                    Color{.red = 244, .green = 203, .blue = 102});
        }
        canvas.rect(0.0F, 91.0F, 321.0F, 90.0F,
            Color{.red = 36, .green = 40, .blue = 47});
        canvas.line(0.0F, 118.0F, 321.0F, 118.0F,
            Color{.red = 226, .green = 229, .blue = 221}, 2.0F);
        canvas.line(0.0F, 151.0F, 321.0F, 151.0F,
            Color{.red = 226, .green = 229, .blue = 221}, 2.0F);
        for (int dash = -1; dash < 9; ++dash) {
            const float offset = wrap(static_cast<float>(dash * 44) - time * 24.0F, 396.0F) - 40.0F;
            canvas.rect(offset, 133.0F, 24.0F, 3.0F,
                Color{.red = 250, .green = 218, .blue = 76});
        }

        const float nearCarX = -44.0F + time * 225.0F;
        const float farCarX = 299.0F - time * 181.0F;
        drawCar(canvas, nearCarX, 130.0F,
            Color{.red = 224, .green = 49, .blue = 64}, true);
        drawCar(canvas, farCarX, 96.0F,
            Color{.red = 43, .green = 139, .blue = 224}, false);
        canvas.rect(nearCarX + 7.0F, 159.0F, 29.0F, 2.0F,
            Color{.red = 100, .green = 50, .blue = 50});

        canvas.rect(212.0F, 47.0F, 5.0F, 134.0F,
            Color{.red = 19, .green = 24, .blue = 30});
        canvas.rect(199.0F, 48.0F, 31.0F, 20.0F,
            Color{.red = 27, .green = 32, .blue = 39});
        canvas.circle(207.0F, 57.0F, 4.0F,
            Color{.red = 235, .green = 65, .blue = 49});
        canvas.detailRect(198.0F, 47.0F, 33.0F, 22.0F);
    }

    void drawPerson(Canvas& canvas, const float centerX, const float footY,
            const float phase, const Color shirt) {
        const float sway = std::sin(phase) * 1.5F;
        const float limb = std::sin(phase * 1.7F) * 5.0F;
        canvas.circle(centerX + sway, footY - 28.0F, 4.2F,
            Color{.red = 205, .green = 151, .blue = 111});
        canvas.line(centerX + sway, footY - 23.0F, centerX, footY - 10.0F,
            shirt, 5.0F);
        canvas.line(centerX, footY - 10.0F, centerX - limb, footY,
            Color{.red = 31, .green = 39, .blue = 54}, 2.0F);
        canvas.line(centerX, footY - 10.0F, centerX + limb, footY,
            Color{.red = 31, .green = 39, .blue = 54}, 2.0F);
        canvas.line(centerX, footY - 20.0F, centerX + limb, footY - 11.0F,
            shirt, 2.0F);
        canvas.line(centerX, footY - 20.0F, centerX - limb, footY - 12.0F,
            shirt, 2.0F);
        canvas.detailRect(centerX - 8.0F, footY - 34.0F, 16.0F, 36.0F);
    }

    void renderCrowd(Canvas& canvas, const float time) {
        renderBackground(
            canvas,
            Color{.red = 72, .green = 126, .blue = 177},
            Color{.red = 211, .green = 194, .blue = 158}
        );
        canvas.rect(0.0F, 105.0F, 321.0F, 76.0F,
            Color{.red = 79, .green = 91, .blue = 79});
        canvas.rect(0.0F, 140.0F, 321.0F, 41.0F,
            Color{.red = 123, .green = 115, .blue = 98});
        for (int post = 0; post < 33; ++post)
            canvas.rect(static_cast<float>(post * 10), 83.0F, 1.0F, 58.0F,
                Color{.red = 193, .green = 200, .blue = 193});
        for (int rail = 0; rail < 5; ++rail)
            canvas.line(0.0F, 86.0F + rail * 11.0F, 321.0F,
                86.0F + rail * 11.0F,
                Color{.red = 156, .green = 164, .blue = 158});

        constexpr std::array shirts{
            Color{.red = 220, .green = 69, .blue = 58},
            Color{.red = 58, .green = 139, .blue = 213},
            Color{.red = 240, .green = 176, .blue = 56},
            Color{.red = 107, .green = 190, .blue = 100},
            Color{.red = 171, .green = 91, .blue = 204},
            Color{.red = 219, .green = 102, .blue = 156},
            Color{.red = 63, .green = 196, .blue = 185},
        };
        for (size_t index = 0; index < shirts.size(); ++index) {
            const float direction = index % 2 == 0 ? 1.0F : -1.0F;
            const float centerX = 28.0F + static_cast<float>(index) * 43.0F +
                direction * time * (9.0F + static_cast<float>(index) * 2.0F);
            const float footY = 150.0F + static_cast<float>(index % 3) * 7.0F;
            drawPerson(
                canvas,
                centerX,
                footY,
                time * 7.0F + static_cast<float>(index) * 0.93F,
                shirts.at(index)
            );
        }

        const float bannerX = 83.0F + time * 29.0F;
        canvas.rect(bannerX, 48.0F, 71.0F, 25.0F,
            Color{.red = 243, .green = 235, .blue = 208});
        canvas.rect(bannerX + 7.0F, 55.0F, 57.0F, 3.0F,
            Color{.red = 38, .green = 45, .blue = 54});
        canvas.rect(bannerX + 16.0F, 63.0F, 39.0F, 2.0F,
            Color{.red = 214, .green = 71, .blue = 61});
        canvas.line(bannerX + 4.0F, 73.0F, bannerX - 4.0F, 107.0F,
            Color{.red = 59, .green = 46, .blue = 35}, 2.0F);
        canvas.line(bannerX + 67.0F, 73.0F, bannerX + 77.0F, 107.0F,
            Color{.red = 59, .green = 46, .blue = 35}, 2.0F);
        canvas.detailRect(bannerX, 47.0F, 72.0F, 27.0F);
    }

    void renderCameraPan(Canvas& canvas, const float time) {
        renderBackground(
            canvas,
            Color{.red = 21, .green = 79, .blue = 135},
            Color{.red = 157, .green = 203, .blue = 225},
            false
        );
        const float farOffset = time * 9.0F;
        for (int mountain = -1; mountain < 6; ++mountain) {
            const float left = static_cast<float>(mountain * 76) - farOffset;
            const std::array<std::pair<float, float>, 3> points{
                std::pair{left, 112.0F},
                std::pair{left + 38.0F, 51.0F + static_cast<float>((mountain & 1) * 13)},
                std::pair{left + 81.0F, 112.0F},
            };
            canvas.triangle(points,
                Color{.red = 72, .green = 104, .blue = 119});
        }
        canvas.rect(0.0F, 109.0F, 321.0F, 72.0F,
            Color{.red = 64, .green = 112, .blue = 74});

        const float middleOffset = time * 33.0F;
        for (int building = -1; building < 8; ++building) {
            const float left = static_cast<float>(building * 52) - middleOffset;
            const float height = static_cast<float>(34 + (building * building + 17) % 29);
            canvas.rect(left, 119.0F - height, 39.0F, height,
                Color{.red = 82, .green = 73, .blue = 67});
            for (int window = 0; window < 3; ++window)
                canvas.rect(left + 7.0F + window * 10.0F, 93.0F, 3.0F, 8.0F,
                    Color{.red = 232, .green = 191, .blue = 89});
        }

        const float nearOffset = time * 77.0F;
        for (int tree = -1; tree < 7; ++tree) {
            const float center = static_cast<float>(tree * 67 + 16) - nearOffset;
            canvas.rect(center - 3.0F, 107.0F, 6.0F, 74.0F,
                Color{.red = 61, .green = 43, .blue = 31});
            canvas.circle(center, 102.0F, 18.0F,
                Color{.red = 30, .green = 90, .blue = 47});
            canvas.circle(center - 11.0F, 109.0F, 11.0F,
                Color{.red = 38, .green = 111, .blue = 54});
        }

        canvas.line(-10.0F - nearOffset, 34.0F, 331.0F - nearOffset, 57.0F,
            Color{.red = 24, .green = 29, .blue = 33}, 1.0F);
        canvas.line(-10.0F - nearOffset, 42.0F, 331.0F - nearOffset, 65.0F,
            Color{.red = 24, .green = 29, .blue = 33}, 1.0F);

        const float droneX = 52.0F + time * 198.0F;
        const float droneY = 42.0F + std::sin(time * std::numbers::pi_v<float>) * 9.0F;
        canvas.rect(droneX - 8.0F, droneY - 3.0F, 16.0F, 7.0F,
            Color{.red = 211, .green = 78, .blue = 57});
        canvas.line(droneX - 15.0F, droneY - 7.0F, droneX + 15.0F,
            droneY - 7.0F, Color{.red = 31, .green = 35, .blue = 41});
        canvas.circle(droneX - 12.0F, droneY - 7.0F, 2.0F,
            Color{.red = 220, .green = 229, .blue = 233});
        canvas.circle(droneX + 12.0F, droneY - 7.0F, 2.0F,
            Color{.red = 220, .green = 229, .blue = 233});
        canvas.detailRect(droneX - 17.0F, droneY - 11.0F, 34.0F, 17.0F);
        canvas.detailRect(0.0F, 31.0F, 321.0F, 38.0F);
    }

    void renderHudDisocclusion(Canvas& canvas, const float time) {
        renderBackground(
            canvas,
            Color{.red = 22, .green = 25, .blue = 43},
            Color{.red = 67, .green = 76, .blue = 91}
        );
        for (int stripe = 0; stripe < 20; ++stripe)
            canvas.line(static_cast<float>(stripe * 21 - 60), 31.0F,
                static_cast<float>(stripe * 21 + 18), 128.0F,
                stripe % 2 == 0 ?
                    Color{.red = 196, .green = 79, .blue = 50} :
                    Color{.red = 218, .green = 170, .blue = 57},
                3.0F);
        canvas.rect(0.0F, 128.0F, 321.0F, 53.0F,
            Color{.red = 35, .green = 39, .blue = 47});

        const float doorX = 64.0F + time * 113.0F;
        canvas.rect(doorX, 22.0F, 91.0F, 108.0F,
            Color{.red = 32, .green = 88, .blue = 121});
        canvas.rect(doorX + 5.0F, 28.0F, 81.0F, 4.0F,
            Color{.red = 114, .green = 202, .blue = 218});
        canvas.rect(doorX + 72.0F, 76.0F, 7.0F, 4.0F,
            Color{.red = 231, .green = 194, .blue = 79});
        canvas.line(doorX + 13.0F, 39.0F, doorX + 13.0F, 117.0F,
            Color{.red = 87, .green = 151, .blue = 173});

        const float targetX = 279.0F - time * 122.0F;
        const float targetY = 95.0F - std::sin(time * std::numbers::pi_v<float>) * 25.0F;
        canvas.circle(targetX, targetY, 9.0F,
            Color{.red = 222, .green = 57, .blue = 67});
        canvas.circle(targetX, targetY, 5.0F,
            Color{.red = 242, .green = 235, .blue = 219});
        canvas.circle(targetX, targetY, 2.0F,
            Color{.red = 40, .green = 45, .blue = 53});

        for (int particle = 0; particle < 13; ++particle) {
            const float phase = time * (29.0F + particle) + particle * 17.0F;
            const float xPosition = wrap(phase * 3.1F, 337.0F) - 8.0F;
            const float yPosition = 41.0F + wrap(phase * 1.7F, 83.0F);
            canvas.rect(xPosition, yPosition, 2.0F, 2.0F,
                Color{.red = 224, .green = 232, .blue = 238});
        }

        // The HUD remains static while the world moves behind it.
        canvas.rect(9.0F, 10.0F, 93.0F, 18.0F,
            Color{.red = 8, .green = 12, .blue = 19, .alpha = 255});
        for (int health = 0; health < 8; ++health)
            canvas.rect(15.0F + health * 10.0F, 16.0F, 7.0F, 6.0F,
                health < 6 ? Color{.red = 78, .green = 216, .blue = 132} :
                    Color{.red = 69, .green = 76, .blue = 81});
        canvas.line(160.0F, 82.0F, 160.0F, 100.0F,
            Color{.red = 236, .green = 241, .blue = 242});
        canvas.line(151.0F, 91.0F, 169.0F, 91.0F,
            Color{.red = 236, .green = 241, .blue = 242});
        canvas.rect(265.0F, 149.0F, 45.0F, 20.0F,
            Color{.red = 8, .green = 12, .blue = 19});
        canvas.rect(273.0F, 156.0F, 28.0F, 2.0F,
            Color{.red = 236, .green = 241, .blue = 242});
        canvas.detailRect(8.0F, 9.0F, 95.0F, 20.0F);
        canvas.detailRect(149.0F, 80.0F, 22.0F, 22.0F);
        canvas.detailRect(targetX - 11.0F, targetY - 11.0F, 22.0F, 22.0F);
    }

    [[nodiscard]] std::vector<uint8_t> renderScene(
            const QualitySceneKind kind, const float time, const uint32_t width,
            const uint32_t height, std::vector<uint8_t>* detailMask = nullptr) {
        Canvas canvas(width, height, detailMask);
        switch (kind) {
        case QualitySceneKind::MotionBoundary:
            renderMotionBoundary(canvas, time);
            break;
        case QualitySceneKind::Traffic:
            renderTraffic(canvas, time);
            break;
        case QualitySceneKind::Crowd:
            renderCrowd(canvas, time);
            break;
        case QualitySceneKind::CameraPan:
            renderCameraPan(canvas, time);
            break;
        case QualitySceneKind::HudDisocclusion:
            renderHudDisocclusion(canvas, time);
            break;
        }
        return canvas.release();
    }

    void markRect(std::vector<uint8_t>& mask, const uint32_t width,
            const uint32_t height, const int left, const int top,
            const int rectangleWidth, const int rectangleHeight) {
        for (int row = std::max(top, 0);
                row < std::min(top + rectangleHeight, static_cast<int>(height)); ++row) {
            for (int column = std::max(left, 0);
                    column < std::min(left + rectangleWidth, static_cast<int>(width)); ++column) {
                mask.at(static_cast<size_t>(row) * width +
                    static_cast<size_t>(column)) = 1;
            }
        }
    }

    [[nodiscard]] std::vector<uint8_t> dilateMask(
            const std::span<const uint8_t> mask, const uint32_t width,
            const uint32_t height, const int radius) {
        std::vector<uint8_t> result(mask.size());
        for (uint32_t row = 0; row < height; ++row) {
            for (uint32_t column = 0; column < width; ++column) {
                if (mask[static_cast<size_t>(row) * width + column] == 0)
                    continue;
                markRect(
                    result,
                    width,
                    height,
                    static_cast<int>(column) - radius,
                    static_cast<int>(row) - radius,
                    radius * 2 + 1,
                    radius * 2 + 1
                );
            }
        }
        return result;
    }

    [[nodiscard]] std::vector<uint8_t> makeFocusMask(
            const std::span<const uint8_t> previous,
            const std::span<const uint8_t> current, const uint32_t width,
            const uint32_t height) {
        std::vector<uint8_t> changed(static_cast<size_t>(width) * height);
        for (uint32_t row = 0; row < height; ++row) {
            for (uint32_t column = 0; column < width; ++column) {
                const size_t pixel = static_cast<size_t>(row) * width + column;
                const size_t offset = pixel * 4;
                for (size_t channel = 0; channel < 3; ++channel) {
                    if (previous[offset + channel] != current[offset + channel]) {
                        changed[pixel] = 1;
                        break;
                    }
                }
            }
        }
        const int radius = std::max(2, static_cast<int>(std::lround(
            6.0F * static_cast<float>(width) / static_cast<float>(baseWidth)
        )));
        return dilateMask(changed, width, height, radius);
    }

    [[nodiscard]] double normalizedMean(const double total, const size_t samples) {
        return samples == 0 ? 0.0 : total /
            (static_cast<double>(samples) * 255.0);
    }

    [[nodiscard]] ImageQualityMetrics evaluate(
            const uint32_t width, const uint32_t height,
            const std::span<const uint8_t> reference,
            const std::span<const uint8_t> focusMask,
            const std::span<const uint8_t> detailMask,
            const std::span<const uint8_t> generated) {
        const size_t pixels = static_cast<size_t>(width) * height;
        const size_t expectedBytes = pixels * 4;
        if (reference.size() != expectedBytes || generated.size() != expectedBytes ||
                focusMask.size() != pixels || detailMask.size() != pixels)
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
                    static_cast<int>(reference[offset + channel])
                ));
                absoluteError += difference;
                maximumDifference = std::max(maximumDifference, difference);
                if (focusMask[pixel] != 0) {
                    focusAbsoluteError += difference;
                    ++focusSamples;
                }
                if (detailMask[pixel] != 0) {
                    detailAbsoluteError += difference;
                    ++detailSamples;
                }
            }
            if (focusMask[pixel] != 0) {
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
}

std::span<const QualitySceneDescriptor> mako::quality::qualitySceneCatalog() {
    return sceneCatalog;
}

std::optional<QualitySceneKind> mako::quality::qualitySceneFromName(
        const std::string_view name) {
    for (const auto& descriptor : sceneCatalog) {
        if (descriptor.name == name)
            return descriptor.kind;
    }
    return std::nullopt;
}

std::string_view mako::quality::qualitySceneName(const QualitySceneKind kind) {
    for (const auto& descriptor : sceneCatalog) {
        if (descriptor.kind == kind)
            return descriptor.name;
    }
    throw std::invalid_argument("unknown image-quality scene");
}

ImageQualityThresholds mako::quality::imageQualityThresholds(
        const QualitySceneKind kind) {
    ImageQualityThresholds thresholds{};
    switch (kind) {
    case QualitySceneKind::Traffic:
        thresholds.maximumFocusMeanAbsoluteError = 0.11;
        break;
    case QualitySceneKind::HudDisocclusion:
        thresholds.maximumFocusMeanAbsoluteError = 0.14;
        break;
    case QualitySceneKind::MotionBoundary:
    case QualitySceneKind::Crowd:
    case QualitySceneKind::CameraPan:
        break;
    }
    return thresholds;
}

RegressionScene mako::quality::makeImageQualityRegressionScene(
        const QualitySceneKind kind, const float interpolation) {
    if (!std::isfinite(interpolation) || interpolation <= 0.0F || interpolation >= 1.0F)
        throw std::invalid_argument("image-quality interpolation must be between zero and one");

    RegressionScene scene{
        .width = baseWidth,
        .height = baseHeight,
        .previous = renderScene(kind, 0.0F, baseWidth, baseHeight),
        .current = renderScene(kind, 1.0F, baseWidth, baseHeight),
        .reference = {},
        .focusMask = {},
        .detailMask = std::vector<uint8_t>(
            static_cast<size_t>(baseWidth) * baseHeight
        ),
    };
    scene.reference = renderScene(
        kind,
        interpolation,
        baseWidth,
        baseHeight,
        &scene.detailMask
    );
    scene.focusMask = makeFocusMask(
        scene.previous,
        scene.current,
        baseWidth,
        baseHeight
    );
    return scene;
}

RegressionScene mako::quality::makeAmdImageQualityRegressionScene() {
    return makeImageQualityRegressionScene(QualitySceneKind::MotionBoundary, 0.5F);
}

SpatialRegressionScene mako::quality::makeSpatialQualityRegressionScene(
        const QualitySceneKind kind, const float scalingFactor, const float time) {
    if (!std::isfinite(scalingFactor) || scalingFactor <= 1.0F || scalingFactor > 4.0F)
        throw std::invalid_argument("spatial quality scaling factor must be above one and at most four");
    if (!std::isfinite(time) || time < 0.0F || time > 1.0F)
        throw std::invalid_argument("spatial quality scene time must be between zero and one");

    const uint32_t presentationWidth = static_cast<uint32_t>(std::lround(
        static_cast<float>(baseWidth) * scalingFactor
    ));
    const uint32_t presentationHeight = static_cast<uint32_t>(std::lround(
        static_cast<float>(baseHeight) * scalingFactor
    ));
    SpatialRegressionScene scene{
        .sourceWidth = baseWidth,
        .sourceHeight = baseHeight,
        .presentationWidth = presentationWidth,
        .presentationHeight = presentationHeight,
        .source = renderScene(kind, time, baseWidth, baseHeight),
        .reference = {},
        .focusMask = {},
        .detailMask = std::vector<uint8_t>(
            static_cast<size_t>(presentationWidth) * presentationHeight
        ),
    };
    scene.reference = renderScene(
        kind,
        time,
        presentationWidth,
        presentationHeight,
        &scene.detailMask
    );
    const int radius = std::max(2, static_cast<int>(std::lround(
        4.0F * scalingFactor
    )));
    scene.focusMask = dilateMask(
        scene.detailMask,
        presentationWidth,
        presentationHeight,
        radius
    );
    return scene;
}

CombinedRegressionScene mako::quality::makeCombinedQualityRegressionScene(
        const QualitySceneKind kind, const float scalingFactor,
        const float interpolation) {
    if (!std::isfinite(interpolation) || interpolation <= 0.0F || interpolation >= 1.0F)
        throw std::invalid_argument("combined quality interpolation must be between zero and one");
    const auto spatialReference = makeSpatialQualityRegressionScene(
        kind, scalingFactor, interpolation
    );
    const auto previousReference = renderScene(
        kind,
        0.0F,
        spatialReference.presentationWidth,
        spatialReference.presentationHeight
    );
    const auto currentReference = renderScene(
        kind,
        1.0F,
        spatialReference.presentationWidth,
        spatialReference.presentationHeight
    );
    return {
        .sourceWidth = spatialReference.sourceWidth,
        .sourceHeight = spatialReference.sourceHeight,
        .presentationWidth = spatialReference.presentationWidth,
        .presentationHeight = spatialReference.presentationHeight,
        .previous = renderScene(kind, 0.0F, baseWidth, baseHeight),
        .current = renderScene(kind, 1.0F, baseWidth, baseHeight),
        .reference = spatialReference.reference,
        .focusMask = makeFocusMask(
            previousReference,
            currentReference,
            spatialReference.presentationWidth,
            spatialReference.presentationHeight
        ),
        .detailMask = spatialReference.detailMask,
    };
}

ImageQualityMetrics mako::quality::evaluateImageQuality(
        const RegressionScene& scene, const std::span<const uint8_t> generated) {
    return evaluate(
        scene.width,
        scene.height,
        scene.reference,
        scene.focusMask,
        scene.detailMask,
        generated
    );
}

ImageQualityMetrics mako::quality::evaluateImageQuality(
        const CombinedRegressionScene& scene,
        const std::span<const uint8_t> generated) {
    return evaluate(
        scene.presentationWidth,
        scene.presentationHeight,
        scene.reference,
        scene.focusMask,
        scene.detailMask,
        generated
    );
}

ImageQualityMetrics mako::quality::evaluateImageQuality(
        const SpatialRegressionScene& scene,
        const std::span<const uint8_t> generated) {
    return evaluate(
        scene.presentationWidth,
        scene.presentationHeight,
        scene.reference,
        scene.focusMask,
        scene.detailMask,
        generated
    );
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
