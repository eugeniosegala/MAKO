/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-backend/mako.hpp"
#include "extraction/dll_reader.hpp"
#include "extraction/shader_registry.hpp"
#include "helpers/limits.hpp"
#include "helpers/utils.hpp"
#include "mako-common/helpers/errors.hpp"
#include "mako-common/helpers/pointers.hpp"
#include "mako-common/vulkan/buffer.hpp"
#include "mako-common/vulkan/command_buffer.hpp"
#include "mako-common/vulkan/fence.hpp"
#include "mako-common/vulkan/image.hpp"
#include "mako-common/vulkan/timeline_semaphore.hpp"
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
    class ContextImpl {
    public:
        /// create a context
        /// (see mako documentation)
        ContextImpl(const InstanceImpl& instance,
            std::pair<int, int> sourceFds, const std::vector<int>& destFds, int syncFd,
            VkExtent2D extent, FrameEncoding encoding, float flow, bool perf);

        /// schedule frames
        /// (see mako documentation)
        void scheduleFrames();

        /// schedule a variable number of frames at explicit timestamps
        void scheduleFrames(std::span<const float> timestamps);

        /// update temporal history without generating output frames
        /// (see mako documentation)
        void scheduleFrameHistory();

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

        std::vector<vk::CommandBuffer> cmdbufs;
        vk::Fence cmdbufFence;

        void prepareWork();
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
        bool allowLowPrecision) {
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
                { backend::to_hex_id(props.properties.vendorID),
                  backend::to_hex_id(props.properties.deviceID) },
                has_pci_ext ? std::optional<std::string>{
                    std::to_string(pciInfo.pciBus) + ":" +
                    std::to_string(pciInfo.pciDevice) + "." +
                    std::to_string(pciInfo.pciFunction)
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
    constexpr uint64_t previousWorkFenceTimeoutNs = 250'000'000ULL;
    constexpr size_t maximumRetiredContextCount = 2;

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
        std::unordered_map<uint32_t, std::vector<uint8_t>> resources{};

        try {
            resources = backend::extractResourcesFromDLL(shaderDllPath);
        } catch (const std::exception& e) {
            throw backend::error("Unable to parse Lossless Scaling DLL", e);
        }

        try {
            return backend::buildShaderRegistry(
                vk, allowLowPrecision && vk.supportsFP16(),
                resources
            );
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
    vk.persistPipelineCache(); // will silently fail
}

Context& Instance::openContext(std::pair<int, int> sourceFds, const std::vector<int>& destFds,
        int syncFd, uint32_t width, uint32_t height,
        const FrameEncoding encoding, float flow, bool perf) {
    this->collectRetiredContexts();
    if (this->m_retiredContexts.size() >= maximumRetiredContextCount) {
        throw backend::error(
            "Too many frame-generation contexts are still retiring"
        );
    }
    const VkExtent2D extent{ width, height };
    return *this->m_contexts.emplace_back(std::make_unique<ContextImpl>(*this->m_impl,
        sourceFds, destFds, syncFd,
        extent, encoding, flow, perf
    )).get();
}

namespace {
    /// import source images
    std::pair<vk::Image, vk::Image> importImages(const vk::Vulkan& vk,
            const std::pair<int, int>& sourceFds,
            VkExtent2D extent, VkFormat format) {
        try {
            return {
                vk::Image(vk, extent, format,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, sourceFds.first),
                vk::Image(vk, extent, format,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, sourceFds.second)
            };
        } catch (const std::exception& e) {
            throw backend::error("Unable to import destination images", e);
        }
    }
    /// import destination images
    std::vector<vk::Image> importImages(const vk::Vulkan& vk,
            const std::vector<int>& destFds,
            VkExtent2D extent, VkFormat format) {
        try {
            std::vector<vk::Image> destImages;
            destImages.reserve(destFds.size());

            for (const auto& fd : destFds)
                destImages.emplace_back(vk, extent, format,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, fd);

            return destImages;
        } catch (const std::exception& e) {
            throw backend::error("Unable to import destination images", e);
        }
    }
    /// create a black image
    vk::Image createBlackImage(const vk::Vulkan& vk) {
        try {
            return{vk,
                { .width = 4, .height = 4 }
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
    /// create prepass semaphores
    vk::TimelineSemaphore createPrepassSemaphore(const vk::Vulkan& vk) {
        try {
            return{vk, 0};
        } catch (const std::exception& e) {
            throw backend::error("Unable to create prepass semaphore", e);
        }
    }
    /// create command buffers
    std::vector<vk::CommandBuffer> createCommandBuffers(const vk::Vulkan& vk, size_t count) {
        try {
            std::vector<vk::CommandBuffer> cmdbufs;
            cmdbufs.reserve(count);

            for (size_t i = 0; i < count; ++i)
                cmdbufs.emplace_back(vk);

            return cmdbufs;
        } catch (const std::exception& e) {
            throw backend::error("Unable to create command buffers", e);
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

    bool usesHdrModel(const FrameEncoding encoding) {
        return encoding == FrameEncoding::ScRgbLinear ||
            encoding == FrameEncoding::Hdr10Pq ||
            encoding == FrameEncoding::Hdr10PqPacked;
    }

    bool requiresPqConversion(const FrameEncoding encoding) {
        return encoding == FrameEncoding::Hdr10Pq ||
            encoding == FrameEncoding::Hdr10PqPacked;
    }

    std::optional<std::pair<vk::Image, vk::Image>> createWorkingSourceImages(
            const vk::Vulkan& vk, const VkExtent2D extent,
            const FrameEncoding encoding) {
        if (!requiresPqConversion(encoding))
            return std::nullopt;

        return std::make_optional(std::pair{
            vk::Image(vk, extent, VK_FORMAT_R16G16B16A16_SFLOAT),
            vk::Image(vk, extent, VK_FORMAT_R16G16B16A16_SFLOAT)
        });
    }

    std::optional<std::vector<vk::Image>> createWorkingDestImages(
            const vk::Vulkan& vk, const VkExtent2D extent,
            const FrameEncoding encoding, const size_t count) {
        if (!requiresPqConversion(encoding))
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
            const Ctx& ctx, const FrameEncoding encoding,
            const std::pair<vk::Image, vk::Image>& transportImages,
            const std::optional<std::pair<vk::Image, vk::Image>>& workingImages) {
        if (!requiresPqConversion(encoding))
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
        if (!requiresPqConversion(encoding))
            return std::nullopt;
        return std::optional<ColorConversion>(std::in_place,
            ctx, usesPackedHdr10Transport(encoding)
                ? shaders.scrgb_to_hdr10_pq_packed.value()
                : shaders.scrgb_to_hdr10_pq,
            workingImages.value(), transportImages
        );
    }

    Ctx createCtx(const InstanceImpl& instance, VkExtent2D extent,
            const FrameEncoding encoding, float flow, bool perf, size_t count) {
        const auto& vk = instance.getVulkan();
        const auto& shaders = instance.getShaderRegistry();

        try {
            std::vector<vk::Buffer> constantBuffers{};
            constantBuffers.reserve(count);

            for (size_t i = 0; i < count; ++i)
                constantBuffers.emplace_back(vk,
                    backend::getDefaultConstantBuffer(
                        i, count, usesHdrModel(encoding), flow
                    )
                );

            return {
                .vk = std::ref(vk),
                .shaders = std::ref(shaders),
                .pool{vk, backend::calculateDescriptorPoolLimits(
                    count, perf, requiresPqConversion(encoding)
                )},
                .constantBuffer{vk, backend::getDefaultConstantBuffer(
                    0, 1, usesHdrModel(encoding), flow
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
                .highPrecision = usesHighPrecisionModel(encoding),
                .hdr = usesHdrModel(encoding),
                .flow = flow,
                .perf = perf,
                .count = count
            };
        } catch (const std::exception& e) {
            throw backend::error("Unable to create context", e);
        }
    }
}

ContextImpl::ContextImpl(const InstanceImpl& instance,
            std::pair<int, int> sourceFds, const std::vector<int>& destFds, int syncFd,
            VkExtent2D extent, const FrameEncoding encoding, float flow, bool perf) :
        sourceImages(importImages(instance.getVulkan(), sourceFds,
            extent, transportFormat(encoding))),
        destImages(importImages(instance.getVulkan(), destFds,
            extent, transportFormat(encoding))),
        workingSourceImages(createWorkingSourceImages(
            instance.getVulkan(), extent, encoding
        )),
        workingDestImages(createWorkingDestImages(
            instance.getVulkan(), extent, encoding, destFds.size()
        )),
        blackImage(createBlackImage(instance.getVulkan())),
        syncSemaphore(importTimelineSemaphore(instance.getVulkan(), syncFd)),
        prepassSemaphore(createPrepassSemaphore(instance.getVulkan())),
        cmdbufs(createCommandBuffers(instance.getVulkan(), destFds.size() + 1)),
        cmdbufFence(instance.getVulkan()),
        ctx(createCtx(instance, extent, encoding, flow, perf, destFds.size())),
        sourceColorConversion(createSourceColorConversion(
            ctx, encoding, sourceImages, workingSourceImages
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
            Alpha1(ctx, 3, alpha0.at(0).getImages()),
            Alpha1(ctx, 2, alpha0.at(1).getImages()),
            Alpha1(ctx, 2, alpha0.at(2).getImages()),
            Alpha1(ctx, 2, alpha0.at(3).getImages()),
            Alpha1(ctx, 2, alpha0.at(4).getImages()),
            Alpha1(ctx, 2, alpha0.at(5).getImages()),
            Alpha1(ctx, 2, alpha0.at(6).getImages())
        },
        beta0(ctx, alpha1.at(0).getImages()),
        beta1(ctx, beta0.getImages()) {
    const auto& modelSourceImages = selectModelSourceImages(
        this->sourceImages, this->workingSourceImages
    );
    const auto& modelDestImages = selectModelDestImages(
        this->destImages, this->workingDestImages
    );

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
                    this->blackImage
                );
                pass.gamma1.emplace_back(ctx, i,
                    pass.gamma0.at(j).getImages(),
                    this->blackImage,
                    this->beta1.getImages().at(5)
                );
            } else { // other passes use prior data
                pass.gamma0.emplace_back(ctx, i,
                    this->alpha1.at(6 - j).getImages(),
                    pass.gamma1.at(j - 1).getImage()
                );
                pass.gamma1.emplace_back(ctx, i,
                    pass.gamma0.at(j).getImages(),
                    pass.gamma1.at(j - 1).getImage(),
                    this->beta1.getImages().at(6 - j)
                );
            }

            if (j == 4) { // first special pass has no prior data
                pass.delta0.emplace_back(ctx, i,
                    this->alpha1.at(6 - j).getImages(),
                    this->blackImage,
                    pass.gamma1.at(j - 1).getImage()
                );
                pass.delta1.emplace_back(ctx, i,
                    pass.delta0.at(j - 4).getImages0(),
                    pass.delta0.at(j - 4).getImages1(),
                    this->blackImage,
                    this->beta1.getImages().at(6 - j),
                    this->blackImage
                );
            } else if (j > 4) { // further passes do
                pass.delta0.emplace_back(ctx, i,
                    this->alpha1.at(6 - j).getImages(),
                    pass.delta1.at(j - 5).getImage0(),
                    pass.gamma1.at(j - 1).getImage()
                );
                pass.delta1.emplace_back(ctx, i,
                    pass.delta0.at(j - 4).getImages0(),
                    pass.delta0.at(j - 4).getImages1(),
                    pass.delta1.at(j - 5).getImage0(),
                    this->beta1.getImages().at(6 - j),
                    pass.delta1.at(j - 5).getImage1()
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
    images.push_back(this->blackImage.handle());
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
    for (const auto& pass : this->passes) {
        for (size_t i = 0; i < 7; ++i) {
            pass.gamma0.at(i).prepare(images);
            pass.gamma1.at(i).prepare(images);

            if (i < 4) continue;
            pass.delta0.at(i - 4).prepare(images);
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
    cmdbuf.end(ctx.vk);
    cmdbuf.submit(ctx.vk); // wait for completion
}

void Instance::scheduleFrames(Context& context) { // NOLINT (static)
#ifdef MAKO_TESTING_RENDERDOC
    const auto& impl = this->m_impl;
    if (impl->getRenderDocAPI()) {
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
    if (impl->getRenderDocAPI()) {
        impl->getVulkan().df().DeviceWaitIdle(impl->getVulkan().dev());
        impl->getRenderDocAPI()->EndFrameCapture(
            RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(impl->getVulkan().inst()),
            nullptr);
    }
#endif
}

void Instance::scheduleFrames(Context& context, std::span<const float> timestamps) { // NOLINT (static)
    if (timestamps.empty())
        throw backend::error("At least one interpolation timestamp is required");
#ifdef MAKO_TESTING_RENDERDOC
    const auto& impl = this->m_impl;
    if (impl->getRenderDocAPI()) {
        impl->getRenderDocAPI()->StartFrameCapture(
            RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(impl->getVulkan().inst()),
            nullptr);
    }
#endif
    try {
        context.scheduleFrames(timestamps);
    } catch (const std::exception& e) {
        throw backend::error("Unable to schedule frames", e);
    }
#ifdef MAKO_TESTING_RENDERDOC
    if (impl->getRenderDocAPI()) {
        impl->getVulkan().df().DeviceWaitIdle(impl->getVulkan().dev());
        impl->getRenderDocAPI()->EndFrameCapture(
            RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(impl->getVulkan().inst()),
            nullptr);
    }
#endif
}

void Instance::scheduleFrameHistory(Context& context) { // NOLINT (static)
    context.scheduleFrameHistory();
}

bool Instance::contextReady(const Context& context) const {
    return context.waitForIdle(0);
}

void Context::prepareWork() {
    if (this->workScheduled && !this->cmdbufFence.wait(
            this->ctx.vk, previousWorkFenceTimeoutNs)) {
        std::cerr << "mako: backend work fence timed out after 250 ms; "
                     "aborting frame scheduling\n";
        throw backend::error("Timeout waiting for previous frame to complete");
    }
    this->cmdbufFence.reset(this->ctx.vk);
}

void Context::schedulePrepass(const VkFence completionFence) {
    const auto& cmdbuf = this->cmdbufs.at(0);
    cmdbuf.begin(ctx.vk);

    if (this->sourceColorConversion)
        this->sourceColorConversion->render(ctx.vk, cmdbuf, this->fidx);
    this->mipmaps.render(ctx.vk, cmdbuf, this->fidx);
    for (size_t i = 0; i < 7; ++i) {
        this->alpha0.at(6 - i).render(ctx.vk, cmdbuf);
        this->alpha1.at(6 - i).render(ctx.vk, cmdbuf, this->fidx);
    }
    this->beta0.render(ctx.vk, cmdbuf, this->fidx);
    this->beta1.render(ctx.vk, cmdbuf);

    cmdbuf.end(ctx.vk);
    cmdbuf.submit(this->ctx.vk,
        {}, this->syncSemaphore.handle(), this->idx,
        {}, this->prepassSemaphore.handle(), this->idx,
        completionFence
    );

    this->idx++;
}

void Context::scheduleFrames() {
    this->scheduleFrames({});
}

void Context::scheduleFrames(std::span<const float> timestamps) {
    const bool explicitTimestamps = !timestamps.empty();
    const size_t generatedFrameCount = explicitTimestamps
        ? timestamps.size()
        : this->destImages.size();
    if (generatedFrameCount > this->destImages.size())
        throw backend::error("Too many interpolation timestamps for this context");

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

    // Every generated pass has its own uniform buffer and descriptor sets.
    // Previous GPU work is complete at this point, so the timestamp can be
    // safely replaced before recording the next set of dispatches.
    if (explicitTimestamps) {
        for (size_t i = 0; i < timestamps.size(); ++i) {
            auto constants = backend::getDefaultConstantBuffer(
                0, 1, this->ctx.hdr, this->ctx.flow
            );
            constants.timestamp = timestamps[i];
            this->ctx.constantBuffers.at(i).write(this->ctx.vk, constants);
        }
    }

    this->schedulePrepass(VK_NULL_HANDLE);

    // schedule main passes
    for (size_t i = 0; i < generatedFrameCount; i++) {
        const auto& cmdbuf = this->cmdbufs.at(i + 1);
        cmdbuf.begin(ctx.vk);

        const auto& pass = this->passes.at(i);
        for (size_t j = 0; j < 7; j++) {
            pass.gamma0.at(j).render(ctx.vk, cmdbuf, this->fidx);
            pass.gamma1.at(j).render(ctx.vk, cmdbuf);

            if (j < 4) continue;
            pass.delta0.at(j - 4).render(ctx.vk, cmdbuf, this->fidx);
            pass.delta1.at(j - 4).render(ctx.vk, cmdbuf);
        }
        pass.generate->render(ctx.vk, cmdbuf, this->fidx);
        if (this->destColorConversion)
            this->destColorConversion->render(ctx.vk, cmdbuf, i);

        cmdbuf.end(ctx.vk);
        cmdbuf.submit(this->ctx.vk,
            {}, this->prepassSemaphore.handle(), this->idx - 1,
            {}, this->syncSemaphore.handle(), this->idx + i,
            i == generatedFrameCount - 1 ? this->cmdbufFence.handle() : VK_NULL_HANDLE
        );
    }

    this->idx += generatedFrameCount;
    this->fidx++;
    this->workScheduled = true;
}

void Context::scheduleFrameHistory() {
    this->prepareWork();
    this->schedulePrepass(this->cmdbufFence.handle());
    this->fidx++;
    this->workScheduled = true;
}

bool Context::waitForIdle(const uint64_t timeoutNs) const {
    return !this->workScheduled || this->cmdbufFence.wait(this->ctx.vk, timeoutNs);
}

void Instance::collectRetiredContexts() {
    std::erase_if(this->m_retiredContexts,
        [](const std::unique_ptr<ContextImpl>& context) {
            try {
                return context->waitForIdle(0);
            } catch (const std::exception& e) {
                std::cerr << "mako: unable to poll a retired backend context: "
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

    try {
        if ((*it)->waitForIdle(previousWorkFenceTimeoutNs)) {
            this->m_contexts.erase(it);
            return;
        }
    } catch (const std::exception& e) {
        std::cerr << "mako: unable to wait for backend context retirement: "
                  << e.what() << '\n';
    }

    // Never issue a device-wide indefinite wait while the game is replacing a
    // swapchain. Keep the exceptional in-flight context alive and reclaim it
    // on a later open/close once its own completion fence signals.
    std::cerr << "mako: backend context did not retire within 250 ms; "
                 "deferring resource destruction\n";
    this->m_retiredContexts.push_back(std::move(*it));
    this->m_contexts.erase(it);
}

Instance::~Instance() = default;

// leaking shenanigans

namespace {
    bool leaking{false}; // NOLINT (global variable)
}

InstanceImpl::~InstanceImpl() {
    if (!leaking) return;

    try {
        new vk::Vulkan(std::move(this->vk));
    } catch (...) {
        std::cerr << "mako: failed to leak Vulkan instance\n";
    }

}

void backend::makeLeaking() {
    leaking = true;
}
