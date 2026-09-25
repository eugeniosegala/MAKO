/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

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
    /// instance creation, so an isolated WSI process cannot safely enter the
    /// Gamescope HDR bridge later, even if compositor feedback reports HDR.
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
            .hdrExposureDisabled = wsiDisabled ||
                environmentFlagEnabled(explicitHdrDisable) ||
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

    /// Some Adaptive ordered shortfalls are direct transport evidence against
    /// the current generated load, such as headroom preflight during a higher
    /// multiplier evaluation. A bounded VRR timeout instead uses the
    /// pressure preflight above, so its caller preserves the scheduler while
    /// transport availability is retried.
    [[nodiscard]] inline bool adaptiveOrderedDeliveryMissRequiresFallback(
            const bool adaptive,
            const bool orderedTransport,
            const size_t requestedGeneratedFrames,
            const size_t admittedGeneratedFrames) noexcept {
        return adaptive && orderedTransport &&
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

    /// Limits application presents before frame-generation policy observes
    /// them. Deadlines stay on an absolute cadence while the application is
    /// early, but any late frame rebases immediately so a loading stall cannot
    /// create a burst of catch-up presents.
    class RealFramePacer {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        [[nodiscard]] TimePoint schedule(
                const TimePoint now, const double framesPerSecond) {
            if (!std::isfinite(framesPerSecond) || framesPerSecond <= 0.0) {
                this->reset();
                return now;
            }

            if (this->activeFramesPerSecond != framesPerSecond) {
                this->nextFrameAt.reset();
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
        }

    private:
        double activeFramesPerSecond{0.0};
        std::optional<TimePoint> nextFrameAt;
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

            const size_t desiredMultiplier =
                scheduler.validatedGenerationLimit + 1;
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

    /// Guards each Steady Adaptive integer-rung handoff from the real-frame
    /// pacer to ordered FIFO. A lost qualification restores the cap and backs
    /// off that rung without delaying a separately validated higher rung.
    class SmoothCadencePacerHandoff {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        struct Decision {
            bool active{false};
            bool changed{false};
            std::optional<size_t> generationLimit;
            std::optional<size_t> previousGenerationLimit;
        };

        [[nodiscard]] Decision update(const TimePoint now,
                const std::optional<size_t> eligibleGenerationLimit) {
            const auto previous = this->activeLimit;
            if (this->activeLimit != eligibleGenerationLimit) {
                if (this->activeLimit && *this->activeLimit >= 1 &&
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
            return {
                .active = this->activeLimit.has_value(),
                .changed = previous != this->activeLimit,
                .generationLimit = this->activeLimit,
                .previousGenerationLimit = previous,
            };
        }

        void pauseForExternalInterruption() {
            // Losing focus is not a failed FIFO pacing experiment. Preserve
            // any genuine earlier failure's backoff without creating one.
            this->activeLimit.reset();
        }

        void reset() {
            this->activeLimit.reset();
            this->retryAt.fill(std::nullopt);
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

            const double desiredOutputs = std::clamp(
                this->smoothedIntervalSeconds * static_cast<double>(*refreshHz),
                1.0,
                static_cast<double>(maximumGeneratedFrames + 1)
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
            if (generated == maximumGeneratedFrames && this->outputCredit >= 1.0)
                this->outputCredit = std::fmod(this->outputCredit, 1.0);
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

}
