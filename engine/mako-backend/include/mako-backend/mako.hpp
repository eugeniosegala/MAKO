/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace mako::backend {

    /// Encoding of the images exchanged with the frame-generation backend.
    enum class FrameEncoding : uint8_t {
        Sdr8,
        SdrHighPrecision,
        ScRgbLinear,
        Hdr10Pq,
        Hdr10PqPacked,
    };

    class [[gnu::visibility("default")]] ContextImpl;
    class [[gnu::visibility("default")]] InstanceImpl;

    using Context = ContextImpl;

    ///
    /// Primitive exception class that deliveres a detailed error message
    ///
    class [[gnu::visibility("default")]] error : public std::runtime_error {
    public:
        ///
        /// Construct an error
        ///
        /// @param msg Error message.
        /// @param inner Inner exception.
        ///
        explicit error(const std::string &msg, const std::exception &inner);

        ///
        /// Construct an error
        ///
        /// @param msg Error message.
        ///
        explicit error(const std::string &msg);

        error(const error &) = default;
        error &operator=(const error &) = default;
        error(error &&) = default;
        error &operator=(error &&) = default;
        ~error() override;
    };

    /// Function type for picking a device based on its name and IDs
    using DevicePicker = std::function<bool(
        const std::string& deviceName,
        std::pair<const std::string&, const std::string&> ids, // (vendor ID, device ID) 0xXXXX format
        const std::optional<std::string>& pci // (bus:slot.func) if available, no padded zeros
    )>;

    ///
    /// Main entry point of the library
    ///
    class [[gnu::visibility("default")]] Instance {
    public:
        ///
        /// Create a mako instance
        ///
        /// @param devicePicker Function that picks a physical device based on some identifiers.
        /// @param shaderDllPath Path to the Lossless.dll file to load shaders from.
        /// @param allowLowPrecision Whether to load low-precision (FP16) shaders if supported.
        ///
        /// @throws backend::error on failure
        ///
        Instance(
            const DevicePicker& devicePicker,
            const std::filesystem::path& shaderDllPath,
            bool allowLowPrecision
        );

        ///
        /// Open a frame generation context.
        ///
        /// The VkFormat of the exchanged images is inferred from encoding:
        /// - Sdr8: VK_FORMAT_R8G8B8A8_UNORM
        /// - Hdr10PqPacked: VK_FORMAT_A2B10G10R10_UNORM_PACK32
        /// - all other encodings: VK_FORMAT_R16G16B16A16_SFLOAT
        ///
        /// The application and library must keep track of the frame index. When the next frame
        /// is ready, signal the syncFd with one increment (with the first trigger being 1).
        /// Each generated frame will increment the semaphore by one:
        /// - Application signals 1 -> Start generating with (curr, next) source images
        /// - Library signals 1 -> First frame between (curr, next) is ready
        /// - Library signals N -> N-th frame between (curr, next) is ready
        /// - Application signals N+1 -> Start generating with (next, curr) source images
        ///
        /// @param sourceFds Pair of file descriptors for the source images alternated between.
        /// @param destFds Vector with file descriptors to import output images from.
        /// @param syncFd File descriptor for the timeline semaphore used for synchronization.
        /// @param width Width of the images.
        /// @param height Height of the images.
        /// @param encoding Colour encoding of the exchanged images.
        /// @param flow Motion flow factor.
        /// @param perf Whether to enable performance mode.
        ///
        /// @throws backend::error on failure
        ///
        Context& openContext(
            std::pair<int, int> sourceFds,
            const std::vector<int>& destFds,
            int syncFd,
            uint32_t width, uint32_t height,
            FrameEncoding encoding, float flow, bool perf
        );

        /// Return whether the backend device can import and write the packed
        /// RGB10A2 images used by the low-bandwidth HDR10 transport path.
        [[nodiscard]] bool supportsPackedHdr10Transport() const;

        ///
        /// Schedule a new set of generated frames.
        ///
        /// @param context Context to use.
        /// @throws backend::error on failure
        ///
        void scheduleFrames(Context& context);

        ///
        /// Schedule generated frames at explicit interpolation timestamps.
        ///
        /// Timestamps must be strictly increasing values between 0 and 1. The
        /// number of timestamps may not exceed the destination-image capacity
        /// supplied when the context was opened.
        ///
        /// @param context Context to use.
        /// @param timestamps Normalized positions between the previous and current real frame.
        /// @throws backend::error on failure
        ///
        void scheduleFrames(Context& context, std::span<const float> timestamps);

        ///
        /// Update temporal model history without generating output frames.
        ///
        /// The caller must submit the source frame and signal its shared timeline
        /// value first. The shared pre-pass is then run so temporal feature maps,
        /// source-image parity, and synchronization remain aligned while the
        /// expensive per-output generation passes are skipped.
        ///
        /// @param context Context to advance.
        ///
        void scheduleFrameHistory(Context& context);

        /// Check whether the context's previously submitted work has completed
        /// without blocking.
        [[nodiscard]] bool contextReady(const Context& context) const;

        ///
        /// Close a frame generation context
        ///
        /// @param context Context to close.
        ///
        void closeContext(const Context& context);

        // Non-copyable and non-movable
        Instance(const Instance&) = delete;
        Instance& operator=(const Instance&) = delete;
        Instance(Instance&&) = delete;
        Instance& operator=(Instance&&) = delete;
        virtual ~Instance();
    private:
        void collectRetiredContexts();

        std::unique_ptr<InstanceImpl> m_impl;

        std::vector<std::unique_ptr<Context>> m_contexts;
        std::vector<std::unique_ptr<Context>> m_retiredContexts;
    };

    ///
    /// Make all mako instances leaking.
    /// This is to workaround a bug in the Vulkan loader, which
    /// makes it impossible to destroy Vulkan instances and devices.
    ///
    void makeLeaking();

}
