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
        [[nodiscard]] bool recoveryWindow(const Clock::time_point now) const {
            return fresh(now) && gameFocused == true && returnedAt &&
                now >= *returnedAt && now - *returnedAt <= std::chrono::seconds{75};
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

    /// Ordered 3x/4x/5x presentation can legitimately wait once per generated
    /// image. Recovery classifies the longest individual wait; summing healthy
    /// refresh-sized waits would falsely turn a normal multi-image sequence
    /// into starvation. The separately enforced configured budget remains
    /// cumulative across the whole application present.
    [[nodiscard]] inline std::chrono::steady_clock::duration
    orderedAcquireRecoveryClassificationDuration(
            const std::chrono::steady_clock::duration totalDuration,
            const std::chrono::steady_clock::duration maximumDuration) {
        return std::min(totalDuration, maximumDuration);
    }

    /// A recovery probe owns one image and one small, display-relative wait.
    /// Later failed attempts may span more refresh periods, but never exceed
    /// 25 ms or the user's normal application-present acquire ceiling. This
    /// gives ordered FIFO presentation a phase boundary to release an image
    /// without reintroducing the ordinary 50 ms multi-image wait.
    [[nodiscard]] inline uint64_t orderedRecoveryAcquireTimeout(
            const std::optional<uint32_t> refreshHz,
            const std::optional<uint64_t> configuredTimeout,
            const size_t consecutiveFailures) {
        constexpr uint64_t nanosecondsPerSecond = 1'000'000'000;
        constexpr uint64_t minimumProbePeriod = 8'000'000;
        constexpr uint64_t maximumProbeTimeout = 25'000'000;
        const uint64_t refreshPeriod = refreshHz && *refreshHz > 0
            ? (nanosecondsPerSecond + *refreshHz - 1) / *refreshHz
            : 16'666'667;
        const uint64_t probePeriod = std::max(
            refreshPeriod, minimumProbePeriod
        );
        const uint64_t probePeriods = std::clamp<size_t>(
            consecutiveFailures, 1, 3
        );
        const uint64_t recoveryTimeout = std::min(
            probePeriod * probePeriods, maximumProbeTimeout
        );
        return configuredTimeout
            ? std::min(recoveryTimeout, *configuredTimeout)
            : recoveryTimeout;
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
    };

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
                return true;
            }

            this->missedAttempts++;
            return (this->missedAttempts & (this->missedAttempts - 1)) == 0;
        }

        void reportBypassedFrame() {
            if (this->pressure)
                this->bypassedFrames++;
        }

        [[nodiscard]] GeneratedImageAdmissionRecovery reportAvailable() {
            const GeneratedImageAdmissionRecovery recovery{
                .resumed = this->pressure,
                .missedAttempts = this->missedAttempts,
                .bypassedFrames = this->bypassedFrames,
            };
            this->reset();
            return recovery;
        }

        void reset() {
            this->pressure = false;
            this->missedAttempts = 0;
            this->bypassedFrames = 0;
        }

    private:
        bool pressure{false};
        size_t missedAttempts{0};
        size_t bypassedFrames{0};
    };

    /// Classifies generated-image starvation on the ordered SDR transport.
    /// One isolated slow successful generated-image acquire arms a
    /// zero-wait guard for the next present so transport delay cannot
    /// immediately recur or contaminate Adaptive cadence. A guard miss gives
    /// one native frame back to the FIFO before normal policy retries; only a
    /// repeated slow image, an exhausted cumulative budget, or one severe
    /// image requests a native-only drain. Recovery warms history and attempts
    /// one bounded single-image probe, returning to backoff on failure.
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
            // A recovery probe must not spend the normal bounded acquire wait:
            // the initial guard stays nonblocking, while a post-drain probe
            // receives one small display-relative timeout.
            bool preacquireGeneratedFrame{false};
            bool boundedAcquireProbe{false};
            // After one drain probe succeeds, native-only presentation remains
            // deterministic until one fixed deadline. No availability miss may
            // extend this interval or intermittently re-enable generation.
            bool nativeOnlyStabilization{false};
            bool recoveryStabilized{false};
            // A drained native FIFO which already satisfies the requested
            // output cadence needs no synthetic-image availability probe.
            // Hold the native path without repeated history warm-up, then
            // re-arm one bounded probe when native cadence materially falls.
            bool nativeCadenceSaturated{false};
            bool nativeCadenceSaturationEntered{false};
            bool nativeCadenceDemandResumed{false};
            size_t bypassedFrames{0};
            size_t consecutiveFailures{0};
            Duration drainDuration{};
            Duration stabilizationRemaining{};
            double nativeBaseFps{0.0};
            double nativeTargetFps{0.0};
        };

        struct Observation {
            bool quarantined{false};
            bool recovered{false};
            bool stabilizing{false};
            bool timedOut{false};
            bool deadlineExceeded{false};
            bool severe{false};
            bool guardArmed{false};
            bool guardCleared{false};
            size_t consecutiveSlowFrames{0};
            size_t consecutiveFailures{0};
            size_t bypassedFrames{0};
            Duration retryDelay{};
            Duration recoveryDuration{};
        };

        struct NonblockingMissObservation {
            bool diagnostic{false};
            bool quarantined{false};
            bool guardBypassed{false};
            bool boundedProbeFailed{false};
            size_t consecutiveFailures{0};
            size_t bypassedFrames{0};
            Duration retryDelay{};
            Duration recoveryDuration{};
        };

        [[nodiscard]] static constexpr auto minimumSlowAcquireDuration() {
            return std::chrono::milliseconds{25};
        }

        [[nodiscard]] static Duration slowAcquireDuration(
                const std::optional<uint32_t> refreshHz) {
            if (!refreshHz || *refreshHz == 0)
                return minimumSlowAcquireDuration();

            const auto displayRelativeDuration =
                std::chrono::duration_cast<Duration>(
                    std::chrono::duration<double>(
                        1.5 / static_cast<double>(*refreshHz)
                    )
                );
            return std::max<Duration>(
                minimumSlowAcquireDuration(), displayRelativeDuration
            );
        }

        [[nodiscard]] static constexpr auto stabilizationDuration() {
            return std::chrono::milliseconds{250};
        }

        [[nodiscard]] static constexpr auto maximumRetryDelay() {
            return std::chrono::milliseconds{30000};
        }

        [[nodiscard]] static constexpr auto
        recreationQualificationDuration() {
            return std::chrono::seconds{3};
        }

        [[nodiscard]] static constexpr auto
        nativeCadenceSaturationQualificationDuration() {
            return std::chrono::milliseconds{200};
        }

        [[nodiscard]] static constexpr auto
        nativeCadenceDemandQualificationDuration() {
            return std::chrono::milliseconds{100};
        }

        [[nodiscard]] static constexpr double
        nativeCadenceSaturationRatio() {
            return 0.95;
        }

        [[nodiscard]] static constexpr double
        nativeCadenceDemandRatio() {
            return 0.90;
        }

        [[nodiscard]] static constexpr Duration severeAcquireDuration(
                const Duration slowAcquireThreshold) {
            return slowAcquireThreshold * 2;
        }

        [[nodiscard]] PresentDecision beforePresent(const TimePoint now,
                const std::optional<Duration> nativePresentInterval =
                    std::nullopt,
                const std::optional<double> nativeTargetFps = std::nullopt) {
            if (this->retryAt) {
                this->observeNativeCadence(
                    now, nativePresentInterval, nativeTargetFps
                );
                if (this->nativeCadenceSaturated) {
                    if (this->nativeCadenceDemandSince &&
                            now - *this->nativeCadenceDemandSince >=
                                nativeCadenceDemandQualificationDuration()) {
                        const double nativeBaseFps =
                            this->nativeCadenceBaseFps();
                        const double targetFps = this->nativeTargetFps;
                        this->nativeCadenceSaturated = false;
                        this->nativeCadenceSaturationSince.reset();
                        this->nativeCadenceDemandSince.reset();
                        this->retryAt.reset();
                        this->probePending = true;
                        return {
                            .beginHistoryWarmup = true,
                            .limitGeneratedFrames = true,
                            .preacquireGeneratedFrame = true,
                            .boundedAcquireProbe = true,
                            .nativeCadenceDemandResumed = true,
                            .bypassedFrames = this->bypassedFrames,
                            .consecutiveFailures =
                                this->consecutiveFailures,
                            .drainDuration = this->recoveryStartedAt
                                ? now - *this->recoveryStartedAt
                                : Duration{},
                            .nativeBaseFps = nativeBaseFps,
                            .nativeTargetFps = targetFps,
                        };
                    }

                    this->bypassedFrames++;
                    return {
                        .bypassGeneration = true,
                        .nativeCadenceSaturated = true,
                        .bypassedFrames = this->bypassedFrames,
                        .consecutiveFailures = this->consecutiveFailures,
                        .drainDuration = this->recoveryStartedAt
                            ? now - *this->recoveryStartedAt
                            : Duration{},
                        .nativeBaseFps = this->nativeCadenceBaseFps(),
                        .nativeTargetFps = this->nativeTargetFps,
                    };
                }

                // Qualify native cadence while the retry timer is running.
                // Otherwise a menu-to-gameplay transition during a long
                // backoff cannot re-arm a probe until that timer expires.
                // Probe failure still retains the acquisition failure count.
                if (this->nativeCadenceSaturationSince &&
                        now - *this->nativeCadenceSaturationSince >=
                            nativeCadenceSaturationQualificationDuration()) {
                    this->nativeCadenceSaturated = true;
                    this->nativeCadenceDemandSince.reset();
                    this->bypassedFrames++;
                    return {
                        .bypassGeneration = true,
                        .nativeCadenceSaturated = true,
                        .nativeCadenceSaturationEntered = true,
                        .bypassedFrames = this->bypassedFrames,
                        .consecutiveFailures = this->consecutiveFailures,
                        .drainDuration = this->recoveryStartedAt
                            ? now - *this->recoveryStartedAt
                            : Duration{},
                        .nativeBaseFps = this->nativeCadenceBaseFps(),
                        .nativeTargetFps = this->nativeTargetFps,
                    };
                }
            }

            if (!this->retryAt) {
                if (this->guardPending) {
                    return {
                        .limitGeneratedFrames = true,
                        .preacquireGeneratedFrame = true,
                    };
                }
                if (this->probePending) {
                    return {
                        .limitGeneratedFrames = true,
                        .preacquireGeneratedFrame = true,
                        .boundedAcquireProbe = true,
                        .consecutiveFailures = this->consecutiveFailures,
                    };
                }
                if (this->stabilizingUntil &&
                        now < *this->stabilizingUntil) {
                    this->bypassedFrames++;
                    return {
                        .bypassGeneration = true,
                        .nativeOnlyStabilization = true,
                        .bypassedFrames = this->bypassedFrames,
                        .consecutiveFailures = this->consecutiveFailures,
                        .drainDuration = this->recoveryStartedAt
                            ? now - *this->recoveryStartedAt
                            : Duration{},
                        .stabilizationRemaining =
                            *this->stabilizingUntil - now,
                    };
                }
                if (this->stabilizingUntil) {
                    const size_t completedFailures =
                        this->consecutiveFailures;
                    const size_t completedBypassedFrames =
                        this->bypassedFrames;
                    const Duration completedRecoveryDuration =
                        this->recoveryStartedAt
                        ? now - *this->recoveryStartedAt
                        : Duration{};
                    this->stabilizingUntil.reset();
                    this->recoveryStartedAt.reset();
                    this->healthySince.reset();
                    this->consecutiveFailures = 0;
                    this->bypassedFrames = 0;
                    this->nonblockingProbeMisses = 0;
                    return {
                        .beginHistoryWarmup = true,
                        .recoveryStabilized = true,
                        .bypassedFrames = completedBypassedFrames,
                        .consecutiveFailures = completedFailures,
                        .drainDuration = completedRecoveryDuration,
                    };
                }
                return {
                };
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
                const Duration acquireDuration,
                const Duration slowAcquireThreshold,
                const bool timedOut,
                const bool deadlineExceeded = false,
                const bool boundedRecoveryProbe = false) {
            const bool severe = deadlineExceeded ||
                acquireDuration >= severeAcquireDuration(
                    slowAcquireThreshold
                );
            const bool boundedProbeSucceeded = boundedRecoveryProbe &&
                this->probePending && !timedOut;
            const bool slow = !boundedProbeSucceeded &&
                (timedOut || severe ||
                 acquireDuration >= slowAcquireThreshold);
            if (slow) {
                this->healthySince.reset();
                this->stabilizingUntil.reset();
                this->nonblockingProbeMisses = 0;
                this->consecutiveSlowFrames++;
                const size_t observedSlowFrames =
                    this->consecutiveSlowFrames;
                const bool isolatedDeadlineMiss = timedOut &&
                    acquireDuration < slowAcquireThreshold;
                if ((!timedOut || isolatedDeadlineMiss) && !severe &&
                        !this->guardPending &&
                        !this->probePending &&
                        observedSlowFrames < slowFrameThreshold) {
                    // One slow successful acquire or a short per-image
                    // deadline miss can be transient FIFO pressure. Neither
                    // proves exhaustion of the cumulative present budget.
                    // The next application present must not
                    // repeat a blocking acquire or feed that transport delay
                    // back into Adaptive's source-cadence clock. Reuse the
                    // zero-wait guard; repeated or severe pressure still
                    // enters native-drain recovery below.
                    this->guardPending = true;
                    return {
                        .timedOut = timedOut,
                        .guardArmed = true,
                        .consecutiveSlowFrames = observedSlowFrames,
                        .consecutiveFailures = this->consecutiveFailures,
                        .bypassedFrames = this->bypassedFrames,
                    };
                }

                return this->beginNativeDrain(
                    now, timedOut, deadlineExceeded, severe,
                    observedSlowFrames
                );
            }

            const bool guardCleared = this->guardPending;
            // A guard tests only one immediately available image. It permits
            // a normal batch retry, but cannot prove that a 3x/4x/5x batch is
            // healthy. Retain pressure until an unrestricted acquire succeeds
            // so timeout/guard-success cycles cannot avoid native recovery.
            if (!guardCleared)
                this->consecutiveSlowFrames = 0;
            const bool drainProbeRecovered = this->probePending;
            const bool recovered = guardCleared || drainProbeRecovered;
            if (recovered) {
                this->guardPending = false;
                this->probePending = false;
                this->healthySince = now;
                // One immediately available image is enough to clear an
                // isolated slow-acquire guard: the guarded scheduler sample
                // already excluded transport delay and no native drain needs
                // to be qualified. Reserve the sustained zero-wait window for
                // recovery from an actual quarantine, where FIFO readiness
                // has not yet been demonstrated across normal presentation.
                if (drainProbeRecovered) {
                    this->stabilizingUntil =
                        now + stabilizationDuration();
                }
                this->nonblockingProbeMisses = 0;
            } else if (this->consecutiveFailures > 0 &&
                    !this->stabilizingUntil) {
                if (!this->healthySince)
                    this->healthySince = now;
                if (now - *this->healthySince >= healthyResetDuration) {
                    this->consecutiveFailures = 0;
                    this->bypassedFrames = 0;
                    this->recoveryStartedAt.reset();
                    this->healthySince.reset();
                }
            } else if (!this->recoveryStartedAt) {
                // A zero-wait guard miss owns one real-only relief frame. Once
                // the following normal acquire is healthy, that isolated
                // bypass must not leak into a later recovery's counters.
                this->bypassedFrames = 0;
            }

            return {
                .recovered = recovered,
                .stabilizing = drainProbeRecovered,
                .guardCleared = guardCleared,
                .consecutiveFailures = this->consecutiveFailures,
                .bypassedFrames = this->bypassedFrames,
                .recoveryDuration = this->recoveryStartedAt
                    ? now - *this->recoveryStartedAt
                    : Duration{},
            };
        }

        [[nodiscard]] bool active() const {
            return this->retryAt.has_value() || this->guardPending ||
                this->probePending ||
                this->stabilizingUntil.has_value();
        }

        /// Repeated bounded probes which cannot reacquire even one generated
        /// image have exhausted this context's in-place recovery. The caller
        /// still requires a recent compositor interruption, a successful
        /// retirement-protected lower present, and a surface-level budget
        /// before turning this into one application-owned recreation.
        [[nodiscard]] bool recreationRequested(
                const TimePoint now) const {
            return !this->recreationSignaled && this->recoveryStartedAt &&
                this->consecutiveFailures >= 3 &&
                now - *this->recoveryStartedAt >=
                    recreationQualificationDuration();
        }

        [[nodiscard]] bool signalRecreation(const TimePoint now) {
            if (!this->recreationRequested(now))
                return false;
            this->recreationSignaled = true;
            return true;
        }

        /// A zero-wait guard miss is only one native relief frame, not proof of
        /// sustained starvation. A bounded post-drain probe miss is terminal
        /// for that attempt and returns to native backoff; probePending can
        /// therefore never become a permanent real-only state.
        [[nodiscard]] NonblockingMissObservation
        reportNonblockingProbeUnavailable(
                const TimePoint now) {
            if (this->guardPending) {
                this->guardPending = false;
                this->bypassedFrames++;
                return {
                    .diagnostic = true,
                    .guardBypassed = true,
                    .consecutiveFailures = this->consecutiveFailures,
                    .bypassedFrames = this->bypassedFrames,
                };
            }

            if (this->probePending) {
                this->bypassedFrames++;
                const auto observation = this->beginNativeDrain(
                    now, true, false, false,
                    this->consecutiveSlowFrames
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
            this->nonblockingProbeMisses++;
            return {
                .diagnostic = (this->nonblockingProbeMisses &
                    (this->nonblockingProbeMisses - 1)) == 0,
                .consecutiveFailures = this->consecutiveFailures,
                .bypassedFrames = this->bypassedFrames,
                .recoveryDuration = this->recoveryStartedAt
                    ? now - *this->recoveryStartedAt
                    : Duration{},
            };
        }

        void reset() {
            this->retryAt.reset();
            this->recoveryStartedAt.reset();
            this->healthySince.reset();
            this->stabilizingUntil.reset();
            this->guardPending = false;
            this->probePending = false;
            this->consecutiveSlowFrames = 0;
            this->consecutiveFailures = 0;
            this->bypassedFrames = 0;
            this->nonblockingProbeMisses = 0;
            this->resetNativeCadenceObservation();
        }

    private:
        void observeNativeCadence(const TimePoint now,
                const std::optional<Duration> nativePresentInterval,
                const std::optional<double> targetFps) {
            if (!targetFps || !std::isfinite(*targetFps) ||
                    *targetFps <= 0.0) {
                this->resetNativeCadenceObservation();
                return;
            }
            if (this->nativeTargetFps == 0.0 ||
                    std::abs(this->nativeTargetFps - *targetFps) > 0.01) {
                this->resetNativeCadenceObservation();
                this->nativeTargetFps = *targetFps;
            }
            if (!nativePresentInterval)
                return;
            if (this->ignoreNextNativeInterval) {
                this->ignoreNextNativeInterval = false;
                return;
            }

            const double rawIntervalSeconds =
                std::chrono::duration<double>(*nativePresentInterval).count();
            if (!std::isfinite(rawIntervalSeconds) ||
                    rawIntervalSeconds <= 0.0 ||
                    rawIntervalSeconds > 0.25) {
                this->nativeSmoothedIntervalSeconds = 0.0;
                this->nativeCadenceSaturationSince.reset();
                if (this->nativeCadenceSaturated &&
                        !this->nativeCadenceDemandSince) {
                    this->nativeCadenceDemandSince = now;
                }
                return;
            }
            if (this->nativeSmoothedIntervalSeconds == 0.0) {
                this->nativeSmoothedIntervalSeconds = rawIntervalSeconds;
            } else {
                this->nativeSmoothedIntervalSeconds =
                    this->nativeSmoothedIntervalSeconds * 0.75 +
                    rawIntervalSeconds * 0.25;
            }

            const double baseFps = this->nativeCadenceBaseFps();
            if (baseFps >= this->nativeTargetFps *
                    nativeCadenceSaturationRatio()) {
                if (!this->nativeCadenceSaturationSince)
                    this->nativeCadenceSaturationSince = now;
                this->nativeCadenceDemandSince.reset();
                return;
            }

            this->nativeCadenceSaturationSince.reset();
            if (this->nativeCadenceSaturated &&
                    baseFps < this->nativeTargetFps *
                        nativeCadenceDemandRatio()) {
                if (!this->nativeCadenceDemandSince)
                    this->nativeCadenceDemandSince = now;
            } else {
                this->nativeCadenceDemandSince.reset();
            }
        }

        [[nodiscard]] double nativeCadenceBaseFps() const {
            return this->nativeSmoothedIntervalSeconds > 0.0
                ? 1.0 / this->nativeSmoothedIntervalSeconds
                : 0.0;
        }

        void resetNativeCadenceObservation() {
            this->nativeCadenceSaturationSince.reset();
            this->nativeCadenceDemandSince.reset();
            this->nativeSmoothedIntervalSeconds = 0.0;
            this->nativeTargetFps = 0.0;
            this->nativeCadenceSaturated = false;
            this->ignoreNextNativeInterval = true;
        }

        [[nodiscard]] Observation beginNativeDrain(const TimePoint now,
                const bool timedOut, const bool deadlineExceeded,
                const bool severe,
                const size_t observedSlowFrames) {
            if (!this->recoveryStartedAt)
                this->recoveryStartedAt = now;
            this->consecutiveFailures++;
            const auto delay = retryDelayForFailure(
                this->consecutiveFailures
            );
            this->retryAt = now + delay;
            this->guardPending = false;
            this->probePending = false;
            this->consecutiveSlowFrames = 0;
            this->resetNativeCadenceObservation();
            return {
                .quarantined = true,
                .timedOut = timedOut,
                .deadlineExceeded = deadlineExceeded,
                .severe = severe,
                .consecutiveSlowFrames = observedSlowFrames,
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

        static constexpr size_t slowFrameThreshold = 2;
        static constexpr auto healthyResetDuration =
            std::chrono::seconds{2};

        std::optional<TimePoint> retryAt;
        std::optional<TimePoint> recoveryStartedAt;
        std::optional<TimePoint> healthySince;
        std::optional<TimePoint> stabilizingUntil;
        bool guardPending{false};
        bool probePending{false};
        size_t consecutiveSlowFrames{0};
        size_t consecutiveFailures{0};
        size_t bypassedFrames{0};
        size_t nonblockingProbeMisses{0};
        std::optional<TimePoint> nativeCadenceSaturationSince;
        std::optional<TimePoint> nativeCadenceDemandSince;
        double nativeSmoothedIntervalSeconds{0.0};
        double nativeTargetFps{0.0};
        bool nativeCadenceSaturated{false};
        bool ignoreNextNativeInterval{true};
        bool recreationSignaled{false};
    };

    /// Keep the application-present budget cumulative for 3x/4x/5x, while
    /// preventing one unavailable lower image from consuming the full legacy
    /// 50 ms ceiling by itself. On a known-refresh ordered path, allow two and
    /// a half display periods with an 8 ms floor. Any extension beyond the
    /// original one-and-a-half-period window must leave half a display period
    /// below the pressure threshold: a short deadline miss must still reach
    /// the zero-wait guard instead of immediately starting native drain.
    /// Unknown-refresh and unconfigured paths retain their historical 25 ms
    /// and unbounded contracts respectively.
    [[nodiscard]] inline uint64_t orderedGeneratedImageAcquireTimeout(
            const std::optional<uint32_t> refreshHz,
            const std::optional<uint64_t> remainingBudget) {
        if (!remainingBudget)
            return std::numeric_limits<uint64_t>::max();
        constexpr uint64_t nanosecondsPerSecond = 1'000'000'000;
        constexpr uint64_t minimumPerImageTimeout = 8'000'000;
        constexpr uint64_t unknownRefreshTimeout = 25'000'000;
        const auto pressureCeiling = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                OrderedAcquireRecovery::slowAcquireDuration(refreshHz)
            ).count()
        );
        uint64_t perImageCeiling = unknownRefreshTimeout;
        if (refreshHz && *refreshHz > 0) {
            const uint64_t divisor = static_cast<uint64_t>(*refreshHz) * 2;
            const auto displayPeriods = [divisor](const uint64_t halves) {
                return (nanosecondsPerSecond * halves + divisor - 1) / divisor;
            };
            const uint64_t guardMargin = displayPeriods(1);
            const uint64_t extensionCeiling = pressureCeiling > guardMargin
                ? pressureCeiling - guardMargin : 0;
            perImageCeiling = std::max({minimumPerImageTimeout,
                displayPeriods(3),
                std::min(displayPeriods(5), extensionCeiling)});
        }
        return std::min(
            *remainingBudget,
            perImageCeiling
        );
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

    /// Guards the Steady Adaptive handoff from the explicit real-frame pacer
    /// to ordered FIFO. A lost qualification restores the cap immediately and
    /// applies a long retry delay so an unsuitable game cannot receive a
    /// periodic pacing disturbance.
    class SmoothCadencePacerHandoff {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        struct Decision {
            bool active{false};
            bool changed{false};
        };

        [[nodiscard]] Decision update(const TimePoint now,
                const bool eligible) {
            if (this->handoffActive && !eligible) {
                this->handoffActive = false;
                this->retryAt = now + retryDelay();
                return {.active = false, .changed = true};
            }
            if (!this->handoffActive && eligible &&
                    (!this->retryAt || now >= *this->retryAt)) {
                this->handoffActive = true;
                this->retryAt.reset();
                return {.active = true, .changed = true};
            }
            return {.active = this->handoffActive};
        }

        void pauseForExternalInterruption() {
            // Losing focus is not a failed FIFO pacing experiment. Preserve
            // any genuine earlier failure's backoff without creating one.
            this->handoffActive = false;
        }

        void reset() {
            this->handoffActive = false;
            this->retryAt.reset();
        }

        [[nodiscard]] static constexpr std::chrono::seconds retryDelay() {
            return std::chrono::seconds{60};
        }

    private:
        bool handoffActive{false};
        std::optional<TimePoint> retryAt;
    };

    /// A successful lower QueuePresentKHR can still block long enough to make
    /// Steam's overlay unresponsive. This is distinct from generated-image
    /// acquisition pressure: once observed, stop adding synthetic presents
    /// for one fixed stabilization window, then warm temporal history before
    /// retrying. The deadline is absolute so recovery cannot become a
    /// self-extending native-only mode.
    class LowerPresentStallRecovery {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;
        using Duration = Clock::duration;

        struct PresentDecision {
            bool bypassGeneration{false};
            bool beginHistoryWarmup{false};
            bool recovered{false};
            bool recreationWaitExpired{false};
            size_t bypassedFrames{0};
            Duration recoveryDuration{};
        };

        struct Observation {
            bool quarantined{false};
            Duration presentDuration{};
            Duration threshold{};
            Duration stabilizationDuration{};
            size_t consecutiveStalls{0};
        };

        struct NativeRecoveryObservation {
            bool newlyArmedRecreation{false};
            bool cancelledRecreation{false};
            Duration presentDuration{};
            Duration threshold{};
            size_t severeNativeStalls{0};
        };

        [[nodiscard]] static Duration stallThreshold(
                const std::optional<uint32_t> refreshHz) {
            constexpr auto minimumThreshold =
                std::chrono::milliseconds{250};
            if (!refreshHz || *refreshHz == 0)
                return minimumThreshold;
            const auto displayRelativeThreshold =
                std::chrono::duration_cast<Duration>(
                    std::chrono::duration<double>(
                        4.0 / static_cast<double>(*refreshHz)
                    )
                );
            return std::max<Duration>(
                minimumThreshold, displayRelativeThreshold
            );
        }

        [[nodiscard]] static constexpr auto stabilizationDuration(
                const size_t consecutiveStalls = 1) {
            if (consecutiveStalls >= 4)
                return std::chrono::seconds{60};
            if (consecutiveStalls == 3)
                return std::chrono::seconds{30};
            if (consecutiveStalls == 2)
                return std::chrono::seconds{10};
            return std::chrono::seconds{2};
        }

        [[nodiscard]] Observation observe(const TimePoint now,
                const Duration maximumPresentDuration,
                const std::optional<uint32_t> refreshHz) {
            const auto threshold = stallThreshold(refreshHz);
            if (maximumPresentDuration < threshold) {
                if (this->lastStallAt &&
                        now - *this->lastStallAt >=
                            healthyResetDuration()) {
                    this->consecutiveStalls = 0;
                    this->lastStallAt.reset();
                }
                return {
                    .presentDuration = maximumPresentDuration,
                    .threshold = threshold,
                };
            }

            if (this->lastStallAt &&
                    now - *this->lastStallAt < healthyResetDuration()) {
                this->consecutiveStalls++;
            } else {
                this->consecutiveStalls = 1;
            }
            this->lastStallAt = now;
            const auto stabilization = stabilizationDuration(
                this->consecutiveStalls
            );
            this->startedAt = now;
            this->stabilizingUntil = now + stabilization;
            this->bypassedFrames = 0;
            this->nativeHealthySince.reset();
            this->severeNativeStalls = 0;
            this->recreationArmed = false;
            return {
                .quarantined = true,
                .presentDuration = maximumPresentDuration,
                .threshold = threshold,
                .stabilizationDuration = stabilization,
                .consecutiveStalls = this->consecutiveStalls,
            };
        }

        /// Repeated severe native presents justify a guarded recreation, but
        /// can also be external throttling. Cancel stale evidence after one
        /// healthy second and never turn an unavailable rebuild into an
        /// indefinite native-only latch.
        [[nodiscard]] NativeRecoveryObservation observeNativeRecoveryPresent(
                const TimePoint now, const Duration presentDuration,
                const std::optional<uint32_t> refreshHz) {
            const auto threshold = stallThreshold(refreshHz);
            if (!this->active())
                return {};
            if (presentDuration < threshold) {
                if (!this->nativeHealthySince)
                    this->nativeHealthySince = now;
                const bool healthy = now - *this->nativeHealthySince >=
                    std::chrono::seconds{1};
                const bool cancelled = healthy && this->recreationArmed;
                if (healthy) {
                    this->recreationArmed = false;
                    this->severeNativeStalls = 0;
                }
                return {
                    .cancelledRecreation = cancelled,
                    .presentDuration = presentDuration,
                    .threshold = threshold,
                    .severeNativeStalls = this->severeNativeStalls,
                };
            }

            this->nativeHealthySince.reset();
            this->severeNativeStalls++;
            const bool newlyArmed = !this->recreationArmed &&
                !this->recreationSignaled && this->severeNativeStalls >= 2;
            this->recreationArmed |= newlyArmed;
            return {
                .newlyArmedRecreation = newlyArmed,
                .presentDuration = presentDuration,
                .threshold = threshold,
                .severeNativeStalls = this->severeNativeStalls,
            };
        }

        [[nodiscard]] PresentDecision beforePresent(const TimePoint now) {
            if (!this->stabilizingUntil)
                return {};
            const auto deadline = this->recreationArmed && this->startedAt
                ? std::max(*this->stabilizingUntil,
                    *this->startedAt + std::chrono::seconds{30})
                : *this->stabilizingUntil;
            if (now < deadline) {
                this->bypassedFrames++;
                return {
                    .bypassGeneration = true,
                    .bypassedFrames = this->bypassedFrames,
                    .recoveryDuration = this->startedAt
                        ? now - *this->startedAt : Duration{},
                };
            }

            const PresentDecision decision{
                .beginHistoryWarmup = true,
                .recovered = true,
                .recreationWaitExpired = this->recreationArmed,
                .bypassedFrames = this->bypassedFrames,
                .recoveryDuration = this->startedAt
                    ? now - *this->startedAt : Duration{},
            };
            this->startedAt.reset();
            this->stabilizingUntil.reset();
            this->bypassedFrames = 0;
            this->recreationArmed = false;
            this->nativeHealthySince.reset();
            return decision;
        }

        [[nodiscard]] bool active() const {
            return this->stabilizingUntil.has_value();
        }

        [[nodiscard]] bool recreationRequested() const {
            return this->recreationArmed;
        }

        [[nodiscard]] bool signalRecreation() {
            if (!this->recreationArmed || this->recreationSignaled)
                return false;
            this->recreationArmed = false;
            this->recreationSignaled = true;
            return true;
        }

        [[nodiscard]] size_t severeRecoveryStalls() const {
            return this->severeNativeStalls;
        }

        void reset() {
            this->startedAt.reset();
            this->stabilizingUntil.reset();
            this->bypassedFrames = 0;
            this->lastStallAt.reset();
            this->consecutiveStalls = 0;
            this->severeNativeStalls = 0;
            this->recreationArmed = false;
            this->nativeHealthySince.reset();
            // One request per game-owned context, including live Off/On or
            // private resource changes if the application ignored OUT_OF_DATE.
        }

    private:
        [[nodiscard]] static constexpr std::chrono::seconds
        healthyResetDuration() {
            return std::chrono::seconds{30};
        }

        std::optional<TimePoint> startedAt;
        std::optional<TimePoint> stabilizingUntil;
        size_t bypassedFrames{0};
        std::optional<TimePoint> lastStallAt;
        size_t consecutiveStalls{0};
        size_t severeNativeStalls{0};
        bool recreationArmed{false};
        bool recreationSignaled{false};
        std::optional<TimePoint> nativeHealthySince;
    };

    /// One guarded request per context, including after live/private resets.
    /// FPS/cadence changes alone never arm a game-owned recreation.
    class PersistentGenerationRecoveryRecreation {
    public:
        [[nodiscard]] bool signal(const bool qualified = false) {
            if (!qualified || this->signaled)
                return false;
            this->signaled = true;
            return true;
        }
    private:
        bool signaled{false};
    };

    /// All recovery actions share minimum surface spacing. The menu watchdog
    /// also has a two-attempt, 75-second episode limit, rather than a minutes-
    /// long cooldown that can outlive the interruption being repaired.
    [[nodiscard]] constexpr std::chrono::seconds
    persistentGenerationRecoveryActionCooldown(
            const size_t completedRequests) noexcept {
        return completedRequests == 0 ? std::chrono::seconds::zero()
            : std::chrono::seconds{30};
    }

    /// Process-surface budget shared by replacement swapchains. Only confirmed
    /// Steam focus-return episodes authorize the performance watchdog.
    [[nodiscard]] inline std::optional<uint32_t>
    persistentGenerationRecoveryTargetFps(
            const bool adaptiveMode,
            const uint32_t adaptiveTargetFps,
            const std::optional<uint32_t> refreshHz) noexcept {
        if (adaptiveMode)
            return adaptiveTargetFps > 0
                ? std::optional<uint32_t>{adaptiveTargetFps}
                : std::nullopt;
        return refreshHz && *refreshHz > 0 ? refreshHz : std::nullopt;
    }

    class PersistentGenerationRecoverySurfaceBudget {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;
        using Duration = Clock::duration;

        struct RecoverySample {
            uint64_t contextId{0};
            uint64_t configurationRevision{0};
            bool adaptiveMode{false};
            uint32_t targetFps{0};
            uint32_t refreshHz{0};
            std::array<uint32_t, 4> extents{};
            GamescopeFocusFeedback focus;
            std::optional<double> outputFps;
            double lowerPresentShare{0.0};
        };

        struct DeficitObservation {
            bool qualified{false};
            bool newlyQualified{false};
            bool cancelled{false};
            bool retainedBaselineUsed{false};
            Duration duration{};
            std::optional<double> baselineOutputFps;
            std::optional<double> baselineLowerPresentShare;
            std::optional<double> recoveryThresholdFps;
        };

        /// Retain a tiny, allocation-free pre-menu performance history, then
        /// compare returned gameplay with that exact baseline. This recognizes
        /// recovery to a game's previous below-target performance instead of
        /// treating the configured target as proof that the game was healthy.
        /// Keep the episode across our own replacement for one bounded retry.
        [[nodiscard]] DeficitObservation observeSustainedDeficit(
                const TimePoint now,
                const std::optional<RecoverySample>& sample) {
            const bool wasQualified = this->deficitQualified;
            if (!sample || sample->targetFps == 0 ||
                    !sample->focus.fresh(now) || !sample->focus.gameFocused) {
                this->resetDeficitWatch();
                return {.cancelled = wasQualified};
            }
            const bool contextChanged = this->lastRecoverySample &&
                sample->contextId != this->lastRecoverySample->contextId;
            const bool policyChanged = this->lastRecoverySample &&
                (sample->configurationRevision !=
                    this->lastRecoverySample->configurationRevision ||
                 sample->adaptiveMode !=
                    this->lastRecoverySample->adaptiveMode ||
                 sample->targetFps != this->lastRecoverySample->targetFps ||
                 sample->refreshHz != this->lastRecoverySample->refreshHz ||
                 sample->extents != this->lastRecoverySample->extents);
            if (policyChanged || (contextChanged && !this->expectedReplacement))
                this->resetDeficitWatch();
            const bool interrupted = this->lastRecoverySample &&
                sample->focus.returnSequence >
                    this->lastRecoverySample->focus.returnSequence &&
                sample->focus.recoveryWindow(now);
            if (contextChanged) {
                this->expectedReplacement = false;
                this->deficitSince.reset();
                this->deficitRecoveredSince.reset();
                this->clearPerformanceHistory();
                this->retainedPerformanceBaseline.reset();
                this->stagedScaledRecreationAt.reset();
            }
            if (sample->focus.menuOpen(now)) {
                if (sample->focus.openedAt &&
                        (!this->pendingOpenedAt ||
                         *this->pendingOpenedAt != *sample->focus.openedAt)) {
                    this->pendingOpenedAt = sample->focus.openedAt;
                    const auto selection = this->selectInterruptionBaseline(
                        *sample->focus.openedAt
                    );
                    this->pendingInterruptionBaseline = selection.baseline;
                    this->pendingRetainedBaselineUsed =
                        selection.retainedBaselineUsed;
                }
                // Never learn Steam's throttled output or accrue a recovery
                // action while the menu is open.
                this->lastRecoverySample = sample;
                this->interruptionBaseline.reset();
                this->deficitRecoveredSince.reset();
                this->deficitSince.reset();
                this->deficitQualified = false;
                this->postInterruption = false;
                return {.cancelled = wasQualified};
            }
            if (interrupted) {
                const bool pendingMatches = sample->focus.openedAt &&
                    this->pendingOpenedAt &&
                    *sample->focus.openedAt == *this->pendingOpenedAt;
                if (pendingMatches) {
                    this->interruptionBaseline =
                        this->pendingInterruptionBaseline;
                    this->interruptionRetainedBaselineUsed =
                        this->pendingRetainedBaselineUsed;
                } else if (sample->focus.openedAt) {
                    const auto selection = this->selectInterruptionBaseline(
                        *sample->focus.openedAt
                    );
                    this->interruptionBaseline = selection.baseline;
                    this->interruptionRetainedBaselineUsed =
                        selection.retainedBaselineUsed;
                } else {
                    this->interruptionBaseline.reset();
                    this->interruptionRetainedBaselineUsed = false;
                }
                this->pendingInterruptionBaseline.reset();
                this->pendingOpenedAt.reset();
                this->pendingRetainedBaselineUsed = false;
                this->postInterruption =
                    this->interruptionBaseline.has_value();
                this->episodeRequests = 0;
                this->deficitRecoveredSince.reset();
            }
            this->lastRecoverySample = sample;
            const bool valid = sample->outputFps &&
                std::isfinite(*sample->outputFps) && *sample->outputFps > 0.0;
            const auto referenceOutputFps = this->interruptionBaseline
                ? std::min(
                    this->interruptionBaseline->outputFps,
                    static_cast<double>(sample->targetFps)
                )
                : 0.0;
            const auto recoveryThresholdFps = referenceOutputFps * 0.75;
            const auto baselineOutputFps = this->interruptionBaseline
                ? std::optional<double>{this->interruptionBaseline->outputFps}
                : std::nullopt;
            const auto baselineLowerPresentShare = this->interruptionBaseline
                ? std::optional<double>{
                    this->interruptionBaseline->lowerPresentShare
                }
                : std::nullopt;
            if (!sample->focus.recoveryWindow(now)) {
                this->postInterruption = false;
                this->interruptionBaseline.reset();
                this->interruptionRetainedBaselineUsed = false;
                this->stagedScaledRecreationAt.reset();
            }
            // Once delivered output recovers to the pre-menu comparison band,
            // a later workload slowdown needs a new interruption; do not retain
            // a menu visit as permission for an unrelated future action.
            if (valid && !interrupted &&
                    this->postInterruption &&
                    *sample->outputFps >= recoveryThresholdFps) {
                if (!this->deficitRecoveredSince)
                    this->deficitRecoveredSince = now;
                if (now - *this->deficitRecoveredSince >= std::chrono::seconds{1}) {
                    this->postInterruption = false;
                    this->interruptionBaseline.reset();
                    this->interruptionRetainedBaselineUsed = false;
                    this->stagedScaledRecreationAt.reset();
                }
            } else {
                this->deficitRecoveredSince.reset();
            }
            const bool deficit = valid && this->interruptionBaseline &&
                this->postInterruption &&
                this->episodeRequests <
                    (this->interruptionRetainedBaselineUsed &&
                            !this->stagedScaledRecreationAt
                        ? 1 : 2) &&
                *sample->outputFps < recoveryThresholdFps &&
                std::isfinite(sample->lowerPresentShare) &&
                sample->lowerPresentShare >= 0.5;
            if (deficit) {
                if (!this->deficitSince)
                    this->deficitSince = now;
            } else {
                this->deficitSince.reset();
            }
            const auto duration = this->deficitSince
                ? now - *this->deficitSince : Duration{};
            const auto requiredDeficitDuration = this->stagedScaledRecreationAt
                ? stagedScaledRecreationDelay
                : std::chrono::seconds{4};
            this->deficitQualified = deficit &&
                duration >= requiredDeficitDuration;
            if (valid && !sample->focus.menuOpen(now))
                this->recordPerformanceSample(now, *sample);
            return {
                .qualified = this->deficitQualified,
                .newlyQualified = this->deficitQualified && !wasQualified,
                .cancelled = wasQualified && !this->deficitQualified,
                .retainedBaselineUsed =
                    this->interruptionRetainedBaselineUsed,
                .duration = duration,
                .baselineOutputFps = baselineOutputFps,
                .baselineLowerPresentShare = baselineLowerPresentShare,
                .recoveryThresholdFps = baselineOutputFps
                    ? std::optional<double>{recoveryThresholdFps}
                    : std::nullopt,
            };
        }

        [[nodiscard]] bool available(const TimePoint now) const {
            return !this->lastRequestedAt ||
                now - *this->lastRequestedAt >=
                    persistentGenerationRecoveryActionCooldown(
                        this->completedRequestCount
                    );
        }

        /// Returned severe native stalls share the same minimum spacing.
        [[nodiscard]] bool severeLowerPresentAvailable(
                const TimePoint now) const {
            return this->available(now);
        }

        [[nodiscard]] bool stagedScaledRecreationPending() const {
            return this->stagedScaledRecreationAt.has_value();
        }

        [[nodiscard]] bool stagedScaledRecreationAvailable(
                const TimePoint now) const {
            return this->stagedScaledRecreationAt &&
                now >= *this->stagedScaledRecreationAt;
        }

        void recordRequest(const TimePoint now,
                const bool sustainedDeficit = false,
                const bool expectedReplacement = true,
                const bool inPlaceScaledRecovery = false,
                const bool stagedScaledRecreation = false) {
            this->completedRequestCount++;
            this->lastRequestedAt = now;
            this->expectedReplacement = expectedReplacement;
            if (sustainedDeficit)
                ++this->episodeRequests;
            if (stagedScaledRecreation)
                this->episodeRequests = 2;
            if (inPlaceScaledRecovery) {
                this->stagedScaledRecreationAt =
                    now + stagedScaledRecreationDelay;
            } else if (expectedReplacement) {
                this->stagedScaledRecreationAt.reset();
            }
            this->deficitQualified = false;
            if (inPlaceScaledRecovery)
                this->deficitSince = now;
            else
                this->deficitSince.reset();
            this->deficitRecoveredSince.reset();
        }

        [[nodiscard]] size_t completedRequests() const {
            return this->completedRequestCount;
        }

        [[nodiscard]] std::chrono::seconds nextCooldown() const {
            if (this->stagedScaledRecreationAt)
                return stagedScaledRecreationDelay;
            return persistentGenerationRecoveryActionCooldown(
                this->completedRequestCount
            );
        }

    private:
        struct PerformancePoint {
            TimePoint sampledAt{};
            double outputFps{0.0};
            double lowerPresentShare{0.0};
        };

        struct PerformanceBaseline {
            double outputFps{0.0};
            double lowerPresentShare{0.0};
            TimePoint firstSampledAt{};
            TimePoint lastSampledAt{};
            size_t sampleCount{0};
        };

        struct BaselineSelection {
            std::optional<PerformanceBaseline> baseline;
            bool retainedBaselineUsed{false};
        };

        static constexpr size_t performanceHistoryCapacity = 8;
        static constexpr auto retainedBaselineLifetime =
            std::chrono::minutes{3};
        static constexpr auto stagedScaledRecreationDelay =
            std::chrono::seconds{3};

        void recordPerformanceSample(const TimePoint now,
                const RecoverySample& sample) {
            if (!sample.outputFps ||
                    !std::isfinite(*sample.outputFps) ||
                    *sample.outputFps <= 0.0 ||
                    !std::isfinite(sample.lowerPresentShare)) {
                return;
            }
            if (this->lastPerformanceSampleAt) {
                if (now < *this->lastPerformanceSampleAt) {
                    this->clearPerformanceHistory();
                } else if (now - *this->lastPerformanceSampleAt <
                        std::chrono::milliseconds{250}) {
                    return;
                }
            }
            this->performanceHistory[this->performanceHistoryNext] = {
                .sampledAt = now,
                .outputFps = *sample.outputFps,
                .lowerPresentShare = sample.lowerPresentShare,
            };
            this->performanceHistoryNext =
                (this->performanceHistoryNext + 1) % performanceHistoryCapacity;
            this->performanceHistoryCount = std::min(
                this->performanceHistoryCount + 1,
                performanceHistoryCapacity
            );
            this->lastPerformanceSampleAt = now;
        }

        [[nodiscard]] std::optional<PerformanceBaseline>
        capturePerformanceBaseline(const TimePoint openedAt) const {
            std::array<double, performanceHistoryCapacity> outputFps{};
            std::array<double, performanceHistoryCapacity> lowerPresentShare{};
            size_t selected = 0;
            std::optional<TimePoint> firstSampledAt;
            std::optional<TimePoint> lastSampledAt;
            const size_t first =
                (this->performanceHistoryNext + performanceHistoryCapacity -
                 this->performanceHistoryCount) % performanceHistoryCapacity;
            for (size_t index = 0;
                    index < this->performanceHistoryCount; ++index) {
                const auto& point = this->performanceHistory[
                    (first + index) % performanceHistoryCapacity
                ];
                if (point.sampledAt > openedAt ||
                        openedAt - point.sampledAt > std::chrono::seconds{15}) {
                    continue;
                }
                outputFps[selected] = point.outputFps;
                lowerPresentShare[selected] = point.lowerPresentShare;
                if (!firstSampledAt)
                    firstSampledAt = point.sampledAt;
                lastSampledAt = point.sampledAt;
                ++selected;
            }
            if (selected < 3 || !firstSampledAt || !lastSampledAt ||
                    *lastSampledAt - *firstSampledAt < std::chrono::seconds{1}) {
                return std::nullopt;
            }
            const auto median = [selected](auto& values) {
                std::sort(values.begin(), values.begin() + selected);
                const size_t middle = selected / 2;
                return selected % 2 == 0
                    ? (values[middle - 1] + values[middle]) / 2.0
                    : values[middle];
            };
            return PerformanceBaseline{
                .outputFps = median(outputFps),
                .lowerPresentShare = median(lowerPresentShare),
                .firstSampledAt = *firstSampledAt,
                .lastSampledAt = *lastSampledAt,
                .sampleCount = selected,
            };
        }

        /// A return can look healthy for one second and collapse later while
        /// the same WSI context remains wedged. Preserve the last pre-menu
        /// baseline across that apparent recovery. A later confirmed menu may
        /// reuse it only when the immediate pre-menu history is severely
        /// slower and QueuePresent owns most of the interval. This excludes a
        /// game/GPU workload slowdown in the usual case and authorizes only
        /// one action for the retained-reference episode.
        [[nodiscard]] BaselineSelection selectInterruptionBaseline(
                const TimePoint openedAt) {
            const auto current = this->capturePerformanceBaseline(openedAt);
            if (!current)
                return {};

            if (this->retainedPerformanceBaseline &&
                    (openedAt <
                        this->retainedPerformanceBaseline->lastSampledAt ||
                     openedAt -
                        this->retainedPerformanceBaseline->lastSampledAt >
                            retainedBaselineLifetime)) {
                this->retainedPerformanceBaseline.reset();
            }

            const bool transportBoundRegression =
                this->retainedPerformanceBaseline &&
                current->outputFps <
                    this->retainedPerformanceBaseline->outputFps * 0.6 &&
                current->lowerPresentShare >= 0.75;
            if (transportBoundRegression) {
                return {
                    .baseline = this->retainedPerformanceBaseline,
                    .retainedBaselineUsed = true,
                };
            }

            this->retainedPerformanceBaseline = current;
            return {.baseline = current};
        }

        void clearPerformanceHistory() {
            this->performanceHistoryCount = 0;
            this->performanceHistoryNext = 0;
            this->lastPerformanceSampleAt.reset();
        }

        void resetDeficitWatch() {
            this->lastRecoverySample.reset();
            this->clearPerformanceHistory();
            this->pendingInterruptionBaseline.reset();
            this->pendingOpenedAt.reset();
            this->pendingRetainedBaselineUsed = false;
            this->interruptionBaseline.reset();
            this->interruptionRetainedBaselineUsed = false;
            this->retainedPerformanceBaseline.reset();
            this->deficitRecoveredSince.reset();
            this->deficitSince.reset();
            this->postInterruption = false;
            this->episodeRequests = 0;
            this->deficitQualified = false;
            this->expectedReplacement = false;
            this->stagedScaledRecreationAt.reset();
        }

        size_t completedRequestCount{0};
        std::optional<TimePoint> lastRequestedAt;
        std::optional<RecoverySample> lastRecoverySample;
        std::array<PerformancePoint, performanceHistoryCapacity>
            performanceHistory{};
        size_t performanceHistoryCount{0};
        size_t performanceHistoryNext{0};
        std::optional<TimePoint> lastPerformanceSampleAt;
        std::optional<PerformanceBaseline> pendingInterruptionBaseline;
        std::optional<TimePoint> pendingOpenedAt;
        bool pendingRetainedBaselineUsed{false};
        std::optional<PerformanceBaseline> interruptionBaseline;
        bool interruptionRetainedBaselineUsed{false};
        std::optional<PerformanceBaseline> retainedPerformanceBaseline;
        std::optional<TimePoint> deficitRecoveredSince;
        std::optional<TimePoint> deficitSince;
        bool postInterruption{false};
        size_t episodeRequests{0};
        bool deficitQualified{false};
        bool expectedReplacement{false};
        std::optional<TimePoint> stagedScaledRecreationAt;
    };

    /// Count successful lower presents, not the selected multiplier. Close
    /// each batch at the next application-present start so its output is
    /// paired with the interval that actually contained its work. A short
    /// window also handles fractional plans alternating real-only/FG frames.
    class RecoveryPresentHealth {
    public:
        using Clock = std::chrono::steady_clock;
        using Duration = Clock::duration;

        void beginPresent(const Clock::time_point now) {
            if (this->lastStarted) {
                const auto interval = now - *this->lastStarted;
                if (interval <= Duration::zero() ||
                        interval >= std::chrono::seconds{1} ||
                        this->batchFailed) {
                    this->windowDuration = {};
                    this->windowFrames = 0;
                    this->windowLowerPresentDuration = {};
                    this->observedDuration.reset();
                } else {
                    this->windowDuration += interval;
                    this->windowFrames += this->batchFrames;
                    this->windowLowerPresentDuration += this->batchLowerPresentDuration;
                    if (this->windowDuration >= std::chrono::milliseconds{250}) {
                        this->observedDuration = this->windowDuration;
                        this->observedFrames = this->windowFrames;
                        this->observedLowerPresentDuration = this->windowLowerPresentDuration;
                        this->windowDuration = {};
                        this->windowFrames = 0;
                        this->windowLowerPresentDuration = {};
                    }
                }
            }
            this->lastStarted = now;
            this->batchFrames = 0;
            this->batchFailed = false;
            this->maximumDuration = {};
            this->batchLowerPresentDuration = {};
        }

        void observePresent(const Duration duration, const bool succeeded) {
            this->maximumDuration = std::max(this->maximumDuration, duration);
            this->batchLowerPresentDuration += duration;
            this->batchFrames += succeeded ? 1 : 0;
            this->batchFailed |= !succeeded;
        }

        [[nodiscard]] Duration maximumPresentDuration() const {
            return this->maximumDuration;
        }

        [[nodiscard]] std::optional<double> outputFps() const {
            if (this->batchFailed || !this->observedDuration ||
                    *this->observedDuration <= Duration::zero())
                return std::nullopt;
            return this->observedFrames /
                std::chrono::duration<double>(*this->observedDuration).count();
        }

        [[nodiscard]] double lowerPresentShare() const {
            if (!this->observedDuration || *this->observedDuration <= Duration::zero())
                return 0.0;
            return std::chrono::duration<double>(this->observedLowerPresentDuration).count() /
                std::chrono::duration<double>(*this->observedDuration).count();
        }

    private:
        std::optional<Clock::time_point> lastStarted;
        Duration windowDuration{};
        size_t windowFrames{0};
        std::optional<Duration> observedDuration;
        size_t observedFrames{0};
        size_t batchFrames{0};
        bool batchFailed{false};
        Duration maximumDuration{};
        Duration batchLowerPresentDuration{};
        Duration windowLowerPresentDuration{};
        Duration observedLowerPresentDuration{};
    };

    [[nodiscard]] constexpr bool fixedCadenceCollapseRecoveryEligible(
            const bool schedulerEnabled,
            const bool privateOrderedTransport,
            const bool orderedAcquireRecoveryActive,
            const bool historyWarmupActive,
            const std::optional<uint32_t> refreshHz,
            const size_t maximumGeneratedFrames) noexcept {
        return !schedulerEnabled && privateOrderedTransport &&
            !orderedAcquireRecoveryActive && !historyWarmupActive &&
            refreshHz && *refreshHz > 0 && maximumGeneratedFrames > 0;
    }

    /// Ordered FIFO can make a healthy Fixed source appear permanently slow:
    /// one generated image plus the original holds the next application
    /// present, so a temporary overlay/menu cadence collapse feeds back into
    /// every following frame without producing an acquire or QueuePresent
    /// timeout. Qualify a healthy target first, then use a short, history-only
    /// native probe only after a sustained severe collapse. A true workload
    /// slowdown rejects on the first probe sample and requires materially new
    /// cadence evidence before another probe, in addition to bounded backoff;
    /// a faster exposed cadence must survive both three native samples and a
    /// generated-delivery verification window before the failure count clears.
    class FixedCadenceCollapseRecovery {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;
        using Duration = Clock::duration;

        struct Decision {
            bool suppressGeneration{false};
            bool probeStarted{false};
            bool probeRejected{false};
            bool probeRecovered{false};
            bool recoveryUnstable{false};
            bool recoveryVerified{false};
            size_t confirmedSamples{0};
            size_t consecutiveFailures{0};
            double baselineBaseFps{0.0};
            double observedBaseFps{0.0};
            Duration retryDelay{};
        };

        [[nodiscard]] static constexpr auto healthyQualificationDuration() {
            return std::chrono::seconds{1};
        }

        [[nodiscard]] static constexpr auto collapseQualificationDuration() {
            return std::chrono::milliseconds{250};
        }

        [[nodiscard]] static constexpr auto verificationDuration() {
            return std::chrono::seconds{1};
        }

        [[nodiscard]] static constexpr double healthyOutputRatio() {
            return 0.95;
        }

        [[nodiscard]] static constexpr double collapsedOutputRatio() {
            return 0.88;
        }

        [[nodiscard]] static constexpr double collapsedBaselineRatio() {
            return 0.90;
        }

        [[nodiscard]] static constexpr double minimumProbeRiseRatio() {
            return 1.25;
        }

        [[nodiscard]] Decision observe(const TimePoint now,
                const std::optional<Duration> realInterval,
                const std::optional<uint32_t> refreshHz,
                const size_t maximumGeneratedFrames) {
            if (!refreshHz || *refreshHz == 0 ||
                    maximumGeneratedFrames == 0) {
                this->reset();
                return {};
            }

            const auto instantaneousBaseFps = baseFps(realInterval);
            if (!instantaneousBaseFps) {
                this->clearQualification();
                return {};
            }

            if (this->probeActive) {
                return this->advanceProbe(now, *instantaneousBaseFps);
            }

            this->updateSmoothedBaseFps(*instantaneousBaseFps);
            const double targetFps = static_cast<double>(*refreshHz);
            const double possibleOutputFps = this->smoothedBaseFps *
                static_cast<double>(maximumGeneratedFrames + 1);
            const bool targetHealthy = possibleOutputFps >=
                targetFps * healthyOutputRatio();

            if (this->verificationUntil) {
                const bool collapsedAgain = possibleOutputFps <
                        targetFps * collapsedOutputRatio() &&
                    this->smoothedBaseFps < this->healthyBaseFps *
                        collapsedBaselineRatio();
                if (collapsedAgain) {
                    const double baselineBaseFps = this->probeBaselineBaseFps;
                    this->verificationUntil.reset();
                    this->recordFailure(now);
                    return {
                        .recoveryUnstable = true,
                        .consecutiveFailures = this->consecutiveFailures,
                        .baselineBaseFps = baselineBaseFps,
                        .observedBaseFps = this->smoothedBaseFps,
                        .retryDelay = *this->retryAt - now,
                    };
                }
                if (now >= *this->verificationUntil) {
                    this->verificationUntil.reset();
                    this->consecutiveFailures = 0;
                    this->retryAt.reset();
                    this->probeBaselineBaseFps = 0.0;
                    return {
                        .recoveryVerified = true,
                        .observedBaseFps = this->smoothedBaseFps,
                    };
                }
                return {};
            }

            if (targetHealthy) {
                this->collapseSince.reset();
                if (!this->healthySince)
                    this->healthySince = now;
                if (now - *this->healthySince >=
                        healthyQualificationDuration()) {
                    if (this->healthyBaseFps == 0.0) {
                        this->healthyBaseFps = this->smoothedBaseFps;
                    } else {
                        this->healthyBaseFps =
                            this->healthyBaseFps * 0.9 +
                            this->smoothedBaseFps * 0.1;
                    }
                    this->retryAt.reset();
                    this->consecutiveFailures = 0;
                    this->rejectedProbeBaseFps = 0.0;
                }
                return {};
            }
            this->healthySince.reset();

            if (this->healthyBaseFps <= 0.0 ||
                    possibleOutputFps >=
                        targetFps * collapsedOutputRatio() ||
                    this->smoothedBaseFps >= this->healthyBaseFps *
                        collapsedBaselineRatio()) {
                this->collapseSince.reset();
                return {};
            }
            if (this->retryAt && now < *this->retryAt)
                return {};
            if (this->rejectedProbeBaseFps > 0.0 &&
                    this->smoothedBaseFps > this->rejectedProbeBaseFps /
                        minimumProbeRiseRatio() &&
                    this->smoothedBaseFps < this->rejectedProbeBaseFps *
                        minimumProbeRiseRatio()) {
                // The native sample already disproved a hidden faster rate.
                // A retry timer alone is not fresh collapse evidence. Keep
                // generation steady until cadence moves outside that band
                // for the normal qualification window or becomes healthy.
                this->collapseSince.reset();
                return {};
            }
            if (!this->collapseSince) {
                this->collapseSince = now;
                return {};
            }
            if (now - *this->collapseSince <
                    collapseQualificationDuration()) {
                return {};
            }

            this->probeActive = true;
            this->probeBaselineBaseFps = this->smoothedBaseFps;
            this->minimumProbeBaseFps = 0.0;
            this->probeConfirmedSamples = 0;
            this->collapseSince.reset();
            return {
                .suppressGeneration = true,
                .probeStarted = true,
                .consecutiveFailures = this->consecutiveFailures,
                .baselineBaseFps = this->probeBaselineBaseFps,
                .observedBaseFps = this->smoothedBaseFps,
            };
        }

        void reset() {
            this->smoothedBaseFps = 0.0;
            this->healthyBaseFps = 0.0;
            this->probeBaselineBaseFps = 0.0;
            this->rejectedProbeBaseFps = 0.0;
            this->minimumProbeBaseFps = 0.0;
            this->healthySince.reset();
            this->collapseSince.reset();
            this->retryAt.reset();
            this->verificationUntil.reset();
            this->probeActive = false;
            this->probeConfirmedSamples = 0;
            this->consecutiveFailures = 0;
        }

        [[nodiscard]] bool active() const {
            return this->probeActive || this->verificationUntil.has_value();
        }

    private:
        [[nodiscard]] static std::optional<double> baseFps(
                const std::optional<Duration> interval) {
            if (!interval)
                return std::nullopt;
            const double seconds = std::chrono::duration<double>(
                *interval
            ).count();
            if (!std::isfinite(seconds) || seconds <= 0.0 || seconds > 0.25)
                return std::nullopt;
            return 1.0 / seconds;
        }

        void updateSmoothedBaseFps(const double instantaneousBaseFps) {
            if (this->smoothedBaseFps == 0.0) {
                this->smoothedBaseFps = instantaneousBaseFps;
            } else {
                this->smoothedBaseFps = this->smoothedBaseFps * 0.75 +
                    instantaneousBaseFps * 0.25;
            }
        }

        [[nodiscard]] Decision advanceProbe(const TimePoint now,
                const double instantaneousBaseFps) {
            const bool fasterCadence = instantaneousBaseFps >=
                this->probeBaselineBaseFps * minimumProbeRiseRatio();
            if (!fasterCadence) {
                const double baselineBaseFps = this->probeBaselineBaseFps;
                this->rejectedProbeBaseFps = baselineBaseFps;
                this->probeActive = false;
                this->minimumProbeBaseFps = 0.0;
                this->probeConfirmedSamples = 0;
                this->recordFailure(now);
                return {
                    .probeRejected = true,
                    .consecutiveFailures = this->consecutiveFailures,
                    .baselineBaseFps = baselineBaseFps,
                    .observedBaseFps = instantaneousBaseFps,
                    .retryDelay = *this->retryAt - now,
                };
            }

            this->probeConfirmedSamples++;
            this->minimumProbeBaseFps = this->minimumProbeBaseFps == 0.0
                ? instantaneousBaseFps
                : std::min(
                    this->minimumProbeBaseFps, instantaneousBaseFps
                );
            if (this->probeConfirmedSamples < confirmationFrames) {
                return {
                    .suppressGeneration = true,
                    .confirmedSamples = this->probeConfirmedSamples,
                    .consecutiveFailures = this->consecutiveFailures,
                    .baselineBaseFps = this->probeBaselineBaseFps,
                    .observedBaseFps = instantaneousBaseFps,
                };
            }

            const double recoveredBaseFps = this->minimumProbeBaseFps;
            this->rejectedProbeBaseFps = 0.0;
            this->probeActive = false;
            this->probeConfirmedSamples = 0;
            this->minimumProbeBaseFps = 0.0;
            this->smoothedBaseFps = recoveredBaseFps;
            this->healthyBaseFps = std::max(
                this->healthyBaseFps, recoveredBaseFps
            );
            this->healthySince = now;
            this->verificationUntil = now + verificationDuration();
            return {
                .suppressGeneration = true,
                .probeRecovered = true,
                .confirmedSamples = confirmationFrames,
                .consecutiveFailures = this->consecutiveFailures,
                .baselineBaseFps = this->probeBaselineBaseFps,
                .observedBaseFps = recoveredBaseFps,
            };
        }

        void recordFailure(const TimePoint now) {
            this->consecutiveFailures++;
            this->retryAt = now + retryDelayForFailure(
                this->consecutiveFailures
            );
            this->collapseSince.reset();
            this->healthySince.reset();
        }

        [[nodiscard]] static Duration retryDelayForFailure(
                const size_t failures) {
            constexpr std::array delays{
                std::chrono::seconds{2},
                std::chrono::seconds{5},
                std::chrono::seconds{15},
                std::chrono::seconds{30},
            };
            return delays.at(std::min(failures, delays.size()) - 1);
        }

        void clearQualification() {
            this->smoothedBaseFps = 0.0;
            this->healthySince.reset();
            this->collapseSince.reset();
            if (this->probeActive) {
                this->probeActive = false;
                this->minimumProbeBaseFps = 0.0;
                this->probeConfirmedSamples = 0;
            }
        }

        static constexpr size_t confirmationFrames = 3;

        double smoothedBaseFps{0.0};
        double healthyBaseFps{0.0};
        double probeBaselineBaseFps{0.0};
        double rejectedProbeBaseFps{0.0};
        double minimumProbeBaseFps{0.0};
        std::optional<TimePoint> healthySince;
        std::optional<TimePoint> collapseSince;
        std::optional<TimePoint> retryAt;
        std::optional<TimePoint> verificationUntil;
        bool probeActive{false};
        size_t probeConfirmedSamples{0};
        size_t consecutiveFailures{0};
    };

    /// Normally suppress synthetic frames that exceed confirmed refresh.
    /// Fixed Smooth Cadence can instead request the full multiplier and let
    /// ordered FIFO back-pressure presents without a timed CPU sleep.
    class FixedRefreshBudget {
    public:
        using TimePoint = std::chrono::steady_clock::time_point;

        [[nodiscard]] size_t plan(const TimePoint now,
                const std::optional<uint32_t> refreshHz,
                const size_t maximumGeneratedFrames,
                const bool fifoPacedFullCadence = false) {
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
            // allow ordered FIFO to back-pressure a full Fixed multiplier.
            return fifoPacedFullCadence ? maximumGeneratedFrames : generated;
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
