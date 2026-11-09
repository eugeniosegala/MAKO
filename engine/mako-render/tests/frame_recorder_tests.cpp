/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "frame_recorder.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>

#include <unistd.h>

using namespace mako::layer;

namespace {
void expect(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

std::map<std::string, std::string> environment;
const char* lookup(const char* name) {
    const auto found = environment.find(name);
    return found == environment.end() ? nullptr : found->second.c_str();
}

constexpr uint64_t gib = 1ull << 30;

void settingsAreArmedOnlyByTheLaunchOption() {
    environment.clear();
    expect(!frameRecordingSettings(lookup, 16 * gib).armed, "unarmed without MAKO_RECORD_FRAMES");
    environment["MAKO_RECORD_FRAMES"] = "0";
    expect(!frameRecordingSettings(lookup, 16 * gib).armed, "MAKO_RECORD_FRAMES=0 stays unarmed");
    environment["MAKO_RECORD_FRAMES"] = "1";
    auto burst = frameRecordingSettings(lookup, 16 * gib);
    expect(burst.armed && !burst.ring && burst.durationSeconds == 3.0 && burst.delaySeconds == 5.0,
        "burst defaults");
    environment["MAKO_RECORD_FRAMES_SECONDS"] = "99";
    environment["MAKO_RECORD_FRAMES_MAX_GB"] = "nonsense";
    burst = frameRecordingSettings(lookup, 16 * gib);
    expect(burst.durationSeconds == 10.0, "burst length clamps to ten seconds");
    expect(burst.budgetBytes == 40 * gib, "malformed budget keeps its default");
    expect(burst.ringBytes == 4 * gib && burst.keepBytes == 100 * gib, "memory and disk defaults");
    environment["MAKO_RECORD_FRAMES"] = "ring";
    environment.erase("MAKO_RECORD_FRAMES_SECONDS");
    environment["MAKO_RECORD_FRAMES_RING_GB"] = "1";
    const auto ring = frameRecordingSettings(lookup, 16 * gib);
    expect(ring.armed && ring.ring && ring.durationSeconds == 10.0, "ring defaults to ten seconds");
    expect(ring.ringBytes == gib, "ring memory budget override");
}

void ringDefaultsToAQuarterOfTheRam() {
    environment.clear();
    environment["MAKO_RECORD_FRAMES"] = "ring";
    expect(frameRecordingSettings(lookup, 0).ringBytes == 4 * gib, "unknown RAM keeps the 4 GB floor");
    expect(frameRecordingSettings(lookup, 14 * gib).ringBytes == 4 * gib, "a 16 GB handheld keeps the floor");
    expect(frameRecordingSettings(lookup, 32 * gib).ringBytes == 8 * gib, "a quarter of 32 GB");
    expect(frameRecordingSettings(lookup, 1024 * gib).ringBytes == 64 * gib, "the default stops at 64 GB");
    environment["MAKO_RECORD_FRAMES_RING_GB"] = "2";
    expect(frameRecordingSettings(lookup, 32 * gib).ringBytes == 2 * gib, "an override beats the RAM default");
}

void savesFitTheBudgetAndKeepTheDiskFree() {
    const uint64_t frame = 100;
    auto room = frameRecordingRoom(frame, 0, 1000, 100 * gib);
    expect(room.frames == 10 && !room.diskFull, "the launch budget bounds a save");
    room = frameRecordingRoom(frame, 650, 1000, 100 * gib);
    expect(room.frames == 3 && !room.diskFull, "a save gets only what is left of the budget");
    room = frameRecordingRoom(frame, 1200, 1000, 100 * gib);
    expect(room.frames == 0 && !room.diskFull, "a spent budget leaves no room");
    room = frameRecordingRoom(frame, 0, 1000, 2 * gib + 500);
    expect(room.frames == 5 && !room.diskFull, "2 GB of the disk stays free");
    room = frameRecordingRoom(frame, 0, 1000, gib);
    expect(room.frames == 0 && room.diskFull, "a disk inside the reserve is full");
    expect(frameRecordingRoom(0, 0, 1000, 100 * gib).frames == 0, "an empty frame has no room");
}

void requestsCarryTheirTime() {
    expect(frameRecordingRequestTime("requested=1700000000.500\n") == 1700000000.5, "request time");
    expect(frameRecordingRequestTime("delay=5\n") < 0, "a request without a time is invalid");
}

void formatsHaveTheirPixelSize() {
    expect(frameRecordingBytesPerPixel(VK_FORMAT_B8G8R8A8_SRGB) == 4, "8-bit BGRA");
    expect(frameRecordingBytesPerPixel(VK_FORMAT_A2B10G10R10_UNORM_PACK32) == 4, "10-bit packed");
    expect(frameRecordingBytesPerPixel(VK_FORMAT_R16G16B16A16_SFLOAT) == 8, "half float");
    expect(frameRecordingBytesPerPixel(VK_FORMAT_R8G8B8_UNORM) == 0, "unsupported format");
}

void rotationDeletesTheOldestFinishedRecordings() {
    char pattern[] = "/tmp/mako-frame-recorder.XXXXXX";
    const char* directory = mkdtemp(pattern);
    expect(directory != nullptr, "temporary root");
    const std::filesystem::path root(directory);
    const auto recording = [&](const std::string& name, const size_t bytes, const bool finished) {
        std::filesystem::create_directories(root / name);
        std::ofstream(root / name / "frame-000000.raw") << std::string(bytes, 'x');
        if (finished)
            std::ofstream(root / name / "recording.json") << "{}";
        return root / name;
    };
    const auto oldest = recording("20261026-100000-game", 4000, true);
    const auto unfinished = recording("20261026-105000-game", 4000, false);
    const auto middle = recording("20261026-110000-game", 4000, true);
    const auto newest = recording("20261026-120000-game", 4000, true);
    // Three finished recordings of about 4 KB; keep about 8 KB, protecting the newest.
    const auto removed = rotateFrameRecordings(root, 8100, newest);
    expect(removed.size() == 1 && removed.front() == oldest, "only the oldest finished recording goes");
    expect(std::filesystem::exists(middle) && std::filesystem::exists(newest), "newer recordings stay");
    expect(std::filesystem::exists(unfinished), "an unfinished recording is never rotated away");
    expect(rotateFrameRecordings(root, 0, newest).size() == 1 && std::filesystem::exists(newest),
        "the protected recording survives any budget");
    std::filesystem::remove_all(root);
}
}

int main() {
    settingsAreArmedOnlyByTheLaunchOption();
    ringDefaultsToAQuarterOfTheRam();
    savesFitTheBudgetAndKeepTheDiskFree();
    requestsCarryTheirTime();
    formatsHaveTheirPixelSize();
    rotationDeletesTheOldestFinishedRecordings();
    std::cout << "frame recorder tests passed\n";
}
