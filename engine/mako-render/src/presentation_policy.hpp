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
    /// a backend failure. Ordered/legacy paths retain their configured ceiling
    /// because their synchronous FIFO contract is intentionally different.
    [[nodiscard]] inline uint64_t generatedImageAcquireTimeout(
            const bool gamescopeHdrTransport,
            const std::optional<uint64_t> configuredTimeout) {
        if (gamescopeHdrTransport)
            return 0;
        return configuredTimeout.value_or(std::numeric_limits<uint64_t>::max());
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
    /// an engine or temporal-history failure. Gamescope admission is always
    /// non-blocking; this state exists only to aggregate diagnostics and report
    /// the eventual recovery.
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

                if (now >= *this->retryAt &&
                        this->nativeCadenceSaturationSince &&
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
                if (!timedOut && !severe && !this->guardPending &&
                        !this->probePending &&
                        observedSlowFrames < slowFrameThreshold) {
                    // One slow successful acquire can still be ordinary FIFO
                    // pressure, but the next application present must not
                    // repeat a blocking acquire or feed that transport delay
                    // back into Adaptive's source-cadence clock. Constrain a
                    // short zero-wait guard first; a miss escalates through
                    // the existing native-drain recovery below.
                    this->guardPending = true;
                    return {
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

            this->consecutiveSlowFrames = 0;
            const bool guardCleared = this->guardPending;
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
    };

    /// Keep the application-present budget cumulative for 3x/4x/5x, while
    /// preventing one unavailable lower image from consuming the full legacy
    /// 50 ms ceiling by itself. On a known-refresh ordered path, one image gets
    /// one-and-a-half display periods with an 8 ms floor; an image that misses
    /// that useful delivery window must enter recovery instead of blocking a
    /// 120 Hz application present for the separate 25 ms pressure threshold.
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
        const uint64_t perImageCeiling = refreshHz && *refreshHz > 0
            ? std::max(
                minimumPerImageTimeout,
                (nanosecondsPerSecond * 3 +
                    static_cast<uint64_t>(*refreshHz) * 2 - 1) /
                    (static_cast<uint64_t>(*refreshHz) * 2)
            )
            : unknownRefreshTimeout;
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
    /// one second before lowering the cap. Validated scheduler limits remain
    /// the authority, so this policy cannot activate a multiplier that has not
    /// passed delivery and throughput checks.
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
            bool rearmRequired{false};
            bool discontinuityRecoveryActive{false};
        };

        [[nodiscard]] Decision update(const TimePoint now,
                const bool eligible, const uint32_t targetFps,
                const SchedulerState scheduler) {
            const auto previousMultiplier = this->activeMultiplier;
            if (!eligible || targetFps == 0 ||
                    scheduler.discontinuityRecoveryActive ||
                    scheduler.rearmRequired || scheduler.rampEvaluationActive) {
                this->resetCandidate();
                this->activeMultiplier.reset();
                return this->decision(previousMultiplier);
            }

            const size_t desiredMultiplier =
                scheduler.validatedGenerationLimit + 1;
            if (desiredMultiplier < 3 || scheduler.smoothedBaseFps <= 0.0) {
                this->resetCandidate();
                this->activeMultiplier.reset();
                return this->decision(previousMultiplier);
            }

            const double desiredCap = static_cast<double>(targetFps) /
                static_cast<double>(desiredMultiplier);
            const double previousRung = static_cast<double>(targetFps) /
                static_cast<double>(desiredMultiplier - 1);
            const bool cadenceNeedsRung =
                scheduler.smoothedBaseFps < previousRung * 0.98;
            const bool rungSustainable =
                scheduler.smoothedBaseFps >= desiredCap * 0.95;
            if (!cadenceNeedsRung || !rungSustainable) {
                this->resetCandidate();
                this->activeMultiplier.reset();
                return this->decision(previousMultiplier);
            }

            if (this->activeMultiplier == desiredMultiplier) {
                this->activeTargetFps = static_cast<double>(targetFps);
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
            this->resetCandidate();
        }

        [[nodiscard]] static constexpr std::chrono::seconds
        qualificationDuration() {
            return std::chrono::seconds{1};
        }

    private:
        [[nodiscard]] Decision decision(
                const std::optional<size_t> previousMultiplier) const {
            if (!this->activeMultiplier) {
                return {
                    .changed = previousMultiplier.has_value(),
                };
            }
            return {
                .framesPerSecond = this->activeTargetFps /
                    static_cast<double>(*this->activeMultiplier),
                .multiplier = *this->activeMultiplier,
                .changed = previousMultiplier != this->activeMultiplier,
            };
        }

        void resetCandidate() {
            this->candidateMultiplier.reset();
            this->candidateSince.reset();
        }

        std::optional<size_t> activeMultiplier;
        std::optional<size_t> candidateMultiplier;
        std::optional<TimePoint> candidateSince;
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

        [[nodiscard]] static Duration stallThreshold(
                const std::optional<uint32_t> refreshHz) {
            constexpr auto minimumThreshold =
                std::chrono::milliseconds{50};
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
            return {
                .quarantined = true,
                .presentDuration = maximumPresentDuration,
                .threshold = threshold,
                .stabilizationDuration = stabilization,
                .consecutiveStalls = this->consecutiveStalls,
            };
        }

        [[nodiscard]] PresentDecision beforePresent(const TimePoint now) {
            if (!this->stabilizingUntil)
                return {};
            if (now < *this->stabilizingUntil) {
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
                .bypassedFrames = this->bypassedFrames,
                .recoveryDuration = this->startedAt
                    ? now - *this->startedAt : Duration{},
            };
            this->startedAt.reset();
            this->stabilizingUntil.reset();
            this->bypassedFrames = 0;
            return decision;
        }

        [[nodiscard]] bool active() const {
            return this->stabilizingUntil.has_value();
        }

        void reset() {
            this->startedAt.reset();
            this->stabilizingUntil.reset();
            this->bypassedFrames = 0;
            this->lastStallAt.reset();
            this->consecutiveStalls = 0;
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
    };

    /// Deterministically suppress synthetic frames which cannot be scanned out
    /// at the confirmed Gamescope refresh rate. Fixed mode remains at its full
    /// multiplier whenever that output fits the display budget.
    class FixedRefreshBudget {
    public:
        using TimePoint = std::chrono::steady_clock::time_point;

        [[nodiscard]] size_t plan(const TimePoint now,
                const std::optional<uint32_t> refreshHz,
                const size_t maximumGeneratedFrames) {
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
            return generated;
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
