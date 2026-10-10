/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <cstddef>
#include <cstdint>

// Shared with the synthetic translator so sanitizer-instrumented calls use
// the same function and structure types, not layout-only approximations.
namespace mako::backend::dxbc_abi {
    // Minimal declarations for vkd3d-shader's stable public C ABI. MAKO loads
    // the LGPL vkd3d-shader implementation dynamically and does not incorporate
    // it into the Renderer. Keep these layouts aligned with vkd3d_shader.h.
    enum class StructureType : int32_t {
        CompileInfo = 0,
        InterfaceInfo = 1,
        SpirvTargetInfo = 4,
    };
    enum class SourceType : int32_t {
        DxbcTpf = 1,
    };
    enum class TargetType : int32_t {
        SpirvBinary = 1,
    };
    enum class LogLevel : int32_t {
        Warning = 2,
    };
    enum class DescriptorType : int32_t {
        Srv = 0,
        Uav = 1,
        Cbv = 2,
        Sampler = 3,
    };
    enum class Visibility : int32_t {
        Compute = 1000000000,
    };
    enum class SpirvEnvironment : int32_t {
        Vulkan10 = 2,
    };

    struct ShaderCode {
        const void* code;
        size_t size;
    };
    struct DescriptorBinding {
        uint32_t set;
        uint32_t binding;
        uint32_t count;
    };
    struct ResourceBinding {
        DescriptorType type;
        uint32_t registerSpace;
        uint32_t registerIndex;
        Visibility visibility;
        uint32_t flags;
        DescriptorBinding binding;
    };
    struct InterfaceInfo {
        StructureType type;
        const void* next;
        const ResourceBinding* bindings;
        uint32_t bindingCount;
        const void* pushConstantBuffers;
        uint32_t pushConstantBufferCount;
        const void* combinedSamplers;
        uint32_t combinedSamplerCount;
        const void* uavCounters;
        uint32_t uavCounterCount;
    };
    struct SpirvTargetInfo {
        StructureType type;
        const void* next;
        const char* entryPoint;
        SpirvEnvironment environment;
        const void* extensions;
        uint32_t extensionCount;
        const void* parameters;
        uint32_t parameterCount;
        bool dualSourceBlending;
        const uint32_t* outputSwizzles;
        uint32_t outputSwizzleCount;
    };
    struct CompileInfo {
        StructureType type;
        const void* next;
        ShaderCode source;
        SourceType sourceType;
        TargetType targetType;
        const void* options;
        uint32_t optionCount;
        LogLevel logLevel;
        const char* sourceName;
    };

    using CompileFunction = int (*)(
        const CompileInfo*, ShaderCode*, char**
    );
    using FreeCodeFunction = void (*)(ShaderCode*);
    using FreeMessagesFunction = void (*)(char*);

}
