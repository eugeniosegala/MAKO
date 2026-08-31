/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "runtime_status.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include <unistd.h>

namespace {

    void expect(const bool condition, const char* message) {
        if (condition)
            return;
        std::cerr << "runtime status test failed: " << message << '\n';
        std::exit(1);
    }

}

int main() {
    ls::GameConf applied;
    applied.name = "game \"profile\"";
    applied.multiplier = 2;
    applied.adaptive = false;

    auto requested = applied;
    requested.multiplier = 5;
    requested.performance_mode = true;
    requested.flow_scale = 0.5F;

    const mako::layer::RuntimeStatusRecord record{
        .phase = mako::layer::RuntimeApplicationPhase::Draining,
        .reason = "frame-generation-resources",
        .stateRevision = 9,
        .requestedProfile = requested,
        .appliedProfile = applied,
        .appliedGeneratedCapacity = 1,
        .frameGenerationActive = true,
        .frameGenerationPrivatePending = true,
        .spatialScalingActive = false,
        .spatialScalingActivationSupported = false,
        .spatialScalingInactiveReason =
            "gamescope-wsi-surface-unproven",
        .spatialSourceWidth = 960,
        .spatialSourceHeight = 540,
        .spatialPresentationWidth = 1280,
        .spatialPresentationHeight = 720,
        .gamescopeTargetWidth = 1280,
        .gamescopeTargetHeight = 800,
        .spatialRequestedMethod = ls::ScalingMethod::Ls1,
        .spatialActiveMethod = ls::ScalingMethod::Mako,
        .spatialEffectiveFactor = 4.0 / 3.0,
        .spatialPipeline = "pre-frame-generation",
        .spatialSupersamplingActive = false,
        .spatialFallbackReason = "translator unavailable",
        .nonSupersamplingFactorCeiling = 4.0 / 3.0,
    };
    const auto json = mako::layer::runtimeStatusJson(
        record, 123, 321, 456, "frame-generation", 789
    );
    expect(json.find("\"schema_version\":4") != std::string::npos,
        "schema version missing");
    expect(json.find("\"phase\":\"draining\"") != std::string::npos,
        "phase missing");
    expect(json.find("\"process_start_ticks\":321") != std::string::npos,
        "process start identity missing");
    expect(json.find("game \\\"profile\\\"") != std::string::npos,
        "profile name was not JSON escaped");
    expect(json.find("\"multiplier\":5") != std::string::npos,
        "requested multiplier missing");
    expect(json.find("\"required_generated_capacity\":4") !=
            std::string::npos,
        "requested capacity missing");
    expect(json.find("\"applied_generated_capacity\":1") !=
            std::string::npos,
        "applied capacity missing");
    expect(json.find("\"frame_generation_active\":true") !=
            std::string::npos,
        "effective frame generation state missing");
    expect(json.find("\"presentation_width\":1280") !=
            std::string::npos,
        "spatial presentation width missing");
    expect(json.find("\"requested_method\":\"ls1\"") !=
            std::string::npos,
        "requested spatial method missing");
    expect(json.find("\"active_method\":\"mako\"") !=
            std::string::npos,
        "active spatial method missing");
    expect(json.find("\"pipeline\":\"pre-frame-generation\"") !=
            std::string::npos,
        "spatial pipeline placement missing");
    expect(json.find("\"fallback_reason\":\"translator unavailable\"") !=
            std::string::npos,
        "spatial fallback reason missing");
    expect(json.find("\"non_supersampling_factor_ceiling\":1.33333") !=
            std::string::npos,
        "spatial scaling display ceiling missing");

    const auto temporaryRoot = std::filesystem::temp_directory_path() /
        ("mako-runtime-status-test-" +
         std::to_string(static_cast<uint64_t>(::getpid())));
    std::filesystem::create_directories(temporaryRoot);
    const auto configPath = temporaryRoot / "conf.toml";
    expect(::setenv("MAKO_CONFIG", configPath.c_str(), 1) == 0,
        "failed to set MAKO_CONFIG");

    std::filesystem::path statusPath;
    {
        mako::layer::RuntimeStatusPublisher publisher(
            456, "frame-generation"
        );
        statusPath = publisher.path();
        publisher.publish(record);
        expect(std::filesystem::is_regular_file(statusPath),
            "atomic status file was not published");
        std::ifstream input(statusPath, std::ios::binary);
        const std::string published{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };
        expect(published.find("\"state_revision\":9") !=
                std::string::npos,
            "published state revision missing");
    }
    expect(!std::filesystem::exists(statusPath),
        "status file was not retired with its context");
    std::filesystem::remove_all(temporaryRoot);
    return 0;
}
