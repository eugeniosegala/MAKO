/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-common/vulkan/command_buffer.hpp"
#include "mako-common/vulkan/fence.hpp"
#include "mako-common/vulkan/semaphore.hpp"
#include "mako-common/vulkan/vulkan.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace mako::layer {

    /// Launch-time recording settings. `MAKO_RECORD_FRAMES=1` arms burst
    /// recordings: each request pauses frame generation and records
    /// `MAKO_RECORD_FRAMES_SECONDS` (default 3, at most 10) after
    /// `MAKO_RECORD_FRAMES_DELAY` (default 5) seconds. `MAKO_RECORD_FRAMES=ring`
    /// keeps the last `MAKO_RECORD_FRAMES_SECONDS` (default 10, at most 60) of
    /// real frames in memory while frame generation stays on, within
    /// `MAKO_RECORD_FRAMES_RING_GB` (default a quarter of the device's RAM, at
    /// least 4), and a request saves them. `MAKO_RECORD_FRAMES_MAX_GB`
    /// (default 40) bounds what one launch writes and
    /// `MAKO_RECORD_FRAMES_KEEP_GB` (default 100) the saved recordings kept,
    /// oldest deleted first. Every save keeps 2 GB of the disk free.
    struct FrameRecordingSettings {
        bool armed{false};
        bool ring{false};
        double delaySeconds{5.0};
        double durationSeconds{3.0};
        uint64_t budgetBytes{40ull << 30};
        uint64_t ringBytes{4ull << 30};
        uint64_t keepBytes{100ull << 30};
    };

    /// Read the settings from a getenv-like lookup; malformed values keep
    /// defaults. `memoryBytes` is the device's RAM, which sizes the default ring.
    [[nodiscard]] FrameRecordingSettings frameRecordingSettings(
        const char* (*lookup)(const char*), uint64_t memoryBytes);

    /// What a save may still write: frames of `frameBytes` within what is left
    /// of the launch's `budgetBytes` after `usedBytes`, with 2 GB of the disk
    /// kept free.
    struct FrameRecordingRoom {
        uint64_t frames{0};
        bool diskFull{false};   ///< fewer than three frames fit on the disk
    };
    [[nodiscard]] FrameRecordingRoom frameRecordingRoom(uint64_t frameBytes, uint64_t usedBytes,
        uint64_t budgetBytes, uint64_t availableDiskBytes);

    /// Unix time a request was written (`requested=` line), or a negative value.
    [[nodiscard]] double frameRecordingRequestTime(const std::string& text);

    /// Bytes per pixel of a swapchain format the recorder can write, or 0.
    [[nodiscard]] uint32_t frameRecordingBytesPerPixel(VkFormat format);

    /// Delete the oldest finished recordings under `root` until the rest fit in
    /// `keepBytes`; `protect` is never deleted. Returns the directories removed.
    std::vector<std::filesystem::path> rotateFrameRecordings(const std::filesystem::path& root,
        uint64_t keepBytes, const std::filesystem::path& protect);

    /// Test recordings of the real frames a game presents, for offline frame
    /// generation evaluation. Only a launch armed with `MAKO_RECORD_FRAMES`
    /// creates a recorder; it announces itself with an `armed.<pid>` marker in
    /// the user's `~/.local/share/mako-render/captures` directory and answers
    /// fresh `request` files there. Each real frame is copied to host memory on
    /// the present queue; a background thread writes it (burst) or keeps it in
    /// a bounded memory ring until a request saves the ring (ring).
    class FrameRecorder {
    public:
        explicit FrameRecorder(FrameRecordingSettings settings);
        ~FrameRecorder();
        FrameRecorder(const FrameRecorder&) = delete;
        FrameRecorder& operator=(const FrameRecorder&) = delete;

        /// The launch's settings; only an armed launch creates a recorder.
        [[nodiscard]] static const FrameRecordingSettings& launchSettings();

        /// Observe one real present before MAKO's own work. Returns the
        /// semaphores the rest of the present must wait on: the application's,
        /// or the recorder's own when this frame's copy was submitted.
        [[nodiscard]] std::span<const VkSemaphore> observe(
            const vk::Vulkan& vk, VkQueue queue, VkImage image,
            VkFormat format, VkColorSpaceKHR colorSpace, VkExtent2D extent,
            bool transferable, std::span<const VkSemaphore> waitSemaphores);

        /// True while a burst recording is scheduled or running.
        [[nodiscard]] bool holdsFrameGeneration() const {
            return !this->settings.ring && this->state != State::Idle;
        }

    private:
        enum class State { Idle, Scheduled, Recording };
        struct Slot {
            const vk::Vulkan* vk{nullptr};
            VkBuffer buffer{VK_NULL_HANDLE};
            VkDeviceMemory memory{VK_NULL_HANDLE};
            void* mapped{nullptr};
            VkDeviceSize size{0};
            std::unique_ptr<vk::CommandBuffer> commands;
            std::unique_ptr<vk::Fence> fence;
            std::unique_ptr<vk::Semaphore> semaphore;
            std::atomic<bool> busy{false};
            bool submitted{false};
            uint64_t lastUsePresent{0};
            ~Slot();
        };
        struct Job {
            Slot* slot;
            uint64_t sequence;
            int64_t presentNs;
        };
        struct Frame {
            uint64_t sequence;
            int64_t presentNs;
            std::string file;
        };
        struct Held {
            uint64_t sequence;
            int64_t presentNs;
            std::unique_ptr<uint8_t[]> pixels;
        };
        struct Writer {
            std::filesystem::path root;
            std::filesystem::path directory;   // burst: the recording; ring: next save
            std::string header;
            std::string reason{"complete"};
            bool ring{false};
            uint64_t frameBytes{0};
            size_t ringFrames{0};
            int64_t ringWindowNs{0};
            uint64_t keepBytes{0};
            uint64_t budgetBytes{0};
            std::mutex mutex;
            std::condition_variable wake;
            std::deque<Job> jobs;
            std::vector<Frame> frames;
            std::vector<uint64_t> dropped;
            std::vector<uint64_t> failed;
            std::deque<Held> held;
            std::vector<std::unique_ptr<uint8_t[]>> spare;
            bool saveRequested{false};
            bool closing{false};
            std::atomic<uint64_t>* launchBytes{nullptr};
            std::atomic<bool> saving{false};
            std::atomic<bool> finished{false};
            std::thread thread;
        };

        void poll(std::chrono::steady_clock::time_point now);
        void writeMarker();
        void begin(const vk::Vulkan& vk, VkFormat format, VkColorSpaceKHR colorSpace,
            VkExtent2D extent, bool transferable);
        void finish(const char* reason);
        void releaseFinishedWriter();
        void releaseSlots();
        Slot* freeSlot();
        static void write(Writer& writer, const vk::Vulkan& vk);
        static void save(Writer& writer, std::deque<Held>& frames, std::vector<uint64_t>& dropped);

        FrameRecordingSettings settings;
        std::filesystem::path marker;
        std::chrono::steady_clock::time_point lastHeartbeat;
        std::atomic<uint64_t> launchBytes{0};
        uint32_t maxFrames{0};
        State state{State::Idle};
        std::chrono::steady_clock::time_point lastPoll;
        std::chrono::steady_clock::time_point start;
        std::chrono::steady_clock::time_point end;
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkExtent2D extent{};
        uint32_t bytesPerPixel{0};
        uint64_t sequence{0};
        uint64_t presents{0};
        uint64_t recorded{0};
        bool refusedRing{false};
        /// Why the ring could not start, so a request is still answered.
        std::string refusal;
        std::vector<std::unique_ptr<Slot>> slots;
        std::unique_ptr<Writer> writer;
        std::vector<std::unique_ptr<Writer>> draining;
    };
}
