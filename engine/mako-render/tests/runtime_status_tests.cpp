/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "runtime_status.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <locale>
#include <string>

#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>

namespace {

    void expect(const bool condition, const char* message) {
        if (condition)
            return;
        std::cerr << "runtime status test failed: " << message << '\n';
        std::exit(1);
    }

    class LocalizedNumericPunctuation final : public std::numpunct<char> {
    protected:
        char do_decimal_point() const override {
            return ',';
        }

        char do_thousands_sep() const override {
            return '.';
        }

        std::string do_grouping() const override {
            return "\3";
        }
    };

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
        .spatialScalingConstraintReason =
            "variable-surface-memory-budget",
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
    expect(json.find("\"schema_version\":5") != std::string::npos,
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
    expect(json.find(
            "\"constraint_reason\":\"variable-surface-memory-budget\""
        ) != std::string::npos,
        "spatial constraint reason missing");
    expect(json.find("\"non_supersampling_factor_ceiling\":1.33333") !=
            std::string::npos,
        "spatial scaling display ceiling missing");

    const auto previousLocale = std::locale();
    std::locale::global(std::locale(
        std::locale::classic(), new LocalizedNumericPunctuation
    ));
    const auto localeIndependentJson = mako::layer::runtimeStatusJson(
        record, 1234567, 2345678, 3456789, "frame-generation", 4567890
    );
    std::locale::global(previousLocale);
    expect(localeIndependentJson.find("\"pid\":1234567") !=
            std::string::npos,
        "application locale changed the runtime PID");
    expect(localeIndependentJson.find("\"context\":3456789") !=
            std::string::npos,
        "application locale changed the runtime context ID");
    expect(localeIndependentJson.find("\"presentation_width\":1280") !=
            std::string::npos,
        "application locale changed a runtime extent");
    expect(localeIndependentJson.find("\"effective_factor\":1.33333") !=
            std::string::npos,
        "application locale changed a runtime floating-point value");

    const auto temporaryRoot = std::filesystem::temp_directory_path() /
        ("mako-runtime-status-test-" +
         std::to_string(static_cast<uint64_t>(::getpid())));
    std::filesystem::create_directories(temporaryRoot);
    const auto configPath = temporaryRoot / "conf.toml";
    expect(::setenv("MAKO_CONFIG", configPath.c_str(), 1) == 0,
        "failed to set MAKO_CONFIG");

    std::filesystem::path statusPath;
    std::filesystem::path livenessPath;
    {
        mako::layer::RuntimeStatusPublisher publisher(
            456, "frame-generation"
        );
        statusPath = publisher.path();
        livenessPath = statusPath;
        livenessPath += ".lock";
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
        const auto competingDescriptor = ::open(
            livenessPath.c_str(), O_RDWR | O_CLOEXEC
        );
        expect(competingDescriptor >= 0,
            "runtime liveness lock was not published");
        expect(::flock(competingDescriptor, LOCK_EX | LOCK_NB) != 0,
            "runtime liveness lock was not held by its publisher");
        ::close(competingDescriptor);
    }
    expect(!std::filesystem::exists(statusPath),
        "status file was not retired with its context");
    expect(!std::filesystem::exists(livenessPath),
        "liveness lock was not retired with its context");
    std::filesystem::remove_all(temporaryRoot);
    return 0;
}
