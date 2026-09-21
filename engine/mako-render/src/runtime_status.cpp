/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "runtime_status.hpp"

#include "profile_update.hpp"

#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <system_error>
#include <utility>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

    constexpr std::string_view statusSuffix{".json"};
    constexpr std::string_view temporaryStatusSuffix{".json.tmp"};
    constexpr std::string_view livenessSuffix{".json.lock"};

    bool hasSuffix(
            const std::string_view value, const std::string_view suffix) {
        return value.size() >= suffix.size() &&
            value.substr(value.size() - suffix.size()) == suffix;
    }

    bool isDecimal(const std::string_view value) {
        if (value.empty())
            return false;
        for (const auto character : value) {
            if (character < '0' || character > '9')
                return false;
        }
        return true;
    }

    bool matchesManagedStatusStem(
            const std::string_view stem, const std::string_view roleMarker) {
        const auto rolePosition = stem.find(roleMarker);
        if (rolePosition == std::string_view::npos)
            return false;
        const auto processIdentity = stem.substr(0, rolePosition);
        const auto context = stem.substr(rolePosition + roleMarker.size());
        if (!isDecimal(context))
            return false;

        const auto identitySeparator = processIdentity.find('-');
        if (identitySeparator == std::string_view::npos)
            return isDecimal(processIdentity);
        return processIdentity.find('-', identitySeparator + 1) ==
                std::string_view::npos &&
            isDecimal(processIdentity.substr(0, identitySeparator)) &&
            isDecimal(processIdentity.substr(identitySeparator + 1));
    }

    bool isManagedStatusFilename(const std::string_view filename) {
        size_t suffixSize = 0;
        if (hasSuffix(filename, livenessSuffix))
            suffixSize = livenessSuffix.size();
        else if (hasSuffix(filename, temporaryStatusSuffix))
            suffixSize = temporaryStatusSuffix.size();
        else if (hasSuffix(filename, statusSuffix))
            suffixSize = statusSuffix.size();
        else
            return false;

        const auto stem = filename.substr(0, filename.size() - suffixSize);
        return matchesManagedStatusStem(stem, "-frame-generation-") ||
            matchesManagedStatusStem(stem, "-spatial-scaling-");
    }

    void removeManagedFile(const std::filesystem::path& path) noexcept {
        struct stat status {};
        if (::lstat(path.c_str(), &status) != 0 ||
                !S_ISREG(status.st_mode) || status.st_uid != ::geteuid())
            return;
        std::error_code error;
        std::filesystem::remove(path, error);
    }

    void pruneUnlockedStatusPair(
            const std::filesystem::path& livenessPath) noexcept {
        const auto descriptor = ::open(
            livenessPath.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW
        );
        if (descriptor < 0)
            return;

        struct stat descriptorStatus {};
        if (::fstat(descriptor, &descriptorStatus) != 0 ||
                !S_ISREG(descriptorStatus.st_mode) ||
                descriptorStatus.st_uid != ::geteuid() ||
                ::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
            ::close(descriptor);
            return;
        }

        const auto filename = livenessPath.filename().string();
        const auto statusPath = livenessPath.parent_path() /
            filename.substr(0, filename.size() - std::string_view{".lock"}.size());
        auto temporaryPath = statusPath;
        temporaryPath += ".tmp";
        removeManagedFile(statusPath);
        removeManagedFile(temporaryPath);
        removeManagedFile(livenessPath);
        ::close(descriptor);
    }

    bool pathEntryIsMissing(const std::filesystem::path& path) noexcept {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(path, error);
        return status.type() == std::filesystem::file_type::not_found &&
            (!error ||
             error == std::errc::no_such_file_or_directory);
    }

    void pruneInactiveRuntimeStatus(
            const std::filesystem::path& directory) noexcept {
        try {
            struct stat directoryStatus {};
            if (::lstat(directory.c_str(), &directoryStatus) != 0 ||
                    !S_ISDIR(directoryStatus.st_mode) ||
                    directoryStatus.st_uid != ::geteuid())
                return;

            std::error_code error;
            for (std::filesystem::directory_iterator iterator(directory, error),
                    end; iterator != end && !error;
                    iterator.increment(error)) {
                const auto filename = iterator->path().filename().string();
                if (hasSuffix(filename, livenessSuffix) &&
                        isManagedStatusFilename(filename))
                    pruneUnlockedStatusPair(iterator->path());
            }

            error.clear();
            for (std::filesystem::directory_iterator iterator(directory, error),
                    end; iterator != end && !error;
                    iterator.increment(error)) {
                const auto path = iterator->path();
                const auto filename = path.filename().string();
                if (!isManagedStatusFilename(filename))
                    continue;
                std::filesystem::path statusPath;
                if (hasSuffix(filename, statusSuffix)) {
                    statusPath = path;
                } else if (hasSuffix(filename, temporaryStatusSuffix)) {
                    statusPath = path.parent_path() /
                        filename.substr(
                            0, filename.size() -
                                std::string_view{".tmp"}.size()
                        );
                } else {
                    continue;
                }

                auto livenessPath = statusPath;
                livenessPath += ".lock";
                if (pathEntryIsMissing(livenessPath))
                    removeManagedFile(path);
            }
        } catch (...) {
            // Runtime status cleanup is observational and must never prevent
            // a game from creating its own status publisher.
        }
    }

    void pruneInactiveRuntimeStatusOnce(
            const std::filesystem::path& directory) noexcept {
        static std::atomic<pid_t> prunedProcess{0};
        const auto process = ::getpid();
        if (prunedProcess.exchange(process, std::memory_order_relaxed) == process)
            return;
        pruneInactiveRuntimeStatus(directory);
    }

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
        stream.imbue(std::locale::classic());
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
        stream.imbue(std::locale::classic());
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
    // Applications such as Dolphin install a process-wide locale with digit
    // grouping. JSON numbers never permit locale separators, so keep this
    // observational boundary independent from application stream state.
    stream.imbue(std::locale::classic());
    stream << std::boolalpha
           << '{'
           << "\"schema_version\":5"
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
           << ",\"frame_generation_active\":"
           << status.frameGenerationActive
           << ",\"spatial_scaling\":{"
           << "\"active\":" << status.spatialScalingActive
           << ",\"activation_supported\":"
           << status.spatialScalingActivationSupported
           << ",\"inactive_reason\":";
    if (status.spatialScalingInactiveReason)
        stream << jsonString(*status.spatialScalingInactiveReason);
    else
        stream << "null";
    stream << ",\"constraint_reason\":";
    if (status.spatialScalingConstraintReason)
        stream << jsonString(*status.spatialScalingConstraintReason);
    else
        stream << "null";
    stream << ",\"source_width\":" << status.spatialSourceWidth
           << ",\"source_height\":" << status.spatialSourceHeight
           << ",\"presentation_width\":"
           << status.spatialPresentationWidth
           << ",\"presentation_height\":"
           << status.spatialPresentationHeight
           << ",\"gamescope_target_width\":"
           << status.gamescopeTargetWidth
           << ",\"gamescope_target_height\":"
           << status.gamescopeTargetHeight
           << ",\"requested_method\":"
           << jsonString(ls::scalingMethodName(status.spatialRequestedMethod))
           << ",\"active_method\":"
           << jsonString(ls::scalingMethodName(status.spatialActiveMethod))
           << ",\"effective_factor\":"
           << status.spatialEffectiveFactor
           << ",\"pipeline\":" << jsonString(status.spatialPipeline)
           << ",\"supersampling_active\":"
           << status.spatialSupersamplingActive
           << ",\"fallback_reason\":";
    if (status.spatialFallbackReason)
        stream << jsonString(*status.spatialFallbackReason);
    else
        stream << "null";
    stream << ",\"non_supersampling_factor_ceiling\":";
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
         std::to_string(processStartTicks()) + "-" + this->role + "-" +
         std::to_string(contextId) + ".json");
    this->livenessPath = this->statusPath;
    this->livenessPath += ".lock";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (!error) {
        if (this->role == "frame-generation")
            pruneInactiveRuntimeStatusOnce(directory);
        this->livenessDescriptor = ::open(
            this->livenessPath.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0600
        );
        if (this->livenessDescriptor >= 0 &&
                ::flock(this->livenessDescriptor, LOCK_EX | LOCK_NB) != 0) {
            ::close(this->livenessDescriptor);
            this->livenessDescriptor = -1;
        }
    }
    if (this->livenessDescriptor < 0) {
        this->statusPath.clear();
        this->livenessPath.clear();
    }
}

mako::layer::RuntimeStatusPublisher::RuntimeStatusPublisher(
        RuntimeStatusPublisher&& other) noexcept :
    statusPath(std::exchange(other.statusPath, {})),
    livenessPath(std::exchange(other.livenessPath, {})),
    livenessDescriptor(std::exchange(other.livenessDescriptor, -1)),
    contextId(other.contextId), role(std::move(other.role)) {}

mako::layer::RuntimeStatusPublisher&
mako::layer::RuntimeStatusPublisher::operator=(
        RuntimeStatusPublisher&& other) noexcept {
    if (this == &other)
        return *this;
    this->remove();
    this->statusPath = std::exchange(other.statusPath, {});
    this->livenessPath = std::exchange(other.livenessPath, {});
    this->livenessDescriptor = std::exchange(
        other.livenessDescriptor, -1
    );
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
    if (this->livenessDescriptor >= 0) {
        ::close(this->livenessDescriptor);
        this->livenessDescriptor = -1;
    }
    error.clear();
    std::filesystem::remove(this->livenessPath, error);
    this->statusPath.clear();
    this->livenessPath.clear();
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
