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
#include <sys/wait.h>

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

    const auto runtimeRoot = temporaryRoot / "runtime-state";
    std::filesystem::create_directories(runtimeRoot);
    const auto staleStatusPath = runtimeRoot /
        "101-202-frame-generation-303.json";
    const auto staleLivenessPath = runtimeRoot /
        "101-202-frame-generation-303.json.lock";
    const auto orphanStatusPath = runtimeRoot /
        "102-spatial-scaling-304.json";
    const auto orphanTemporaryPath = runtimeRoot /
        "103-204-frame-generation-305.json.tmp";
    const auto orphanLivenessPath = runtimeRoot /
        "104-205-spatial-scaling-306.json.lock";
    const auto heldStatusPath = runtimeRoot /
        "105-206-frame-generation-307.json";
    const auto heldLivenessPath = runtimeRoot /
        "105-206-frame-generation-307.json.lock";
    const auto linkedStatusPath = runtimeRoot /
        "106-207-frame-generation-308.json";
    const auto linkedStatusTarget = temporaryRoot / "linked-target";
    const auto protectedStatusPath = runtimeRoot /
        "107-208-frame-generation-309.json";
    const auto protectedLivenessPath = runtimeRoot /
        "107-208-frame-generation-309.json.lock";
    const auto unrelatedJsonPath = runtimeRoot / "user-notes.json";
    std::ofstream(staleStatusPath) << "stale";
    std::ofstream(staleLivenessPath) << "stale";
    std::ofstream(orphanStatusPath) << "orphan";
    std::ofstream(orphanTemporaryPath) << "temporary";
    std::ofstream(orphanLivenessPath) << "orphan";
    std::ofstream(heldStatusPath) << "held";
    std::ofstream(linkedStatusTarget) << "target";
    std::ofstream(protectedStatusPath) << "protected";
    std::ofstream(unrelatedJsonPath) << "unrelated";
    std::filesystem::create_directory(protectedLivenessPath);
    std::filesystem::create_symlink(linkedStatusTarget, linkedStatusPath);
    const auto heldDescriptor = ::open(
        heldLivenessPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600
    );
    expect(heldDescriptor >= 0,
        "failed to create held cleanup-test liveness lock");
    expect(::flock(heldDescriptor, LOCK_EX | LOCK_NB) == 0,
        "failed to hold cleanup-test liveness lock");

    std::filesystem::path statusPath;
    std::filesystem::path livenessPath;
    {
        mako::layer::RuntimeStatusPublisher publisher(
            456, "frame-generation"
        );
        expect(!std::filesystem::exists(staleStatusPath) &&
                !std::filesystem::exists(staleLivenessPath),
            "unlocked stale status pair was not pruned at startup");
        expect(!std::filesystem::exists(orphanStatusPath),
            "orphan status was not pruned at startup");
        expect(!std::filesystem::exists(orphanTemporaryPath),
            "orphan temporary status was not pruned at startup");
        expect(!std::filesystem::exists(orphanLivenessPath),
            "orphan liveness lock was not pruned at startup");
        expect(std::filesystem::exists(heldStatusPath) &&
                std::filesystem::exists(heldLivenessPath),
            "held status pair was pruned while its publisher was active");
        expect(std::filesystem::is_symlink(linkedStatusPath) &&
                std::filesystem::exists(linkedStatusTarget),
            "orphan status symlink was followed or removed");
        expect(std::filesystem::exists(protectedStatusPath) &&
                std::filesystem::exists(protectedLivenessPath),
            "unopenable status pair blocked startup or was removed");
        expect(std::filesystem::exists(unrelatedJsonPath),
            "an unrelated JSON file was removed from runtime-state");
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
    expect(::flock(heldDescriptor, LOCK_UN) == 0,
        "failed to release cleanup-test liveness lock");
    ::close(heldDescriptor);
    {
        mako::layer::RuntimeStatusPublisher publisher(
            789, "frame-generation"
        );
        expect(std::filesystem::exists(heldStatusPath) &&
                std::filesystem::exists(heldLivenessPath),
            "runtime-state cleanup repeated within one Renderer process");
    }
    const auto cleanupChild = ::fork();
    expect(cleanupChild >= 0,
        "failed to create next-process cleanup test child");
    if (cleanupChild == 0) {
        bool stalePairRemoved = false;
        {
            mako::layer::RuntimeStatusPublisher publisher(
                987, "frame-generation"
            );
            stalePairRemoved =
                !std::filesystem::exists(heldStatusPath) &&
                !std::filesystem::exists(heldLivenessPath);
        }
        ::_exit(stalePairRemoved ? 0 : 1);
    }
    int cleanupChildStatus = 0;
    expect(::waitpid(cleanupChild, &cleanupChildStatus, 0) == cleanupChild,
        "failed to wait for next-process cleanup test child");
    expect(WIFEXITED(cleanupChildStatus) &&
            WEXITSTATUS(cleanupChildStatus) == 0,
        "a new Renderer process did not prune the stale status pair");
    std::filesystem::remove_all(temporaryRoot);
    return 0;
}
