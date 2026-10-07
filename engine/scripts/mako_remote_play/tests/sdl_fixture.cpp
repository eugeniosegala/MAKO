// A public SDL3 ABI fixture: no window system, video decoder, or Vulkan device.
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>

using Properties = std::uint32_t;
using Cleanup = void (*)(void*, void*);
struct Property {
    int type = 3; // SDL_PROPERTY_TYPE_NUMBER
    std::int64_t number = 0;
    void* pointer = nullptr;
    Cleanup cleanup = nullptr;
    void* userdata = nullptr;
};
using Values = std::map<std::string, Property>;
struct SDL_Renderer { Properties properties; };
static std::map<Properties, Values> properties;
static std::map<Properties, unsigned> locks;
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
    locks[id] = 0;
    return id;
}
void SDL_DestroyProperties(Properties id) {
    if (locks.at(id)) std::abort();
    for (const auto& [name, property] : properties.at(id)) {
        if (property.cleanup) property.cleanup(property.userdata, property.pointer);
    }
    properties.erase(id);
    locks.erase(id);
}
bool SDL_LockProperties(Properties id) {
    if (fails("lock")) return false;
    ++locks.at(id);
    return true;
}
void SDL_UnlockProperties(Properties id) { --locks.at(id); }
bool SDL_CopyProperties(Properties source, Properties destination) {
    if (fails("copy")) return false;
    // SDL deliberately omits properties that have cleanup callbacks.
    for (const auto& [name, property] : properties.at(source)) {
        if (!property.cleanup) properties.at(destination)[name] = property;
    }
    return true;
}
int SDL_GetPropertyType(Properties id, const char* name) {
    const auto& values = properties.at(id);
    const auto item = values.find(name);
    return item == values.end() ? 0 : item->second.type;
}
bool SDL_EnumerateProperties(Properties id, void (*callback)(void*, Properties, const char*), void* userdata) {
    if (fails("enumerate")) return false;
    ++locks.at(id);
    for (const auto& [name, property] : properties.at(id)) callback(userdata, id, name.c_str());
    --locks.at(id);
    return true;
}
void* SDL_GetPointerProperty(Properties id, const char* name, void* fallback) {
    const auto& values = properties.at(id);
    const auto item = values.find(name);
    return item == values.end() || item->second.type != 1 ? fallback : item->second.pointer;
}
bool SDL_SetPointerPropertyWithCleanup(Properties id, const char* name, void* value, Cleanup cleanup, void* userdata) {
    auto& values = properties.at(id);
    const auto previous = values.find(name);
    if (previous != values.end() && previous->second.cleanup)
        previous->second.cleanup(previous->second.userdata, previous->second.pointer);
    if (value) values[name] = Property{1, 0, value, cleanup, userdata};
    else values.erase(name);
    return true;
}
bool SDL_SetPointerProperty(Properties id, const char* name, void* value) {
    if (fails("pointer")) return false;
    return SDL_SetPointerPropertyWithCleanup(id, name, value, nullptr, nullptr);
}
bool SDL_SetNumberProperty(Properties id, const char* name, std::int64_t value) {
    if (fails("set") && std::string(name) == "SDL.renderer.create.output_colorspace") return false;
    properties.at(id)[name] = Property{3, value};
    return true;
}
std::int64_t SDL_GetNumberProperty(Properties id, const char* name, std::int64_t fallback) {
    const auto property = properties.find(id);
    if (property == properties.end()) return fallback;
    const auto item = property->second.find(name);
    return item == property->second.end() || item->second.type != 3 ? fallback : item->second.number;
}
SDL_Renderer* SDL_CreateRendererWithProperties(Properties id) {
    if (fails("create")) { error = "fixture renderer error"; return nullptr; }
    const auto output = SDL_CreateProperties();
    // Observe input values without giving the renderer ownership of callbacks.
    for (const auto& [name, property] : properties.at(id)) {
        auto observed = property;
        observed.cleanup = nullptr;
        observed.userdata = nullptr;
        properties.at(output)[name] = observed;
    }
    SDL_SetNumberProperty(output, "SDL.renderer.output_colorspace", fails("actual") ? 0x12002600 :
        SDL_GetNumberProperty(id, "SDL.renderer.create.output_colorspace", 0x120005a0));
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
unsigned fixturePropertyLocks(Properties id) { return locks.at(id); }
}
