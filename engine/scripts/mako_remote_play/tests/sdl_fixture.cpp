// A public SDL3 ABI fixture: no window system, video decoder, or Vulkan device.
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>

using Properties = std::uint32_t;
using Numbers = std::map<std::string, std::int64_t>;
struct SDL_Renderer { Properties properties; };
static std::map<Properties, Numbers> properties;
static Properties nextId = 1;
static unsigned renderers = 0;
static const char* error = "";

static bool fails(const char* phase) {
    const char* requested = std::getenv("MAKO_SDL_FIXTURE_FAILURE");
    return requested && std::string(requested) == phase;
}

extern "C" {
Properties SDL_CreateProperties() {
    const auto id = nextId++;
    properties[id] = {};
    return id;
}
void SDL_DestroyProperties(Properties id) { properties.erase(id); }
bool SDL_CopyProperties(Properties source, Properties destination) {
    if (fails("copy")) return false;
    properties.at(destination) = properties.at(source);
    return true;
}
bool SDL_SetNumberProperty(Properties id, const char* name, std::int64_t value) {
    if (fails("set") && std::string(name) == "SDL.renderer.create.output_colorspace") return false;
    properties.at(id)[name] = value;
    return true;
}
std::int64_t SDL_GetNumberProperty(Properties id, const char* name, std::int64_t fallback) {
    const auto property = properties.find(id);
    if (property == properties.end()) return fallback;
    const auto item = property->second.find(name);
    return item == property->second.end() ? fallback : item->second;
}
SDL_Renderer* SDL_CreateRendererWithProperties(Properties id) {
    if (fails("create")) { error = "fixture renderer error"; return nullptr; }
    const auto output = SDL_CreateProperties();
    properties.at(output) = properties.at(id);
    properties.at(output)["SDL.renderer.output_colorspace"] = fails("actual") ? 0x12002600 :
        SDL_GetNumberProperty(id, "SDL.renderer.create.output_colorspace", 0x120005a0);
    ++renderers;
    return new SDL_Renderer{output};
}
Properties SDL_GetRendererProperties(SDL_Renderer* renderer) { return renderer->properties; }
void SDL_DestroyRenderer(SDL_Renderer* renderer) {
    SDL_DestroyProperties(renderer->properties);
    --renderers;
    delete renderer;
}
bool SDL_SetError(const char*, ...) { error = "MAKO helper error"; return false; }
const char* SDL_GetError() { return error; }
unsigned fixturePropertyCount() { return static_cast<unsigned>(properties.size()); }
unsigned fixtureRendererCount() { return renderers; }
}
