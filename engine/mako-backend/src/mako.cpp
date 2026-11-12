/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "context.hpp"
#include "mako_fg.hpp"
#include "helpers/temporal_phases.hpp"
#include "helpers/black_image.hpp"
#include "helpers/frame_color_policy.hpp"
#include "mako-backend/device_identity.hpp"
#include "mako-backend/mako.hpp"
#include "extraction/dll_reader.hpp"
#include "extraction/shader_registry.hpp"
#include "helpers/limits.hpp"
#include "helpers/managed_shader.hpp"
#include "helpers/timestamp_upload_cache.hpp"
#include "helpers/utils.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/pointers.hpp"
#include "mako-common/helpers/file_descriptors.hpp"
#include "mako-common/vulkan/buffer.hpp"
#include "mako-common/vulkan/command_buffer.hpp"
#include "mako-common/vulkan/fence.hpp"
#include "mako-common/vulkan/external_image.hpp"
#include "mako-common/vulkan/image.hpp"
#include "mako-common/vulkan/timeline_semaphore.hpp"
#include "mako-common/vulkan/timestamp_query_pool.hpp"
#include "mako-common/vulkan/vulkan.hpp"
#include "shaderchains/alpha0.hpp"
#include "shaderchains/alpha1.hpp"
#include "shaderchains/beta0.hpp"
#include "shaderchains/beta1.hpp"
#include "shaderchains/color_conversion.hpp"
#include "shaderchains/delta0.hpp"
#include "shaderchains/delta1.hpp"
#include "shaderchains/gamma0.hpp"
#include "shaderchains/gamma1.hpp"
#include "shaderchains/generate.hpp"
#include "shaderchains/mipmaps.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <vulkan/vulkan_core.h>

#ifdef MAKO_TESTING_RENDERDOC
#include <renderdoc_app.h>
#include <dlfcn.h>
#endif

using namespace mako;
using namespace mako::backend;

namespace mako::backend {
    struct FrameProfilingState {
        FrameProfilingState(const vk::Vulkan& vk, const size_t outputs) :
            // Three prepass boundaries, batch completion, and three per output.
            // Round up for the pair-sized pool; readback excludes the spare slot.
            queries(vk, static_cast<uint32_t>(((4 + outputs * 3 + 1) / 2) * 2)) {}
        vk::TimestampQueryPool queries;
        FrameProfile timings;
        size_t generatedCount{};
    };

    using ProfileClock = std::chrono::steady_clock;
    double profileMicroseconds(const ProfileClock::time_point start) {
        return std::chrono::duration<double, std::micro>(ProfileClock::now() - start).count();
    }
    error::error(const std::string& msg, const std::exception& inner)
        : std::runtime_error(msg + "\n- " + inner.what()) {}
    error::error(const std::string& msg)
        : std::runtime_error(msg) {}
    error::~error() = default;

    /// instance class
    class InstanceImpl {
    public:
        /// create an instance
        /// (see mako documentation)
        InstanceImpl(vk::PhysicalDeviceSelector selectPhysicalDevice,
            const std::filesystem::path& shaderDllPath,
            bool allowLowPrecision);

        /// get the Vulkan instance
        /// @return the Vulkan instance
        [[nodiscard]] const auto& getVulkan() const { return this->vk; }
        /// get the shader registry
        /// @return the shader registry
        [[nodiscard]] const auto& getShaderRegistry() const { return this->shaders; }
#ifdef MAKO_TESTING_RENDERDOC
        /// get the RenderDoc API
        /// @return the RenderDoc API
        [[nodiscard]] const auto& getRenderDocAPI() const { return this->renderdoc; }
#endif
        // Movable, non-copyable, custom destructor
        InstanceImpl(const InstanceImpl&) = delete;
        InstanceImpl& operator=(const InstanceImpl&) = delete;
        InstanceImpl(InstanceImpl&&) = default;
        InstanceImpl& operator=(InstanceImpl&&) = default;
        ~InstanceImpl();
    private:
        vk::Vulkan vk;
        ShaderRegistry shaders;

#ifdef MAKO_TESTING_RENDERDOC
        std::optional<RENDERDOC_API_1_6_0> renderdoc;
#endif
    };

    /// context class
    class LSFGContext final : public ContextImpl {
    public:
        /// create a context
        /// (see mako documentation)
        LSFGContext(const InstanceImpl& instance,
            ls::FileDescriptorScope& sourceFds, ls::FileDescriptorScope& destFds,
            ls::FileDescriptorScope& syncFd, VkExtent2D extent, FrameEncoding encoding, float flow, bool perf, bool reducedHdrPrecision);

        // Borrowed scratch and cached descriptors require stable context owners.
        LSFGContext(const LSFGContext&) = delete;
        LSFGContext& operator=(const LSFGContext&) = delete;
        LSFGContext(LSFGContext&&) = delete;
        LSFGContext& operator=(LSFGContext&&) = delete;

        /// schedule frames
        /// (see mako documentation)
        void scheduleFrames();

        void enableFrameProfiling();
        [[nodiscard]] FrameProfile readFrameProfile() const;

        /// schedule a variable number of frames at explicit timestamps
        void scheduleFrames(std::span<const float> timestamps);
        /// LSFG takes no real-frame interval.
        void scheduleFrames(std::span<const float> timestamps, std::chrono::nanoseconds) override {
            this->scheduleFrames(timestamps);
        }

        /// update temporal history without generating output frames
        /// (see mako documentation)
        void scheduleFrameHistory();
        void scheduleFrameHistory(std::chrono::nanoseconds) override {
            this->scheduleFrameHistory();
        }

        /// wait for this context's most recently submitted work only
        [[nodiscard]] bool waitForIdle(uint64_t timeoutNs) const;
    private:
        std::pair<vk::Image, vk::Image> sourceImages;
        std::vector<vk::Image> destImages;
        std::optional<std::pair<vk::Image, vk::Image>> workingSourceImages;
        std::optional<std::vector<vk::Image>> workingDestImages;
        vk::Image blackImage;

        vk::TimelineSemaphore syncSemaphore; // imported
        vk::TimelineSemaphore prepassSemaphore;
        size_t idx{1};
        size_t fidx{0}; // real frame index
        bool workScheduled{false};

        // Descriptor bindings repeat every lcm(2 source slots, 3 history slots).
        // Record lazily, once per phase and output; prepareWork serializes reuse.
        // Declared before recordings so query pools outlive referring commands.
        std::unique_ptr<FrameProfilingState> profiling;
        using RecordedCommands = std::array<std::optional<vk::CommandBuffer>, commandPhaseCount>;
        std::vector<RecordedCommands> cmdbufs;
        std::optional<vk::CommandBuffer> prepassDependency;
        std::optional<vk::CommandBuffer> sourceRelease;
        vk::Fence cmdbufFence;
        std::vector<float> constantBufferTimestamps;

        void prepareWork();
        const vk::CommandBuffer& recordPrepass();
        const vk::CommandBuffer& recordSourceRelease();
        void schedulePrepass(VkFence completionFence);

        Ctx ctx;

        std::optional<ColorConversion> sourceColorConversion;
        std::optional<ColorConversion> destColorConversion;
        Mipmaps mipmaps;
        std::array<Alpha0, 7> alpha0;
        std::array<Alpha1, 7> alpha1;
        Beta0 beta0;
        Beta1 beta1;
        struct Pass {
            std::vector<Gamma0> gamma0;
            std::vector<Gamma1> gamma1;

            std::vector<Delta0> delta0;
            std::vector<Delta1> delta1;
            ls::lazy<Generate> generate;
        };
        std::vector<Pass> passes;
    };
}

Instance::Instance(
        const DevicePicker& devicePicker,
        const std::filesystem::path& shaderDllPath,
        bool allowLowPrecision,
        std::optional<std::filesystem::path> modelPackage,
        const std::optional<DriverIdentity>& gameDriver) {
    if (modelPackage) {
#ifdef MAKO_FG
        this->m_fg = std::make_unique<MakoFG>(devicePicker, *modelPackage, allowLowPrecision, gameDriver);
        return;
#else
        throw backend::error("This MAKO Renderer build has no MAKO FG support");
#endif
    }
    const auto selectFunc = [&devicePicker](const vk::VulkanInstanceFuncs funcs,
            const std::vector<VkPhysicalDevice>& devices) {
        for (const auto& device : devices) {
            // check if the physical device supports VK_EXT_pci_bus_info
            uint32_t ext_count{};
            funcs.EnumerateDeviceExtensionProperties(device, nullptr, &ext_count, VK_NULL_HANDLE);

            std::vector<VkExtensionProperties> extensions(ext_count);
            funcs.EnumerateDeviceExtensionProperties(device, nullptr, &ext_count, extensions.data());

            const bool has_pci_ext = std::ranges::find_if(extensions,
                [](const VkExtensionProperties& ext) {
                    return std::string(std::to_array(ext.extensionName).data())
                        == VK_EXT_PCI_BUS_INFO_EXTENSION_NAME;
                }) != extensions.end();

            // then fetch all available properties
            VkPhysicalDevicePCIBusInfoPropertiesEXT pciInfo{
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT
            };
            VkPhysicalDeviceProperties2 props{
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                .pNext = has_pci_ext ? &pciInfo : nullptr
            };
            funcs.GetPhysicalDeviceProperties2(device, &props);

            std::array<char, 256> devname = std::to_array(props.properties.deviceName);
            devname.at(255) = '\0'; // ensure null-termination

            if (devicePicker(
                std::string(devname.data()),
                { backend::deviceIdText(props.properties.vendorID),
                  backend::deviceIdText(props.properties.deviceID) },
                has_pci_ext ? std::optional<std::string>{
                    backend::pciAddressText(pciInfo.pciBus, pciInfo.pciDevice,
                        pciInfo.pciFunction)
                } : std::nullopt
            ))
                return device;
        }

        throw ls::vulkan_error("no suitable physical device found");
    };

    this->m_impl = std::make_unique<InstanceImpl>(
        selectFunc, shaderDllPath, allowLowPrecision
    );
}

bool Instance::supportsPackedHdr10Transport() const {
#ifdef MAKO_FG
    // MAKO FG imports and converts packed HDR10 on its own device.
    if (this->m_fg)
        return this->m_fg->supportsPackedHdr10();
#endif
    const auto& vk = this->m_impl->getVulkan();
    constexpr VkImageUsageFlags usage =
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    return this->m_impl->getShaderRegistry().scrgb_to_hdr10_pq_packed.has_value() &&
        vk.supportsStorageImageExtendedFormats() &&
        vk.supportsExternalImageFormat(
            VK_FORMAT_A2B10G10R10_UNORM_PACK32,
            usage,
            VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT
        );
}

namespace {
    constexpr auto previousWorkFenceTimeout = std::chrono::milliseconds(250);
    constexpr uint64_t previousWorkFenceTimeoutNs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            previousWorkFenceTimeout
        ).count()
    );
    constexpr size_t maximumRetiredContextCount = 2;

    vk::DeviceMemoryTotals memoryDelta(
            const vk::DeviceMemoryTotals& after,
            const vk::DeviceMemoryTotals& before) {
        return {
            .bytes = after.bytes >= before.bytes
                ? after.bytes - before.bytes
                : 0,
            .allocations = after.allocations >= before.allocations
                ? after.allocations - before.allocations
                : 0,
        };
    }

    /// find the cache file path
    std::filesystem::path findCacheFilePath() {
        const char* xdgCacheHome = std::getenv("XDG_CACHE_HOME");
        if (xdgCacheHome && *xdgCacheHome != '\0')
            return std::filesystem::path(xdgCacheHome) / "mako_pipeline_cache.bin";

        const char* home = std::getenv("HOME");
        if (home && *home != '\0')
            return std::filesystem::path(home) / ".cache" / "mako_pipeline_cache.bin";

        return{"/tmp/mako_pipeline_cache.bin"};
    }
    /// create a Vulkan instance
    vk::Vulkan createVulkanInstance(vk::PhysicalDeviceSelector selectPhysicalDevice) {
        try {
            return{
                "mako", vk::version{2, 0, 0},
                "mako-engine", vk::version{2, 0, 0},
                selectPhysicalDevice,
                false, std::nullopt,
                findCacheFilePath()
            };
        } catch (const std::exception& e) {
            throw backend::error("Unable to initialize Vulkan", e);
        }
    }
    /// build a shader registry
    ShaderRegistry createShaderRegistry(vk::Vulkan& vk,
            const std::filesystem::path& shaderDllPath,
            bool allowLowPrecision) {
        std::shared_ptr<const backend::DllResourceArchive> archive;

        try {
            archive = backend::loadDllResourceArchive(shaderDllPath);
        } catch (const std::exception& e) {
            throw backend::error("Unable to parse Lossless Scaling DLL", e);
        }

        try {
            if (allowLowPrecision && !vk.supportsFP16())
                throw ls::error("LSFG FP16 requested but shaderFloat16 is unavailable on the selected GPU; select FP32");
            const bool fp16 = allowLowPrecision;
            auto registry = backend::buildShaderRegistry(
                vk, fp16, *archive, shaderDllPath
            );
            std::clog << "MAKO Renderer: model DLL inspected: component=lsfg"
                      << "; dll_sha256=" << archive->fileSha256
                      << "; resource_layout_sha256="
                      << archive->resourceLayoutSha256
                      << "; resource_count=" << archive->resources.size()
                      << "; precision=" << (registry.is_fp16 ? "fp16" : "fp32")
                      << "; compatibility=structural-and-vulkan" << '\n';
            return registry;
        } catch (const std::exception& e) {
            throw backend::error("Unable to build shader registry", e);
        }
    }
#ifdef MAKO_TESTING_RENDERDOC
    /// load RenderDoc integration
    std::optional<RENDERDOC_API_1_6_0> loadRenderDocIntegration() {
        void* module = dlopen("librenderdoc.so", RTLD_NOW | RTLD_NOLOAD);
        if (!module)
            return std::nullopt;

        auto renderdocGetAPI = reinterpret_cast<pRENDERDOC_GetAPI>(
            dlsym(module, "RENDERDOC_GetAPI"));
        if (!renderdocGetAPI)
            return std::nullopt;

        RENDERDOC_API_1_6_0* api{};
        renderdocGetAPI(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void**>(&api));
        if (!api)
            return std::nullopt;

        return *api;
    }
#endif
}

InstanceImpl::InstanceImpl(vk::PhysicalDeviceSelector selectPhysicalDevice,
            const std::filesystem::path& shaderDllPath,
            bool allowLowPrecision)
        : vk(createVulkanInstance(selectPhysicalDevice)),
        shaders(createShaderRegistry(this->vk, shaderDllPath,
            allowLowPrecision && vk.supportsFP16())) {
#ifdef MAKO_TESTING_RENDERDOC
    this->renderdoc = loadRenderDocIntegration();
#endif
    std::clog << "MAKO Renderer: robustImageAccess2 "
        << (vk.supportsRobustImageAccess2() ? "enabled" : "unavailable")
        << '\n';
    vk.persistPipelineCache(); // will silently fail
}

Context& Instance::openContext(std::pair<int, int> sourceFds, const std::vector<int>& destFds,
        int syncFd, uint32_t width, uint32_t height,
        const FrameEncoding encoding, float flow, bool perf, bool reducedHdrPrecision,
        const int motionPeriod) {
    // Take the complete batch before any operation that can fail. Individual
    // import wrappers consume their descriptor even on failure; unattempted
    // descriptors remain here until construction succeeds or unwinds.
    const std::array sourceDescriptors{sourceFds.first, sourceFds.second};
    ls::FileDescriptorScope sourceScope{sourceDescriptors};
    ls::FileDescriptorScope destinationScope{destFds};
    ls::FileDescriptorScope syncScope{{&syncFd, 1}};
    this->collectRetiredContexts();
    if (this->m_retiredContexts.size() >= maximumRetiredContextCount) {
        throw backend::error(
            "Too many frame-generation contexts are still retiring"
        );
    }
#ifdef MAKO_FG
    if (this->m_fg) {
        auto context = this->m_fg->open(sourceScope, destinationScope, syncScope, width, height, encoding, flow, motionPeriod);
        return *this->m_contexts.emplace_back(std::move(context));
    }
#endif
    const VkExtent2D extent{ width, height };
    const auto& vulkan = this->m_impl->getVulkan();
    const auto before = vulkan.deviceMemorySnapshot();
    const float modelFlow = generationFlowFactor(encoding, reducedHdrPrecision, flow);
    auto context = std::make_unique<LSFGContext>(*this->m_impl,
        sourceScope, destinationScope, syncScope,
        extent, encoding, modelFlow, perf, reducedHdrPrecision
    );
    if (encoding == FrameEncoding::Hdr10Pq || encoding == FrameEncoding::Hdr10PqPacked ||
            encoding == FrameEncoding::ScRgbLinear) {
        std::clog << "MAKO Renderer: HDR generation precision: reduced_requested="
            << reducedHdrPrecision << "; model_space="
            << (usesReducedHdrGeneration(encoding, reducedHdrPrecision) ? "pq-code-values" : "linear-scrgb")
            << "; pq_conversion=" << requiresPqConversion(encoding, reducedHdrPrecision)
            << "; requested_flow_scale=" << 1.0F / flow
            << "; motion_flow_scale=" << 1.0F / modelFlow
            << "; motion_width=" << static_cast<uint32_t>(static_cast<float>(width) / modelFlow)
            << "; motion_height=" << static_cast<uint32_t>(static_cast<float>(height) / modelFlow)
            << '\n';
    }
    const auto after = vulkan.deviceMemorySnapshot();
    const auto contextInternal = memoryDelta(after.internal, before.internal);
    const auto contextImported = memoryDelta(after.imported, before.imported);
    const auto contextExported = memoryDelta(after.exported, before.exported);
    auto* result = this->m_contexts.emplace_back(std::move(context)).get();
    std::clog << "MAKO Renderer: backend-memory operation=context-open"
        << " width=" << width
        << " height=" << height
        << " outputs=" << destFds.size()
        << " precision="
        << (this->m_impl->getShaderRegistry().is_fp16 ? "fp16" : "fp32")
        << " model=" << (perf ? "performance" : "quality")
        << " context_internal_bytes=" << contextInternal.bytes
        << " context_internal_allocations=" << contextInternal.allocations
        << " context_imported_mapped_bytes=" << contextImported.bytes
        << " context_imported_allocations=" << contextImported.allocations
        << " context_exported_bytes=" << contextExported.bytes
        << " context_exported_allocations=" << contextExported.allocations
        << " live_internal_bytes=" << after.internal.bytes
        << " live_internal_allocations=" << after.internal.allocations
        << " peak_internal_bytes=" << after.peakInternal.bytes
        << " peak_internal_allocations=" << after.peakInternal.allocations
        << '\n';
    return *result;
}

namespace {
    /// import source images
    std::pair<vk::Image, vk::Image> importSourceImages(const vk::Vulkan& vk,
            ls::FileDescriptorScope& sourceFds,
            VkExtent2D extent, VkFormat format) {
        try {
            return {
                vk::Image(vk, extent, format,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, sourceFds.take()),
                vk::Image(vk, extent, format,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, sourceFds.take())
            };
        } catch (const std::exception& e) {
            throw backend::error("Unable to import destination images", e);
        }
    }
    /// import destination images
    std::vector<vk::Image> importDestinationImages(const vk::Vulkan& vk,
            ls::FileDescriptorScope& destFds,
            VkExtent2D extent, VkFormat format) {
        try {
            std::vector<vk::Image> destImages;
            destImages.reserve(destFds.size());

            for (size_t index = 0; index < destFds.size(); ++index)
                destImages.emplace_back(vk, extent, format,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, destFds.take());

            return destImages;
        } catch (const std::exception& e) {
            throw backend::error("Unable to import destination images", e);
        }
    }
    /// create a black image
    vk::Image createBlackImage(const vk::Vulkan& vk) {
        try {
            return{vk,
                { .width = 4, .height = 4 }, VK_FORMAT_R8G8B8A8_UNORM,
                backend::blackImageUsage
            };
        } catch (const std::exception& e) {
            throw backend::error("Unable to create black image", e);
        }
    }
    /// import timeline semaphore
    vk::TimelineSemaphore importTimelineSemaphore(const vk::Vulkan& vk, int syncFd) {
        try {
            return{vk, 0, syncFd};
        } catch (const std::exception& e) {
            throw backend::error("Unable to import timeline semaphore", e);
        }
    }
    /// create prepass semaphore
    vk::TimelineSemaphore createPrepassSemaphore(const vk::Vulkan& vk) {
        try {
            return{vk, 0};
        } catch (const std::exception& e) {
            throw backend::error("Unable to create prepass semaphore", e);
        }
    }
    /// create context data
    bool usesHighPrecisionModel(const FrameEncoding encoding) {
        return encoding != FrameEncoding::Sdr8;
    }

    bool usesPackedHdr10Transport(const FrameEncoding encoding) {
        return encoding == FrameEncoding::Hdr10PqPacked;
    }

    VkFormat transportFormat(const FrameEncoding encoding) {
        if (encoding == FrameEncoding::Sdr8)
            return VK_FORMAT_R8G8B8A8_UNORM;
        if (usesPackedHdr10Transport(encoding))
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    }

    std::optional<std::pair<vk::Image, vk::Image>> createWorkingSourceImages(
            const vk::Vulkan& vk, const VkExtent2D extent,
            const FrameEncoding encoding, const bool reduced) {
        if (!requiresPqConversion(encoding, reduced))
            return std::nullopt;

        return std::make_optional(std::pair{
            vk::Image(vk, extent, VK_FORMAT_R16G16B16A16_SFLOAT),
            vk::Image(vk, extent, VK_FORMAT_R16G16B16A16_SFLOAT)
        });
    }

    std::optional<std::vector<vk::Image>> createWorkingDestImages(
            const vk::Vulkan& vk, const VkExtent2D extent,
            const FrameEncoding encoding, const size_t count, const bool reduced) {
        if (!requiresPqConversion(encoding, reduced))
            return std::nullopt;

        std::vector<vk::Image> images;
        images.reserve(count);
        for (size_t i = 0; i < count; ++i)
            images.emplace_back(vk, extent, VK_FORMAT_R16G16B16A16_SFLOAT);
        return images;
    }

    const std::pair<vk::Image, vk::Image>& selectModelSourceImages(
            const std::pair<vk::Image, vk::Image>& transportImages,
            const std::optional<std::pair<vk::Image, vk::Image>>& workingImages) {
        return workingImages ? *workingImages : transportImages;
    }

    const std::vector<vk::Image>& selectModelDestImages(
            const std::vector<vk::Image>& transportImages,
            const std::optional<std::vector<vk::Image>>& workingImages) {
        return workingImages ? *workingImages : transportImages;
    }

    std::optional<ColorConversion> createSourceColorConversion(
            const Ctx& ctx,
            const std::pair<vk::Image, vk::Image>& transportImages,
            const std::optional<std::pair<vk::Image, vk::Image>>& workingImages) {
        if (!workingImages)
            return std::nullopt;
        return std::optional<ColorConversion>(std::in_place,
            ctx, ctx.shaders.get().hdr10_pq_to_scrgb,
            transportImages, workingImages.value()
        );
    }

    std::optional<ColorConversion> createDestColorConversion(
            const Ctx& ctx, const ShaderRegistry& shaders,
            const FrameEncoding encoding,
            const std::optional<std::vector<vk::Image>>& workingImages,
            const std::vector<vk::Image>& transportImages) {
        if (!workingImages)
            return std::nullopt;
        return std::optional<ColorConversion>(std::in_place,
            ctx, usesPackedHdr10Transport(encoding)
                ? shaders.scrgb_to_hdr10_pq_packed.value()
                : shaders.scrgb_to_hdr10_pq,
            workingImages.value(), transportImages
        );
    }

    Ctx createCtx(const InstanceImpl& instance, VkExtent2D extent,
            const FrameEncoding encoding, float flow, bool perf, size_t count, bool reduced) {
        const auto& vk = instance.getVulkan();
        const auto& shaders = instance.getShaderRegistry();

        try {
            std::vector<vk::Buffer> constantBuffers{};
            constantBuffers.reserve(count);

            for (size_t i = 0; i < count; ++i)
                constantBuffers.emplace_back(vk,
                    backend::getDefaultConstantBuffer(
                        i, count, usesHdrModel(encoding, reduced), flow
                    )
                );

            return {
                .vk = std::ref(vk),
                .shaders = std::ref(shaders),
                .imageMemoryPool{std::make_shared<vk::ImageMemoryPool>(vk)},
                .pool{vk, backend::calculateDescriptorPoolLimits(
                    count, perf, requiresPqConversion(encoding, reduced)
                )},
                .constantBuffer{vk, backend::getDefaultConstantBuffer(
                    0, 1, usesHdrModel(encoding, reduced), flow
                )},
                .constantBuffers{std::move(constantBuffers)},
                .bnbSampler{vk, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_COMPARE_OP_NEVER, false},
                .bnwSampler{vk, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_COMPARE_OP_NEVER, true},
                .eabSampler{vk, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_COMPARE_OP_ALWAYS, false},
                .sourceExtent = extent,
                .flowExtent = VkExtent2D {
                    .width = static_cast<uint32_t>(static_cast<float>(extent.width) / flow),
                    .height = static_cast<uint32_t>(static_cast<float>(extent.height) / flow)
                },
                .packedPqOutputShader = usesReducedHdrGeneration(encoding, reduced) &&
                        usesPackedHdr10Transport(encoding)
                    ? std::optional<vk::Shader>(shaders.packedPqShader(vk))
                    : std::nullopt,
                .highPrecision = usesHighPrecisionModel(encoding),
                .hdr = usesHdrModel(encoding, reduced),
                .flow = flow,
                .perf = perf,
                .count = count
            };
        } catch (const std::exception& e) {
            throw backend::error("Unable to create context", e);
        }
    }
}

LSFGContext::LSFGContext(const InstanceImpl& instance,
            ls::FileDescriptorScope& sourceFds, ls::FileDescriptorScope& destFds,
            ls::FileDescriptorScope& syncFd, VkExtent2D extent, const FrameEncoding encoding, float flow, bool perf, bool reducedHdrPrecision) :
        sourceImages(importSourceImages(instance.getVulkan(), sourceFds,
            extent, transportFormat(encoding))),
        destImages(importDestinationImages(instance.getVulkan(), destFds,
            extent, transportFormat(encoding))),
        workingSourceImages(createWorkingSourceImages(
            instance.getVulkan(), extent, encoding, reducedHdrPrecision
        )),
        workingDestImages(createWorkingDestImages(
            instance.getVulkan(), extent, encoding, destFds.size(), reducedHdrPrecision
        )),
        blackImage(createBlackImage(instance.getVulkan())),
        syncSemaphore(importTimelineSemaphore(instance.getVulkan(), syncFd.take())),
        prepassSemaphore(createPrepassSemaphore(instance.getVulkan())),
        cmdbufs(destFds.size() + 1),
        cmdbufFence(instance.getVulkan()),
        ctx(createCtx(instance, extent, encoding, flow, perf, destFds.size(), reducedHdrPrecision)),
        sourceColorConversion(createSourceColorConversion(
            ctx, sourceImages, workingSourceImages
        )),
        destColorConversion(createDestColorConversion(
            ctx, instance.getShaderRegistry(), encoding,
            workingDestImages, destImages
        )),
        mipmaps(ctx, selectModelSourceImages(sourceImages, workingSourceImages)),
        alpha0{
            Alpha0(ctx, mipmaps.getImages().at(0)),
            Alpha0(ctx, mipmaps.getImages().at(1)),
            Alpha0(ctx, mipmaps.getImages().at(2)),
            Alpha0(ctx, mipmaps.getImages().at(3)),
            Alpha0(ctx, mipmaps.getImages().at(4)),
            Alpha0(ctx, mipmaps.getImages().at(5)),
            Alpha0(ctx, mipmaps.getImages().at(6))
        },
        alpha1{
            Alpha1(ctx, alphaHistoryCounts.at(0), alpha0.at(0).getImages()),
            Alpha1(ctx, alphaHistoryCounts.at(1), alpha0.at(1).getImages()),
            Alpha1(ctx, alphaHistoryCounts.at(2), alpha0.at(2).getImages()),
            Alpha1(ctx, alphaHistoryCounts.at(3), alpha0.at(3).getImages()),
            Alpha1(ctx, alphaHistoryCounts.at(4), alpha0.at(4).getImages()),
            Alpha1(ctx, alphaHistoryCounts.at(5), alpha0.at(5).getImages()),
            Alpha1(ctx, alphaHistoryCounts.at(6), alpha0.at(6).getImages())
        },
        beta0(ctx, alpha1.at(0).getImages()),
        beta1(ctx, beta0.getImages(), alpha0.at(0).getImages()) {
    const auto& modelSourceImages = selectModelSourceImages(
        this->sourceImages, this->workingSourceImages
    );
    const auto& modelDestImages = selectModelDestImages(
        this->destImages, this->workingDestImages
    );

    // Generated outputs run sequentially on the same compute queue. Keep
    // descriptors/uniforms per output, but share the intermediate images.
    // Compute dependencies order the previous readers and writers.
    // build main passes
    for (size_t i = 0; i < destImages.size(); ++i) {
        auto& pass = this->passes.emplace_back();

        pass.gamma0.reserve(7);
        pass.gamma1.reserve(7);
        pass.delta0.reserve(3);
        pass.delta1.reserve(3);
        for (size_t j = 0; j < 7; j++) {
            if (j == 0) { // first pass has no prior data
                pass.gamma0.emplace_back(ctx, i,
                    this->alpha1.at(6 - j).getImages(),
                    this->blackImage,
                    i == 0 ? nullptr : &this->passes.front().gamma0.at(j)
                );
                pass.gamma1.emplace_back(ctx, i,
                    pass.gamma0.at(j).getImages(),
                    this->alpha0.at(6 - j).getImages(),
                    this->blackImage,
                    this->beta1.getImages().at(5),
                    i == 0 ? nullptr : &this->passes.front().gamma1.at(j)
                );
            } else { // other passes use prior data
                pass.gamma0.emplace_back(ctx, i,
                    this->alpha1.at(6 - j).getImages(),
                    pass.gamma1.at(j - 1).getImage(),
                    i == 0 ? nullptr : &this->passes.front().gamma0.at(j)
                );
                pass.gamma1.emplace_back(ctx, i,
                    pass.gamma0.at(j).getImages(),
                    this->alpha0.at(6 - j).getImages(),
                    pass.gamma1.at(j - 1).getImage(),
                    this->beta1.getImages().at(6 - j),
                    i == 0 ? nullptr : &this->passes.front().gamma1.at(j)
                );
            }

            if (j == 4) { // first special pass has no prior data
                pass.delta0.emplace_back(ctx, i,
                    this->alpha1.at(6 - j).getImages(),
                    pass.gamma0.at(j).getImages(),
                    pass.gamma1.at(j).getTempImages0(),
                    this->blackImage,
                    pass.gamma1.at(j - 1).getImage()
                );
                pass.delta1.emplace_back(ctx, i,
                    pass.gamma0.at(j).getImages(),
                    pass.gamma1.at(j).getTempImages0(),
                    pass.gamma1.at(j).getTempImages1(),
                    this->blackImage,
                    this->beta1.getImages().at(6 - j),
                    this->blackImage,
                    i == 0 ? nullptr : &this->passes.front().delta1.at(j - 4),
                    this->alpha0.at(7 - j).getTemporaryImages0(),
                    this->alpha0.at(7 - j).getTemporaryImages1()
                );
            } else if (j > 4) { // further passes do
                pass.delta0.emplace_back(ctx, i,
                    this->alpha1.at(6 - j).getImages(),
                    pass.gamma0.at(j).getImages(),
                    pass.gamma1.at(j).getTempImages0(),
                    pass.delta1.at(j - 5).getImage0(),
                    pass.gamma1.at(j - 1).getImage()
                );
                pass.delta1.emplace_back(ctx, i,
                    pass.gamma0.at(j).getImages(),
                    pass.gamma1.at(j).getTempImages0(),
                    pass.gamma1.at(j).getTempImages1(),
                    pass.delta1.at(j - 5).getImage0(),
                    this->beta1.getImages().at(6 - j),
                    pass.delta1.at(j - 5).getImage1(),
                    i == 0 ? nullptr : &this->passes.front().delta1.at(j - 4),
                    this->alpha0.at(7 - j).getTemporaryImages0(),
                    this->alpha0.at(7 - j).getTemporaryImages1()
                );
            }
        }

        pass.generate.emplace(ctx, i,
            modelSourceImages,
            pass.gamma1.at(6).getImage(),
            pass.delta1.at(2).getImage0(),
            pass.delta1.at(2).getImage1(),
            modelDestImages.at(i)
        );
    }

    // initialize all images
    std::vector<VkImage> images{};
    images.push_back(this->sourceImages.first.handle());
    images.push_back(this->sourceImages.second.handle());
    for (const auto& image : this->destImages)
        images.push_back(image.handle());
    if (this->workingSourceImages) {
        images.push_back(this->workingSourceImages->first.handle());
        images.push_back(this->workingSourceImages->second.handle());
    }
    if (this->workingDestImages) {
        for (const auto& image : *this->workingDestImages)
            images.push_back(image.handle());
    }
    mipmaps.prepare(images);
    for (size_t i = 0; i < 7; ++i) {
        alpha0.at(i).prepare(images);
        alpha1.at(i).prepare(images);
    }
    beta0.prepare(images);
    beta1.prepare(images);
    // Every output references the same scratch images: initialize once.
    if (!this->passes.empty()) {
        const auto& pass = this->passes.front();
        for (size_t i = 0; i < 7; ++i) {
            pass.gamma0.at(i).prepare(images);
            pass.gamma1.at(i).prepare(images);

            if (i < 4) continue;
            pass.delta1.at(i - 4).prepare(images);
        }
    }

    std::vector<vk::Barrier> barriers{};
    barriers.reserve(images.size());

    for (const auto& image : images) {
        barriers.emplace_back(vk::Barrier {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = 1,
                .layerCount = 1
            }
        });
    }

    const vk::CommandBuffer cmdbuf{ctx.vk};
    cmdbuf.begin(ctx.vk);
    cmdbuf.insertBarriers(ctx.vk, barriers);
    backend::initializeBlackImage(ctx.vk.get().df(), cmdbuf.handle(), this->blackImage.handle());
    // The exporter may overwrite either source before the first scheduled
    // frame. Leave both aliases in the documented external GENERAL state.
    for (const auto image : {this->sourceImages.first.handle(), this->sourceImages.second.handle()})
        vk::releaseExternalImage(ctx.vk, cmdbuf.handle(), image, 0,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    cmdbuf.end(ctx.vk);
    cmdbuf.submit(ctx.vk); // wait for completion

    this->constantBufferTimestamps.reserve(this->ctx.constantBuffers.size());
    for (size_t i = 0; i < this->ctx.constantBuffers.size(); ++i) {
        this->constantBufferTimestamps.push_back(
            backend::getDefaultConstantBuffer(
                i, this->ctx.constantBuffers.size(),
                this->ctx.hdr, this->ctx.flow
            ).timestamp
        );
    }
}

void Instance::scheduleFrames(Context& context) { // NOLINT (static)
#ifdef MAKO_TESTING_RENDERDOC
    const auto& impl = this->m_impl;
    if (impl && impl->getRenderDocAPI()) {
        impl->getRenderDocAPI()->StartFrameCapture(
            RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(impl->getVulkan().inst()),
            nullptr);
    }
#endif
    try {
        context.scheduleFrames();
    } catch (const std::exception& e) {
        throw backend::error("Unable to schedule frames", e);
    }
#ifdef MAKO_TESTING_RENDERDOC
    if (impl && impl->getRenderDocAPI()) {
        impl->getVulkan().df().DeviceWaitIdle(impl->getVulkan().dev());
        impl->getRenderDocAPI()->EndFrameCapture(
            RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(impl->getVulkan().inst()),
            nullptr);
    }
#endif
}

void Instance::enableFrameProfiling(Context& context) { // NOLINT (static)
    context.enableFrameProfiling();
}

FrameProfile Instance::readFrameProfile(const Context& context) const { // NOLINT (static)
    return context.readFrameProfile();
}

void LSFGContext::enableFrameProfiling() {
    if (this->fidx != 0 || this->workScheduled || this->profiling)
        throw backend::error("Frame profiling requires a fresh unscheduled context");
    this->profiling = std::make_unique<FrameProfilingState>(this->ctx.vk, this->destImages.size());
}

FrameProfile LSFGContext::readFrameProfile() const {
    if (!this->profiling || !this->workScheduled || this->profiling->generatedCount == 0)
        throw backend::error("No generated batch is available for frame profiling");
    const auto& state = *this->profiling;
    const auto ticks = state.queries.timestamps(static_cast<uint32_t>(4 + state.generatedCount * 3));
    auto result = state.timings;
    result.gpuPrepassUs = state.queries.elapsedMicroseconds(ticks.at(0), ticks.at(2));
    result.gpuInputConversionUs = this->sourceColorConversion
        ? state.queries.elapsedMicroseconds(ticks.at(0), ticks.at(1)) : 0.0;
    result.gpuMotionEstimationUs = state.queries.elapsedMicroseconds(
        ticks.at(this->sourceColorConversion ? 1 : 0), ticks.at(2));
    result.gpuGeneratedUs.reserve(state.generatedCount);
    result.gpuSynthesisUs.reserve(state.generatedCount);
    result.gpuOutputConversionUs.reserve(state.generatedCount);
    for (size_t i = 0; i < state.generatedCount; ++i) {
        const size_t begin = 4 + i * 3;
        result.gpuGeneratedUs.push_back(state.queries.elapsedMicroseconds(ticks.at(begin), ticks.at(begin + 2)));
        result.gpuSynthesisUs.push_back(state.queries.elapsedMicroseconds(
            ticks.at(begin), ticks.at(begin + (this->destColorConversion ? 1 : 2))));
        result.gpuOutputConversionUs.push_back(this->destColorConversion
            ? state.queries.elapsedMicroseconds(ticks.at(begin + 1), ticks.at(begin + 2)) : 0.0);
    }
    result.gpuSpanUs = state.queries.elapsedMicroseconds(ticks.front(), ticks.at(3));
    result.timestampValidBits = state.queries.timestampValidBits();
    result.timestampPeriodNs = state.queries.timestampPeriodNanoseconds();
    return result;
}

void Instance::scheduleFrames(Context& context, std::span<const float> timestamps, // NOLINT (static)
        const std::chrono::nanoseconds realFrameInterval) {
    if (timestamps.empty())
        throw backend::error("At least one interpolation timestamp is required");
#ifdef MAKO_TESTING_RENDERDOC
    const auto& impl = this->m_impl;
    if (impl && impl->getRenderDocAPI()) {
        impl->getRenderDocAPI()->StartFrameCapture(
            RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(impl->getVulkan().inst()),
            nullptr);
    }
#endif
    try {
        context.scheduleFrames(timestamps, realFrameInterval);
    } catch (const std::exception& e) {
        throw backend::error("Unable to schedule frames", e);
    }
#ifdef MAKO_TESTING_RENDERDOC
    if (impl && impl->getRenderDocAPI()) {
        impl->getVulkan().df().DeviceWaitIdle(impl->getVulkan().dev());
        impl->getRenderDocAPI()->EndFrameCapture(
            RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(impl->getVulkan().inst()),
            nullptr);
    }
#endif
}

void Instance::scheduleFrameHistory(Context& context, // NOLINT (static)
        const std::chrono::nanoseconds realFrameInterval) {
    context.scheduleFrameHistory(realFrameInterval);
}

bool Instance::contextReady(const Context& context) const {
    return context.waitForIdle(0);
}

void Instance::setContextLossHandler(Context& context, std::function<void()> handler) { // NOLINT (static)
    context.setLossHandler(std::move(handler));
}

bool Instance::contextLost(const Context& context) const {
    return context.lost();
}

bool Instance::restartFrameGeneration(const Context& context) {
#ifdef MAKO_FG
    if (this->m_fg)
        return this->m_fg->restart(context);
#endif
    static_cast<void>(context);
    return false;
}

bool Instance::usesSeparateProgram() const {
#ifdef MAKO_FG
    return this->m_fg != nullptr;
#else
    return false;
#endif
}

void LSFGContext::prepareWork() {
    const auto start = this->profiling ? ProfileClock::now() : ProfileClock::time_point{};
    if (this->workScheduled && !this->cmdbufFence.wait(
            this->ctx.vk, previousWorkFenceTimeoutNs)) {
        std::cerr << "MAKO Renderer: backend work fence timed out after "
                  << previousWorkFenceTimeout.count() << " ms; "
                     "aborting frame scheduling\n";
        throw backend::error("Timeout waiting for previous frame to complete");
    }
    if (this->profiling) {
        this->profiling->timings.cpuPreviousWaitUs = profileMicroseconds(start);
        this->profiling->timings.cpuGenerationSubmitUs = 0.0;
    }
}

const vk::CommandBuffer& LSFGContext::recordPrepass() {
    auto& recorded = this->cmdbufs.at(0).at(this->fidx % commandPhaseCount);
    if (!recorded) {
        vk::CommandBuffer cmdbuf(ctx.vk);
        cmdbuf.begin(ctx.vk, 0);

        if (this->profiling) {
            this->profiling->queries.reset(cmdbuf.handle());
            this->profiling->queries.write(cmdbuf.handle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0);
        }

        const std::array sourceAcquires{
            vk::externalImageAcquire(this->sourceImages.first.handle(), ctx.vk.get().queueFamilyIndex(), VK_ACCESS_SHADER_READ_BIT),
            vk::externalImageAcquire(this->sourceImages.second.handle(), ctx.vk.get().queueFamilyIndex(), VK_ACCESS_SHADER_READ_BIT),
        };
        ctx.vk.get().df().CmdPipelineBarrier(cmdbuf.handle(),
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(sourceAcquires.size()), sourceAcquires.data());

        if (this->sourceColorConversion)
            this->sourceColorConversion->render(ctx.vk, cmdbuf, this->fidx);
        if (this->profiling)
            this->profiling->queries.write(cmdbuf.handle(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 1);
        this->mipmaps.render(ctx.vk, cmdbuf, this->fidx);
        // Pyramid levels share no writable images. Record one dependency per
        // stage, allowing the seven levels to fill the GPU together.
        std::vector<ManagedDispatch> batch;
        batch.reserve(this->alpha0.size());
        for (size_t stage = 0; stage < 3; ++stage) {
            batch.clear();
            for (size_t i = 0; i < 7; ++i)
                batch.push_back(this->alpha0.at(6 - i).getDispatch(stage));
            ManagedShader::dispatchIndependent(ctx.vk, cmdbuf, batch);
        }
        batch.clear();
        for (size_t i = 0; i < 7; ++i)
            batch.push_back(this->alpha1.at(6 - i).getDispatch(this->fidx));
        ManagedShader::dispatchIndependent(ctx.vk, cmdbuf, batch);
        this->beta0.render(ctx.vk, cmdbuf, this->fidx);
        this->beta1.render(ctx.vk, cmdbuf);

        if (this->profiling)
            this->profiling->queries.write(cmdbuf.handle(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 2);

        cmdbuf.end(ctx.vk);
        recorded.emplace(std::move(cmdbuf));
    }
    return *recorded;
}

const vk::CommandBuffer& LSFGContext::recordSourceRelease() {
    if (!this->sourceRelease) {
        vk::CommandBuffer command(ctx.vk);
        command.begin(ctx.vk, 0);
        const std::array releases{
            vk::externalImageRelease(this->sourceImages.first.handle(), ctx.vk.get().queueFamilyIndex(), VK_ACCESS_SHADER_READ_BIT),
            vk::externalImageRelease(this->sourceImages.second.handle(), ctx.vk.get().queueFamilyIndex(), VK_ACCESS_SHADER_READ_BIT),
        };
        ctx.vk.get().df().CmdPipelineBarrier(command.handle(),
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0, 0, nullptr, 0, nullptr, static_cast<uint32_t>(releases.size()), releases.data());
        // Include the final ownership handoff in the profiled batch span.
        if (this->profiling)
            this->profiling->queries.write(command.handle(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 3);
        command.end(ctx.vk);
        this->sourceRelease.emplace(std::move(command));
    }
    return *this->sourceRelease;
}

void LSFGContext::schedulePrepass(const VkFence completionFence) {
    const auto& cmdbuf = this->recordPrepass();
    const std::array commands{cmdbuf.handle(), this->recordSourceRelease().handle()};
    // Preserve the completed fence while allocation/recording can still fail.
    // From the first submit onward, retain the context even if a later submit
    // fails before the final fence can be signalled.
    this->cmdbufFence.reset(this->ctx.vk);
    this->workScheduled = true;
    const auto start = this->profiling ? ProfileClock::now() : ProfileClock::time_point{};
    vk::CommandBuffer::submitCommands(this->ctx.vk,
        std::span{commands}.first(completionFence != VK_NULL_HANDLE ? 2 : 1),
        {}, this->syncSemaphore.handle(), this->idx,
        {}, this->prepassSemaphore.handle(), this->idx,
        completionFence
    );
    if (this->profiling)
        this->profiling->timings.cpuPrepassSubmitUs = profileMicroseconds(start);

    this->idx++;
}

void LSFGContext::scheduleFrames() {
    this->scheduleFrames({});
}

void LSFGContext::scheduleFrames(std::span<const float> timestamps) {
    const bool explicitTimestamps = !timestamps.empty();
    const size_t generatedFrameCount = explicitTimestamps
        ? timestamps.size()
        : this->destImages.size();
    if (generatedFrameCount > this->destImages.size())
        throw backend::error("Too many interpolation timestamps for this context");
    if (generatedFrameCount == 0) {
        this->scheduleFrameHistory();
        return;
    }

    float previousTimestamp = 0.0F;
    if (explicitTimestamps) {
        for (const float timestamp : timestamps) {
            if (timestamp <= previousTimestamp || timestamp >= 1.0F)
                throw backend::error(
                    "Interpolation timestamps must be strictly increasing between 0 and 1"
                );
            previousTimestamp = timestamp;
        }
    }

    this->prepareWork();
    if (this->profiling)
        this->profiling->generatedCount = generatedFrameCount;

    // Every generated pass has its own uniform buffer and descriptor sets.
    // Previous GPU work is complete at this point, so a changed timestamp can
    // be safely uploaded before submitting the next set of dispatches.
    if (explicitTimestamps) {
        for (size_t i = 0; i < timestamps.size(); ++i) {
            backend::uploadChangedTimestamp(
                this->constantBufferTimestamps, i, timestamps[i],
                [this, i](const float timestamp) {
                    auto constants = backend::getDefaultConstantBuffer(
                        0, 1, this->ctx.hdr, this->ctx.flow
                    );
                    constants.timestamp = timestamp;
                    this->ctx.constantBuffers.at(i).write(
                        this->ctx.vk, constants
                    );
                }
            );
        }
    }

    // Finish fallible allocation/recording before submitting any work. A cold
    // cache failure must not leave a prepass without its completion fence.
    const auto& sourceReleaseCommand = this->recordSourceRelease();
    for (size_t i = 0; i < generatedFrameCount; i++) {
        auto& recorded = this->cmdbufs.at(i + 1).at(this->fidx % commandPhaseCount);
        if (!recorded) {
            vk::CommandBuffer cmdbuf(ctx.vk);
            cmdbuf.begin(ctx.vk, 0);

            if (this->profiling)
                this->profiling->queries.write(cmdbuf.handle(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, static_cast<uint32_t>(4 + i * 3));

            // Every generated output is fully overwritten. Discarding its old
            // contents permits taking ownership without a consumer release.
            const VkImageMemoryBarrier discard{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_GENERAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = this->destImages.at(i).handle(),
                .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            };
            ctx.vk.get().df().CmdPipelineBarrier(cmdbuf.handle(),
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &discard);

            const auto& pass = this->passes.at(i);
            for (size_t j = 0; j < 7; j++) {
                pass.gamma0.at(j).render(ctx.vk, cmdbuf, this->fidx);
                pass.gamma1.at(j).render(ctx.vk, cmdbuf);

                if (j < 4) continue;
                pass.delta0.at(j - 4).render(ctx.vk, cmdbuf, this->fidx);
                pass.delta1.at(j - 4).render(ctx.vk, cmdbuf);
            }
            pass.generate->render(ctx.vk, cmdbuf, this->fidx);
            if (this->profiling)
                this->profiling->queries.write(cmdbuf.handle(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, static_cast<uint32_t>(5 + i * 3));
            if (this->destColorConversion)
                this->destColorConversion->render(ctx.vk, cmdbuf, i);

            vk::releaseExternalImage(ctx.vk, cmdbuf.handle(), this->destImages.at(i).handle(),
                VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

            if (this->profiling)
                this->profiling->queries.write(cmdbuf.handle(), VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, static_cast<uint32_t>(6 + i * 3));

            cmdbuf.end(ctx.vk);
            recorded.emplace(std::move(cmdbuf));
        }
    }

    if (generatedFrameCount == 1) {
        const auto& prepass = this->recordPrepass();
        if (!this->prepassDependency) {
            // Keep this dependency separate from the phase recordings so the
            // multiple-output path retains its existing barriers and overlap.
            vk::CommandBuffer dependency(ctx.vk);
            dependency.begin(ctx.vk, 0);
            const VkMemoryBarrier barrier{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            };
            ctx.vk.get().df().CmdPipelineBarrier(dependency.handle(),
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                0, 1, &barrier, 0, nullptr, 0, nullptr);
            dependency.end(ctx.vk);
            this->prepassDependency.emplace(std::move(dependency));
        }
        const std::array commands{
            prepass.handle(), this->prepassDependency->handle(),
            this->cmdbufs.at(1).at(this->fidx % commandPhaseCount)->handle(),
            sourceReleaseCommand.handle(),
        };
        // Finish fallible recording before resetting the completion fence.
        this->cmdbufFence.reset(this->ctx.vk);
        this->workScheduled = true;
        const auto start = this->profiling ? ProfileClock::now() : ProfileClock::time_point{};
        vk::CommandBuffer::submitCommands(this->ctx.vk, commands,
            {}, this->syncSemaphore.handle(), this->idx,
            {}, this->syncSemaphore.handle(), this->idx + 1, this->cmdbufFence.handle());
        if (this->profiling) {
            this->profiling->timings.cpuPrepassSubmitUs = 0.0;
            this->profiling->timings.cpuGenerationSubmitUs = profileMicroseconds(start);
        }
        this->idx += 2;
    } else {
        this->schedulePrepass(VK_NULL_HANDLE);
        const auto start = this->profiling ? ProfileClock::now() : ProfileClock::time_point{};
        for (size_t i = 0; i < generatedFrameCount; i++) {
            const auto& cmdbuf = *this->cmdbufs.at(i + 1).at(this->fidx % commandPhaseCount);
            const std::array commands{cmdbuf.handle(), sourceReleaseCommand.handle()};
            vk::CommandBuffer::submitCommands(this->ctx.vk,
                std::span{commands}.first(i == generatedFrameCount - 1 ? 2 : 1),
                {}, this->prepassSemaphore.handle(), this->idx - 1,
                {}, this->syncSemaphore.handle(), this->idx + i,
                i == generatedFrameCount - 1 ? this->cmdbufFence.handle() : VK_NULL_HANDLE);
        }
        if (this->profiling)
            this->profiling->timings.cpuGenerationSubmitUs = profileMicroseconds(start);
        this->idx += generatedFrameCount;
    }
    this->fidx++;
}

void LSFGContext::scheduleFrameHistory() {
    this->prepareWork();
    if (this->profiling)
        this->profiling->generatedCount = 0;
    this->schedulePrepass(this->cmdbufFence.handle());
    this->fidx++;
}

bool LSFGContext::waitForIdle(const uint64_t timeoutNs) const {
    return !this->workScheduled || this->cmdbufFence.wait(this->ctx.vk, timeoutNs);
}

void Instance::collectRetiredContexts() {
    std::erase_if(this->m_retiredContexts,
        [](const std::unique_ptr<ContextImpl>& context) {
            try {
                return context->lost() || context->waitForIdle(0);
            } catch (const std::exception& e) {
                std::cerr << "MAKO Renderer: unable to poll a retired backend context: "
                          << e.what() << '\n';
                return false;
            }
        });
}

void Instance::closeContext(const Context& context) {
    this->collectRetiredContexts();
    auto it = std::ranges::find_if(this->m_contexts,
        [context = &context](const std::unique_ptr<ContextImpl>& ctx) {
            return ctx.get() == context;
        });
    if (it == this->m_contexts.end())
        throw backend::error("attempted to close unknown context",
            std::runtime_error("no such context"));

    // The timeline this handler signals is destroyed with the caller's resources.
    (*it)->setLossHandler(nullptr);
    try {
        if ((*it)->lost() || (*it)->waitForIdle(previousWorkFenceTimeoutNs)) {
            this->m_contexts.erase(it);
            return;
        }
    } catch (const std::exception& e) {
        std::cerr << "MAKO Renderer: unable to wait for backend context retirement: "
                  << e.what() << '\n';
    }

    // Never issue a device-wide indefinite wait while the game is replacing a
    // swapchain. Keep the exceptional in-flight context alive and reclaim it
    // on a later open/close once its own completion fence signals.
    std::cerr << "MAKO Renderer: backend context did not retire within "
              << previousWorkFenceTimeout.count() << " ms; "
                 "deferring resource destruction\n";
    this->m_retiredContexts.push_back(std::move(*it));
    this->m_contexts.erase(it);
}

Instance::~Instance() = default;

std::shared_ptr<const vk::DeviceMemoryAccounting>
Instance::deviceMemoryAccounting() const {
    return this->m_impl ? this->m_impl->getVulkan().deviceMemoryAccounting() : nullptr;
}

// leaking shenanigans

namespace {
    bool leaking{false}; // NOLINT (global variable)
}

InstanceImpl::~InstanceImpl() {
    if (!leaking) return;

    try {
        new vk::Vulkan(std::move(this->vk));
    } catch (...) {
        std::cerr << "MAKO Renderer: failed to leak Vulkan instance\n";
    }

}

void backend::makeLeaking() {
    leaking = true;
}
