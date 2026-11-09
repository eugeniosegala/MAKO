/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "generated_frame_plan.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace mako::layer {

    /// Explicit Gamescope presentation state published on the verified
    /// server-zero root. Keep the Steam preference, connector capability and
    /// compositor-active feedback separate: an overlay may temporarily stop
    /// adaptive scanout without changing the user's VRR policy.
    struct GamescopePresentationFeedback {
        std::optional<bool> vrrEnabled;
        std::optional<bool> vrrCapable;
        std::optional<bool> vrrActive;
        std::optional<bool> allowTearing;

        friend bool operator==(
            const GamescopePresentationFeedback&,
            const GamescopePresentationFeedback&) = default;

        /// Select VRR-aware pacing from an explicit active signal or from a
        /// requested policy whose capability is not conclusively absent.
        /// Missing properties preserve MAKO's existing fixed-refresh policy.
        [[nodiscard]] bool variableRefreshRequested() const noexcept {
            return this->vrrActive == true ||
                (this->vrrEnabled == true && this->vrrCapable != false);
        }

        [[nodiscard]] bool fixedRefreshPacingEligible() const noexcept {
            return !this->variableRefreshRequested();
        }
    };

    /// Gamescope updates root properties independently. A missing or malformed
    /// read is not an explicit policy transition, so retain the last valid
    /// value for that field instead of flapping pacing ownership.
    [[nodiscard]] inline GamescopePresentationFeedback
    mergeGamescopePresentationFeedback(
            GamescopePresentationFeedback current,
            const GamescopePresentationFeedback& sampled) noexcept {
        if (sampled.vrrEnabled)
            current.vrrEnabled = sampled.vrrEnabled;
        if (sampled.vrrCapable)
            current.vrrCapable = sampled.vrrCapable;
        if (sampled.vrrActive)
            current.vrrActive = sampled.vrrActive;
        if (sampled.allowTearing)
            current.allowTearing = sampled.allowTearing;
        return current;
    }

    /// A combined spatial-scaling WSI replacement is a last-stage recovery.
    /// It requires a failed in-place attempt and is still forbidden when the
    /// create-time scaling admission was memory constrained. Natural and
    /// profile-required recreations retain their existing ownership rules.
    [[nodiscard]] constexpr bool automaticRecoveryRecreationAllowed(
            const bool spatialScalingActive,
            const bool failedInPlaceRecovery = false,
            const bool spatialScalingMemoryConstrained = false) noexcept {
        return !spatialScalingActive ||
            (failedInPlaceRecovery && !spatialScalingMemoryConstrained);
    }

    struct ScalingAdmissionRetryKey {
        uint32_t sourceWidth{0};
        uint32_t sourceHeight{0};
        uint64_t policyRevision{0};

        friend bool operator==(
            const ScalingAdmissionRetryKey&,
            const ScalingAdmissionRetryKey&) = default;
    };

    /// A live resolution change may sample VK_EXT_memory_budget before the
    /// driver's usage estimate has caught up with the retired scaled context.
    /// Permit one later recreation for that exact source/policy episode. A
    /// replacement which is still constrained consumes the attempt instead of
    /// creating a recreation loop; changing source or scaling policy starts a
    /// distinct episode.
    class ScalingAdmissionRetrySurfaceBudget {
    public:
        [[nodiscard]] bool available(
                const ScalingAdmissionRetryKey& key) {
            if (!this->key || *this->key != key) {
                this->key = key;
                this->attempted = false;
            }
            return !this->attempted;
        }

        void record(const ScalingAdmissionRetryKey& key) {
            if (!this->key || *this->key != key)
                this->key = key;
            this->attempted = true;
        }

    private:
        std::optional<ScalingAdmissionRetryKey> key;
        bool attempted{false};
    };

    /// Ordinary readiness checks preserve progress. A new interruption must
    /// discard partial pre-interruption history and start a complete warm-up.
    [[nodiscard]] constexpr size_t historyWarmupFramesAfterRequest(
            const size_t remaining, const size_t required,
            const bool restart) noexcept {
        return restart || remaining == 0 ? required : remaining;
    }

    /// Event recovery measures the resumed application's cadence from the
    /// application-present boundary, excluding MAKO's private work. Ordinary
    /// transport recovery retains the 3.3 completion boundary so a generated-
    /// image timeout cannot turn its own recovery work into new gameplay
    /// cadence evidence. Fixed + Dynamic Cadence Recovery and Adaptive share
    /// this scheduler boundary.
    [[nodiscard]] constexpr std::chrono::steady_clock::time_point
    historyWarmupCadenceBoundary(
            const std::chrono::steady_clock::time_point frameStarted,
            const std::chrono::steady_clock::time_point recoveryCompleted,
            const bool explicitTransitionActive) noexcept {
        return explicitTransitionActive ? frameStarted : recoveryCompleted;
    }

    struct GamescopeFocusFeedback {
        using Clock = std::chrono::steady_clock;
        std::optional<bool> gameFocused;
        std::optional<Clock::time_point> sampledAt;
        std::optional<Clock::time_point> openedAt;
        std::optional<Clock::time_point> returnedAt;
        uint64_t returnSequence{0};

        [[nodiscard]] bool fresh(const Clock::time_point now) const {
            return sampledAt && now >= *sampledAt &&
                now - *sampledAt <= std::chrono::seconds{1};
        }
        [[nodiscard]] bool menuOpen(const Clock::time_point now) const {
            return fresh(now) && gameFocused == false;
        }
    };

    /// MAKO's current presentation transport owns one swapchain per lower
    /// submit/present sequence. A Vulkan present batch supplies its binary
    /// waits and per-swapchain pNext arrays once for the whole batch, so it
    /// cannot be decomposed safely until an explicit fan-out transport owns
    /// those semantics.
    [[nodiscard]] constexpr bool shouldRejectManagedMultiSwapchainPresent(
            const uint32_t swapchainCount,
            const bool containsManagedMakoContext) noexcept {
        return swapchainCount > 1 && containsManagedMakoContext;
    }

    [[nodiscard]] inline bool environmentFlagEnabled(const char* value) {
        if (!value)
            return false;
        const std::string_view flag(value);
        return flag == "1" || flag == "true" ||
            flag == "yes" || flag == "on";
    }

    /// Process-start policy shared by HDR classification and presentation
    /// transport selection. Gamescope WSI membership is fixed before Vulkan
    /// instance creation. The isolated surface bridge carries explicit HDR
    /// colour and metadata without switching to the legacy full-WSI transport.
    struct PresentationEnvironmentPolicy {
        bool gamescopeWsiDisabled{false};
        bool hdrExposureDisabled{false};
    };

    [[nodiscard]] inline PresentationEnvironmentPolicy
    resolvePresentationEnvironmentPolicy(
            const char* explicitHdrDisable, const char* dxvkHdr,
            const char* gamescopeWsiDisable) {
        const bool wsiDisabled = environmentFlagEnabled(gamescopeWsiDisable);
        return {
            .gamescopeWsiDisabled = wsiDisabled,
            .hdrExposureDisabled = environmentFlagEnabled(explicitHdrDisable) ||
                (wsiDisabled && (!explicitHdrDisable ||
                    std::string_view(explicitHdrDisable) != "0")) ||
                (dxvkHdr && !environmentFlagEnabled(dxvkHdr)),
        };
    }

    /// SteamOS/Gamescope integration boundary.
    ///
    /// This is not a general Vulkan rule that Linux SDR requires FIFO and HDR
    /// requires MAILBOX. Gamescope's WSI layer runs above MAKO, implements the
    /// application's pacing there, then forwards a MAILBOX lower swapchain.
    /// MAKO runs below that hook and expands one application present into
    /// synthetic present(s) plus the original, so those injected presents do
    /// not pass through Gamescope's upper QueuePresent policy individually.
    ///
    /// The fork's established SDR path therefore owns a private FIFO sequence:
    /// it provides ordering and backpressure for every generated/original
    /// image. HDR-capable Gamescope swapchains retain Gamescope's lower WSI
    /// contract because its format/colour-space normalization and HDR feedback
    /// are part of that bridge. The decision is made once from create-time
    /// capability and must remain stable for the lifetime of the swapchain.
    // OrderedSdr is the established private ordered transport name. Explicit
    // HDR on the isolated bridge uses that same scheduling contract.
    enum class PresentationTransport {
        OrderedSdr,
        GamescopeHdr,
    };

    /// Gamescope can normalize the colour space before MAKO sees it, so the
    /// HDR-capable create-time format is the stable discriminator while live
    /// application-HDR feedback is still provisional. Live feedback may
    /// rebuild colour resources; it must not change the transport underneath
    /// an already-created VkSwapchainKHR.
    [[nodiscard]] inline PresentationTransport selectPresentationTransport(
            const bool gamescopeDetected,
            const bool hdrCapableSwapchain,
            const PresentationEnvironmentPolicy& environment) {
        return gamescopeDetected && hdrCapableSwapchain &&
                !environment.gamescopeWsiDisabled &&
                !environment.hdrExposureDisabled
            ? PresentationTransport::GamescopeHdr
            : PresentationTransport::OrderedSdr;
    }

    /// Generated images on the Gamescope HDR bridge are opportunistic: waiting
    /// for one blocks the application's real present and caused deterministic
    /// 7-13 ms stalls at 120 Hz. A zero timeout means "native frame wins", not
    /// a backend failure. Headroom-bearing ordered and legacy paths retain
    /// their configured ceiling because their synchronous FIFO contract is
    /// intentionally different.
    [[nodiscard]] inline uint64_t generatedImageAcquireTimeout(
            const bool gamescopeHdrTransport,
            const std::optional<uint64_t> configuredTimeout) {
        if (gamescopeHdrTransport)
            return 0;
        return configuredTimeout.value_or(std::numeric_limits<uint64_t>::max());
    }

    /// The real frame already belongs to the application's minimum. Ordered
    /// delivery acquires and presents generated images one at a time, so a
    /// complete generated batch does not require another unused relief image.
    /// Only an undersized pool needs opportunistic admission before backend
    /// work. A pool that just fits uses the bounded acquire budget below.
    [[nodiscard]] inline bool orderedGeneratedBatchNeedsNonblockingAdmission(
            const uint32_t requestedMinImages,
            const size_t returnedImages,
            const size_t requestedGeneratedFrames) noexcept {
        if (requestedGeneratedFrames == 0)
            return false;
        if (returnedImages < requestedMinImages)
            return true;
        return returnedImages - requestedMinImages <
            requestedGeneratedFrames;
    }

    /// Moving preparation into source-cap slack must not move a zero-wait
    /// admission attempt ahead of the image-release opportunity it previously
    /// had. Check the active policy's full capacity because planning can
    /// qualify a higher generated count after this decision.
    [[nodiscard]] inline bool orderedGeneratedBatchCanPrepareBeforeCap(
            const uint32_t requestedMinImages,
            const size_t returnedImages,
            const size_t maximumGeneratedFrames,
            const bool admissionUnderPressure) noexcept {
        return maximumGeneratedFrames > 0 && !admissionUnderPressure &&
            !orderedGeneratedBatchNeedsNonblockingAdmission(
                requestedMinImages, returnedImages, maximumGeneratedFrames);
    }

    enum class AdaptiveOrderedDeliveryPolicy {
        NotApplicable,
        VariableRefreshBounded,
    };

    /// Adapt normal Adaptive ordered delivery to the compositor's explicit
    /// pacing owner. Allow Tearing is deliberately not an input: MAKO's lower
    /// SDR swapchain is FIFO regardless of that Steam preference.
    [[nodiscard]] inline AdaptiveOrderedDeliveryPolicy
    selectAdaptiveOrderedDeliveryPolicy(
            const bool adaptive,
            const bool orderedTransport,
            const bool orderedAcquireRecoveryProbe,
            const bool gamescopeHdrTransport,
            const bool headroomTightOrderedBatch,
            const GamescopePresentationFeedback& presentationFeedback,
            const size_t requestedGeneratedFrames) noexcept {
        if (!adaptive || !orderedTransport || orderedAcquireRecoveryProbe ||
                gamescopeHdrTransport || headroomTightOrderedBatch ||
                requestedGeneratedFrames == 0) {
            return AdaptiveOrderedDeliveryPolicy::NotApplicable;
        }
        return presentationFeedback.variableRefreshRequested()
            ? AdaptiveOrderedDeliveryPolicy::VariableRefreshBounded
            : AdaptiveOrderedDeliveryPolicy::NotApplicable;
    }

    /// Variable refresh retains the established finite application-present
    /// ceiling because lower-image release is not locked to a fixed output
    /// period. Fixed refresh retains the 3.3 ordered acquire contract and does
    /// not call this helper. Preserve any shorter explicit ceiling and never
    /// derive it from source cadence that may already include MAKO's own
    /// acquire wait.
    [[nodiscard]] inline std::optional<uint64_t>
    adaptiveVariableRefreshDeliveryAcquireBudget(
            const AdaptiveOrderedDeliveryPolicy policy,
            const std::optional<uint64_t> configuredBudget) noexcept {
        if (policy !=
                AdaptiveOrderedDeliveryPolicy::VariableRefreshBounded) {
            return configuredBudget;
        }
        constexpr uint64_t maximumVariableRefreshBudget = 50'000'000;
        return std::min(
            configuredBudget.value_or(maximumVariableRefreshBudget),
            maximumVariableRefreshBudget
        );
    }

    /// After one bounded Adaptive VRR acquire times out, retry lower-image
    /// admission before backend work and without blocking. This circuit
    /// breaker preserves the validated scheduler load while Gamescope is
    /// temporarily holding every lower image, and automatically resumes as
    /// soon as an image is available again.
    [[nodiscard]] constexpr bool
    adaptiveOrderedDeliveryNeedsPressurePreflight(
            const AdaptiveOrderedDeliveryPolicy policy,
            const bool generatedImageAdmissionUnderPressure,
            const size_t requestedGeneratedFrames) noexcept {
        return policy ==
                AdaptiveOrderedDeliveryPolicy::VariableRefreshBounded &&
            generatedImageAdmissionUnderPressure &&
            requestedGeneratedFrames > 0;
    }

    /// Headroom preflight can reject an active multiplier experiment, but a
    /// zero-wait miss is not evidence against an already validated load.
    /// Preserve that load so the next real frame can retry admission without
    /// a stabilization interval and multiplier backoff. Bounded VRR timeouts
    /// use the separate pressure preflight above.
    [[nodiscard]] inline bool adaptiveOrderedDeliveryMissRequiresFallback(
            const bool adaptive,
            const bool orderedTransport,
            const size_t requestedGeneratedFrames,
            const size_t admittedGeneratedFrames,
            const bool multiplierEvaluationActive) noexcept {
        return adaptive && orderedTransport && multiplierEvaluationActive &&
            requestedGeneratedFrames > 0 &&
            admittedGeneratedFrames < requestedGeneratedFrames;
    }

    /// Without a relief image, FIFO may need to release an earlier present
    /// before the next output can be acquired. Allow that progress, but never
    /// introduce an unbounded wait on this path, even for standalone launches.
    /// Preserve smaller user ceilings and share the budget across the batch;
    /// the existing per-image deadline and recovery still apply.
    [[nodiscard]] inline std::optional<uint64_t>
    orderedGeneratedBatchAcquireBudget(
            const uint32_t requestedMinImages,
            const size_t returnedImages,
            const size_t requestedGeneratedFrames,
            const std::optional<uint64_t> configuredBudget) noexcept {
        if (requestedGeneratedFrames == 0 ||
                returnedImages < requestedMinImages ||
                returnedImages - requestedMinImages != requestedGeneratedFrames)
            return configuredBudget;
        constexpr uint64_t maximumBatchBudget = 50'000'000;
        return std::min(configuredBudget.value_or(maximumBatchBudget),
            maximumBatchBudget);
    }

    /// Return a tighter Adaptive ceiling only when native-first ordered
    /// admission proved, during a higher-multiplier evaluation, that part but
    /// not all of a multi-output batch was available. A miss outside the probe
    /// may be transient and keeps the existing retry owner; Fixed is an
    /// explicit policy and never enters this path.
    [[nodiscard]] inline std::optional<size_t>
    adaptiveOrderedWsiLimitAfterPartialAdmission(
            const bool adaptive,
            const bool orderedTransport,
            const bool higherMultiplierEvaluationActive,
            const size_t requestedGeneratedFrames,
            const size_t admittedGeneratedFrames,
            const std::optional<size_t> currentLimit) noexcept {
        if (!adaptive || !orderedTransport ||
                !higherMultiplierEvaluationActive ||
                admittedGeneratedFrames == 0 ||
                admittedGeneratedFrames >= requestedGeneratedFrames ||
                (currentLimit && admittedGeneratedFrames >= *currentLimit)) {
            return std::nullopt;
        }
        return admittedGeneratedFrames;
    }

    /// Ordered SDR owns one configured acquire-wait budget per application
    /// present, not one full wait for every generated image. Returning zero
    /// means the caller must stop acquiring and retain the real frame; it must
    /// not issue another zero-timeout acquire on the ordered path.
    [[nodiscard]] inline std::optional<uint64_t>
    remainingGeneratedImageAcquireBudget(
            const std::optional<uint64_t> configuredBudget,
            const uint64_t consumedNanoseconds) {
        if (!configuredBudget)
            return std::nullopt;
        if (consumedNanoseconds >= *configuredBudget)
            return 0;
        return *configuredBudget - consumedNanoseconds;
    }

    /// A recovery probe owns one image. An explicit configured acquire budget
    /// remains authoritative; otherwise one confirmed display period bounds
    /// the retry. Without either contract the retry stays nonblocking instead
    /// of guessing a device-independent timeout.
    [[nodiscard]] inline uint64_t orderedRecoveryAcquireTimeout(
            const std::optional<uint32_t> refreshHz,
            const std::optional<uint64_t> configuredTimeout) {
        if (configuredTimeout)
            return *configuredTimeout;
        if (!refreshHz || *refreshHz == 0)
            return 0;

        constexpr uint64_t nanosecondsPerSecond = 1'000'000'000;
        return (nanosecondsPerSecond + *refreshHz - 1) / *refreshHz;
    }

    /// Recovery can probe only when the current policy requests generated work.
    /// A display-budget or Adaptive real-only frame is not a failed acquisition
    /// and must leave the pending probe available for the next eligible frame.
    [[nodiscard]] constexpr bool orderedAcquireProbeEligible(
            const bool probeRequested, const bool historyWarmupActive,
            const size_t requestedGeneratedFrames) noexcept {
        return probeRequested && !historyWarmupActive &&
            requestedGeneratedFrames > 0;
    }

    /// Once a lower-swapchain image has been acquired, transport ownership is
    /// independent of HDR classification. A caught backend failure must retire
    /// every owned image before the application's original image can be
    /// presented natively.
    [[nodiscard]] inline bool preacquiredImagesRequireRetirement(
            const bool generatedImagesPreacquired,
            const size_t admittedGeneratedFrameCount) {
        return generatedImagesPreacquired && admittedGeneratedFrameCount > 0;
    }

    struct GeneratedImageAdmissionRecovery {
        bool resumed{false};
        size_t missedAttempts{0};
        size_t bypassedFrames{0};
        size_t stableBatches{0};
        size_t requiredStableBatches{0};
    };

    /// A successful generated batch presents every generated image plus the
    /// application's real image. Require enough consecutive full batches to
    /// account for one complete lower-swapchain turnover before re-arming a
    /// bounded acquire. This is derived from owned WSI capacity rather than a
    /// refresh-rate, source-rate, or device-specific timer.
    [[nodiscard]] constexpr size_t
    generatedImageAdmissionRecoveryBatches(
            const size_t swapchainImages,
            const size_t requestedGeneratedFrames) noexcept {
        if (swapchainImages == 0 || requestedGeneratedFrames == 0)
            return 1;
        const size_t imagesPresentedPerBatch = requestedGeneratedFrames + 1;
        return std::max<size_t>(
            1,
            (swapchainImages + imagesPresentedPerBatch - 1) /
                imagesPresentedPerBatch
        );
    }

    /// Tracks temporary generated-swapchain pressure without turning it into
    /// an engine or temporal-history failure. Gamescope HDR and headroom-tight
    /// ordered SDR admission are nonblocking; this state exists only to
    /// aggregate diagnostics and report the eventual recovery.
    class GeneratedImageAdmission {
    public:
        [[nodiscard]] bool underPressure() const {
            return this->pressure;
        }

        /// Record an unavailable image. Returns true for the first miss and
        /// then at power-of-two intervals, allowing diagnostics to remain
        /// useful without synchronously logging every real frame.
        [[nodiscard]] bool reportUnavailable() {
            if (!this->pressure) {
                this->pressure = true;
                this->missedAttempts = 1;
                this->bypassedFrames = 0;
                this->stableBatches = 0;
                return true;
            }

            this->missedAttempts++;
            this->stableBatches = 0;
            return (this->missedAttempts & (this->missedAttempts - 1)) == 0;
        }

        void reportBypassedFrame() {
            if (this->pressure)
                this->bypassedFrames++;
        }

        [[nodiscard]] GeneratedImageAdmissionRecovery reportAvailable(
                const size_t requiredStableBatches = 1) {
            if (!this->pressure)
                return {};
            this->stableBatches++;
            const size_t required = std::max<size_t>(
                1, requiredStableBatches
            );
            const GeneratedImageAdmissionRecovery recovery{
                .resumed = this->stableBatches >= required,
                .missedAttempts = this->missedAttempts,
                .bypassedFrames = this->bypassedFrames,
                .stableBatches = this->stableBatches,
                .requiredStableBatches = required,
            };
            if (recovery.resumed)
                this->reset();
            return recovery;
        }

        void reset() {
            this->pressure = false;
            this->missedAttempts = 0;
            this->bypassedFrames = 0;
            this->stableBatches = 0;
        }

    private:
        bool pressure{false};
        size_t missedAttempts{0};
        size_t bypassedFrames{0};
        size_t stableBatches{0};
    };

    /// Recovers the ordered SDR transport only from explicit Vulkan acquire
    /// failure. Successful calls are never reclassified from their duration,
    /// FPS, device, resolution, or workload. An explicit menu return or live
    /// generation-policy transition may authorize one recreation if the next
    /// generated-image transport actually fails.
    class OrderedAcquireRecovery {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;
        using Duration = Clock::duration;

        struct PresentDecision {
            bool bypassGeneration{false};
            bool beginHistoryWarmup{false};
            // The first generated present after the drain is deliberately a
            // single-image transport probe, never the normal 3x/4x/5x plan.
            bool limitGeneratedFrames{false};
            // A post-failure recovery probe receives one small display-
            // relative timeout instead of a whole multi-image budget.
            bool preacquireGeneratedFrame{false};
            bool boundedAcquireProbe{false};
            size_t bypassedFrames{0};
            size_t consecutiveFailures{0};
            Duration drainDuration{};
        };

        struct Observation {
            bool quarantined{false};
            bool recovered{false};
            bool timedOut{false};
            bool deadlineExceeded{false};
            size_t consecutiveFailures{0};
            size_t bypassedFrames{0};
            Duration retryDelay{};
            Duration recoveryDuration{};
        };

        struct NonblockingMissObservation {
            bool diagnostic{false};
            bool quarantined{false};
            bool boundedProbeFailed{false};
            size_t consecutiveFailures{0};
            size_t bypassedFrames{0};
            Duration retryDelay{};
            Duration recoveryDuration{};
        };

        [[nodiscard]] static constexpr auto maximumRetryDelay() {
            return std::chrono::milliseconds{30000};
        }

        [[nodiscard]] PresentDecision beforePresent(const TimePoint now) {
            if (!this->retryAt) {
                if (this->probePending) {
                    return {
                        .limitGeneratedFrames = true,
                        .preacquireGeneratedFrame = true,
                        .boundedAcquireProbe = true,
                        .consecutiveFailures = this->consecutiveFailures,
                    };
                }
                return {};
            }

            if (now < *this->retryAt) {
                this->bypassedFrames++;
                return {
                    .bypassGeneration = true,
                    .bypassedFrames = this->bypassedFrames,
                    .consecutiveFailures = this->consecutiveFailures,
                    .drainDuration = this->recoveryStartedAt
                        ? now - *this->recoveryStartedAt
                        : Duration{},
                };
            }

            this->retryAt.reset();
            this->probePending = true;
            return {
                .beginHistoryWarmup = true,
                .limitGeneratedFrames = true,
                .preacquireGeneratedFrame = true,
                .boundedAcquireProbe = true,
                .bypassedFrames = this->bypassedFrames,
                .consecutiveFailures = this->consecutiveFailures,
                .drainDuration = this->recoveryStartedAt
                    ? now - *this->recoveryStartedAt
                    : Duration{},
            };
        }

        [[nodiscard]] Observation observe(const TimePoint now,
                const bool timedOut,
                const bool deadlineExceeded = false,
                const bool boundedRecoveryProbe = false) {
            if (timedOut || deadlineExceeded) {
                if (this->transitionRecreationArmed)
                    this->transitionTransportFailed = true;
                return this->beginNativeDrain(
                    now, timedOut, deadlineExceeded
                );
            }

            const bool drainProbeRecovered = this->probePending;
            if (drainProbeRecovered) {
                this->probePending = false;
                this->retryAt.reset();
                this->recoveryStartedAt.reset();
                this->consecutiveFailures = 0;
                this->bypassedFrames = 0;
            }
            if (this->transitionRecreationArmed &&
                    !this->transitionTransportFailed) {
                this->transitionRecreationArmed = false;
            }

            return {
                .recovered = drainProbeRecovered,
                .consecutiveFailures = this->consecutiveFailures,
                .bypassedFrames = this->bypassedFrames,
            };
        }

        [[nodiscard]] bool active() const {
            return this->retryAt.has_value() || this->probePending;
        }

        void armTransitionRecreation() {
            this->transitionRecreationArmed = true;
            this->transitionTransportFailed = false;
        }

        [[nodiscard]] bool transitionRecoveryActive() const {
            return this->transitionRecreationArmed;
        }

        [[nodiscard]] bool signalRecreation() {
            if (!this->transitionRecreationArmed ||
                    !this->transitionTransportFailed ||
                    this->recreationSignaled) {
                return false;
            }
            this->transitionRecreationArmed = false;
            this->transitionTransportFailed = false;
            this->recreationSignaled = true;
            return true;
        }

        /// A bounded post-failure probe miss returns to native backoff;
        /// probePending can therefore never become a permanent real-only
        /// state.
        [[nodiscard]] NonblockingMissObservation
        reportNonblockingProbeUnavailable(
                const TimePoint now) {
            if (this->probePending) {
                this->bypassedFrames++;
                if (this->transitionRecreationArmed)
                    this->transitionTransportFailed = true;
                const auto observation = this->beginNativeDrain(
                    now, true, false
                );
                return {
                    .diagnostic = true,
                    .quarantined = true,
                    .boundedProbeFailed = true,
                    .consecutiveFailures =
                        observation.consecutiveFailures,
                    .bypassedFrames = observation.bypassedFrames,
                    .retryDelay = observation.retryDelay,
                    .recoveryDuration = observation.recoveryDuration,
                };
            }

            // Normal presentation does not call this without a guard or probe.
            // Keep accidental calls observable without creating recovery state.
            this->bypassedFrames++;
            return {
                .diagnostic = true,
                .consecutiveFailures = this->consecutiveFailures,
                .bypassedFrames = this->bypassedFrames,
                .recoveryDuration = this->recoveryStartedAt
                    ? now - *this->recoveryStartedAt
                    : Duration{},
            };
        }

        void pauseForExternalInterruption() {
            // Steam-owned presentation is a discontinuity, not a transport
            // failure. Discard any incomplete retry before the return event
            // arms a fresh one-shot permission.
            this->reset();
        }

        void reset() {
            this->retryAt.reset();
            this->recoveryStartedAt.reset();
            this->probePending = false;
            this->consecutiveFailures = 0;
            this->bypassedFrames = 0;
            this->transitionRecreationArmed = false;
            this->transitionTransportFailed = false;
        }

    private:
        [[nodiscard]] Observation beginNativeDrain(const TimePoint now,
                const bool timedOut, const bool deadlineExceeded) {
            if (!this->recoveryStartedAt)
                this->recoveryStartedAt = now;
            this->consecutiveFailures++;
            const auto delay = retryDelayForFailure(
                this->consecutiveFailures
            );
            this->retryAt = now + delay;
            this->probePending = false;
            return {
                .quarantined = true,
                .timedOut = timedOut,
                .deadlineExceeded = deadlineExceeded,
                .consecutiveFailures = this->consecutiveFailures,
                .bypassedFrames = this->bypassedFrames,
                .retryDelay = delay,
                .recoveryDuration = now - *this->recoveryStartedAt,
            };
        }

        [[nodiscard]] static Duration retryDelayForFailure(
                const size_t failures) {
            constexpr std::array delays{
                std::chrono::milliseconds{250},
                std::chrono::milliseconds{500},
                std::chrono::milliseconds{1000},
                std::chrono::milliseconds{2000},
                std::chrono::milliseconds{5000},
                std::chrono::milliseconds{15000},
                maximumRetryDelay(),
            };
            return delays.at(std::min(failures, delays.size()) - 1);
        }

        std::optional<TimePoint> retryAt;
        std::optional<TimePoint> recoveryStartedAt;
        bool probePending{false};
        size_t consecutiveFailures{0};
        size_t bypassedFrames{0};
        bool transitionRecreationArmed{false};
        bool transitionTransportFailed{false};
        bool recreationSignaled{false};
    };

    /// The configured application-present budget is the transport contract.
    /// Do not derive a second per-image failure threshold from refresh rate or
    /// measured duration; the remaining cumulative budget is authoritative.
    [[nodiscard]] inline uint64_t orderedGeneratedImageAcquireTimeout(
            const std::optional<uint32_t>,
            const std::optional<uint64_t> remainingBudget) {
        return remainingBudget.value_or(std::numeric_limits<uint64_t>::max());
    }

    struct PipelineBusyDecision {
        bool diagnostic{false};
        bool requestHistoryWarmup{false};
        size_t consecutiveFrames{0};
        size_t totalBypassedFrames{0};
        std::chrono::steady_clock::duration duration{};
    };

    struct PipelineBusyRecoveryEvent {
        bool resumed{false};
        bool diagnostic{false};
        bool historyWarmupRequested{false};
        size_t bypassedFrames{0};
        size_t totalRecoveries{0};
        std::chrono::steady_clock::duration duration{};
    };

    /// Classifies overlap with previously submitted GPU work separately from
    /// a genuine pipeline stall. A one-frame busy result is normal at high
    /// real-frame rates and must not invalidate temporal history. Only one
    /// uninterrupted busy interval that reaches the sustained threshold asks
    /// the caller to warm history again.
    class PipelineBusyRecovery {
    public:
        using TimePoint = std::chrono::steady_clock::time_point;

        [[nodiscard]] static constexpr auto sustainedThreshold() {
            return std::chrono::milliseconds{250};
        }

        [[nodiscard]] PipelineBusyDecision reportBusy(const TimePoint now) {
            if (!this->startedAt) {
                this->startedAt = now;
                this->bypassedFrames = 0;
                this->historyWarmupRequested = false;
            }

            this->bypassedFrames++;
            this->totalBypassedFrames++;
            const auto duration = now - *this->startedAt;
            const bool requestHistoryWarmup =
                !this->historyWarmupRequested &&
                duration >= sustainedThreshold();
            if (requestHistoryWarmup)
                this->historyWarmupRequested = true;

            const bool powerOfTwo =
                (this->totalBypassedFrames &
                    (this->totalBypassedFrames - 1)) == 0;
            return {
                .diagnostic = powerOfTwo || requestHistoryWarmup,
                .requestHistoryWarmup = requestHistoryWarmup,
                .consecutiveFrames = this->bypassedFrames,
                .totalBypassedFrames = this->totalBypassedFrames,
                .duration = duration,
            };
        }

        [[nodiscard]] PipelineBusyRecoveryEvent reportReady(
                const TimePoint now) {
            if (this->startedAt)
                this->totalRecoveries++;
            const bool diagnostic = this->startedAt &&
                (this->historyWarmupRequested ||
                 (this->totalRecoveries & (this->totalRecoveries - 1)) == 0);
            const PipelineBusyRecoveryEvent event{
                .resumed = this->startedAt.has_value(),
                .diagnostic = diagnostic,
                .historyWarmupRequested = this->historyWarmupRequested,
                .bypassedFrames = this->bypassedFrames,
                .totalRecoveries = this->totalRecoveries,
                .duration = this->startedAt
                    ? now - *this->startedAt
                    : std::chrono::steady_clock::duration{},
            };
            this->clearInterval();
            return event;
        }

        void reset() {
            this->clearInterval();
            this->totalBypassedFrames = 0;
            this->totalRecoveries = 0;
        }

    private:
        void clearInterval() {
            this->startedAt.reset();
            this->bypassedFrames = 0;
            this->historyWarmupRequested = false;
        }

        std::optional<TimePoint> startedAt;
        size_t bypassedFrames{0};
        bool historyWarmupRequested{false};
        size_t totalBypassedFrames{0};
        size_t totalRecoveries{0};
    };

    /// Spaces ordered compositor commits without a lower Wayland FIFO wait.
    /// The isolated bridge learns readiness from its own compositor feedback,
    /// never from source-frame intervals (which include application waits).
    /// Legacy WSI, without queue-stage observations, retains its fixed batch lead.
    /// Deadlines remain ordered across changes and submission stays bounded.
    class OrderedPresentTimeline {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;
        struct Slot {
            TimePoint submitAt;
            TimePoint presentAt;
            /// Whole refreshes added before this output to drain a proven queue.
            Clock::duration realignment{};
        };

        /// Clock changes invalidate learned work without moving output targets.
        void invalidateReadiness() { this->resetReadiness(); }

        /// A lost feedback transport returns to the fixed legacy lead. Clearing
        /// samples alone would retain observed-mode startup lead indefinitely.
        void useUnobservedTiming() {
            this->resetReadiness();
            this->lastBatchSerial.reset();
            this->lastBatchAt.reset();
        }

        [[nodiscard]] static bool validRate(const double fps) noexcept {
            return std::isfinite(fps) && fps >= 1.0 && fps <= 1000.0;
        }

        /// Identify one generated batch. A pause retires old feedback and
        /// primes recovery once; native history warm-up must not prime a
        /// second startup delay when the known generated batch resumes.
        void observeSourceBatch(const TimePoint now, const uint64_t serial) {
            if (this->lastBatchSerial == serial)
                return;
            if (this->lastBatchAt) {
                if (now <= *this->lastBatchAt)
                    this->resetReadiness();
                else if (now - *this->lastBatchAt > std::chrono::seconds(1)) {
                    this->clearPending();
                    if (!this->primeRecovery) {
                        this->recoveryReadiness = this->readinessCount
                            ? std::optional{*std::max_element(this->readiness.begin(),
                                this->readiness.begin() + this->readinessCount)}
                            : std::nullopt;
                    }
                    this->primeRecovery = true;
                }
            }
            this->lastBatchSerial = serial;
            this->lastBatchAt = now;
            this->batchIndex = (this->batchIndex + 1) % this->pending.size();
            // V2 reports individual drops, possibly before earlier outputs.
            // Retire forgotten entries individually when the bounded ring wraps.
            const auto& expired = this->pending[this->batchIndex];
            if (expired.staged)
                this->completedOutputs += expired.count - expired.acknowledged;
            this->pending[this->batchIndex] = {};
            this->queuedOutputs = this->submittedOutputs - this->completedOutputs;
        }

        /// Record only this owner's protocol IDs, after submission backpressure.
        /// Fixed storage also bounds sessions with absent/dropped feedback.
        void submitted(const uint32_t id, const TimePoint desired,
                const TimePoint submittedAt) {
            this->recordSubmission(id, desired, submittedAt, false);
        }

        /// Timing-v2 serials identify this swapchain's outputs without a
        /// wrapping 32-bit namespace or legacy broadcast feedback.
        void submittedTiming(const uint64_t serial, const TimePoint desired,
                const TimePoint submittedAt) {
            this->recordSubmission(serial, desired, submittedAt, true);
        }

        /// Queue completion is observed before the compositor's requested-time
        /// hold. Learn only that stage, not latch/feedback latency or a nominal
        /// FIFO duration. Zero queueEnd includes explicit drops and unavailable
        /// timestamps: acknowledge the output without learning fabricated work.
        void feedbackTiming(const uint64_t serial, const TimePoint queueEnd,
                const TimePoint receivedAt, const TimePoint displayed = {}) {
            for (auto& batch : this->pending) {
                const auto index = serial - batch.firstId;
                if (!batch.staged || index >= batch.count || batch.received[index])
                    continue;
                batch.received[index] = true;
                ++batch.acknowledged;
                ++this->completedOutputs;
                this->observeDisplay(batch.desired[index], displayed);
                const auto submittedAt = batch.outputSubmittedAt[index];
                if (batch.observed || queueEnd <= TimePoint{} ||
                        queueEnd < submittedAt || queueEnd > receivedAt ||
                        queueEnd - submittedAt > maximumReadiness)
                    return;
                batch.observed = true;
                this->recordReadiness(queueEnd - submittedAt);
                return;
            }
        }

    private:
        void recordSubmission(const uint64_t id, const TimePoint desired,
                const TimePoint submittedAt, const bool staged) {
            if (!this->lastBatchSerial || !this->generationEnabled)
                return;
            auto& batch = this->pending[this->batchIndex];
            if (batch.count == batch.desired.size())
                return;
            if (batch.count == 0) {
                batch.firstId = id;
                batch.staged = staged;
                batch.firstSubmission = this->submittedOutputs + 1;
                batch.lead = this->scheduledLead;
                batch.refreshPeriod = this->scheduledRefreshPeriod;
                // FIFO eligibility can include slots belonging to earlier
                // batches. Those slots already constrain the next deadline;
                // they are not additional GPU work for this batch.
                batch.queuedDuration = this->scheduledInterval * static_cast<int64_t>(
                    std::min<uint64_t>(this->submittedOutputs - this->completedOutputs,
                        GeneratedFramePlan::capacity + 1));
                batch.submittedAt = submittedAt;
            } else if (staged != batch.staged || id != (staged
                    ? batch.firstId + batch.count
                    : static_cast<uint32_t>(batch.firstId + batch.count))) {
                return;
            }
            batch.outputSubmittedAt[batch.count] = submittedAt;
            batch.desired[batch.count++] = desired;
            ++this->submittedOutputs;
        }

        void recordReadiness(const Clock::duration sample) {
            this->readiness[this->readinessIndex] = sample;
            this->readinessIndex = (this->readinessIndex + 1) % this->readiness.size();
            this->readinessCount = std::min(this->readinessCount + 1,
                this->readiness.size());
        }

    public:

        /// Gamescope reports when a ready output was first eligible to latch,
        /// separately from the requested hold. Correct the lead
        /// by that difference: using submission-to-feedback elapsed time would
        /// count our already queued output spacing as new GPU work.
        /// Use the first feedback received for each batch. If an earlier output
        /// was dropped, a later output still supplies readiness evidence for
        /// the batch's first deadline; later outputs cannot shrink its budget.
        void feedback(const uint32_t id, const TimePoint desired,
                const TimePoint earliest, const Clock::duration margin = {},
                const TimePoint displayed = {}) {
            for (auto& batch : this->pending) {
                const auto index = static_cast<uint32_t>(id - batch.firstId);
                if (batch.staged || index >= batch.count || batch.desired[index] != desired)
                    continue;
                this->observeDisplay(desired, displayed);
                // Completion is acknowledged at the compositor latch, which
                // can precede the predicted display timestamp. Every matched
                // output advances completion, even after this batch supplied
                // its readiness sample or if optional readiness is absent.
                // An acknowledged ordered output retires its prefix, including
                // dropped outputs. Use owned ordinals, not wrapping protocol IDs
                // or the future display timestamp, to measure queue depth.
                this->completedOutputs = std::max(this->completedOutputs,
                    batch.firstSubmission + index);
                if (margin < Clock::duration::zero() || earliest <= TimePoint{} ||
                        margin >= earliest.time_since_epoch())
                    return;
                const auto readyAt = earliest - margin;
                if (batch.observed || readyAt < batch.submittedAt ||
                        readyAt - batch.submittedAt > maximumReadiness)
                    return;
                batch.observed = true;
                // Preserve half the latch-to-refresh room for dispatch jitter.
                // The full margin can ratchet deadlines into another refresh.
                // Queued GPU work has its separate bounded reserve below.
                const auto allowance = std::min(margin, batch.refreshPeriod) / 2;
                const auto reservedSpacing = std::max(Clock::duration::zero(),
                    batch.desired[0] - batch.submittedAt - batch.lead);
                const auto queuedHold = std::max(Clock::duration::zero(),
                    batch.queuedDuration - reservedSpacing);
                const auto sample = std::max(Clock::duration::zero(),
                    batch.lead + (readyAt - batch.desired[0]) + allowance - queuedHold);
                if (sample > maximumReadiness)
                    return;
                this->recordReadiness(sample);
                return;
            }
        }

        [[nodiscard]] std::optional<Slot> schedule(const TimePoint now,
                const double outputFps, const double refreshFps,
                const size_t outputBatchSize = 1,
                const bool generationEnabled = true,
                const std::optional<TimePoint> sourceDeadline = std::nullopt) {
            if (!validRate(outputFps) || !validRate(refreshFps) ||
                    outputBatchSize == 0 ||
                    outputBatchSize > GeneratedFramePlan::capacity + 1)
                return std::nullopt;
            // Warm-up can learn native-only readiness before generation starts.
            // Prime once when a larger admitted batch first appears. Retain the
            // high-water mark across Fractional small/large alternation so it
            // cannot repeatedly impose startup latency.
            if (this->lastBatchSerial && generationEnabled &&
                    outputBatchSize > this->readinessBatchCapacity) {
                this->resetReadiness();
                this->readinessBatchCapacity = outputBatchSize;
            }
            const auto period = [](const double fps) {
                return std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<double>(1.0 / fps));
            };
            const auto refreshPeriod = period(refreshFps);
            const auto interval = period(std::min(outputFps, refreshFps));
            const auto batchDuration = interval * static_cast<int64_t>(outputBatchSize);
            // Apply the retained work estimate when generated work resumes.
            // Native history/relief can last longer than the sample window;
            // it must neither consume this allowance nor inherit extra lead.
            if (this->primeRecovery && outputBatchSize > 1 && generationEnabled) {
                this->readiness[0] = std::max(
                    this->recoveryReadiness.value_or(Clock::duration::zero()),
                    std::min((this->recoveryReadiness ? 2 : 4) * refreshPeriod,
                        Clock::duration(maximumReadiness)));
                this->readinessCount = 1;
                this->readinessIndex = 1;
                this->primeRecovery = false;
                this->recoveryReadiness.reset();
            }
            // Four refreshes prime unknown asynchronous work. Observed ready
            // batches can release that allowance; CPU pacing cannot enlarge it.
            const auto measuredReadiness = this->readinessCount
                ? *std::max_element(this->readiness.begin(),
                    this->readiness.begin() + this->readinessCount)
                : std::min(4 * refreshPeriod, Clock::duration(maximumReadiness));
            // A threaded presentation waiter releases source credit while
            // another batch is pending. Its deadlines already reserve those
            // slots; a second refresh of lead consumes the next frame's work
            // budget. Keep one refresh of dispatch room for shallow queues,
            // measured readiness when larger, and three for deeper queues.
            const auto reserve = (this->queuedOutputs <= outputBatchSize ? 1 : 3)
                * refreshPeriod;
            const auto bridgeReadiness = std::max(measuredReadiness,
                std::min(reserve, Clock::duration(maximumReadiness)));
            const auto lead = generationEnabled
                ? (this->lastBatchSerial ? bridgeReadiness :
                    std::max(2 * refreshPeriod, batchDuration))
                : Clock::duration::zero();
            // A proven queue drains while this one slot waits; every later
            // output keeps the existing spacing from the realigned target.
            const auto realignment = this->lastPresentAt
                ? std::exchange(this->pendingRealignment, Clock::duration::zero())
                : Clock::duration::zero();
            const auto next = this->lastPresentAt
                ? *this->lastPresentAt + interval + realignment : now;
            auto presentAt = std::max(now + lead, next);
            // Completion-paced clients can return just after the previous
            // deadline. Reapplying a whole refresh of dispatch reserve then
            // adds that return delay on every batch. Keep the existing slot
            // when learned work fits and at most one batch is pending. Cold
            // start, recovery allowance, deeper queues and genuinely late
            // work retain their full lead and rebase instead of catching up.
            if (generationEnabled && this->lastBatchSerial &&
                    this->readinessCount && this->lastPresentAt &&
                    this->queuedOutputs <= outputBatchSize &&
                    next > now && next >= now + measuredReadiness)
                presentAt = next;
            this->lastPresentAt = presentAt;
            if (realignment > Clock::duration::zero()) {
                this->realignedFrom = presentAt;
                this->realignmentUnverified = true;
            }
            // Readiness feedback must use the allowance actually granted;
            // otherwise a retained slot would relearn its saved reserve as
            // additional GPU work and undo the phase correction.
            this->scheduledLead = std::min(lead, presentAt - now);
            this->scheduledRefreshPeriod = refreshPeriod;
            this->scheduledInterval = interval;
            if (!generationEnabled && this->generationEnabled) {
                this->resetReadiness();
                this->lastBatchAt.reset();
            }
            this->generationEnabled = generationEnabled;
            // A capped producer has already reserved its return deadline.
            // Stage this bounded batch by then so submission backpressure
            // does not consume the next source interval. Display targets and
            // readiness lead remain unchanged; late work never submits early.
            const auto submitAt = presentAt - lead - refreshPeriod;
            return Slot{
                .submitAt = std::max(now, sourceDeadline
                    ? std::min(submitAt, *sourceDeadline) : submitAt),
                .presentAt = presentAt,
                .realignment = realignment,
            };
        }

    private:
        /// Timing phase alone keeps an output within one refresh of its target.
        /// When every recent owned output is shown a whole refresh or more after
        /// its target, each one waited behind an earlier output: a missed
        /// refresh left a queue that the absolute deadlines would otherwise keep
        /// forever as added latency. Request removal of those whole refreshes
        /// once; outputs targeted before the realignment cannot prove it again.
        /// Whole-refresh delays cannot change lateness from timing phase or
        /// compositor latency. If the first realigned output is still a whole
        /// refresh late, nothing was queued and each repeat would only hold a
        /// refresh, so stop realigning until timing resets.
        void observeDisplay(const TimePoint desired, const TimePoint displayed) {
            const auto refresh = this->scheduledRefreshPeriod;
            if (displayed <= TimePoint{} || desired <= TimePoint{} ||
                    refresh <= Clock::duration::zero() ||
                    this->pendingRealignment > Clock::duration::zero() ||
                    (this->realignedFrom && desired < *this->realignedFrom))
                return;
            const auto lateness = displayed - desired;
            const bool late = lateness >= refresh + realignmentTolerance;
            if (std::exchange(this->realignmentUnverified, false) && late)
                this->realignmentDisproved = true;
            if (this->realignmentDisproved)
                return;
            if (!late) {
                this->queuedDisplayCount = 0;
                return;
            }
            this->queuedDisplayMinimum = this->queuedDisplayCount
                ? std::min(this->queuedDisplayMinimum, lateness) : lateness;
            if (++this->queuedDisplayCount < realignmentEvidence)
                return;
            this->queuedDisplayCount = 0;
            const auto refreshes = std::min<int64_t>(maximumRealignmentRefreshes,
                (this->queuedDisplayMinimum - realignmentTolerance) / refresh);
            this->pendingRealignment = refresh * refreshes;
        }

        void resetReadiness() {
            this->clearPending();
            this->readinessCount = 0;
            this->readinessIndex = 0;
            this->readinessBatchCapacity = 0;
            this->primeRecovery = false;
            this->recoveryReadiness.reset();
        }

        void clearPending() {
            this->pending = {};
            this->submittedOutputs = 0;
            this->completedOutputs = 0;
            this->queuedOutputs = 0;
            // A pause or timing reset rebases deadlines; old display evidence
            // describes a queue that no longer exists.
            this->queuedDisplayCount = 0;
            this->pendingRealignment = {};
            this->realignedFrom.reset();
            this->realignmentUnverified = false;
            this->realignmentDisproved = false;
        }

        static constexpr auto maximumReadiness = std::chrono::milliseconds(100);
        static constexpr size_t realignmentEvidence = 12;
        static constexpr auto realignmentTolerance = std::chrono::microseconds(250);
        static constexpr int64_t maximumRealignmentRefreshes = 4;
        struct PendingBatch {
            uint64_t firstId{};
            uint64_t firstSubmission{};
            size_t count{};
            std::array<TimePoint, GeneratedFramePlan::capacity + 1> desired{};
            std::array<TimePoint, GeneratedFramePlan::capacity + 1> outputSubmittedAt{};
            std::array<bool, GeneratedFramePlan::capacity + 1> received{};
            size_t acknowledged{};
            bool staged{};
            TimePoint submittedAt{};
            Clock::duration lead{};
            Clock::duration refreshPeriod{};
            Clock::duration queuedDuration{};
            bool observed{};
        };
        std::optional<TimePoint> lastPresentAt;
        std::optional<uint64_t> lastBatchSerial;
        std::optional<TimePoint> lastBatchAt;
        std::array<PendingBatch, 16> pending{};
        size_t batchIndex{};
        std::array<Clock::duration, 4> readiness{};
        size_t readinessIndex{};
        size_t readinessCount{};
        size_t readinessBatchCapacity{};
        Clock::duration scheduledLead{};
        Clock::duration scheduledRefreshPeriod{};
        Clock::duration scheduledInterval{};
        uint64_t submittedOutputs{};
        uint64_t completedOutputs{};
        uint64_t queuedOutputs{};
        bool generationEnabled{};
        bool primeRecovery{};
        std::optional<Clock::duration> recoveryReadiness;
        size_t queuedDisplayCount{};
        Clock::duration queuedDisplayMinimum{};
        Clock::duration pendingRealignment{};
        std::optional<TimePoint> realignedFrom;
        bool realignmentUnverified{};
        bool realignmentDisproved{};
    };

    /// Limits application presents before frame-generation policy observes
    /// them. Deadlines stay on an absolute cadence while the application is
    /// early, but any late frame rebases immediately so a loading stall cannot
    /// create a burst of catch-up presents.
    class RealFramePacer {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        struct OutputBatch {
            TimePoint first;
            Clock::duration interval;
            size_t count;

            [[nodiscard]] TimePoint at(const size_t index) const {
                return first + interval * static_cast<int64_t>(index);
            }
        };

        /// Spend only the existing source-cap interval. Keep a target-output
        /// clock across changing batch counts: spacing each batch separately
        /// would produce 12.5/25 ms gaps for a 40 -> 60 FPS source/target pair.
        /// The final output may precede the source deadline; the caller still
        /// retains that deadline before returning control to the application.
        [[nodiscard]] std::optional<OutputBatch> scheduleOutputs(
                const TimePoint readyAt, const TimePoint realFrameAt, const double outputFps,
                const size_t count, const bool targetClockActive) {
            if (!OrderedPresentTimeline::validRate(outputFps) ||
                    this->activeFramesPerSecond <= 0.0 || count == 0 ||
                    count > GeneratedFramePlan::capacity + 1 ||
                    (count == 1 && !targetClockActive)) {
                // A zero-generation turn belongs on the output clock only
                // when it is part of a running Fractional ledger. Startup,
                // native preference and native probes keep the source cap.
                this->resetOutputs();
                return std::nullopt;
            }
            const auto period = [](const double fps) {
                return std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<double>(1.0 / fps));
            };
            const auto interval = period(outputFps);
            const auto sourceInterval = period(this->activeFramesPerSecond);
            const auto span = interval * static_cast<int64_t>(count - 1);
            if (span >= sourceInterval) {
                this->resetOutputs();
                return std::nullopt;
            }
            const auto latestFirst = realFrameAt - span;
            auto first = latestFirst;
            if (this->outputFramesPerSecond == outputFps && this->lastOutputAt) {
                const auto next = *this->lastOutputAt + interval;
                if (next > realFrameAt - sourceInterval)
                    first = std::min(next, latestFirst);
            }
            // Never borrow time from the next real frame or compress an
            // overdue output batch into catch-up submissions. A late source
            // or slow preparation retains the ordinary source-cap deadline.
            if (first < readyAt || (this->lastOutputAt && first <= *this->lastOutputAt)) {
                this->resetOutputs();
                return std::nullopt;
            }
            this->outputFramesPerSecond = outputFps;
            this->lastOutputAt = first + span;
            return OutputBatch{first, interval, count};
        }

        void resetOutputs() {
            this->lastOutputAt.reset();
            this->outputFramesPerSecond = 0.0;
        }

        [[nodiscard]] TimePoint schedule(
                const TimePoint now, const double framesPerSecond,
                const bool preserveRatePhase = false) {
            if (!std::isfinite(framesPerSecond) || framesPerSecond <= 0.0) {
                this->reset();
                return now;
            }

            if (this->activeFramesPerSecond != framesPerSecond) {
                if (preserveRatePhase && this->activeFramesPerSecond > 0.0 && this->nextFrameAt) {
                    // A governor's small rate adjustment belongs to the same
                    // pacing episode. Move its next boundary without granting
                    // a free frame or discarding the Adaptive output clock.
                    *this->nextFrameAt += std::chrono::duration_cast<Clock::duration>(
                        std::chrono::duration<double>(1.0 / framesPerSecond - 1.0 / this->activeFramesPerSecond));
                } else {
                    this->nextFrameAt.reset();
                    this->resetOutputs();
                }
                this->activeFramesPerSecond = framesPerSecond;
            }

            const auto interval = std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(
                    1.0 / framesPerSecond
                )
            );
            if (!this->nextFrameAt) {
                this->nextFrameAt = now + interval;
                return now;
            }

            if (now >= *this->nextFrameAt) {
                this->nextFrameAt = now + interval;
                return now;
            }

            const auto deadline = *this->nextFrameAt;
            *this->nextFrameAt += interval;
            return deadline;
        }

        void reset() {
            this->nextFrameAt.reset();
            this->activeFramesPerSecond = 0.0;
            this->resetOutputs();
        }

    private:
        double activeFramesPerSecond{0.0};
        std::optional<TimePoint> nextFrameAt;
        double outputFramesPerSecond{0.0};
        std::optional<TimePoint> lastOutputAt;
    };

    /// When Steady Adaptive has already proven that it needs at least 3x, a
    /// target/2 cap can leave the source cadence between integer generation
    /// ratios (for example 45 -> 120 FPS). Qualify the exact target/N rung for
    /// one second before lowering the cap. A qualified rung tolerates small
    /// cadence dips and requires sustained loss before releasing the cap;
    /// scheduler and transport guards still restore normal pacing immediately.
    /// Validated scheduler limits remain the authority, so this policy cannot
    /// activate a multiplier that has not passed delivery and throughput checks.
    class SmoothCadenceBaseCap {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        struct Decision {
            std::optional<double> framesPerSecond;
            size_t multiplier{0};
            bool changed{false};
        };

        struct SchedulerState {
            size_t validatedGenerationLimit{0};
            std::optional<size_t> stableCadenceLimit;
            double smoothedBaseFps{0.0};
            bool rampEvaluationActive{false};
            std::optional<size_t> efficiencyProbeGenerationLimit;
            bool rearmRequired{false};
            bool discontinuityRecoveryActive{false};
        };

        [[nodiscard]] Decision update(const TimePoint now,
                const bool eligible, const uint32_t targetFps,
                const SchedulerState scheduler) {
            const auto previousMultiplier = this->effectiveMultiplier();
            const auto previousProbeMultiplier = this->probeMultiplier;
            if (!eligible || targetFps == 0 ||
                    scheduler.discontinuityRecoveryActive ||
                    scheduler.rearmRequired || scheduler.rampEvaluationActive) {
                this->reset();
                return this->decision(previousMultiplier);
            }

            // A lower-generation efficiency probe must run against the normal
            // target/(tested generation limit + 1) cadence rather than the
            // qualified target/N rung it is evaluating. Preserve that rung
            // while the temporary probe cadence is active so rejection can
            // restore it immediately.
            if (scheduler.efficiencyProbeGenerationLimit) {
                this->resetCandidate();
                this->releaseSince.reset();
                this->probeMultiplier =
                    *scheduler.efficiencyProbeGenerationLimit + 1;
                this->activeTargetFps = static_cast<double>(targetFps);
                return this->decision(previousMultiplier);
            }
            this->probeMultiplier.reset();

            // Validated capacity can exceed the constant plan currently in
            // use. A 3x cap applied to an accepted 2x plan would turn 120 FPS
            // into 40 real + 40 generated FPS and trigger false rescue.
            const size_t desiredMultiplier = std::min(
                scheduler.validatedGenerationLimit,
                scheduler.stableCadenceLimit.value_or(
                    scheduler.validatedGenerationLimit)) + 1;
            if (this->activeMultiplier &&
                    *this->activeMultiplier > desiredMultiplier) {
                // Do not retain a stricter cap while a lower active cadence
                // qualifies. Its previous cap would starve the new plan.
                this->activeMultiplier.reset();
                this->resetCandidate();
                this->releaseSince.reset();
            }
            if (previousProbeMultiplier &&
                    desiredMultiplier == *previousProbeMultiplier) {
                // The scheduler accepted the tested lower load. Its temporary
                // cadence is already active, so retain it without another
                // qualification or pacing transition. Exact 2x falls through
                // to the normal target/2 automatic cap.
                this->resetCandidate();
                this->releaseSince.reset();
                if (desiredMultiplier >= 3) {
                    this->activeMultiplier = desiredMultiplier;
                    this->activeTargetFps =
                        static_cast<double>(targetFps);
                    return this->decision(previousMultiplier);
                }
                this->activeMultiplier.reset();
                return {
                    .changed = false,
                };
            }
            if (desiredMultiplier < 3 || scheduler.smoothedBaseFps <= 0.0) {
                this->reset();
                return this->decision(previousMultiplier);
            }

            const double desiredCap = static_cast<double>(targetFps) /
                static_cast<double>(desiredMultiplier);
            const double previousRung = static_cast<double>(targetFps) /
                static_cast<double>(desiredMultiplier - 1);
            const bool cadenceNeedsRung =
                scheduler.smoothedBaseFps < previousRung * 0.98;
            if (this->activeMultiplier == desiredMultiplier) {
                this->activeTargetFps = static_cast<double>(targetFps);
                // Entry needs 95% of the target/N cadence. Retention uses
                // 90% plus a short hold, so jitter at the entry boundary does
                // not alternate caps and reset the real-frame pacer.
                const bool retainRung = cadenceNeedsRung &&
                    scheduler.smoothedBaseFps >= desiredCap * 0.90;
                if (retainRung) {
                    this->releaseSince.reset();
                } else {
                    if (!this->releaseSince)
                        this->releaseSince = now;
                    if (now - *this->releaseSince >=
                            releaseQualificationDuration())
                        this->reset();
                }
                return this->decision(previousMultiplier);
            }
            this->releaseSince.reset();
            const bool rungSustainable =
                scheduler.smoothedBaseFps >= desiredCap * 0.95;
            if (!cadenceNeedsRung || !rungSustainable) {
                this->resetCandidate();
                this->activeMultiplier.reset();
                return this->decision(previousMultiplier);
            }

            if (this->candidateMultiplier != desiredMultiplier) {
                this->candidateMultiplier = desiredMultiplier;
                this->candidateSince = now;
                return this->decision(previousMultiplier);
            }
            if (!this->candidateSince ||
                    now - *this->candidateSince < qualificationDuration()) {
                return this->decision(previousMultiplier);
            }

            this->activeMultiplier = desiredMultiplier;
            this->activeTargetFps = static_cast<double>(targetFps);
            this->resetCandidate();
            return this->decision(previousMultiplier);
        }

        void reset() {
            this->activeMultiplier.reset();
            this->probeMultiplier.reset();
            this->releaseSince.reset();
            this->resetCandidate();
        }

        [[nodiscard]] static constexpr std::chrono::seconds
        qualificationDuration() {
            return std::chrono::seconds{1};
        }

        [[nodiscard]] static constexpr std::chrono::milliseconds
        releaseQualificationDuration() {
            return std::chrono::milliseconds{250};
        }

        [[nodiscard]] std::optional<size_t> activeGenerationLimit() const {
            const auto multiplier = this->effectiveMultiplier();
            return multiplier && *multiplier > 1
                ? std::optional<size_t>{*multiplier - 1} : std::nullopt;
        }

    private:
        [[nodiscard]] std::optional<size_t> effectiveMultiplier() const {
            return this->probeMultiplier
                ? this->probeMultiplier : this->activeMultiplier;
        }

        [[nodiscard]] Decision decision(
                const std::optional<size_t> previousMultiplier) const {
            const auto currentMultiplier = this->effectiveMultiplier();
            if (!currentMultiplier) {
                return {
                    .changed = previousMultiplier.has_value(),
                };
            }
            return {
                .framesPerSecond = this->activeTargetFps /
                    static_cast<double>(*currentMultiplier),
                .multiplier = *currentMultiplier,
                .changed = previousMultiplier != currentMultiplier,
            };
        }

        void resetCandidate() {
            this->candidateMultiplier.reset();
            this->candidateSince.reset();
        }

        std::optional<size_t> activeMultiplier;
        std::optional<size_t> probeMultiplier;
        std::optional<size_t> candidateMultiplier;
        std::optional<TimePoint> candidateSince;
        std::optional<TimePoint> releaseSince;
        double activeTargetFps{0.0};
    };

    /// Under an ordered-FIFO handoff, a source that waits on presentation
    /// settles at the refresh rate divided by outputs per real frame; after a
    /// late frame it can only refill the bounded queue once. A source with its
    /// own faster clock instead delivers surplus frames in every window,
    /// overfilling FIFO and pacing unevenly. Counting surplus frames keeps the
    /// threshold independent of refresh and multiplier.
    class FifoSourceOverrunWatch {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        [[nodiscard]] bool observe(const TimePoint now,
                const std::optional<uint32_t> refreshHz,
                const size_t outputsPerRealFrame) {
            if (!refreshHz || *refreshHz == 0 || outputsPerRealFrame < 2) {
                this->reset();
                return false;
            }
            if (this->observedRefreshHz != refreshHz ||
                    this->observedOutputs != outputsPerRealFrame ||
                    (this->lastArrival &&
                     now - *this->lastArrival > settleDelay())) {
                // A refresh or rung change, or a stall, drains the queue. Its
                // refill is not evidence of an independent source clock.
                this->reset();
                this->observedRefreshHz = refreshHz;
                this->observedOutputs = outputsPerRealFrame;
            }
            this->lastArrival = now;
            if (!this->settleSince)
                this->settleSince = now;
            if (now - *this->settleSince < settleDelay())
                return false;
            if (!this->windowStart) {
                this->windowStart = now;
                this->windowFrames = 0;
                return false;
            }
            ++this->windowFrames;
            const double elapsed = std::chrono::duration<double>(
                now - *this->windowStart
            ).count();
            if (elapsed < 1.0)
                return false;
            const double surplus = static_cast<double>(this->windowFrames) -
                elapsed * static_cast<double>(*refreshHz) /
                    static_cast<double>(outputsPerRealFrame);
            this->surplusWindows = surplus >= surplusFramesPerWindow()
                ? this->surplusWindows + 1 : 0;
            this->windowStart = now;
            this->windowFrames = 0;
            return this->surplusWindows >= requiredSurplusWindows();
        }

        void reset() {
            *this = {};
        }

        [[nodiscard]] static constexpr std::chrono::milliseconds settleDelay() {
            return std::chrono::milliseconds{250};
        }

        [[nodiscard]] static constexpr double surplusFramesPerWindow() {
            return 1.5;
        }

        [[nodiscard]] static constexpr size_t requiredSurplusWindows() {
            return 3;
        }

    private:
        std::optional<uint32_t> observedRefreshHz;
        size_t observedOutputs{0};
        std::optional<TimePoint> settleSince;
        std::optional<TimePoint> windowStart;
        std::optional<TimePoint> lastArrival;
        size_t windowFrames{0};
        size_t surplusWindows{0};
    };

    /// Guards each Adaptive integer-rung handoff from the real-frame pacer to
    /// ordered FIFO. A lost qualification restores any cap and backs off that
    /// rung without delaying a separately validated higher rung. A planned
    /// higher-rung VRR efficiency probe pauses without a failure cooldown.
    /// A source that outruns a rung's FIFO admission rate keeps that rung's
    /// normal pacing until an explicit reset.
    class SmoothCadencePacerHandoff {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        struct Decision {
            bool active{false};
            bool changed{false};
            bool sourceOverrun{false};
            std::optional<size_t> generationLimit;
            std::optional<size_t> previousGenerationLimit;
        };

        [[nodiscard]] Decision update(const TimePoint now,
                const std::optional<size_t> eligibleGenerationLimit,
                const bool plannedProbePause = false,
                const std::optional<uint32_t> refreshHz = std::nullopt) {
            const auto previous = this->activeLimit;
            // Higher VRR rungs may enter above their admission rate, but FIFO
            // pulls a source that waits on presentation down within the
            // bounded queue. Only an independent clock stays above it.
            const bool sourceOverrun = this->activeLimit &&
                eligibleGenerationLimit == this->activeLimit &&
                *this->activeLimit >= 1 &&
                *this->activeLimit <= this->retryAt.size() &&
                this->overrunWatch.observe(now, refreshHz,
                    *this->activeLimit + 1);
            if (sourceOverrun) {
                // An independent game clock does not change while it runs.
                // Retrying this rung would repeat the same uneven overfill.
                this->retryAt[*this->activeLimit - 1] = TimePoint::max();
                this->activeLimit.reset();
            }
            if (this->activeLimit != eligibleGenerationLimit) {
                if (!(plannedProbePause && !eligibleGenerationLimit) &&
                        this->activeLimit &&
                        *this->activeLimit >= 1 &&
                        *this->activeLimit <= this->retryAt.size()) {
                    this->retryAt[*this->activeLimit - 1] = now + retryDelay();
                }
                this->activeLimit.reset();
            }
            if (!this->activeLimit && eligibleGenerationLimit &&
                    *eligibleGenerationLimit >= 1 &&
                    *eligibleGenerationLimit <= this->retryAt.size()) {
                auto& retry = this->retryAt[*eligibleGenerationLimit - 1];
                if (!retry || now >= *retry) {
                    this->activeLimit = eligibleGenerationLimit;
                    retry.reset();
                }
            }
            if (this->activeLimit != previous)
                this->overrunWatch.reset();
            return {
                .active = this->activeLimit.has_value(),
                .changed = previous != this->activeLimit,
                .sourceOverrun = sourceOverrun,
                .generationLimit = this->activeLimit,
                .previousGenerationLimit = previous,
            };
        }

        void pauseForExternalInterruption() {
            // Losing focus is not a failed FIFO pacing experiment. Preserve
            // any genuine earlier failure's backoff without creating one.
            this->activeLimit.reset();
            this->overrunWatch.reset();
        }

        void reset() {
            this->activeLimit.reset();
            this->retryAt.fill(std::nullopt);
            this->overrunWatch.reset();
        }

        [[nodiscard]] bool active() const {
            return this->activeLimit.has_value();
        }

        [[nodiscard]] std::optional<size_t> activeGenerationLimit() const {
            return this->activeLimit;
        }

        [[nodiscard]] static constexpr std::chrono::seconds retryDelay() {
            return std::chrono::seconds{60};
        }

    private:
        std::optional<size_t> activeLimit;
        std::array<std::optional<TimePoint>, 4> retryAt{};
        FifoSourceOverrunWatch overrunWatch;
    };

    /// Normally suppress synthetic frames that exceed confirmed refresh.
    /// Fixed Smooth Cadence can instead request the full multiplier when
    /// ordered FIFO owns real-frame pacing, including under VRR.
    class FixedRefreshBudget {
    public:
        using TimePoint = std::chrono::steady_clock::time_point;

        [[nodiscard]] size_t plan(const TimePoint now,
                const std::optional<uint32_t> refreshHz,
                const size_t maximumGeneratedFrames,
                const bool fullMultiplierCadence = false) {
            if (!refreshHz || *refreshHz == 0 || maximumGeneratedFrames == 0) {
                this->lastRealFrame = now;
                return maximumGeneratedFrames;
            }
            if (!this->lastRealFrame) {
                this->lastRealFrame = now;
                return 0;
            }

            const double rawInterval = std::chrono::duration<double>(
                now - *this->lastRealFrame
            ).count();
            this->lastRealFrame = now;
            if (rawInterval <= 0.0 || rawInterval > 0.25) {
                this->smoothedIntervalSeconds = 0.0;
                this->outputCredit = 0.0;
                return 0;
            }
            if (this->smoothedIntervalSeconds == 0.0)
                this->smoothedIntervalSeconds = rawInterval;
            else
                this->smoothedIntervalSeconds =
                    this->smoothedIntervalSeconds * 0.75 + rawInterval * 0.25;

            // Retain both sides of timestamp jitter. Clipping late intervals
            // at the multiplier while charging every early interval slowly
            // drains the budget even when the average source rate fits exactly.
            const double desiredOutputs = std::max(
                this->smoothedIntervalSeconds * static_cast<double>(*refreshHz),
                1.0
            );
            this->outputCredit += desiredOutputs;
            const size_t requestedOutputs = std::max<size_t>(
                1, static_cast<size_t>(std::floor(this->outputCredit + 1e-9))
            );
            const size_t generated = std::min(
                requestedOutputs - 1, maximumGeneratedFrames
            );
            this->outputCredit -= static_cast<double>(generated + 1);
            if (this->outputCredit < 0.0)
                this->outputCredit = 0.0;
            // Carry at most one output of timing credit, never a backlog from
            // a slow source. It can absorb jitter without authorizing an
            // extended overshoot when the source accelerates.
            this->outputCredit = std::min(this->outputCredit, 1.0);
            // Keep the display budget warm for a later policy change, but
            // retain full Fixed output under ordered FIFO pacing.
            return fullMultiplierCadence ? maximumGeneratedFrames : generated;
        }

        void reset() {
            this->lastRealFrame.reset();
            this->smoothedIntervalSeconds = 0.0;
            this->outputCredit = 0.0;
        }

    private:
        std::optional<TimePoint> lastRealFrame;
        double smoothedIntervalSeconds{0.0};
        double outputCredit{0.0};
    };

    /// Spreads each complete batch across the measured real-frame interval
    /// where no other owner can: no Gamescope, refresh feedback or compositor
    /// output clock, as on a desktop compositor's FIFO. Without it, a batch
    /// below the display rate is shown on consecutive refreshes and then held
    /// (5 FPS x5 at 120 Hz: five outputs in 42 ms, then 158 ms). Outputs wait
    /// on the present thread, so the real frame reaches the display up to one
    /// source interval later. The baseline is measured without waits and is
    /// never raised by them: a source slowed by the wait releases spacing and
    /// retries after a growing cooldown, then only once its unspaced cadence
    /// changes, so a throughput-bound application keeps its frame rate.
    class SourceIntervalOutputSpacing {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;
        using OutputBatch = RealFramePacer::OutputBatch;

        static constexpr size_t windowSize = 8;
        /// The Fixed refresh budget's stall boundary.
        static constexpr Clock::duration maximumInterval = std::chrono::milliseconds(250);
        /// Closer outputs share a refresh even on a 240 Hz display.
        static constexpr Clock::duration minimumOutputInterval = std::chrono::microseconds(4167);
        static constexpr std::array<std::chrono::seconds, 4> retryDelays{
            std::chrono::seconds(5), std::chrono::seconds(15),
            std::chrono::seconds(30), std::chrono::seconds(60)};
        /// Uninterrupted spacing for this long forgets earlier slowdowns.
        static constexpr std::chrono::seconds failureForgiveness{10};
        /// A faster source that waits for its own next slot looks unchanged
        /// while spaced, so game-rate spacing leaves one frame unspaced this
        /// often. Slower sources keep uninterrupted spacing to avoid periodic
        /// visible batches in low-rate video. Effective Dynamic Cadence
        /// Recovery excludes this spacing policy for rate-changing sources.
        static constexpr std::chrono::seconds probeInterval{10};
        static constexpr Clock::duration probeMaximumSourceInterval = std::chrono::milliseconds(50);

        enum class Transition { None, Engaged, Slowed, Faster, Changed, Stalled, Ineligible };

        struct Observation {
            Transition transition{Transition::None};
            Clock::duration sourceInterval{};
            Clock::duration retryAfter{};
        };

        /// Record one application present. Ineligible frames release spacing
        /// but keep retry state; only reset() forgets earlier slowdowns.
        Observation observe(const TimePoint arrival, const bool eligible) {
            const auto previous = std::exchange(this->lastArrival, arrival);
            const bool held = std::exchange(this->heldPrevious, false);
            if (!eligible)
                return this->release(Transition::Ineligible);
            if (!previous)
                return {};
            const auto interval = arrival - *previous;
            if (interval <= Clock::duration::zero() || interval > maximumInterval)
                return this->release(Transition::Stalled);
            if (std::exchange(this->probing, false))
                return interval < scaled(*this->baseline, 0.9)
                    ? this->release(Transition::Faster) : Observation{};
            return this->baseline
                ? this->observeSpaced(arrival, interval, held)
                : this->qualify(arrival, interval);
        }

        /// Deadlines for this frame's outputs, the real frame last. Nothing
        /// is scheduled unless spacing is engaged and the batch finishes
        /// before the next expected source frame.
        [[nodiscard]] std::optional<OutputBatch> schedule(
                const TimePoint readyAt, const size_t count) {
            if (!this->baseline || !this->lastArrival || count < 2 ||
                    count > GeneratedFramePlan::capacity + 1)
                return std::nullopt;
            const auto interval = *this->baseline / static_cast<int64_t>(count);
            const auto lastOutput = readyAt + interval * static_cast<int64_t>(count - 1);
            if (interval < minimumOutputInterval ||
                    lastOutput >= *this->lastArrival + *this->baseline)
                return std::nullopt;
            if (*this->baseline <= probeMaximumSourceInterval &&
                    *this->lastArrival >= this->nextProbeAt) {
                this->nextProbeAt = *this->lastArrival + probeInterval;
                this->probing = true;
                return std::nullopt;
            }
            this->heldPrevious = true;
            return OutputBatch{readyAt, interval, count};
        }

        [[nodiscard]] bool engaged() const { return this->baseline.has_value(); }

        void reset() { *this = SourceIntervalOutputSpacing{}; }

    private:
        struct Window {
            std::array<Clock::duration, windowSize> samples{};
            std::array<bool, windowSize> spaced{};
            size_t count{};
            size_t next{};

            void push(const Clock::duration sample, const bool afterSpacing = false) {
                this->samples[this->next] = sample;
                this->spaced[this->next] = afterSpacing;
                this->next = (this->next + 1) % windowSize;
                this->count = std::min(this->count + 1, windowSize);
            }
            [[nodiscard]] bool full() const { return this->count == windowSize; }
            [[nodiscard]] bool anySpaced() const {
                return std::ranges::any_of(this->spaced, [](const bool value) { return value; });
            }
            /// Upper median: one or two hitches cannot move it.
            [[nodiscard]] Clock::duration median() const {
                auto sorted = this->samples;
                std::nth_element(sorted.begin(), sorted.begin() + windowSize / 2, sorted.end());
                return sorted[windowSize / 2];
            }
        };

        static Clock::duration scaled(const Clock::duration value, const double factor) {
            return std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double, Clock::period>(value) * factor);
        }

        Observation qualify(const TimePoint arrival, const Clock::duration interval) {
            this->window.push(interval);
            if (!this->window.full() || arrival < this->retryAt)
                return {};
            const auto median = this->window.median();
            for (const auto sample : this->window.samples)
                if (sample < scaled(median, 0.9) || sample > scaled(median, 1.1))
                    return {};
            if (this->failures == retryDelays.size() && this->failedInterval &&
                    median > scaled(*this->failedInterval, 0.75) &&
                    median < scaled(*this->failedInterval, 1.25))
                return {};
            this->baseline = median;
            this->window = {};
            this->spacedSince.reset();
            this->nextProbeAt = arrival + probeInterval;
            return {Transition::Engaged, median, {}};
        }

        Observation observeSpaced(const TimePoint arrival,
                const Clock::duration interval, const bool held) {
            // Keep long intervals in the window: the median tolerates isolated
            // hitches, but sustained heavier work must release the old wait.
            // Unspaced frames (native turns, partial or unspaceable batches)
            // still show the source's cadence, so a stale baseline that can
            // no longer space anything is replaced without a penalty.
            this->window.push(interval, held);
            if (!this->window.full())
                return {};
            const auto median = this->window.median();
            // Tight enough that an emulator only just keeping up cannot lose a
            // few percent of speed unnoticed; a false release only unspaces.
            if (median > scaled(*this->baseline, 1.01) + std::chrono::microseconds(250))
                return this->window.anySpaced()
                    ? this->slowed(arrival) : this->release(Transition::Changed);
            if (median < scaled(*this->baseline, 0.9))
                return this->release(Transition::Faster);
            // A start-up cadence can qualify slightly slow. Lowering only
            // shortens waits; a spaced window never raises the baseline.
            this->baseline = std::min(*this->baseline, median);
            if (!this->spacedSince) {
                this->spacedSince = arrival;
            } else if (arrival - *this->spacedSince >= failureForgiveness) {
                this->failures = 0;
                this->failedInterval.reset();
            }
            return {};
        }

        Observation slowed(const TimePoint arrival) {
            const auto delay = retryDelays[std::min(this->failures, retryDelays.size() - 1)];
            this->failures = std::min(this->failures + 1, retryDelays.size());
            this->failedInterval = this->baseline;
            this->retryAt = arrival + delay;
            auto result = this->release(Transition::Slowed);
            result.retryAfter = delay;
            return result;
        }

        Observation release(const Transition reason) {
            Observation result;
            if (this->baseline)
                result = {reason, *this->baseline, {}};
            this->baseline.reset();
            this->window = {};
            this->spacedSince.reset();
            this->probing = false;
            return result;
        }

        std::optional<TimePoint> lastArrival;
        bool heldPrevious{false};
        std::optional<Clock::duration> baseline;
        Window window;
        std::optional<TimePoint> spacedSince;
        TimePoint nextProbeAt{};
        bool probing{false};
        size_t failures{0};
        std::optional<Clock::duration> failedInterval;
        TimePoint retryAt{};
    };

}
