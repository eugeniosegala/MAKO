/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/quality/image_quality.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
    void expect(const bool condition, const std::string_view message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }

    template<typename Callable>
    void expectInvalidArgument(Callable&& callable, const std::string_view message) {
        try {
            callable();
        } catch (const std::invalid_argument&) {
            return;
        }
        expect(false, message);
    }
}

int main() {
    const auto catalog = mako::quality::qualitySceneCatalog();
    expect(catalog.size() == 5,
        "the quality catalog must expose all procedural workload families");
    std::set<std::string> sceneNames;
    for (const auto& descriptor : catalog) {
        expect(!descriptor.name.empty() && !descriptor.description.empty(),
            "every quality scene needs a stable name and useful description");
        expect(sceneNames.insert(std::string(descriptor.name)).second,
            "quality scene names must be unique");
        expect(mako::quality::qualitySceneFromName(descriptor.name) == descriptor.kind,
            "scene-name parsing must round-trip through the catalog");
        expect(mako::quality::qualitySceneName(descriptor.kind) == descriptor.name,
            "scene-kind formatting must round-trip through the catalog");

        const auto catalogScene = mako::quality::makeImageQualityRegressionScene(
            descriptor.kind, 0.5F
        );
        const auto repeatedScene = mako::quality::makeImageQualityRegressionScene(
            descriptor.kind, 0.5F
        );
        expect(catalogScene.width == 321 && catalogScene.height == 181,
            "every temporal scene must keep odd, non-workgroup-aligned dimensions");
        expect(catalogScene.previous == repeatedScene.previous &&
                catalogScene.current == repeatedScene.current &&
                catalogScene.reference == repeatedScene.reference &&
                catalogScene.focusMask == repeatedScene.focusMask &&
                catalogScene.detailMask == repeatedScene.detailMask,
            "every procedural quality scene must be deterministic");
        expect(catalogScene.previous != catalogScene.current,
            "every temporal scene needs visible endpoint motion");
        expect(std::ranges::count(catalogScene.focusMask, uint8_t{1}) > 500,
            "every temporal scene needs a meaningful motion focus region");
        expect(std::ranges::count(catalogScene.detailMask, uint8_t{1}) > 50,
            "every temporal scene needs explicit fine-detail coverage");
        const auto catalogPerfect = mako::quality::evaluateImageQuality(
            catalogScene, catalogScene.reference
        );
        expect(mako::quality::passesImageQualityRegression(catalogPerfect),
            "every ideal temporal reference must pass its quality guardrails");
    }
    expect(!mako::quality::qualitySceneFromName("unknown-scene").has_value(),
        "unknown scene names must be rejected without an implicit fallback");
    expect(mako::quality::imageQualityThresholds(
            mako::quality::QualitySceneKind::MotionBoundary
        ).maximumFocusMeanAbsoluteError == 0.075,
        "the legacy motion-boundary focus threshold must remain stable");
    expect(mako::quality::imageQualityThresholds(
            mako::quality::QualitySceneKind::Traffic
        ).maximumFocusMeanAbsoluteError == 0.11 &&
            mako::quality::imageQualityThresholds(
                mako::quality::QualitySceneKind::HudDisocclusion
            ).maximumFocusMeanAbsoluteError == 0.14,
        "large-disocclusion scenes must use their calibrated focus guardrails");
    expect(mako::quality::spatialQualityThresholds.maximumMeanAbsoluteError == 0.08 &&
            mako::quality::spatialQualityThresholds.maximumFocusMeanAbsoluteError == 0.12 &&
            mako::quality::spatialQualityThresholds.maximumSevereFocusErrorFraction == 0.20 &&
            mako::quality::spatialQualityThresholds.maximumDetailMeanAbsoluteError == 0.15,
        "spatial quality guardrails must remain at their hardware-calibrated values");

    const auto scene = mako::quality::makeAmdImageQualityRegressionScene();
    const auto repeated = mako::quality::makeAmdImageQualityRegressionScene();
    expect(scene.width == 321 && scene.height == 181,
        "the regression must keep odd, non-workgroup-aligned dimensions");
    expect(scene.previous == repeated.previous && scene.current == repeated.current &&
            scene.reference == repeated.reference,
        "the synthetic regression scene must be deterministic");
    expect(std::ranges::count(scene.focusMask, uint8_t{1}) > 1000,
        "the scene must include a meaningful motion/disocclusion focus region");
    expect(std::ranges::count(scene.detailMask, uint8_t{1}) > 100,
        "the scene must include thin-detail coverage");

    const auto perfect = mako::quality::evaluateImageQuality(scene, scene.reference);
    expect(mako::quality::passesImageQualityRegression(perfect),
        "the reference midpoint must pass its own quality guardrails");
    expect(perfect.meanAbsoluteError == 0.0 &&
            perfect.focusMeanAbsoluteError == 0.0 &&
            perfect.detailMeanAbsoluteError == 0.0,
        "the reference midpoint must score zero error");

    const auto duplicatedEndpoint = mako::quality::evaluateImageQuality(
        scene, scene.current
    );
    expect(!mako::quality::passesImageQualityRegression(duplicatedEndpoint),
        "a duplicated endpoint must fail the midpoint ghosting guardrails");

    auto blendedEndpoints = scene.previous;
    for (size_t byte = 0; byte < blendedEndpoints.size(); ++byte) {
        blendedEndpoints[byte] = static_cast<uint8_t>(
            (static_cast<uint16_t>(scene.previous[byte]) + scene.current[byte]) / 2
        );
    }
    const auto doubleImage = mako::quality::evaluateImageQuality(
        scene, blendedEndpoints
    );
    expect(!mako::quality::passesImageQualityRegression(doubleImage),
        "a blended double image must fail the midpoint ghosting guardrails");

    auto corrupted = scene.reference;
    for (size_t pixel = 0; pixel < scene.focusMask.size(); ++pixel) {
        if (scene.focusMask[pixel] == 0)
            continue;
        const size_t offset = pixel * 4;
        corrupted[offset] = 255;
        corrupted[offset + 1] = 0;
        corrupted[offset + 2] = 255;
    }
    const auto corruption = mako::quality::evaluateImageQuality(scene, corrupted);
    expect(!mako::quality::passesImageQualityRegression(corruption),
        "large focus-region corruption must fail the regression");

    expect(mako::quality::flowScaleOnePreset.flowScale == 1.0F &&
            mako::quality::flowScaleOnePreset.multiplier == 2 &&
            !mako::quality::flowScaleOnePreset.performanceMode,
        "the quality preset must use full-resolution flow and the quality shader path");
    const float inverseFlow = 1.0F / mako::quality::flowScaleOnePreset.flowScale;
    expect(static_cast<uint32_t>(static_cast<float>(scene.width) / inverseFlow) ==
            scene.width &&
            static_cast<uint32_t>(static_cast<float>(scene.height) / inverseFlow) ==
            scene.height,
        "Flow Scale 1.0 must preserve the exact odd source extent");

    const auto explicitLegacy = mako::quality::makeImageQualityRegressionScene(
        mako::quality::QualitySceneKind::MotionBoundary, 0.5F
    );
    expect(scene.previous == explicitLegacy.previous &&
            scene.current == explicitLegacy.current &&
            scene.reference == explicitLegacy.reference &&
            scene.focusMask == explicitLegacy.focusMask &&
            scene.detailMask == explicitLegacy.detailMask,
        "the legacy AMD entry point must remain the motion-boundary midpoint");

    for (const auto& descriptor : catalog) {
        const auto spatial = mako::quality::makeSpatialQualityRegressionScene(
            descriptor.kind, 1.5F, 0.37F
        );
        const auto spatialRepeated = mako::quality::makeSpatialQualityRegressionScene(
            descriptor.kind, 1.5F, 0.37F
        );
        expect(spatial.sourceWidth == 321 && spatial.sourceHeight == 181 &&
                spatial.presentationWidth == 482 && spatial.presentationHeight == 272,
            "spatial scenes must preserve the source and rounded presentation extents");
        expect(spatial.source == spatialRepeated.source &&
                spatial.reference == spatialRepeated.reference &&
                spatial.focusMask == spatialRepeated.focusMask &&
                spatial.detailMask == spatialRepeated.detailMask,
            "spatial scenes must be deterministic");
        expect(spatial.source.size() ==
                static_cast<size_t>(spatial.sourceWidth) * spatial.sourceHeight * 4 &&
                spatial.reference.size() == static_cast<size_t>(
                    spatial.presentationWidth
                ) * spatial.presentationHeight * 4,
            "spatial scenes must produce complete RGBA8 source and reference images");
        expect(std::ranges::count(spatial.focusMask, uint8_t{1}) > 100 &&
                std::ranges::count(spatial.detailMask, uint8_t{1}) > 50,
            "spatial scenes must preserve focused fine-detail coverage");
        const auto spatialPerfect = mako::quality::evaluateImageQuality(
            spatial, spatial.reference
        );
        expect(mako::quality::passesImageQualityRegression(
                spatialPerfect, mako::quality::spatialQualityThresholds),
            "every ideal spatial reference must pass its quality guardrails");

        auto spatialCorruption = spatial.reference;
        for (size_t pixel = 0; pixel < spatial.focusMask.size(); ++pixel) {
            if (spatial.focusMask[pixel] == 0)
                continue;
            const size_t offset = pixel * 4;
            spatialCorruption[offset] = 255;
            spatialCorruption[offset + 1] = 0;
            spatialCorruption[offset + 2] = 255;
        }
        expect(!mako::quality::passesImageQualityRegression(
                mako::quality::evaluateImageQuality(spatial, spatialCorruption),
                mako::quality::spatialQualityThresholds),
            "focused spatial corruption must fail the broad regression guardrails");

        const auto combined = mako::quality::makeCombinedQualityRegressionScene(
            descriptor.kind, 1.5F, 0.37F
        );
        const auto combinedRepeated =
            mako::quality::makeCombinedQualityRegressionScene(
                descriptor.kind, 1.5F, 0.37F
            );
        expect(combined.sourceWidth == spatial.sourceWidth &&
                combined.sourceHeight == spatial.sourceHeight &&
                combined.presentationWidth == spatial.presentationWidth &&
                combined.presentationHeight == spatial.presentationHeight,
            "combined scenes must use the same source-to-presentation extent contract");
        expect(combined.previous == combinedRepeated.previous &&
                combined.current == combinedRepeated.current &&
                combined.reference == combinedRepeated.reference &&
                combined.focusMask == combinedRepeated.focusMask &&
                combined.detailMask == combinedRepeated.detailMask,
            "combined frame-generation and scaling scenes must be deterministic");
        expect(combined.previous != combined.current &&
                std::ranges::count(combined.focusMask, uint8_t{1}) > 500 &&
                std::ranges::count(combined.detailMask, uint8_t{1}) > 50,
            "combined scenes must retain motion and fine-detail coverage");
        const auto combinedPerfect = mako::quality::evaluateImageQuality(
            combined, combined.reference
        );
        expect(mako::quality::passesImageQualityRegression(
                combinedPerfect,
                mako::quality::imageQualityThresholds(descriptor.kind)),
            "every ideal combined reference must pass its scene guardrails");
    }

    expectInvalidArgument([] {
        static_cast<void>(mako::quality::makeImageQualityRegressionScene(
            mako::quality::QualitySceneKind::Traffic, 0.0F
        ));
    }, "temporal endpoints must not be accepted as interpolation targets");
    const auto exactSpatial = mako::quality::makeSpatialQualityRegressionScene(
        mako::quality::QualitySceneKind::Traffic,
        320, 180, 640, 360, 0.37F
    );
    expect(exactSpatial.sourceWidth == 320 &&
            exactSpatial.sourceHeight == 180 &&
            exactSpatial.presentationWidth == 640 &&
            exactSpatial.presentationHeight == 360,
        "exact spatial scenes must preserve production extents");
    expect(exactSpatial.source.size() == 320U * 180U * 4U &&
            exactSpatial.reference.size() == 640U * 360U * 4U,
        "exact spatial scenes must render complete source and reference images");
    expectInvalidArgument([] {
        static_cast<void>(mako::quality::makeSpatialQualityRegressionScene(
            mako::quality::QualitySceneKind::Crowd, 1.0F
        ));
    }, "spatial regression must reject a non-upscaling factor");
    expectInvalidArgument([] {
        static_cast<void>(mako::quality::makeSpatialQualityRegressionScene(
            mako::quality::QualitySceneKind::CameraPan, 4.1F
        ));
    }, "spatial regression must reject excessive output dimensions");
    expectInvalidArgument([] {
        static_cast<void>(mako::quality::makeSpatialQualityRegressionScene(
            mako::quality::QualitySceneKind::HudDisocclusion, 1.5F, 1.1F
        ));
    }, "spatial regression must reject an out-of-range scene time");
    expectInvalidArgument([] {
        static_cast<void>(mako::quality::makeCombinedQualityRegressionScene(
            mako::quality::QualitySceneKind::Traffic, 1.5F, 1.0F
        ));
    }, "combined regression must reject temporal endpoints");
    expectInvalidArgument([] {
        static_cast<void>(mako::quality::makeSpatialQualityRegressionScene(
            mako::quality::QualitySceneKind::Traffic,
            1920, 1080, 1280, 720, 0.5F
        ));
    }, "exact spatial scenes must reject downscaling extents");

    std::cout << "Procedural image-quality regression tests passed\n";
    return 0;
}
