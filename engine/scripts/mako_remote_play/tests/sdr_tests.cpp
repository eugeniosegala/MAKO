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
SDL_Renderer* SDL_CreateRendererWithProperties(std::uint32_t);
std::uint32_t SDL_GetRendererProperties(SDL_Renderer*);
void SDL_DestroyRenderer(SDL_Renderer*);
const char* SDL_GetError();
unsigned fixturePropertyCount();
unsigned fixtureRendererCount();
}

static void require(bool condition, const char* message) {
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
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
        SDL_SetNumberProperty(original, "SDL.renderer.create.window", 73);
        SDL_SetNumberProperty(original, "SDL.renderer.create.vulkan.device", 91);
        SDL_SetNumberProperty(original, "SDL.renderer.create.present_vsync", 1);
        // Enable injected errors after preparing the client's properties.
        if (failure) setenv("MAKO_SDL_FIXTURE_FAILURE", std::getenv("MAKO_SDL_FIXTURE_PHASE"), 1);
        auto* renderer = SDL_CreateRendererWithProperties(original);
        require(SDL_GetNumberProperty(original, "SDL.renderer.create.output_colorspace", -1) == requested,
            "helper changed the client's properties");
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
            require(SDL_GetNumberProperty(output, "SDL.renderer.create.window", -1) == 73, "window property lost");
            require(SDL_GetNumberProperty(output, "SDL.renderer.create.vulkan.device", -1) == 91, "Vulkan device property lost");
            require(SDL_GetNumberProperty(output, "SDL.renderer.create.present_vsync", -1) == 1, "VSync property lost");
            SDL_DestroyRenderer(renderer);
        }
        require(fixturePropertyCount() == 1 && fixtureRendererCount() == 0, "temporary properties or renderer leaked");
        SDL_DestroyProperties(original);
    }
    return 0;
}
