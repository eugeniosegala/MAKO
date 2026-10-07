/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "configuration/power_source.hpp"
#include "mako-common/configuration/config.hpp"
#include "mako-common/configuration/detection.hpp"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
    using Clock = std::chrono::steady_clock;

    void expect(const bool condition, const std::string_view message) {
        if (condition) return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }

    template<typename Predicate>
    void await(const Predicate& ready, const std::string_view message) {
        const auto deadline = Clock::now() + std::chrono::seconds(2);
        while (Clock::now() < deadline) {
            if (ready()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        expect(false, message);
    }

    class ControlledSupply {
        std::mutex mutex;
        std::condition_variable wake;
        unsigned calls{};
        unsigned permits{};
        ls::PowerSource source{ls::PowerSource::Unknown};
        bool fail{};

    public:
        ls::PowerSource read() {
            std::unique_lock<std::mutex> lock(this->mutex);
            const auto call = ++this->calls;
            this->wake.notify_all();
            this->wake.wait(lock, [&] { return this->permits >= call; });
            if (this->fail) throw std::runtime_error("supply read failed");
            return this->source;
        }

        void entered(const unsigned call) {
            std::unique_lock<std::mutex> lock(this->mutex);
            expect(this->wake.wait_for(lock, std::chrono::seconds(2), [&] {
                return this->calls == call;
            }), "Background detector did not start");
        }

        void release(const unsigned call, const ls::PowerSource next,
                const bool fail = false) {
            const std::lock_guard<std::mutex> lock(this->mutex);
            this->source = next;
            this->fail = fail;
            this->permits = call;
            this->wake.notify_all();
        }
    };

    void testBlockedMonitor() {
        ControlledSupply supply;
        {
            ls::detail::PowerSourceMonitor monitor("unused", [&](const auto&) {
                return supply.read();
            });
            supply.entered(1);
            const auto started = Clock::now();
            expect(!monitor.waitForInitialSample(),
                "Startup must stop waiting for a stalled power read");
            expect(Clock::now() - started < std::chrono::milliseconds(300),
                "Startup power selection exceeded its bounded grace period");
            expect(monitor.sample() == ls::PowerSource::Unknown,
                "Unavailable initial power must preserve Base settings");
            for (unsigned i = 0; i < 1000; ++i) {
                expect(!monitor.requestSample() &&
                        monitor.sample() == ls::PowerSource::Unknown,
                    "A stalled read must not block cache access or accumulate requests");
            }
            supply.release(1, ls::PowerSource::Docked);
            await([&] { return monitor.sample() == ls::PowerSource::Docked; },
                "A completed initial reading must publish the confirmed source");

            await([&] { return monitor.requestSample(); }, "Unable to request a second reading");
            supply.entered(2);
            expect(monitor.sample() == ls::PowerSource::Docked && !monitor.requestSample(),
                "A later stalled read must retain the confirmed source without a backlog");
            supply.release(2, ls::PowerSource::Unknown);
            await([&] { return monitor.requestSample(); }, "Unknown reading did not complete");
            supply.entered(3);
            expect(monitor.sample() == ls::PowerSource::Docked,
                "Unknown readings must retain the last confirmed source");
            supply.release(3, ls::PowerSource::Unknown, true);
            await([&] { return monitor.requestSample(); }, "Failed reading did not complete");
            supply.entered(4);
            expect(monitor.sample() == ls::PowerSource::Docked,
                "Detector exceptions must retain the last confirmed source");
            supply.release(4, ls::PowerSource::Handheld);
            await([&] { return monitor.sample() == ls::PowerSource::Handheld; },
                "The monitor must recover after unavailable and failed readings");
        }
        // Destruction above must stop and join the idle worker rather than
        // requiring another detector request or leaving detached callbacks.
    }

    void writeText(const std::filesystem::path& path, const std::string_view text) {
        std::ofstream output(path);
        output << text;
    }

    class SupplyPipe {
        int descriptor{-1};
    public:
        explicit SupplyPipe(const std::filesystem::path& path) {
            expect(::mkfifo(path.c_str(), 0600) == 0, "Unable to create stalled supply fixture");
            this->descriptor = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
            expect(this->descriptor >= 0, "Unable to open supply fixture");
        }
        ~SupplyPipe() { if (this->descriptor >= 0) ::close(this->descriptor); }
        SupplyPipe(const SupplyPipe&) = delete;
        SupplyPipe& operator=(const SupplyPipe&) = delete;

        void send(const std::string_view value) const {
            expect(::write(this->descriptor, value.data(), value.size()) ==
                    static_cast<ssize_t>(value.size()), "Unable to release supply reading");
        }
        bool take(const char expectedSource) const {
            std::array<char, 2> unread{};
            return ::read(this->descriptor, unread.data(), unread.size()) == 2 &&
                unread[0] == expectedSource && unread[1] == '\n';
        }
    };

    void testBlockedWatcher(const std::filesystem::path& directory) {
        const auto root = directory / "supplies";
        std::filesystem::create_directories(root / "AC");
        std::filesystem::create_directories(root / "BAT");
        writeText(root / "AC/type", "Mains\n");
        writeText(root / "AC/scope", "System\n");
        writeText(root / "BAT/type", "Battery\n");
        writeText(root / "BAT/scope", "System\n");
        writeText(root / "BAT/present", "1\n");
        const SupplyPipe pipe(root / "AC/online");
        const auto path = directory / "conf.toml";
        ls::ConfigFile file;
        ls::GameConf base;
        base.name = "game";
        base.target_fps = 90;
        auto handheld = base;
        handheld.target_fps = 60;
        auto docked = base;
        docked.target_fps = 120;
        base.power_profiles = {handheld, docked};
        file.profiles() = {base};
        file.write(path);
        setenv("MAKO_CONFIG", path.c_str(), 1);
        unsetenv("MAKO_ENV");

        const auto started = Clock::now();
        auto watched = std::make_unique<ls::WatchedConfig>(root);
        expect(Clock::now() - started < std::chrono::milliseconds(300) &&
                watched->get().power_source == ls::PowerSource::Unknown,
            "A stalled startup supply must choose Base within the startup grace period");
        expect(!watched->update(), "A pending initial reading must not invent a power change");
        pipe.send("0\n");
        await([&] {
            watched->update();
            return watched->get().power_source == ls::PowerSource::Handheld;
        }, "A delayed initial reading must become visible without rewriting configuration");

        const auto timestamp = std::filesystem::last_write_time(path);
        std::this_thread::sleep_for(std::chrono::milliseconds(2050));
        const auto pollStarted = Clock::now();
        expect(!watched->update() &&
                Clock::now() - pollStarted < std::chrono::milliseconds(100),
            "A periodic stalled supply read must not block the presentation-side update");
        file.profiles()[0].power_profiles[0].base_fps_cap = 30;
        file.write(path);
        expect(watched->update() &&
                watched->get().profiles()[0].power_profiles[0].base_fps_cap == 30 &&
                watched->get().power_source == ls::PowerSource::Handheld,
            "Configuration edits must remain live while a power read is stalled");

        // Disable while that same worker is still blocked: no join may occur
        // in update, and completed readings must not start further polling.
        file.profiles()[0].power_profiles.clear();
        file.write(path);
        const auto disableStarted = Clock::now();
        expect(watched->update() &&
                Clock::now() - disableStarted < std::chrono::milliseconds(100),
            "Disabling power profiles must not wait for an outstanding reading");
        pipe.send("0\n");
        std::this_thread::sleep_for(std::chrono::milliseconds(2050));
        pipe.send("1\n");
        expect(!watched->update(), "Disabled power profiles must not apply cached changes");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        expect(pipe.take('1'),
            "Disabled power profiles must leave system supplies unread");

        file.profiles()[0].power_profiles = {handheld, docked};
        file.write(path);
        expect(watched->update(), "Re-enabling power profiles must reload their saved settings");
        const auto reenabledTimestamp = std::filesystem::last_write_time(path);
        pipe.send("1\n");
        await([&] {
            watched->update();
            return watched->get().power_source == ls::PowerSource::Docked;
        }, "Re-enabled profiles must reuse the sampler and observe fresh AC power");
        ls::Identification identity;
        identity.override = "game";
        const auto match = ls::findProfile(watched->get(), identity, false);
        expect(match && match->second.target_fps == 120 &&
                std::filesystem::last_write_time(path) == reenabledTimestamp &&
                timestamp != reenabledTimestamp,
            "Cached power must reach normal profile matching without a configuration write");
        watched.reset();

        // No power tables at startup must not create a sampler or consume a
        // supply value, even after the normal polling interval has elapsed.
        file.profiles()[0].power_profiles.clear();
        file.write(path);
        pipe.send("1\n");
        ls::WatchedConfig disabled(root);
        std::this_thread::sleep_for(std::chrono::milliseconds(2050));
        expect(!disabled.update() && disabled.get().power_source == ls::PowerSource::Unknown,
            "Profiles without power tables must not sample supplies");
        expect(pipe.take('1'),
            "An ordinary profile must leave the supply fixture unread");
    }
}

int main() {
    testBlockedMonitor();
    auto pattern = (std::filesystem::temp_directory_path() / "mako-power-monitor-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const auto* temporary = ::mkdtemp(buffer.data());
    expect(temporary != nullptr, "Unable to create power-monitor fixtures");
    const std::filesystem::path directory(temporary);
    testBlockedWatcher(directory);
    std::filesystem::remove_all(directory);
    std::cout << "power-source background sampling tests passed\n";
}
