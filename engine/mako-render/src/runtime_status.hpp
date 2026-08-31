/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-common/configuration/config.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace mako::layer {

    enum class RuntimeApplicationPhase : uint8_t {
        Active,
        Debouncing,
        Preparing,
        Draining,
        Failed,
        SwapchainRecreation,
        ProcessRestart,
    };

    [[nodiscard]] constexpr std::string_view runtimeApplicationPhaseName(
            const RuntimeApplicationPhase phase) noexcept {
        switch (phase) {
            case RuntimeApplicationPhase::Active:
                return "active";
            case RuntimeApplicationPhase::Debouncing:
                return "debouncing";
            case RuntimeApplicationPhase::Preparing:
                return "preparing";
            case RuntimeApplicationPhase::Draining:
                return "draining";
            case RuntimeApplicationPhase::Failed:
                return "failed";
            case RuntimeApplicationPhase::SwapchainRecreation:
                return "swapchain-recreation";
            case RuntimeApplicationPhase::ProcessRestart:
                return "process-restart";
        }
        return "failed";
    }

    struct RuntimeStatusRecord {
        RuntimeApplicationPhase phase{RuntimeApplicationPhase::Active};
        std::string reason{"configuration"};
        uint64_t stateRevision{0};
        ls::GameConf requestedProfile;
        ls::GameConf appliedProfile;
        size_t appliedGeneratedCapacity{0};
        bool frameGenerationPrivatePending{false};
        bool spatialPrivatePending{false};
        bool swapchainRecreationPending{false};
        bool processRestartPending{false};
        bool spatialScalingActive{false};
        bool spatialScalingActivationSupported{true};
        std::optional<std::string> spatialScalingInactiveReason;
        std::optional<std::string> error;
    };

    /// Publish the current requested-versus-applied runtime state for Decky
    /// and other local controllers. Writes occur only when state changes, use
    /// atomic replacement, and never participate in the presentation result.
    class RuntimeStatusPublisher {
    public:
        RuntimeStatusPublisher() = default;
        RuntimeStatusPublisher(uint64_t contextId, std::string_view role);
        RuntimeStatusPublisher(const RuntimeStatusPublisher&) = delete;
        RuntimeStatusPublisher& operator=(const RuntimeStatusPublisher&) = delete;
        RuntimeStatusPublisher(RuntimeStatusPublisher&& other) noexcept;
        RuntimeStatusPublisher& operator=(RuntimeStatusPublisher&& other) noexcept;
        ~RuntimeStatusPublisher();

        void publish(const RuntimeStatusRecord& status) noexcept;

        [[nodiscard]] const std::filesystem::path& path() const noexcept {
            return this->statusPath;
        }

    private:
        void remove() noexcept;

        std::filesystem::path statusPath;
        uint64_t contextId{0};
        std::string role;
    };

    [[nodiscard]] std::string runtimeStatusJson(
        const RuntimeStatusRecord& status, uint64_t processId,
        uint64_t processStartTicks, uint64_t contextId, std::string_view role,
        int64_t updatedUnixMilliseconds);

}
