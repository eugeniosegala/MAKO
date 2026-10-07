#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

struct SDL_Renderer;
extern "C" {
std::uint32_t SDL_CreateProperties();
void SDL_DestroyProperties(std::uint32_t);
bool SDL_SetNumberProperty(std::uint32_t, const char*, std::int64_t);
std::int64_t SDL_GetNumberProperty(std::uint32_t, const char*, std::int64_t);
bool SDL_SetPointerProperty(std::uint32_t, const char*, void*);
bool SDL_SetPointerPropertyWithCleanup(std::uint32_t, const char*, void*, void (*)(void*, void*), void*);
void* SDL_GetPointerProperty(std::uint32_t, const char*, void*);
SDL_Renderer* SDL_CreateRendererWithProperties(std::uint32_t);
std::uint32_t SDL_GetRendererProperties(SDL_Renderer*);
void SDL_DestroyRenderer(SDL_Renderer*);
const char* SDL_GetError();
unsigned fixturePropertyCount();
unsigned fixtureRendererCount();
unsigned fixturePropertyLocks(std::uint32_t);
}

static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}

struct OwnedPointer { unsigned cleanups = 0; int value = 0; };
static void cleanup(void* userdata, void* value) {
    auto& owner = *static_cast<OwnedPointer*>(userdata);
    require(value == &owner.value, "cleanup received a different pointer");
    ++owner.cleanups;
}

int main(int argc, char** argv) {
    require(argc == 2, "missing test mode");
    const bool forced = std::strcmp(argv[1], "forced") == 0;
    const bool failure = std::strcmp(argv[1], "failure") == 0;
    // Repeat creation to cover renderer replacement without mutating caller state.
    for (const auto requested : {std::int64_t{0x12002600}, std::int64_t{0x120005a0}, std::int64_t{0x12000500}, std::int64_t{-1}}) {
        const auto original = SDL_CreateProperties();
        if (requested != -1)
            require(SDL_SetNumberProperty(original, "SDL.renderer.create.output_colorspace", requested), "fixture setup failed");
        OwnedPointer window, device;
        int ordinary = 0;
        require(SDL_SetPointerPropertyWithCleanup(original, "SDL.renderer.create.window", &window.value, cleanup, &window),
            "owned window setup failed");
        require(SDL_SetPointerPropertyWithCleanup(original, "SDL.renderer.create.vulkan.device", &device.value, cleanup, &device),
            "owned device setup failed");
        require(SDL_SetPointerProperty(original, "Steam.unowned.pointer", &ordinary), "ordinary pointer setup failed");
        SDL_SetNumberProperty(original, "SDL.renderer.create.present_vsync", 1);
        // Enable injected errors after preparing the client's properties.
        if (failure) setenv("MAKO_SDL_FIXTURE_FAILURE", std::getenv("MAKO_SDL_FIXTURE_PHASE"), 1);
        auto* renderer = SDL_CreateRendererWithProperties(original);
        require(SDL_GetNumberProperty(original, "SDL.renderer.create.output_colorspace", -1) == requested,
            "helper changed the client's properties");
        require(SDL_GetPointerProperty(original, "SDL.renderer.create.window", nullptr) == &window.value &&
            SDL_GetPointerProperty(original, "SDL.renderer.create.vulkan.device", nullptr) == &device.value,
            "helper changed the client's pointer properties");
        require(window.cleanups == 0 && device.cleanups == 0, "borrowed pointer was freed during creation");
        require(fixturePropertyLocks(original) == 0, "client properties remained locked");
        if (failure) {
            require(!renderer, "failed SDR setup returned a renderer");
            require(std::strlen(SDL_GetError()) > 0, "SDL creation error was lost");
            unsetenv("MAKO_SDL_FIXTURE_FAILURE");
        } else {
            require(renderer != nullptr, "renderer creation failed");
            const auto output = SDL_GetRendererProperties(renderer);
            const auto expected = forced || requested == -1 ? 0x120005a0 : requested;
            require(SDL_GetNumberProperty(output, "SDL.renderer.output_colorspace", -1) == expected,
                "wrong output colorspace");
            require(SDL_GetPointerProperty(output, "SDL.renderer.create.window", nullptr) == &window.value, "owned window property lost");
            require(SDL_GetPointerProperty(output, "SDL.renderer.create.vulkan.device", nullptr) == &device.value, "owned Vulkan device property lost");
            require(SDL_GetPointerProperty(output, "Steam.unowned.pointer", nullptr) == &ordinary, "ordinary pointer property lost");
            require(SDL_GetNumberProperty(output, "SDL.renderer.create.present_vsync", -1) == 1, "VSync property lost");
            SDL_DestroyRenderer(renderer);
        }
        require(window.cleanups == 0 && device.cleanups == 0, "helper transferred pointer cleanup ownership");
        require(fixturePropertyCount() == 1 && fixtureRendererCount() == 0, "temporary properties or renderer leaked");
        SDL_DestroyProperties(original);
        require(window.cleanups == 1 && device.cleanups == 1, "original pointer cleanup did not run exactly once");
        require(fixturePropertyCount() == 0, "caller properties leaked");
    }
    return 0;
}
