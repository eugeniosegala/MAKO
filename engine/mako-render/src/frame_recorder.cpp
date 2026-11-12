/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "frame_recorder.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>
#include <system_error>

#include <unistd.h>

using namespace mako::layer;

namespace {
    // Host copies the writer has not consumed yet; frames beyond them are
    // dropped and listed, never waited for on the present thread.
    constexpr size_t maxSlots = 16;
    constexpr VkDeviceSize slotBudgetBytes = 512ull << 20;
    // A slot's semaphore is re-signalled only after the present that waited
    // on it has long been submitted.
    constexpr uint64_t semaphoreReuseDistance = 8;
    constexpr auto pollInterval = std::chrono::milliseconds(250);
    // Frames per second a recording window may hold.
    constexpr double maxFramesPerSecond = 240.0;
    // Disk space no save may use.
    constexpr uint64_t diskReserveBytes = 2ull << 30;

    std::filesystem::path recordingsRoot() {
        if (const char* data = std::getenv("XDG_DATA_HOME"); data && *data)
            return std::filesystem::path(data) / "mako-render" / "captures";
        if (const char* home = std::getenv("HOME"); home && *home)
            return std::filesystem::path(home) / ".local/share/mako-render/captures";
        return {};
    }

    std::string processName() {
        std::ifstream comm("/proc/self/comm");
        std::string name;
        std::getline(comm, name);
        for (auto& c : name)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != '_')
                c = '_';
        return name.empty() ? "game" : name;
    }

    std::string utcStamp(const char* format) {
        const std::time_t now = std::time(nullptr);
        std::tm utc{};
        gmtime_r(&now, &utc);
        char text[32];
        std::strftime(text, sizeof(text), format, &utc);
        return text;
    }

    std::string jsonString(const std::string& value) {
        std::string out = "\"";
        for (const char c : value) {
            if (c == '"' || c == '\\') out += '\\';
            if (static_cast<unsigned char>(c) >= 0x20) out += c;
        }
        return out + "\"";
    }

    void writeNote(const std::filesystem::path& root, const std::string& text) {
        std::error_code error;
        std::filesystem::create_directories(root, error);
        std::ofstream note(root / "last-status");
        note << text << '\n';
    }

    bool writeRaw(const std::filesystem::path& path, const void* data, const size_t size) {
        std::FILE* file = std::fopen(path.c_str(), "wb");
        if (!file)
            return false;
        const bool written = std::fwrite(data, 1, size, file) == size;
        return std::fclose(file) == 0 && written;
    }

    uint64_t availableDisk(const std::filesystem::path& root) {
        std::error_code error;
        const auto space = std::filesystem::space(root, error);
        return error ? 0 : space.available;
    }

    const char* roomRefusal(const FrameRecordingRoom& room) {
        return room.diskFull ? "not enough free disk space"
            : "this launch's recording budget is used up (MAKO_RECORD_FRAMES_MAX_GB)";
    }

    std::string frameName(const uint64_t sequence) {
        char name[32];
        std::snprintf(name, sizeof(name), "frame-%06llu.raw", static_cast<unsigned long long>(sequence));
        return name;
    }

    /// recording.json: the shared header, how it ended, its frames and its gaps.
    void writeIndex(const std::filesystem::path& directory, const std::string& header,
            const std::string& reason, const std::vector<std::pair<uint64_t, int64_t>>& frames,
            const std::vector<uint64_t>& dropped, const std::vector<uint64_t>& failed) {
        std::ostringstream index;
        index << "{\n  " << header << "  \"end_reason\": " << jsonString(reason) << ",\n  \"frames\": [";
        for (size_t i = 0; i < frames.size(); ++i)
            index << (i ? ",\n    " : "\n    ") << "{\"sequence\": " << frames[i].first
                  << ", \"present_ns\": " << frames[i].second
                  << ", \"file\": " << jsonString(frameName(frames[i].first)) << "}";
        index << "\n  ],\n  \"dropped\": [";
        for (size_t i = 0; i < dropped.size(); ++i)
            index << (i ? ", " : "") << dropped[i];
        index << "],\n  \"failed\": [";
        for (size_t i = 0; i < failed.size(); ++i)
            index << (i ? ", " : "") << failed[i];
        index << "]\n}\n";
        {
            std::ofstream file(directory / "recording.json.tmp");
            file << index.str();
        }
        std::error_code error;
        std::filesystem::rename(directory / "recording.json.tmp", directory / "recording.json", error);
    }

    std::optional<uint32_t> hostReadableMemory(const vk::Vulkan& vk, uint32_t validTypes) {
        VkPhysicalDeviceMemoryProperties properties{};
        vk.fi().GetPhysicalDeviceMemoryProperties(vk.physdev(), &properties);
        const VkMemoryPropertyFlags coherent =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        // Cached memory keeps the writer's reads fast; coherent memory needs
        // no invalidation, which the layer's dispatch table does not carry.
        for (const VkMemoryPropertyFlags wanted : {coherent | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, coherent})
            for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
                if ((validTypes & (1u << i)) &&
                        (properties.memoryTypes[i].propertyFlags & wanted) == wanted)
                    return i;
        return std::nullopt;
    }
}

FrameRecordingSettings mako::layer::frameRecordingSettings(
        const char* (*lookup)(const char*), const uint64_t memoryBytes) {
    FrameRecordingSettings settings;
    const auto number = [&](const char* name, const double fallback, const double low, const double high) {
        const char* value = lookup(name);
        if (!value || !*value)
            return fallback;
        char* end{};
        const double parsed = std::strtod(value, &end);
        return end == value || *end != '\0' ? fallback : std::clamp(parsed, low, high);
    };
    const auto gigabytes = [&](const char* name, const double fallback, const double low, const double high) {
        return static_cast<uint64_t>(number(name, fallback, low, high) * double(1ull << 30));
    };
    const char* armed = lookup("MAKO_RECORD_FRAMES");
    settings.armed = armed && *armed && std::string_view(armed) != "0";
    settings.ring = settings.armed && std::string_view(armed) == "ring";
    settings.delaySeconds = number("MAKO_RECORD_FRAMES_DELAY", settings.delaySeconds, 0.0, 60.0);
    settings.durationSeconds = settings.ring
        ? number("MAKO_RECORD_FRAMES_SECONDS", 10.0, 2.0, 60.0)
        : number("MAKO_RECORD_FRAMES_SECONDS", 3.0, 0.5, 10.0);
    // A quarter of the RAM, so a 16 GB handheld keeps the 4 GB floor.
    const double ringGigabytes = std::clamp(static_cast<double>(memoryBytes / 4) / double(1ull << 30), 4.0, 64.0);
    settings.budgetBytes = gigabytes("MAKO_RECORD_FRAMES_MAX_GB", 40.0, 0.25, 256.0);
    settings.ringBytes = gigabytes("MAKO_RECORD_FRAMES_RING_GB", ringGigabytes, 0.25, 64.0);
    settings.keepBytes = gigabytes("MAKO_RECORD_FRAMES_KEEP_GB", 100.0, 1.0, 4096.0);
    return settings;
}

FrameRecordingRoom mako::layer::frameRecordingRoom(const uint64_t frameBytes, const uint64_t usedBytes,
        const uint64_t budgetBytes, const uint64_t availableDiskBytes) {
    if (frameBytes == 0)
        return {};
    const uint64_t disk = availableDiskBytes - std::min(availableDiskBytes, diskReserveBytes);
    const uint64_t budget = budgetBytes - std::min(usedBytes, budgetBytes);
    return {std::min(budget, disk) / frameBytes, disk / frameBytes < 3};
}

double mako::layer::frameRecordingRequestTime(const std::string& text) {
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line))
        if (line.rfind("requested=", 0) == 0) {
            char* end{};
            const double value = std::strtod(line.c_str() + 10, &end);
            if (end != line.c_str() + 10)
                return value;
        }
    return -1.0;
}

uint32_t mako::layer::frameRecordingBytesPerPixel(const VkFormat format) {
    switch (format) {
        case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32: case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
            return 4;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return 8;
        default:
            return 0;
    }
}

std::vector<std::filesystem::path> mako::layer::rotateFrameRecordings(
        const std::filesystem::path& root, const uint64_t keepBytes,
        const std::filesystem::path& protect) {
    std::error_code error;
    std::vector<std::pair<std::filesystem::path, uint64_t>> recordings;
    uint64_t total = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
        if (!entry.is_directory(error) || !std::filesystem::exists(entry.path() / "recording.json", error))
            continue;
        uint64_t size = 0;
        for (const auto& file : std::filesystem::recursive_directory_iterator(entry.path(), error))
            if (file.is_regular_file(error))
                size += file.file_size(error);
        recordings.emplace_back(entry.path(), size);
        total += size;
    }
    // Directory names start with the UTC time, so name order is age order.
    std::sort(recordings.begin(), recordings.end());
    std::vector<std::filesystem::path> removed;
    for (const auto& [path, size] : recordings) {
        if (total <= keepBytes)
            break;
        if (path == protect)
            continue;
        std::filesystem::remove_all(path, error);
        if (!error) {
            total -= size;
            removed.push_back(path);
        }
    }
    return removed;
}

FrameRecorder::Slot::~Slot() {
    if (!this->vk)
        return;
    if (this->mapped)
        this->vk->df().UnmapMemory(this->vk->dev(), this->memory);
    if (this->buffer)
        this->vk->df().DestroyBuffer(this->vk->dev(), this->buffer, nullptr);
    if (this->memory)
        this->vk->df().FreeMemory(this->vk->dev(), this->memory, nullptr);
}

const FrameRecordingSettings& FrameRecorder::launchSettings() {
    static const FrameRecordingSettings settings = [] {
        const long pages = ::sysconf(_SC_PHYS_PAGES), pageSize = ::sysconf(_SC_PAGE_SIZE);
        return frameRecordingSettings(
            [](const char* name) -> const char* { return std::getenv(name); },
            pages > 0 && pageSize > 0 ? static_cast<uint64_t>(pages) * static_cast<uint64_t>(pageSize) : 0);
    }();
    return settings;
}

FrameRecorder::FrameRecorder(const FrameRecordingSettings settings) : settings(settings) {
    // The marker tells MAKO Decky that this launch accepts recording requests.
    const auto root = recordingsRoot();
    if (root.empty())
        return;
    std::error_code error;
    std::filesystem::create_directories(root, error);
    this->marker = root / ("armed." + std::to_string(::getpid()));
    this->writeMarker();
    std::clog << "MAKO Renderer: frame recording armed; mode=" << (settings.ring ? "ring" : "burst")
              << " seconds=" << settings.durationSeconds << " delay_s=" << settings.delaySeconds
              << " budget_bytes=" << settings.budgetBytes << " ring_bytes=" << settings.ringBytes
              << " keep_bytes=" << settings.keepBytes << '\n';
}

void FrameRecorder::writeMarker() {
    if (this->marker.empty())
        return;
    // Replaced whole, so MAKO Decky never reads half a marker.
    const auto staged = this->marker.parent_path() / ("." + this->marker.filename().string() + ".tmp");
    {
        std::ofstream file(staged);
        file << "process=" << processName() << "\nmode=" << (this->settings.ring ? "ring" : "burst")
             << "\nseconds=" << this->settings.durationSeconds << "\ndelay=" << this->settings.delaySeconds
             << "\nbudget_bytes=" << this->settings.budgetBytes << '\n';
    }
    std::error_code error;
    std::filesystem::rename(staged, this->marker, error);
}

FrameRecorder::~FrameRecorder() {
    if (this->state == State::Recording)
        this->finish("interrupted");
    for (auto& writer : this->draining)
        if (writer->thread.joinable())
            writer->thread.join();
    std::error_code error;
    if (!this->marker.empty())
        std::filesystem::remove(this->marker, error);
}

std::span<const VkSemaphore> FrameRecorder::observe(
        const vk::Vulkan& vk, const VkQueue queue, const VkImage image,
        const VkFormat format, const VkColorSpaceKHR colorSpace, const VkExtent2D extent,
        const bool transferable, const std::span<const VkSemaphore> waitSemaphores) {
    const auto now = std::chrono::steady_clock::now();
    this->presents++;
    this->releaseFinishedWriter();
    // A fresh marker time tells MAKO Decky this armed game is still running,
    // whatever process namespace the game runs in.
    if (!this->marker.empty() && now - this->lastHeartbeat >= std::chrono::seconds(2)) {
        this->lastHeartbeat = now;
        this->writeMarker();
    }

    const bool changed = this->state == State::Recording && (format != this->format ||
        extent.width != this->extent.width || extent.height != this->extent.height);
    if (this->settings.ring) {
        // The ring records continuously; a resized swapchain starts a new ring.
        if (changed) {
            this->finish("swapchain-changed");
            this->refusedRing = false;
        }
        if (this->state == State::Idle && !this->refusedRing) {
            this->start = now;
            this->begin(vk, format, colorSpace, extent, transferable);
            this->refusedRing = this->state != State::Recording;
        }
        // A ring that cannot record still answers requests with its reason.
        this->poll(now);
        if (this->state != State::Recording)
            return waitSemaphores;
    } else {
        if (this->state == State::Idle) {
            this->poll(now);
            if (this->state == State::Idle) {
                if (!this->slots.empty() && this->draining.empty())
                    this->releaseSlots();
                return waitSemaphores;
            }
        }
        if (this->state == State::Scheduled) {
            if (now < this->start)
                return waitSemaphores;
            this->begin(vk, format, colorSpace, extent, transferable);
            if (this->state != State::Recording)
                return waitSemaphores;
        }
        if (now >= this->end || this->recorded >= this->maxFrames || changed) {
            this->finish(changed ? "swapchain-changed" : "complete");
            return waitSemaphores;
        }
    }

    const uint64_t sequence = this->sequence++;
    const int64_t presentNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now - this->start).count();
    Slot* slot = this->freeSlot();
    if (!slot) {
        std::lock_guard lock(this->writer->mutex);
        this->writer->dropped.push_back(sequence);
        return waitSemaphores;
    }
    try {
        if (slot->submitted)
            slot->fence->reset(vk);
        const VkCommandBuffer commands = slot->commands->handle();
        slot->commands->begin(vk, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        const VkImageMemoryBarrier toTransfer{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = range,
        };
        vk.df().CmdPipelineBarrier(commands, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);
        const VkBufferImageCopy region{
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .imageExtent = {extent.width, extent.height, 1},
        };
        vk.df().CmdCopyImageToBuffer(commands, image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, slot->buffer, 1, &region);
        const VkImageMemoryBarrier toPresent{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = range,
        };
        const VkBufferMemoryBarrier toHost{
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = slot->buffer,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };
        vk.df().CmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0,
            0, nullptr, 1, &toHost, 1, &toPresent);
        slot->commands->end(vk);
        // The copy waits on the game's semaphores at every stage, and the
        // present waits on the recorder's semaphore instead.
        const std::vector<VkPipelineStageFlags> stages(
            waitSemaphores.size(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        const VkSubmitInfo submit{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size()),
            .pWaitSemaphores = waitSemaphores.data(),
            .pWaitDstStageMask = stages.data(),
            .commandBufferCount = 1,
            .pCommandBuffers = &commands,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &slot->semaphore->handle(),
        };
        if (const VkResult result = vk.df().QueueSubmit(queue, 1, &submit, slot->fence->handle());
                result != VK_SUCCESS)
            throw std::runtime_error("vkQueueSubmit failed: " + std::to_string(static_cast<int>(result)));
    } catch (const std::exception& e) {
        std::clog << "MAKO Renderer: frame recording stopped; copy failed: " << e.what() << '\n';
        this->finish("copy-failed");
        this->refusedRing = true;
        this->refusal = std::string("the frame copy failed: ") + e.what();
        return waitSemaphores;
    }
    slot->busy = true;
    slot->submitted = true;
    slot->lastUsePresent = this->presents;
    this->recorded++;
    if (!this->settings.ring)
        this->launchBytes += slot->size;
    {
        std::lock_guard lock(this->writer->mutex);
        this->writer->jobs.push_back({slot, sequence, presentNs});
    }
    this->writer->wake.notify_one();
    return {&slot->semaphore->handle(), 1};
}

void FrameRecorder::poll(const std::chrono::steady_clock::time_point now) {
    if (now - this->lastPoll < pollInterval)
        return;
    this->lastPoll = now;
    const auto root = recordingsRoot();
    if (root.empty())
        return;
    std::error_code error;
    const auto requestPath = root / "request";
    if (!std::filesystem::exists(requestPath, error))
        return;
    // A ring that cannot record leaves a new request to recording swapchains,
    // which poll every 250 ms, and answers it only if none took it.
    if (this->refusedRing) {
        std::ifstream pending(requestPath);
        std::stringstream text;
        text << pending.rdbuf();
        const double requested = frameRecordingRequestTime(text.str());
        if (requested >= 0 && static_cast<double>(std::time(nullptr)) - requested < 1.0)
            return;
    }
    // Exactly one process takes a request: the rename is atomic.
    const auto taken = root / ("request.taken-" + std::to_string(::getpid()));
    std::filesystem::rename(requestPath, taken, error);
    if (error)
        return;
    std::ifstream file(taken);
    std::stringstream text;
    text << file.rdbuf();
    std::filesystem::remove(taken, error);
    // Only a request made moments ago counts, so an old button press can
    // never start a recording in a later game.
    const double requested = frameRecordingRequestTime(text.str());
    const double age = static_cast<double>(std::time(nullptr)) - requested;
    if (requested < 0 || age > 30.0 || age < -5.0) {
        writeNote(root, "refused the request is stale; press L3 + R3 again");
        return;
    }
    if (this->launchBytes >= this->settings.budgetBytes) {
        writeNote(root, "refused this launch's recording budget is used up (MAKO_RECORD_FRAMES_MAX_GB)");
        return;
    }
    if (this->settings.ring) {
        if (this->refusedRing) {
            writeNote(root, "refused " + this->refusal);
            return;
        }
        if (!this->writer || this->writer->saving) {
            writeNote(root, "refused the previous save is still being written");
            return;
        }
        const auto directory = root / (utcStamp("%Y%m%d-%H%M%S") + "-" + processName());
        {
            std::lock_guard lock(this->writer->mutex);
            this->writer->directory = directory;
            this->writer->saveRequested = true;
            this->writer->saving = true;
        }
        this->writer->wake.notify_one();
        writeNote(root, "saving " + directory.filename().string());
        std::clog << "MAKO Renderer: frame recording ring save requested; directory=" << directory.string() << '\n';
        return;
    }
    this->start = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(this->settings.delaySeconds));
    this->end = this->start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(this->settings.durationSeconds));
    this->state = State::Scheduled;
    writeNote(root, "scheduled " + utcStamp("%Y-%m-%dT%H:%M:%SZ"));
    std::clog << "MAKO Renderer: frame recording scheduled; delay_s=" << this->settings.delaySeconds
              << " duration_s=" << this->settings.durationSeconds
              << " frame_generation=paused-until-recording-ends\n";
}

void FrameRecorder::begin(const vk::Vulkan& vk, const VkFormat format,
        const VkColorSpaceKHR colorSpace, const VkExtent2D extent, const bool transferable) {
    const auto root = recordingsRoot();
    const uint32_t bytesPerPixel = frameRecordingBytesPerPixel(format);
    const VkDeviceSize size = VkDeviceSize{extent.width} * extent.height * std::max(bytesPerPixel, 1u);
    std::string refusal;
    if (!transferable)
        refusal = "the game's swapchain was created without copy access; start the game with frame generation enabled";
    else if (bytesPerPixel == 0)
        refusal = "unsupported swapchain format " + std::to_string(static_cast<int>(format));
    // Burst: at most 240 frames per second within what is left of this
    // launch's budget, with 2 GB of the disk kept free. Ring: the memory ring.
    uint64_t frames = static_cast<uint64_t>(this->settings.durationSeconds * maxFramesPerSecond) + 1;
    if (refusal.empty() && this->settings.ring) {
        frames = std::min<uint64_t>(frames, this->settings.ringBytes / size);
        if (frames < 3)
            refusal = "MAKO_RECORD_FRAMES_RING_GB holds fewer than three frames";
    } else if (refusal.empty()) {
        const auto room = frameRecordingRoom(size, this->launchBytes, this->settings.budgetBytes, availableDisk(root));
        frames = std::min(frames, room.frames);
        if (frames < 3)
            refusal = roomRefusal(room);
    }
    if (!refusal.empty()) {
        std::clog << "MAKO Renderer: frame recording refused: " << refusal << '\n';
        writeNote(root, "refused " + refusal);
        this->refusal = refusal;
        this->state = State::Idle;
        return;
    }
    this->format = format;
    this->extent = extent;
    this->bytesPerPixel = bytesPerPixel;
    this->maxFrames = static_cast<uint32_t>(frames);
    this->sequence = 0;
    this->recorded = 0;
    std::filesystem::path directory;
    try {
        const size_t count = std::clamp<size_t>(slotBudgetBytes / size, 4, maxSlots);
        if (this->slots.empty() || this->slots.front()->size != size) {
            this->releaseSlots();
            if (!this->slots.empty())
                throw std::runtime_error("earlier copies are still in flight");
            for (size_t i = 0; i < count; ++i) {
                auto slot = std::make_unique<Slot>();
                slot->vk = &vk;
                slot->size = size;
                const VkBufferCreateInfo bufferInfo{
                    .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                    .size = size,
                    .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                };
                if (vk.df().CreateBuffer(vk.dev(), &bufferInfo, nullptr, &slot->buffer) != VK_SUCCESS)
                    throw std::runtime_error("vkCreateBuffer failed");
                VkMemoryRequirements requirements{};
                vk.df().GetBufferMemoryRequirements(vk.dev(), slot->buffer, &requirements);
                const auto type = hostReadableMemory(vk, requirements.memoryTypeBits);
                if (!type)
                    throw std::runtime_error("no host-coherent memory type");
                const VkMemoryAllocateInfo allocation{
                    .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                    .allocationSize = requirements.size,
                    .memoryTypeIndex = *type,
                };
                if (vk.df().AllocateMemory(vk.dev(), &allocation, nullptr, &slot->memory) != VK_SUCCESS)
                    throw std::runtime_error("vkAllocateMemory failed");
                if (vk.df().BindBufferMemory(vk.dev(), slot->buffer, slot->memory, 0) != VK_SUCCESS ||
                        vk.df().MapMemory(vk.dev(), slot->memory, 0, VK_WHOLE_SIZE, 0, &slot->mapped) != VK_SUCCESS)
                    throw std::runtime_error("buffer bind or map failed");
                slot->commands = std::make_unique<vk::CommandBuffer>(vk);
                slot->fence = std::make_unique<vk::Fence>(vk);
                slot->semaphore = std::make_unique<vk::Semaphore>(vk);
                this->slots.push_back(std::move(slot));
            }
        }
        if (!this->settings.ring) {
            std::error_code error;
            directory = root / (utcStamp("%Y%m%d-%H%M%S") + "-" + processName());
            std::filesystem::create_directories(directory, error);
            if (error)
                throw std::runtime_error("cannot create " + directory.string());
        }
    } catch (const std::exception& e) {
        std::clog << "MAKO Renderer: frame recording refused: " << e.what() << '\n';
        writeNote(root, std::string("refused ") + e.what());
        this->refusal = e.what();
        this->releaseSlots();
        this->state = State::Idle;
        return;
    }

    auto writer = std::make_unique<Writer>();
    writer->root = root;
    writer->directory = directory;
    writer->ring = this->settings.ring;
    writer->frameBytes = size;
    writer->ringFrames = this->maxFrames;
    writer->ringWindowNs = static_cast<int64_t>(this->settings.durationSeconds * 1e9);
    writer->keepBytes = this->settings.keepBytes;
    writer->budgetBytes = this->settings.budgetBytes;
    writer->launchBytes = &this->launchBytes;
    std::ostringstream header;
    header << "\"schema\": \"mako-frame-recording-v1\",\n"
           << "  \"mode\": " << (this->settings.ring ? "\"ring\"" : "\"burst\"") << ",\n"
           << "  \"frame_generation\": " << (this->settings.ring ? "\"on\"" : "\"paused\"") << ",\n"
           << "  \"process\": " << jsonString(processName()) << ",\n"
           << "  \"started_utc\": " << jsonString(utcStamp("%Y-%m-%dT%H:%M:%SZ")) << ",\n"
           << "  \"width\": " << extent.width << ",\n"
           << "  \"height\": " << extent.height << ",\n"
           << "  \"vk_format\": " << static_cast<int>(format) << ",\n"
           << "  \"vk_color_space\": " << static_cast<int>(colorSpace) << ",\n"
           << "  \"bytes_per_pixel\": " << bytesPerPixel << ",\n"
           << "  \"duration_seconds\": " << this->settings.durationSeconds << ",\n"
           << "  \"max_frames\": " << this->maxFrames << ",\n"
           << "  \"clock\": \"present-call steady clock, nanoseconds from the recording start\",\n";
    writer->header = header.str();
    Writer& owned = *writer;
    writer->thread = std::thread([&owned, &vk] { FrameRecorder::write(owned, vk); });
    this->writer = std::move(writer);
    this->state = State::Recording;
    if (this->settings.ring) {
        std::clog << "MAKO Renderer: frame recording ring started; seconds=" << this->settings.durationSeconds
                  << " frames=" << this->maxFrames << " width=" << extent.width << " height=" << extent.height
                  << " format=" << static_cast<int>(format) << " slots=" << this->slots.size() << '\n';
    } else {
        writeNote(root, "recording " + directory.filename().string());
        std::clog << "MAKO Renderer: frame recording started; directory=" << directory.string()
                  << " width=" << extent.width << " height=" << extent.height
                  << " format=" << static_cast<int>(format) << " slots=" << this->slots.size() << '\n';
    }
}

void FrameRecorder::finish(const char* reason) {
    if (!this->writer) {
        this->state = State::Idle;
        return;
    }
    {
        std::lock_guard lock(this->writer->mutex);
        this->writer->reason = reason;
        this->writer->closing = true;
    }
    this->writer->wake.notify_one();
    std::clog << "MAKO Renderer: frame recording ended; mode=" << (this->settings.ring ? "ring" : "burst")
              << " reason=" << reason << " recorded=" << this->recorded
              << (this->settings.ring ? "" : " frame_generation=resumed") << '\n';
    this->draining.push_back(std::move(this->writer));
    this->state = State::Idle;
}

void FrameRecorder::releaseFinishedWriter() {
    for (auto it = this->draining.begin(); it != this->draining.end();) {
        if ((*it)->finished) {
            (*it)->thread.join();
            it = this->draining.erase(it);
        } else {
            ++it;
        }
    }
}

void FrameRecorder::releaseSlots() {
    for (const auto& slot : this->slots)
        if (slot->busy || this->presents - slot->lastUsePresent < semaphoreReuseDistance)
            return;
    this->slots.clear();
}

FrameRecorder::Slot* FrameRecorder::freeSlot() {
    for (auto& slot : this->slots)
        if (!slot->busy && (!slot->submitted ||
                this->presents - slot->lastUsePresent >= semaphoreReuseDistance))
            return slot.get();
    return nullptr;
}

void FrameRecorder::save(Writer& writer, std::deque<Held>& frames, std::vector<uint64_t>& dropped) {
    std::filesystem::path directory;
    {
        std::lock_guard lock(writer.mutex);
        directory = writer.directory;
    }
    if (frames.size() < 3) {
        writeNote(writer.root, "refused fewer than three frames are held yet");
        writer.saving = false;
        return;
    }
    // Only the newest frames that fit what is left of the launch's budget.
    const auto room = frameRecordingRoom(writer.frameBytes, writer.launchBytes->load(),
        writer.budgetBytes, availableDisk(writer.root));
    if (room.frames < 3) {
        writeNote(writer.root, std::string("refused ") + roomRefusal(room));
        writer.saving = false;
        return;
    }
    const size_t trimmed = frames.size() - static_cast<size_t>(std::min<uint64_t>(frames.size(), room.frames));
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    std::vector<std::pair<uint64_t, int64_t>> index;
    std::vector<uint64_t> failed;
    for (size_t i = trimmed; i < frames.size(); ++i) {
        const auto& frame = frames[i];
        if (writeRaw(directory / frameName(frame.sequence), frame.pixels.get(), writer.frameBytes))
            index.emplace_back(frame.sequence, frame.presentNs);
        else
            failed.push_back(frame.sequence);
    }
    const uint64_t first = frames[trimmed].sequence, last = frames.back().sequence;
    std::vector<uint64_t> gaps;
    for (const uint64_t sequence : dropped)
        if (sequence > first && sequence < last)
            gaps.push_back(sequence);
    writeIndex(directory, writer.header, "saved", index, gaps, failed);
    *writer.launchBytes += index.size() * writer.frameBytes;
    rotateFrameRecordings(writer.root, writer.keepBytes, directory);
    writeNote(writer.root, "complete " + directory.filename().string() +
        " frames=" + std::to_string(index.size()) + " dropped=" + std::to_string(gaps.size()));
    std::clog << "MAKO Renderer: frame recording ring saved; directory=" << directory.string()
              << " frames=" << index.size() << " dropped=" << gaps.size() << " trimmed=" << trimmed << '\n';
    writer.saving = false;
}

void FrameRecorder::write(Writer& writer, const vk::Vulkan& vk) {
    for (;;) {
        Job job{};
        bool saveNow = false;
        {
            std::unique_lock lock(writer.mutex);
            writer.wake.wait(lock, [&] {
                return writer.closing || writer.saveRequested || !writer.jobs.empty();
            });
            if (writer.saveRequested) {
                writer.saveRequested = false;
                saveNow = true;
            } else if (!writer.jobs.empty()) {
                job = writer.jobs.front();
                writer.jobs.pop_front();
            } else {
                break;
            }
        }
        if (saveNow) {
            // The held frames leave the ring; recording continues into a new one.
            std::deque<Held> frames;
            std::vector<uint64_t> dropped;
            {
                std::lock_guard lock(writer.mutex);
                frames.swap(writer.held);
                dropped.swap(writer.dropped);
            }
            save(writer, frames, dropped);
            std::lock_guard lock(writer.mutex);
            for (auto& frame : frames)
                writer.spare.push_back(std::move(frame.pixels));
            continue;
        }

        bool copied = false;
        try {
            copied = job.slot->fence->wait(vk, 2'000'000'000ull);
        } catch (const std::exception&) {
            copied = false;
        }
        if (writer.ring) {
            std::unique_ptr<uint8_t[]> pixels;
            {
                std::lock_guard lock(writer.mutex);
                if (!writer.spare.empty()) {
                    pixels = std::move(writer.spare.back());
                    writer.spare.pop_back();
                }
            }
            if (copied) {
                if (!pixels)
                    pixels = std::make_unique<uint8_t[]>(writer.frameBytes);
                std::memcpy(pixels.get(), job.slot->mapped, writer.frameBytes);
            }
            job.slot->busy = false;
            std::lock_guard lock(writer.mutex);
            if (!copied) {
                writer.dropped.push_back(job.sequence);
                if (pixels)
                    writer.spare.push_back(std::move(pixels));
                continue;
            }
            writer.held.push_back({job.sequence, job.presentNs, std::move(pixels)});
            // Keep the newest frames within both the window and the memory budget.
            while (writer.held.size() > writer.ringFrames ||
                    writer.held.back().presentNs - writer.held.front().presentNs > writer.ringWindowNs) {
                writer.spare.push_back(std::move(writer.held.front().pixels));
                writer.held.pop_front();
            }
            const uint64_t oldest = writer.held.front().sequence;
            std::erase_if(writer.dropped, [oldest](const uint64_t sequence) { return sequence < oldest; });
            continue;
        }
        const std::string name = frameName(job.sequence);
        const bool written = copied && writeRaw(writer.directory / name, job.slot->mapped, job.slot->size);
        job.slot->busy = false;
        std::lock_guard lock(writer.mutex);
        if (written)
            writer.frames.push_back({job.sequence, job.presentNs, name});
        else
            writer.failed.push_back(job.sequence);
    }

    if (!writer.ring) {
        std::vector<std::pair<uint64_t, int64_t>> index;
        for (const auto& frame : writer.frames)
            index.emplace_back(frame.sequence, frame.presentNs);
        writeIndex(writer.directory, writer.header, writer.reason, index, writer.dropped, writer.failed);
        rotateFrameRecordings(writer.root, writer.keepBytes, writer.directory);
        writeNote(writer.root, "complete " + writer.directory.filename().string() +
            " frames=" + std::to_string(index.size()) +
            " dropped=" + std::to_string(writer.dropped.size()));
    }
    writer.finished = true;
}
