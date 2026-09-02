/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>

namespace mako::layer {

    /// One generated-frame delivery observation from the presentation path.
    ///
    /// "Accepted" means queued for presentation within the current delivery
    /// budget. It does not claim that the compositor has already scanned the
    /// image out.
    struct GeneratedFrameDelivery {
        size_t requested{0};
        size_t acceptedForPresentation{0};
    };

    /// Accumulates delivery observations for one scheduler evaluation window.
    ///
    /// The five-percent tolerance is intentionally integer based. Windows with
    /// fewer than twenty requested frames therefore require complete delivery,
    /// preserving the existing multiplier-validation contract.
    class GeneratedDeliveryWindow {
    public:
        void record(const GeneratedFrameDelivery sample) {
            this->requestedFrames += sample.requested;
            this->acceptedFrames += std::min(
                sample.requested, sample.acceptedForPresentation
            );
        }

        void reset() {
            this->requestedFrames = 0;
            this->acceptedFrames = 0;
        }

        [[nodiscard]] bool healthy() const {
            if (this->requestedFrames == 0)
                return true;
            const size_t toleratedMisses = this->requestedFrames / 20;
            return this->requestedFrames - this->acceptedFrames <=
                toleratedMisses;
        }

        [[nodiscard]] size_t requested() const {
            return this->requestedFrames;
        }

        [[nodiscard]] size_t acceptedForPresentation() const {
            return this->acceptedFrames;
        }

    private:
        size_t requestedFrames{0};
        size_t acceptedFrames{0};
    };

    struct ReplacementBackendStabilizationDecision {
        bool bypassBackend{false};
        bool resumed{false};
        bool diagnostic{false};
    };

    /// Keep a newly replaced WSI context on its real-frame path briefly before
    /// submitting temporal-history work to the private backend. Applications
    /// commonly cycle through several short-lived extents during startup; a
    /// bounded native-history gap prevents one transient replacement from
    /// stalling startup while preserving the existing, longer scheduler
    /// stabilization window before any generated frame is requested. The
    /// interval is armed only when the replacement's private backend is
    /// active; a later live enable is owned by configuration stabilization.
    class ReplacementBackendStabilization {
    public:
        using Clock = std::chrono::steady_clock;
        static constexpr auto duration = std::chrono::milliseconds(250);

        void begin(const bool replacement, const bool frameGenerationActive,
                const Clock::time_point now = Clock::now()) {
            this->until = replacement && frameGenerationActive
                ? std::optional<Clock::time_point>{now + duration}
                : std::nullopt;
            this->diagnosticEmitted = false;
        }

        [[nodiscard]] ReplacementBackendStabilizationDecision beforeFrame(
                const Clock::time_point now = Clock::now()) {
            if (!this->until)
                return {};
            if (now >= *this->until) {
                this->until.reset();
                return {.resumed = true};
            }
            const bool diagnostic = !this->diagnosticEmitted;
            this->diagnosticEmitted = true;
            return {
                .bypassBackend = true,
                .diagnostic = diagnostic,
            };
        }

        [[nodiscard]] bool active() const {
            return this->until.has_value();
        }

    private:
        std::optional<Clock::time_point> until;
        bool diagnosticEmitted{false};
    };

}
