/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "gamescope_hdr_feedback.hpp"
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <optional>
#include <sstream>
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
    constexpr char gamescopeRefreshProperty[] =
        "GAMESCOPE_DISPLAY_REFRESH_RATE_FEEDBACK";

    bool runningUnderGamescope() {
        const char* gamescopeDisplay = std::getenv("GAMESCOPE_WAYLAND_DISPLAY");
        if (gamescopeDisplay && *gamescopeDisplay)
            return true;
        return environmentFlagEnabled(std::getenv("ENABLE_GAMESCOPE_WSI")) ||
            environmentFlagEnabled(std::getenv("STEAM_GAMESCOPE_HDR_SUPPORTED"));
    }
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
    decltype(&XGetWindowProperty) getWindowProperty{nullptr};
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
                actualFormat == 32 && itemCount == 1 && data) {
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
                    this->getWindowProperty, "XGetWindowProperty"
                ) &&
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
            (!currentIsGamescope && runningUnderGamescope());

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
                (!currentIsGamescope && runningUnderGamescope()))) {
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
        if (!displayName || !*displayName) {
            sample.status = "display-environment-missing";
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
            this->resolverStatus = sample.status;
            this->closeSelectedDisplay();
            return sample;
        }
        sample.refreshHz = this->readCardinal(
            this->display, this->root, gamescopeRefreshProperty
        );
        if (const auto outputHdr = this->readCardinal(
                this->display, this->root, gamescopeHdrOutputProperty)) {
            sample.outputHdrEnabled = *outputHdr != 0;
        }
        sample.appHdrMetadataPresent = this->hasCardinalData(
            this->display, this->root, gamescopeHdrMetadataProperty
        );
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
        const auto sample = this->sampleOnce();
        std::scoped_lock lock(this->sampleMutex);
        this->latestSample = sample;
    }

    std::chrono::milliseconds pollInterval() {
        std::scoped_lock lock(this->sampleMutex);
        return gamescopeFeedbackPollInterval(
            runningUnderGamescope(), this->latestSample.gamescopeDetected
        );
    }

    void start() {
        // One synchronous startup read lets the first swapchain use the right
        // colour pipeline. All later X11 round trips stay off the presentation
        // thread.
        this->refresh();
        if (!gamescopeHdrFeedbackMayChange())
            return;

        this->monitor = std::jthread([this](const std::stop_token stop) {
            while (!stop.stop_requested()) {
                const auto interval = this->pollInterval();
                std::unique_lock waitLock(this->monitorWaitMutex);
                if (this->monitorWake.wait_for(
                        waitLock, interval,
                        [&stop] { return stop.stop_requested(); }))
                    break;
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
