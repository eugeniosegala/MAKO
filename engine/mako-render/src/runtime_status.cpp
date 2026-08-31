/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "runtime_status.hpp"

#include "profile_update.hpp"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

#include <unistd.h>

namespace {

    uint64_t processStartTicks() {
        std::ifstream input("/proc/self/stat");
        std::string line;
        std::getline(input, line);
        const auto commandEnd = line.rfind(')');
        if (commandEnd == std::string::npos || commandEnd + 2 >= line.size())
            return 0;
        std::istringstream fields(line.substr(commandEnd + 2));
        std::string field;
        for (unsigned int fieldNumber = 3; fieldNumber <= 22; ++fieldNumber) {
            if (!(fields >> field))
                return 0;
        }
        try {
            return std::stoull(field);
        } catch (...) {
            return 0;
        }
    }

    std::string jsonString(const std::string_view value) {
        std::ostringstream stream;
        stream << '"';
        for (const unsigned char character : value) {
            switch (character) {
                case '"':
                    stream << "\\\"";
                    break;
                case '\\':
                    stream << "\\\\";
                    break;
                case '\b':
                    stream << "\\b";
                    break;
                case '\f':
                    stream << "\\f";
                    break;
                case '\n':
                    stream << "\\n";
                    break;
                case '\r':
                    stream << "\\r";
                    break;
                case '\t':
                    stream << "\\t";
                    break;
                default:
                    if (character < 0x20) {
                        stream << "\\u" << std::hex << std::setw(4)
                               << std::setfill('0')
                               << static_cast<unsigned int>(character)
                               << std::dec;
                    } else {
                        stream << character;
                    }
            }
        }
        stream << '"';
        return stream.str();
    }

    std::string profileJson(const ls::GameConf& profile) {
        std::ostringstream stream;
        stream << std::boolalpha << std::setprecision(9);
        stream << '{'
               << "\"name\":" << jsonString(profile.name)
               << ",\"gpu\":";
        if (profile.gpu)
            stream << jsonString(*profile.gpu);
        else
            stream << "null";
        stream << ",\"multiplier\":" << profile.multiplier
               << ",\"frame_generation_enabled\":"
               << profile.frame_generation_enabled
               << ",\"scaling_enabled\":" << profile.scaling_enabled
               << ",\"scaling_method\":"
               << jsonString(ls::scalingMethodName(profile.scaling_method))
               << ",\"scaling_factor\":" << profile.scaling_factor
               << ",\"scaling_supersampling\":"
               << profile.scaling_supersampling
               << ",\"scaling_sharpness\":" << profile.scaling_sharpness
               << ",\"frame_generation_refresh_threshold\":"
               << profile.frame_generation_refresh_threshold
               << ",\"base_fps_cap\":" << profile.base_fps_cap
               << ",\"adaptive\":" << profile.adaptive
               << ",\"adaptive_auto_base_fps_cap\":"
               << profile.adaptive_auto_base_fps_cap
               << ",\"target_fps\":" << profile.target_fps
               << ",\"adaptive_max_multiplier\":"
               << profile.adaptive_max_multiplier
               << ",\"adaptive_stable_cadence\":"
               << profile.adaptive_stable_cadence
               << ",\"dynamic_cadence_recovery\":"
               << profile.dynamic_cadence_recovery
               << ",\"dynamic_cadence_probe_interval_seconds\":"
               << profile.dynamic_cadence_probe_interval_seconds
               << ",\"ultra_performance\":" << profile.ultra_performance
               << ",\"flow_scale\":" << profile.flow_scale
               << ",\"effective_flow_scale\":"
               << ls::effectiveFlowScale(profile)
               << ",\"performance_mode\":" << profile.performance_mode
               << ",\"effective_performance_mode\":"
               << ls::effectivePerformanceMode(profile)
               << ",\"pacing\":\"none\""
               << ",\"required_generated_capacity\":"
               << mako::layer::generatedFrameCapacityForActivePolicy(profile)
               << '}';
        return stream.str();
    }

}

std::string mako::layer::runtimeStatusJson(
        const RuntimeStatusRecord& status, const uint64_t processId,
        const uint64_t processStartTicks, const uint64_t contextId,
        const std::string_view role,
        const int64_t updatedUnixMilliseconds) {
    std::ostringstream stream;
    stream << std::boolalpha
           << '{'
           << "\"schema_version\":3"
           << ",\"pid\":" << processId
           << ",\"process_start_ticks\":" << processStartTicks
           << ",\"context\":" << contextId
           << ",\"role\":" << jsonString(role)
           << ",\"updated_unix_ms\":" << updatedUnixMilliseconds
           << ",\"state_revision\":" << status.stateRevision
           << ",\"phase\":"
           << jsonString(runtimeApplicationPhaseName(status.phase))
           << ",\"reason\":" << jsonString(status.reason)
           << ",\"pending\":{"
           << "\"frame_generation_private\":"
           << status.frameGenerationPrivatePending
           << ",\"spatial_private\":" << status.spatialPrivatePending
           << ",\"swapchain_recreation\":"
           << status.swapchainRecreationPending
           << ",\"process_restart\":" << status.processRestartPending
           << '}'
           << ",\"applied_generated_capacity\":"
           << status.appliedGeneratedCapacity
           << ",\"spatial_scaling\":{"
           << "\"active\":" << status.spatialScalingActive
           << ",\"activation_supported\":"
           << status.spatialScalingActivationSupported
           << ",\"inactive_reason\":";
    if (status.spatialScalingInactiveReason)
        stream << jsonString(*status.spatialScalingInactiveReason);
    else
        stream << "null";
    stream << ",\"source_width\":" << status.spatialSourceWidth
           << ",\"source_height\":" << status.spatialSourceHeight
           << ",\"gamescope_target_width\":"
           << status.gamescopeTargetWidth
           << ",\"gamescope_target_height\":"
           << status.gamescopeTargetHeight
           << ",\"non_supersampling_factor_ceiling\":";
    if (status.nonSupersamplingFactorCeiling)
        stream << *status.nonSupersamplingFactorCeiling;
    else
        stream << "null";
    stream << '}'
           << ",\"requested\":" << profileJson(status.requestedProfile)
           << ",\"applied\":" << profileJson(status.appliedProfile)
           << ",\"error\":";
    if (status.error)
        stream << jsonString(*status.error);
    else
        stream << "null";
    stream << "}\n";
    return stream.str();
}

mako::layer::RuntimeStatusPublisher::RuntimeStatusPublisher(
        const uint64_t contextId, const std::string_view role) :
    contextId(contextId), role(role) {
    const auto directory = ls::findConfigurationFile().parent_path() /
        "runtime-state";
    this->statusPath = directory /
        (std::to_string(static_cast<uint64_t>(::getpid())) + "-" +
         this->role + "-" + std::to_string(contextId) + ".json");
}

mako::layer::RuntimeStatusPublisher::RuntimeStatusPublisher(
        RuntimeStatusPublisher&& other) noexcept :
    statusPath(std::exchange(other.statusPath, {})),
    contextId(other.contextId), role(std::move(other.role)) {}

mako::layer::RuntimeStatusPublisher&
mako::layer::RuntimeStatusPublisher::operator=(
        RuntimeStatusPublisher&& other) noexcept {
    if (this == &other)
        return *this;
    this->remove();
    this->statusPath = std::exchange(other.statusPath, {});
    this->contextId = other.contextId;
    this->role = std::move(other.role);
    return *this;
}

mako::layer::RuntimeStatusPublisher::~RuntimeStatusPublisher() {
    this->remove();
}

void mako::layer::RuntimeStatusPublisher::remove() noexcept {
    if (this->statusPath.empty())
        return;
    std::error_code error;
    std::filesystem::remove(this->statusPath, error);
}

void mako::layer::RuntimeStatusPublisher::publish(
        const RuntimeStatusRecord& status) noexcept {
    try {
        if (this->statusPath.empty())
            return;
        std::error_code error;
        std::filesystem::create_directories(
            this->statusPath.parent_path(), error
        );
        if (error)
            return;

        const auto updated = std::chrono::duration_cast<
            std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count();
        auto temporary = this->statusPath;
        temporary += ".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
                return;
            output << runtimeStatusJson(
                status, static_cast<uint64_t>(::getpid()),
                processStartTicks(), this->contextId, this->role, updated
            );
            output.flush();
            if (!output)
                return;
        }
        std::filesystem::rename(temporary, this->statusPath, error);
        if (error) {
            std::filesystem::remove(this->statusPath, error);
            error.clear();
            std::filesystem::rename(temporary, this->statusPath, error);
        }
        if (error)
            std::filesystem::remove(temporary, error);
    } catch (...) {
        // Runtime status is observational. It must never affect a game's
        // swapchain, presentation result, or profile transition.
    }
}
