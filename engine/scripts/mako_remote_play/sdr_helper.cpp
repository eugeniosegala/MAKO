// Steam supplies SDL3. Resolve its public ABI instead of linking a host SDL
// or adding Qt/graphics dependencies to this process-start compatibility shim.
// https://wiki.libsdl.org/SDL3/SDL_CreateRendererWithProperties
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <unistd.h>

struct SDL_Renderer;
using SDL_PropertiesID = std::uint32_t;

namespace {
    constexpr std::int64_t srgb = 0x120005a0;
    constexpr const char* createColorspace = "SDL.renderer.create.output_colorspace";
    constexpr const char* outputColorspace = "SDL.renderer.output_colorspace";

    template <typename Function>
    Function next(const char* name) {
        return reinterpret_cast<Function>(dlsym(RTLD_NEXT, name));
    }

    struct Sdl {
        SDL_Renderer* (*createRenderer)(SDL_PropertiesID) = next<decltype(createRenderer)>("SDL_CreateRendererWithProperties");
        SDL_PropertiesID (*createProperties)() = next<decltype(createProperties)>("SDL_CreateProperties");
        bool (*copyProperties)(SDL_PropertiesID, SDL_PropertiesID) = next<decltype(copyProperties)>("SDL_CopyProperties");
        void (*destroyProperties)(SDL_PropertiesID) = next<decltype(destroyProperties)>("SDL_DestroyProperties");
        bool (*setNumber)(SDL_PropertiesID, const char*, std::int64_t) = next<decltype(setNumber)>("SDL_SetNumberProperty");
        std::int64_t (*getNumber)(SDL_PropertiesID, const char*, std::int64_t) = next<decltype(getNumber)>("SDL_GetNumberProperty");
        SDL_PropertiesID (*rendererProperties)(SDL_Renderer*) = next<decltype(rendererProperties)>("SDL_GetRendererProperties");
        void (*destroyRenderer)(SDL_Renderer*) = next<decltype(destroyRenderer)>("SDL_DestroyRenderer");
        bool (*setError)(const char*, ...) = next<decltype(setError)>("SDL_SetError");

        bool complete() const {
            return createRenderer && createProperties && copyProperties &&
                destroyProperties && setNumber && getNumber && rendererProperties && destroyRenderer;
        }
    };

    bool applies() {
        const char* enabled = std::getenv("MAKO_REMOTE_PLAY_SDR");
        if (!enabled || std::strcmp(enabled, "1") != 0)
            return false;
        // Child processes can inherit LD_PRELOAD. Only the verified native
        // streaming executable may receive this renderer policy.
        char executable[4096];
        const auto length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
        if (length <= 0 || length == static_cast<ssize_t>(sizeof(executable) - 1))
            return false;
        executable[length] = '\0';
        if (char* deleted = std::strstr(executable, " (deleted)"))
            *deleted = '\0';
        const char* name = std::strrchr(executable, '/');
        return name && std::strcmp(name + 1, "streaming_client.mako-original") == 0;
    }

    SDL_Renderer* failure(const Sdl& sdl, const char* reason) {
        std::fprintf(stderr, "MAKO Renderer: remote-play-sdr result=failed; reason=%s\n", reason);
        if (sdl.setError)
            sdl.setError("MAKO Remote Play SDR: %s", reason);
        return nullptr;
    }
}

// The launch owner checks this versioned identity before publishing its wrapper
// and before each stream. Keep it in the stripped runtime payload.
extern "C" __attribute__((visibility("default"))) const char* mako_remote_play_sdr_identity() {
    return "MAKO_REMOTE_PLAY_SDR_HELPER_V1";
}

extern "C" __attribute__((visibility("default"))) SDL_Renderer* SDL_CreateRendererWithProperties(SDL_PropertiesID properties) {
    static const Sdl sdl;
    static const bool enabled = applies();
    if (!enabled)
        return sdl.createRenderer ? sdl.createRenderer(properties) : nullptr;
    if (!sdl.complete())
        return failure(sdl, "required SDL3 API unavailable");

    const auto requested = sdl.getNumber(properties, createColorspace, srgb);
    const auto copy = sdl.createProperties();
    if (!copy)
        return failure(sdl, "cannot allocate renderer properties");
    // Preserve the client's window, Vulkan handles, queues, VSync, and all
    // other properties. Its original properties and texture encodings stay intact.
    if (!sdl.copyProperties(properties, copy) || !sdl.setNumber(copy, createColorspace, srgb)) {
        sdl.destroyProperties(copy);
        return failure(sdl, "cannot set SDR renderer properties");
    }
    auto* renderer = sdl.createRenderer(copy);
    sdl.destroyProperties(copy);
    if (!renderer) {
        // Preserve SDL's creation error for the native client's recovery path.
        std::fprintf(stderr, "MAKO Renderer: remote-play-sdr requested=0x%llx; result=renderer-creation-failed\n",
            static_cast<unsigned long long>(requested));
        return nullptr;
    }
    const auto actual = sdl.getNumber(sdl.rendererProperties(renderer), outputColorspace, -1);
    std::fprintf(stderr, "MAKO Renderer: remote-play-sdr requested=0x%llx; actual=0x%llx; result=%s\n",
        static_cast<unsigned long long>(requested), static_cast<unsigned long long>(actual),
        actual == srgb ? "sRGB" : "unexpected-colorspace");
    if (actual != srgb) {
        sdl.destroyRenderer(renderer);
        return failure(sdl, "SDL did not create an sRGB renderer");
    }
    return renderer;
}
