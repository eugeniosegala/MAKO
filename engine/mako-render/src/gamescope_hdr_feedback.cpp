/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "gamescope_hdr_feedback.hpp"
#include "mako-common/configuration/config.hpp"
#include <atomic>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <optional>
#include <sstream>
#include <iostream>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <dlfcn.h>
#endif

using namespace mako::layer;

namespace {
    bool gamescopeHdrFeedbackMayChange() {
        const char* display = std::getenv("DISPLAY");
        return display && *display;
    }

    constexpr char gamescopePidProperty[] = "GAMESCOPE_PID";
    constexpr char gamescopeServerIdProperty[] = "GAMESCOPE_XWAYLAND_SERVER_ID";
    constexpr char gamescopeHdrProperty[] =
        "GAMESCOPE_COLOR_APP_WANTS_HDR_FEEDBACK";
    constexpr char gamescopeHdrMetadataProperty[] =
        "GAMESCOPE_COLOR_APP_HDR_METADATA_FEEDBACK";
    constexpr char gamescopeHdrOutputProperty[] =
        "GAMESCOPE_HDR_OUTPUT_FEEDBACK";
    constexpr char gamescopeDisplayHdrEnabledProperty[] =
        "GAMESCOPE_DISPLAY_HDR_ENABLED";
    constexpr char gamescopeSdrOnHdrBrightnessProperty[] =
        "GAMESCOPE_SDR_ON_HDR_CONTENT_BRIGHTNESS";
    constexpr char gamescopeRefreshProperty[] =
        "GAMESCOPE_DISPLAY_REFRESH_RATE_FEEDBACK";
    constexpr char gamescopeVrrEnabledProperty[] = "GAMESCOPE_VRR_ENABLED";
    constexpr char gamescopeVrrCapableProperty[] = "GAMESCOPE_VRR_CAPABLE";
    constexpr char gamescopeVrrActiveProperty[] = "GAMESCOPE_VRR_FEEDBACK";
    constexpr char gamescopeAllowTearingProperty[] =
        "GAMESCOPE_ALLOW_TEARING";

}

bool mako::layer::gamescopeProcessEnvironmentHint() {
    const char* gamescopeDisplay = std::getenv("GAMESCOPE_WAYLAND_DISPLAY");
    if (gamescopeDisplay && *gamescopeDisplay)
        return true;
    return environmentFlagEnabled(std::getenv("ENABLE_GAMESCOPE_WSI")) ||
        environmentFlagEnabled(std::getenv("STEAM_GAMESCOPE_HDR_SUPPORTED"));
}

struct GamescopeHdrFeedbackReader::Impl {
    explicit Impl(
            const PresentationEnvironmentPolicy& presentationEnvironment) :
        presentationEnvironment(presentationEnvironment) {}

    const PresentationEnvironmentPolicy presentationEnvironment;
    std::mutex sampleMutex;
    GamescopeHdrFeedbackSample latestSample;
    std::jthread monitor;
    std::mutex monitorWaitMutex;
    std::condition_variable monitorWake;
    GamescopeFocusTracker focusTracker;
    std::optional<uint32_t> focusGamescopePid;
    std::atomic<bool> sdrBrightnessBoostRequested{false};
    std::atomic<uint32_t> sdrBrightnessBoostTargetNits{
        ls::GameConfDefaults::gamescopeHdrBrightnessNits
    };
    std::atomic<uint64_t> controlRevision{0};
    bool lastBrightnessBoostRequested{false};
    uint32_t lastBrightnessTargetNits{
        ls::GameConfDefaults::gamescopeHdrBrightnessNits
    };
    bool brightnessOriginalCaptured{false};
    std::optional<uint32_t> originalBrightnessRaw;
    std::optional<uint32_t> writtenBrightnessRaw;
    bool brightnessExternallyOverridden{false};
    std::string lastBrightnessStatus;
    const std::optional<uint32_t> applicationId = [] {
        const char* value = std::getenv("SteamAppId");
        if (!value || !*value)
            value = std::getenv("STEAM_COMPAT_APP_ID");
        return value ? gamescopeApplicationId(value) : std::nullopt;
    }();

#if defined(__linux__)
    void* library{nullptr};
    Display* display{nullptr};
    Atom feedbackAtom{None};
    Window root{None};
    std::string selectedDisplayName;
    std::string resolverStatus{"not-attempted"};
    std::string resolverCandidates;
    std::optional<uint32_t> observedGamescopePid;
    std::optional<uint32_t> observedServerId;
    std::chrono::steady_clock::time_point nextDiscoveryAttempt{};
    bool symbolsResolved{false};
    decltype(&XOpenDisplay) openDisplay{nullptr};
    decltype(&XCloseDisplay) closeDisplay{nullptr};
    decltype(&XInternAtom) internAtom{nullptr};
    decltype(&XDefaultRootWindow) defaultRootWindow{nullptr};
    decltype(&XGetWindowAttributes) getWindowAttributes{nullptr};
    decltype(&XGetWindowProperty) getWindowProperty{nullptr};
    decltype(&XChangeProperty) changeProperty{nullptr};
    decltype(&XDeleteProperty) deleteProperty{nullptr};
    decltype(&XFlush) flush{nullptr};
    decltype(&XFree) freeData{nullptr};

    template<typename Function>
    bool resolve(Function& function, const char* name) {
        function = reinterpret_cast<Function>(dlsym(this->library, name));
        return function != nullptr;
    }

    std::optional<uint32_t> readCardinal(Display* sourceDisplay,
            const Window sourceRoot, const char* propertyName) {
        const Atom property = this->internAtom(
            sourceDisplay, propertyName, True
        );
        if (property == None)
            return std::nullopt;

        Atom actualType{None};
        int actualFormat{};
        unsigned long itemCount{};
        unsigned long bytesAfter{};
        unsigned char* data{nullptr};
        const int result = this->getWindowProperty(
            sourceDisplay, sourceRoot, property, 0, 1, False, XA_CARDINAL,
            &actualType, &actualFormat, &itemCount, &bytesAfter, &data
        );
        std::optional<uint32_t> value;
        if (result == Success && actualType == XA_CARDINAL &&
                actualFormat == 32 && itemCount == 1 && bytesAfter == 0 && data) {
            value = static_cast<uint32_t>(
                *reinterpret_cast<const unsigned long*>(data)
            );
        }
        if (data)
            this->freeData(data);
        return value;
    }

    bool hasCardinalData(Display* sourceDisplay,
            const Window sourceRoot, const char* propertyName) {
        const Atom property = this->internAtom(
            sourceDisplay, propertyName, True
        );
        if (property == None)
            return false;

        Atom actualType{None};
        int actualFormat{};
        unsigned long itemCount{};
        unsigned long bytesAfter{};
        unsigned char* data{nullptr};
        const int result = this->getWindowProperty(
            sourceDisplay, sourceRoot, property, 0, 64, False, XA_CARDINAL,
            &actualType, &actualFormat, &itemCount, &bytesAfter, &data
        );
        const bool present = result == Success &&
            actualType == XA_CARDINAL && actualFormat == 32 &&
            itemCount > 0 && data;
        if (data)
            this->freeData(data);
        return present;
    }

    bool writeCardinal(const char* propertyName, const uint32_t value) {
        const Atom property = this->internAtom(
            this->display, propertyName, False
        );
        if (property == None)
            return false;
        const unsigned long wireValue = value;
        this->changeProperty(
            this->display, this->root, property, XA_CARDINAL, 32,
            PropModeReplace,
            reinterpret_cast<const unsigned char*>(&wireValue), 1
        );
        this->flush(this->display);
        return true;
    }

    void clearBrightnessOwnership() {
        this->brightnessOriginalCaptured = false;
        this->originalBrightnessRaw.reset();
        this->writtenBrightnessRaw.reset();
    }

    void restoreSdrBrightness() {
        if (!this->brightnessOriginalCaptured || !this->display ||
                this->root == None) {
            this->clearBrightnessOwnership();
            return;
        }

        const auto current = this->readCardinal(
            this->display, this->root,
            gamescopeSdrOnHdrBrightnessProperty
        );
        if (current == this->writtenBrightnessRaw) {
            if (this->originalBrightnessRaw) {
                static_cast<void>(this->writeCardinal(
                    gamescopeSdrOnHdrBrightnessProperty,
                    *this->originalBrightnessRaw
                ));
            } else {
                const Atom property = this->internAtom(
                    this->display,
                    gamescopeSdrOnHdrBrightnessProperty,
                    True
                );
                if (property != None) {
                    this->deleteProperty(this->display, this->root, property);
                    this->flush(this->display);
                }
            }
        }
        this->clearBrightnessOwnership();
    }

    std::string updateSdrBrightnessBoost(
            const GamescopeSdrBrightnessBoostDecision decision) {
        const bool requested = this->sdrBrightnessBoostRequested.load(
            std::memory_order_acquire
        );
        const uint32_t targetNits = this->sdrBrightnessBoostTargetNits.load(
            std::memory_order_acquire
        );
        if (requested != this->lastBrightnessBoostRequested) {
            this->restoreSdrBrightness();
            this->brightnessExternallyOverridden = false;
            this->lastBrightnessBoostRequested = requested;
        }
        if (targetNits != this->lastBrightnessTargetNits) {
            this->brightnessExternallyOverridden = false;
            this->lastBrightnessTargetNits = targetNits;
        }

        if (!decision.apply) {
            this->restoreSdrBrightness();
            return std::string(decision.status);
        }
        if (this->brightnessExternallyOverridden)
            return "externally-overridden";

        const uint32_t requestedRaw = std::bit_cast<uint32_t>(
            static_cast<float>(targetNits)
        );
        if (this->writtenBrightnessRaw) {
            const auto current = this->readCardinal(
                this->display, this->root,
                gamescopeSdrOnHdrBrightnessProperty
            );
            if (current != this->writtenBrightnessRaw) {
                this->clearBrightnessOwnership();
                this->brightnessExternallyOverridden = true;
                return "externally-overridden";
            }
            if (*this->writtenBrightnessRaw == requestedRaw)
                return "applied";
        }

        if (!this->brightnessOriginalCaptured) {
            this->originalBrightnessRaw = this->readCardinal(
                this->display, this->root,
                gamescopeSdrOnHdrBrightnessProperty
            );
            this->brightnessOriginalCaptured = true;
        }
        if (!this->writeCardinal(
                gamescopeSdrOnHdrBrightnessProperty, requestedRaw)) {
            this->clearBrightnessOwnership();
            return "property-write-failed";
        }
        this->writtenBrightnessRaw = requestedRaw;
        return "applied";
    }

    GamescopeXwaylandDisplay identifyDisplay(
            const std::string& name, Display* candidateDisplay) {
        const Window candidateRoot = this->defaultRootWindow(candidateDisplay);
        return {
            .display = name,
            .gamescopePid = this->readCardinal(
                candidateDisplay, candidateRoot, gamescopePidProperty
            ),
            .serverId = this->readCardinal(
                candidateDisplay, candidateRoot, gamescopeServerIdProperty
            ),
        };
    }

    std::vector<std::string> localDisplayCandidates() {
        std::vector<std::string> candidates;
        std::error_code error;
        const std::filesystem::path socketDirectory{"/tmp/.X11-unix"};
        for (std::filesystem::directory_iterator it(socketDirectory, error), end;
                !error && it != end; it.increment(error)) {
            const std::string filename = it->path().filename().string();
            if (filename.size() <= 1 || filename.front() != 'X' ||
                    !std::ranges::all_of(filename.substr(1),
                        [](const char value) { return value >= '0' && value <= '9'; }))
                continue;
            candidates.emplace_back(":" + filename.substr(1));
        }
        std::ranges::sort(candidates);
        candidates.erase(std::unique(candidates.begin(), candidates.end()),
            candidates.end());
        return candidates;
    }

    std::vector<std::string> displayProbeCandidates(
            const std::string& currentName) {
        auto candidates = this->localDisplayCandidates();

        // Pressure Vessel may expose an Xwayland socket through the abstract
        // namespace without mirroring every sibling in /tmp/.X11-unix. Probe
        // the small range Gamescope normally allocates as well as the sockets
        // visible in the filesystem. XOpenDisplay still performs all normal
        // Xauthority checks, so an unrelated display cannot be selected unless
        // its Gamescope PID also matches the game's current display.
        constexpr uint32_t maximumProbeDisplay = 15;
        for (uint32_t index = 0; index <= maximumProbeDisplay; index++)
            candidates.emplace_back(":" + std::to_string(index));
        candidates.push_back(currentName);

        std::ranges::sort(candidates);
        candidates.erase(std::unique(candidates.begin(), candidates.end()),
            candidates.end());
        return candidates;
    }

    std::string describeCandidates(
            const std::vector<GamescopeXwaylandDisplay>& candidates) {
        std::ostringstream description;
        bool first = true;
        for (const auto& candidate : candidates) {
            if (!first)
                description << ',';
            first = false;
            description << candidate.display << "(pid=";
            if (candidate.gamescopePid)
                description << *candidate.gamescopePid;
            else
                description << "unset";
            description << ",server=";
            if (candidate.serverId)
                description << *candidate.serverId;
            else
                description << "unset";
            description << ')';
        }
        return first ? "none" : description.str();
    }

    void closeSelectedDisplay() {
        this->restoreSdrBrightness();
        if (this->display && this->closeDisplay)
            this->closeDisplay(this->display);
        this->display = nullptr;
        this->root = None;
        this->feedbackAtom = None;
        this->selectedDisplayName.clear();
    }

    bool initialize() {
        if (this->display)
            return this->root != None;

        const auto now = std::chrono::steady_clock::now();
        if (now < this->nextDiscoveryAttempt)
            return false;
        constexpr auto discoveryRetryInterval = std::chrono::seconds(1);
        this->nextDiscoveryAttempt = now + discoveryRetryInterval;
        this->observedGamescopePid.reset();
        this->observedServerId.reset();
        this->resolverCandidates.clear();

        if (!this->library)
            this->library = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
        if (!this->library) {
            this->resolverStatus = "x11-library-unavailable";
            return false;
        }

        if (!this->symbolsResolved) {
            const bool resolved =
                this->resolve(this->openDisplay, "XOpenDisplay") &&
                this->resolve(this->closeDisplay, "XCloseDisplay") &&
                this->resolve(this->internAtom, "XInternAtom") &&
                this->resolve(
                    this->defaultRootWindow, "XDefaultRootWindow"
                ) &&
                this->resolve(
                    this->getWindowAttributes, "XGetWindowAttributes"
                ) &&
                this->resolve(
                    this->getWindowProperty, "XGetWindowProperty"
                ) &&
                this->resolve(this->changeProperty, "XChangeProperty") &&
                this->resolve(this->deleteProperty, "XDeleteProperty") &&
                this->resolve(this->flush, "XFlush") &&
                this->resolve(this->freeData, "XFree");
            if (!resolved) {
                this->resolverStatus = "x11-symbol-resolution-failed";
                return false;
            }
            this->symbolsResolved = true;
        }

        Display* currentDisplay = this->openDisplay(nullptr);
        if (!currentDisplay) {
            this->resolverStatus = "current-display-open-failed";
            return false;
        }

        const char* currentNameValue = std::getenv("DISPLAY");
        const std::string currentName = currentNameValue ? currentNameValue : "";
        const auto currentIdentity = this->identifyDisplay(
            currentName, currentDisplay
        );
        this->observedGamescopePid = currentIdentity.gamescopePid;
        this->observedServerId = currentIdentity.serverId;
        const bool currentIsGamescope = currentIdentity.gamescopePid &&
            currentIdentity.serverId;
        const bool currentNeedsRoot = currentIsGamescope &&
            *currentIdentity.serverId != 0;
        const bool needsCandidateProbe = currentNeedsRoot ||
            (!currentIsGamescope && gamescopeProcessEnvironmentHint());

        std::vector<GamescopeXwaylandDisplay> candidateIdentities;
        std::vector<std::pair<std::string, Display*>> candidateConnections;
        if (needsCandidateProbe) {
            for (const auto& candidateName :
                    this->displayProbeCandidates(currentName)) {
                if (candidateName == currentName)
                    continue;
                Display* candidateDisplay =
                    this->openDisplay(candidateName.c_str());
                if (!candidateDisplay)
                    continue;
                candidateIdentities.push_back(this->identifyDisplay(
                    candidateName, candidateDisplay
                ));
                candidateConnections.emplace_back(
                    candidateName, candidateDisplay
                );
            }
        }
        this->resolverCandidates = this->describeCandidates(
            candidateIdentities
        );

        const auto rootDisplayName = selectGamescopeRootDisplay(
            currentIdentity, candidateIdentities
        );
        if (rootDisplayName && *rootDisplayName != currentName) {
            const auto connection = std::ranges::find_if(candidateConnections,
                [&rootDisplayName](const auto& value) {
                    return value.first == *rootDisplayName;
                });
            if (connection != candidateConnections.end()) {
                this->display = connection->second;
                connection->second = nullptr;
                this->closeDisplay(currentDisplay);
                currentDisplay = nullptr;
            }
        }

        if (!this->display && (currentNeedsRoot ||
                (!currentIsGamescope &&
                    gamescopeProcessEnvironmentHint()))) {
            this->resolverStatus = currentNeedsRoot
                ? "gamescope-root-display-unresolved"
                : "gamescope-current-identity-unavailable";
            if (currentDisplay)
                this->closeDisplay(currentDisplay);
            for (const auto& [name, connection] : candidateConnections) {
                static_cast<void>(name);
                if (connection)
                    this->closeDisplay(connection);
            }
            return false;
        }

        if (!this->display) {
            this->display = currentDisplay;
            currentDisplay = nullptr;
        }
        for (const auto& [name, connection] : candidateConnections) {
            static_cast<void>(name);
            if (connection)
                this->closeDisplay(connection);
        }

        this->root = this->defaultRootWindow(this->display);
        if (this->root == None) {
            this->resolverStatus = "x11-root-window-unavailable";
            this->closeSelectedDisplay();
            return false;
        }
        const auto selectedIdentity = this->identifyDisplay(
            rootDisplayName.value_or(currentName), this->display
        );
        this->selectedDisplayName = selectedIdentity.display;
        this->observedGamescopePid = selectedIdentity.gamescopePid;
        this->observedServerId = selectedIdentity.serverId;
        if (selectedIdentity.serverId && *selectedIdentity.serverId == 0) {
            this->resolverStatus = rootDisplayName &&
                    *rootDisplayName != currentName
                ? "gamescope-root-display-resolved"
                : "gamescope-root-display-current";
        } else {
            this->resolverStatus = "current-display-selected";
        }
        this->feedbackAtom = this->internAtom(
            this->display, gamescopeHdrProperty, True
        );
        return this->root != None;
    }

    GamescopeHdrFeedbackSample sampleOnce() {
        const char* displayName = std::getenv("DISPLAY");
        GamescopeHdrFeedbackSample sample{.display = displayName ? displayName : ""};
        sample.sdrBrightnessBoostRequested =
            this->sdrBrightnessBoostRequested.load(std::memory_order_acquire);
        if (!displayName || !*displayName) {
            sample.status = "display-environment-missing";
            sample.sdrBrightnessBoostStatus =
                sample.sdrBrightnessBoostRequested
                    ? "display-environment-missing" : "off";
            return sample;
        }

        // DXVK_HDR is an exposure/capability signal, not an active-HDR
        // signal. A false capability does conclusively mean the game is SDR,
        // but still resolve Gamescope first: its identity, refresh budget and
        // WSI ownership remain relevant to SDR presentation.
        if (!this->initialize()) {
            sample.gamescopePid = this->observedGamescopePid;
            sample.xwaylandServerId = this->observedServerId;
            sample.gamescopeDetected = sample.gamescopePid.has_value() &&
                sample.xwaylandServerId.has_value();
            sample.status = this->resolverStatus;
            sample.resolverStatus = this->resolverStatus;
            sample.resolverCandidates = this->resolverCandidates;
            sample.sdrBrightnessBoostStatus =
                decideGamescopeSdrBrightnessBoost(
                    sample.sdrBrightnessBoostRequested,
                    sample.gamescopeDetected,
                    sample.xwaylandServerId,
                    std::nullopt
                ).status;
            return sample;
        }

        sample.display = this->selectedDisplayName;
        sample.resolverStatus = this->resolverStatus;
        sample.resolverCandidates = this->resolverCandidates;
        sample.gamescopePid = this->readCardinal(
            this->display, this->root, gamescopePidProperty
        );
        sample.xwaylandServerId = this->readCardinal(
            this->display, this->root, gamescopeServerIdProperty
        );
        sample.gamescopeDetected = sample.gamescopePid.has_value() &&
            sample.xwaylandServerId.has_value();

        // Do not remain attached to a stale or non-root Gamescope server. The
        // background monitor owns this X11 connection, so dropping it here is
        // independent of Vulkan presentation and the next bounded discovery
        // attempt is safe.
        if (sample.gamescopeDetected && *sample.xwaylandServerId != 0) {
            sample.status = "gamescope-selected-display-not-root";
            sample.resolverStatus = sample.status;
            sample.sdrBrightnessBoostStatus =
                decideGamescopeSdrBrightnessBoost(
                    sample.sdrBrightnessBoostRequested,
                    sample.gamescopeDetected,
                    sample.xwaylandServerId,
                    std::nullopt
                ).status;
            this->resolverStatus = sample.status;
            this->closeSelectedDisplay();
            return sample;
        }
        if (sample.gamescopeDetected) {
            XWindowAttributes attributes{};
            if (this->getWindowAttributes(
                    this->display, this->root, &attributes) != 0 &&
                    attributes.width > 0 && attributes.height > 0) {
                sample.outputWidth = static_cast<uint32_t>(attributes.width);
                sample.outputHeight = static_cast<uint32_t>(attributes.height);
            }
        }
        sample.refreshHz = this->readCardinal(
            this->display, this->root, gamescopeRefreshProperty
        );
        if (sample.gamescopeDetected && sample.xwaylandServerId == 0) {
            sample.presentation.vrrEnabled = gamescopeBooleanFeedback(
                this->readCardinal(
                    this->display, this->root, gamescopeVrrEnabledProperty
                )
            );
            sample.presentation.vrrCapable = gamescopeBooleanFeedback(
                this->readCardinal(
                    this->display, this->root, gamescopeVrrCapableProperty
                )
            );
            sample.presentation.vrrActive = gamescopeBooleanFeedback(
                this->readCardinal(
                    this->display, this->root, gamescopeVrrActiveProperty
                )
            );
            sample.presentation.allowTearing = gamescopeBooleanFeedback(
                this->readCardinal(
                    this->display, this->root,
                    gamescopeAllowTearingProperty
                )
            );
        }
        if (const auto outputHdr = this->readCardinal(
                this->display, this->root, gamescopeHdrOutputProperty)) {
            sample.outputHdrEnabled = *outputHdr != 0;
        }
        sample.displayHdrEnabled = gamescopeBooleanFeedback(
            this->readCardinal(
                this->display, this->root,
                gamescopeDisplayHdrEnabledProperty
            )
        );
        const auto brightnessDecision = decideGamescopeSdrBrightnessBoost(
            sample.sdrBrightnessBoostRequested,
            sample.gamescopeDetected,
            sample.xwaylandServerId,
            sample.displayHdrEnabled
        );
        sample.sdrBrightnessBoostStatus = this->updateSdrBrightnessBoost(
            brightnessDecision
        );
        if (sample.sdrBrightnessBoostStatus == "applied")
            sample.sdrBrightnessBoostNits =
                this->sdrBrightnessBoostTargetNits.load(
                    std::memory_order_acquire
                );
        sample.appHdrMetadataPresent = this->hasCardinalData(
            this->display, this->root, gamescopeHdrMetadataProperty
        );
        if (sample.gamescopeDetected && sample.xwaylandServerId == 0 &&
                this->applicationId) {
            const auto graphics = this->readCardinal(
                this->display, this->root, "GAMESCOPE_FOCUSED_APP_GFX");
            const auto input = this->readCardinal(
                this->display, this->root, "GAMESCOPE_FOCUSED_APP");
            // The properties are published separately. Reject a graphics
            // focus change during the read; debounce the coherent pair too.
            const auto graphicsAfter = this->readCardinal(
                this->display, this->root, "GAMESCOPE_FOCUSED_APP_GFX");
            if (graphics == graphicsAfter)
                sample.focus.gameFocused = classifyGamescopeFocus(
                    this->applicationId, input, graphics);
        }
        if (this->presentationEnvironment.hdrExposureDisabled) {
            const auto decision = decideGamescopeHdrActivation({
                .outputHdrEnabled = sample.outputHdrEnabled,
                .appHdrMetadataPresent = sample.appHdrMetadataPresent,
                .hdrExposureDisabled = true,
                .gamescopeDetected = sample.gamescopeDetected,
            });
            sample.active = decision.active;
            sample.activationSource = decision.source;
            sample.status = "hdr-exposure-disabled";
            return sample;
        }

        std::optional<bool> appWantsHdr;
        if (this->feedbackAtom == None) {
            this->feedbackAtom = this->internAtom(
                this->display,
                gamescopeHdrProperty,
                True
            );
            if (this->feedbackAtom == None) {
                sample.status = "feedback-atom-unavailable";
            }
        }

        if (this->feedbackAtom != None) {
            Atom actualType{None};
            int actualFormat{};
            unsigned long itemCount{};
            unsigned long bytesAfter{};
            unsigned char* data{nullptr};
            const int result = this->getWindowProperty(
                this->display,
                this->root,
                this->feedbackAtom,
                0,
                1,
                False,
                XA_CARDINAL,
                &actualType,
                &actualFormat,
                &itemCount,
                &bytesAfter,
                &data
            );

            if (result == Success && actualType == XA_CARDINAL &&
                    actualFormat == 32 && itemCount == 1 && data) {
                const auto raw = *reinterpret_cast<const unsigned long*>(data);
                appWantsHdr = raw != 0;
                sample.status = "confirmed";
            } else if (result != Success) {
                sample.status = "property-read-failed";
            } else if (actualType == None || itemCount == 0) {
                sample.status = "feedback-property-unset";
            } else {
                sample.status = "feedback-property-invalid";
            }
            if (data)
                this->freeData(data);
        }

        const auto decision = decideGamescopeHdrActivation({
            .appWantsHdr = appWantsHdr,
            .outputHdrEnabled = sample.outputHdrEnabled,
            .appHdrMetadataPresent = sample.appHdrMetadataPresent,
            .gamescopeDetected = sample.gamescopeDetected,
        });
        sample.active = decision.active;
        sample.activationSource = decision.source;
        return sample;
    }
#else
    GamescopeHdrFeedbackSample sampleOnce() {
        return {
            .status = "unsupported-platform",
        };
    }
#endif

    void refresh() {
        auto sample = this->sampleOnce();
        const auto now = std::chrono::steady_clock::now();
        if (this->focusGamescopePid != sample.gamescopePid) {
            static_cast<void>(this->focusTracker.observe(now, std::nullopt));
            this->focusGamescopePid = sample.gamescopePid;
        }
        sample.focus = this->focusTracker.observe(
            now, sample.focus.gameFocused);
        if (sample.sdrBrightnessBoostStatus != this->lastBrightnessStatus) {
            this->lastBrightnessStatus = sample.sdrBrightnessBoostStatus;
            std::cerr << "MAKO Renderer: Gamescope SDR brightness boost: requested="
                      << sample.sdrBrightnessBoostRequested
                      << "; applied_nits="
                      << sample.sdrBrightnessBoostNits.value_or(0)
                      << "; status="
                      << (sample.sdrBrightnessBoostStatus.empty()
                            ? "unavailable"
                            : sample.sdrBrightnessBoostStatus)
                      << '\n';
        }
        std::scoped_lock lock(this->sampleMutex);
        this->latestSample = sample;
    }

    std::chrono::milliseconds pollInterval() {
        std::scoped_lock lock(this->sampleMutex);
        return gamescopeFeedbackPollInterval(
            gamescopeProcessEnvironmentHint(),
            this->latestSample.gamescopeDetected
        );
    }

    void start() {
        // One synchronous startup read lets the first swapchain use the right
        // colour pipeline. All later X11 round trips stay off the presentation
        // thread.
        this->refresh();
        if (!gamescopeHdrFeedbackMayChange())
            return;

        const uint64_t initialControlRevision = this->controlRevision.load(
            std::memory_order_acquire
        );
        this->monitor = std::jthread([this, initialControlRevision](
                const std::stop_token stop) {
            uint64_t observedControlRevision = initialControlRevision;
            while (!stop.stop_requested()) {
                const auto interval = this->pollInterval();
                std::unique_lock waitLock(this->monitorWaitMutex);
                if (this->monitorWake.wait_for(
                        waitLock, interval,
                        [this, &stop, &observedControlRevision] {
                            return stop.stop_requested() ||
                                this->controlRevision.load(
                                    std::memory_order_acquire
                                ) != observedControlRevision;
                        }) && stop.stop_requested())
                    break;
                observedControlRevision = this->controlRevision.load(
                    std::memory_order_acquire
                );
                waitLock.unlock();
                this->refresh();
            }
        });
    }

    ~Impl() {
        if (this->monitor.joinable()) {
            this->monitor.request_stop();
            this->monitorWake.notify_all();
            this->monitor.join();
        }
#if defined(__linux__)
        this->closeSelectedDisplay();
        if (this->library)
            dlclose(this->library);
#endif
    }
};

GamescopeHdrFeedbackReader::GamescopeHdrFeedbackReader(
        const PresentationEnvironmentPolicy& presentationEnvironment) :
    impl(std::make_unique<Impl>(presentationEnvironment)) {
    this->impl->start();
}

GamescopeHdrFeedbackReader::~GamescopeHdrFeedbackReader() = default;
GamescopeHdrFeedbackReader::GamescopeHdrFeedbackReader(
    GamescopeHdrFeedbackReader&&) noexcept = default;
GamescopeHdrFeedbackReader& GamescopeHdrFeedbackReader::operator=(
    GamescopeHdrFeedbackReader&&) noexcept = default;

std::optional<bool> GamescopeHdrFeedbackReader::sample() const {
    std::scoped_lock lock(this->impl->sampleMutex);
    return this->impl->latestSample.active;
}

GamescopeHdrFeedbackSample
GamescopeHdrFeedbackReader::diagnosticSample() const {
    std::scoped_lock lock(this->impl->sampleMutex);
    return this->impl->latestSample;
}

void GamescopeHdrFeedbackReader::setSdrBrightnessBoost(
        const bool enabled, const uint32_t targetNits) {
    const uint32_t boundedTargetNits = std::clamp(
        targetNits,
        ls::GameConfLimits::minimumGamescopeHdrBrightnessNits,
        ls::GameConfLimits::maximumGamescopeHdrBrightnessNits
    );
    const bool enabledChanged = this->impl->sdrBrightnessBoostRequested.exchange(
        enabled, std::memory_order_acq_rel
    ) != enabled;
    const bool targetChanged = this->impl->sdrBrightnessBoostTargetNits.exchange(
        boundedTargetNits, std::memory_order_acq_rel
    ) != boundedTargetNits;
    if (!enabledChanged && !targetChanged)
        return;
    this->impl->controlRevision.fetch_add(1, std::memory_order_release);
    this->impl->monitorWake.notify_all();
}
