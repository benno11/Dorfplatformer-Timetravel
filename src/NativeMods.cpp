#include "NativeMods.h"
#include "EditionPolicy.h"
#include <SDL3/SDL.h>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#ifdef __ANDROID__
#include <SDL3/SDL_system.h>
#endif
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace NativeMods {
namespace fs = std::filesystem;
using Json = nlohmann::json;
static bool validId(const std::string& id) {
    return !id.empty() && id.size() <= 80 && id.front() != '.' &&
        id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") == std::string::npos && id != "..";
}
static fs::path contained(const fs::path& root, const fs::path& child) {
    const auto base = fs::weakly_canonical(root), path = fs::weakly_canonical(child);
    const auto relative = path.lexically_relative(base);
    if (relative.empty() || relative == "." || relative.is_absolute() || *relative.begin() == "..") throw std::runtime_error("Path outside mod directory");
    return path;
}
std::string Loader::platform() {
#if defined(__ANDROID__)
    const char* os = "android";
#elif defined(_WIN32)
    const char* os = "windows";
#elif defined(PLATFORMER_IOS) && PLATFORMER_IOS
    const char* os = "ios";
#elif defined(__APPLE__)
    const char* os = "macos";
#else
    const char* os = "linux";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    const char* cpu = "arm64";
#elif defined(__arm__) || defined(_M_ARM)
    const char* cpu = "armv7";
#elif defined(__x86_64__) || defined(_M_X64)
    const char* cpu = "x64";
#else
    const char* cpu = "x86";
#endif
    return std::string(os) + "-" + cpu;
}
struct Loader::Impl {
    struct Entry {
        Info info;
        std::string directory, config;
        fs::path library;
        std::vector<std::string> dependencies;
        SDL_SharedObject* object = nullptr;
        DFModCallbacks callbacks{};
        DFModContext context{};
        bool compatible = false, desired = true;
    };
    std::string root;
    fs::path prefsPath;
    Json prefs = Json::object();
    std::vector<std::unique_ptr<Entry>> entries;
    std::vector<Entry*> loadOrder;
    DFHostAPI host{};
    DFPlayerState* player = nullptr;
    const DFLevelState* level = nullptr;
    uint64_t generation = std::numeric_limits<uint64_t>::max();
    bool started = false;
    explicit Impl(const std::string& saveRoot) : root((fs::u8path(saveRoot) / "mods").u8string()), prefsPath(fs::u8path(root) / "enabled.json") {
        host.struct_size = sizeof(host); host.api_major = DF_MOD_API_MAJOR; host.api_minor = DF_MOD_API_MINOR; host.context = this;
        host.log = [](void*, const char* text) { if (text) SDL_Log("MOD: %s", text); };
        host.get_player = [](void* context, DFPlayerState* out) -> int32_t {
            auto* self = static_cast<Impl*>(context);
            if (!self->player || !out || out->struct_size < sizeof(DFPlayerState)) return 0;
            *out = *self->player; return 1;
        };
        host.set_player = [](void* context, const DFPlayerState* in) -> int32_t {
            auto* self = static_cast<Impl*>(context);
            if (!self->player || !in || in->struct_size < sizeof(DFPlayerState) ||
                !std::isfinite(in->x) || !std::isfinite(in->y) || !std::isfinite(in->velocity_x) || !std::isfinite(in->velocity_y) ||
                std::abs(in->x) > 1e7 || std::abs(in->y) > 1e7 || std::abs(in->velocity_x) > 1e5 || std::abs(in->velocity_y) > 1e5) return 0;
            *self->player = *in; self->player->struct_size = sizeof(DFPlayerState); return 1;
        };
        host.get_level = [](void* context, DFLevelState* out) -> int32_t {
            auto* self = static_cast<Impl*>(context);
            if (!self->level || !out || out->struct_size < sizeof(DFLevelState)) return 0;
            *out = *self->level; return 1;
        };
        if (Edition::lite) return;
        try { fs::create_directories(fs::u8path(root)); std::ifstream in(prefsPath); if (in) { in >> prefs; if (!prefs.is_object()) prefs = Json::object(); } }
        catch (const std::exception& e) { SDL_Log("MOD config: %s", e.what()); }
    }
    void discover() {
        if (Edition::lite) return;
        std::set<std::string> existing;
        for (const auto& e : entries) existing.insert(e->directory);
        std::error_code ec;
        std::vector<fs::path> paths;
        for (fs::directory_iterator it(fs::u8path(root), ec), end; !ec && it != end; it.increment(ec)) if (it->is_directory(ec)) paths.push_back(it->path());
        std::sort(paths.begin(), paths.end());
        for (const auto& path : paths) {
            std::error_code manifestError;
            if (path.filename().string().front() == '.' || existing.count(path.u8string()) || !fs::exists(path / "mod.json", manifestError)) continue;
            auto entry = std::make_unique<Entry>(); auto& e = *entry;
            e.directory = path.u8string(); e.info.name = e.info.id = path.filename().string();
            try {
                contained(fs::u8path(root), path);
                const auto manifest = contained(path, path / "mod.json");
                if (fs::file_size(manifest) > 65536) throw std::runtime_error("Manifest exceeds 64 KB");
                std::ifstream input(manifest); Json j; input >> j;
                e.info.id = j.at("id").get<std::string>();
                if (!validId(e.info.id) || e.info.id != path.filename().string()) throw std::runtime_error("Folder must match the mod ID");
                for (const auto& other : entries) if (other->info.id == e.info.id) throw std::runtime_error("Duplicate mod ID");
                e.desired = prefs.contains(e.info.id) && prefs[e.info.id].is_boolean() ? prefs[e.info.id].get<bool>() : true;
                e.info.enabled = e.desired;
                e.info.name = j.value("name", e.info.id); e.info.version = j.at("version").get<std::string>();
                const auto& api = j.at("api");
                if (!api.at("major").is_number_integer() || (api.contains("minor") && !api["minor"].is_number_integer()))
                    throw std::runtime_error("API versions must be integers");
                const uint64_t major = api.at("major").get<uint64_t>();
                const uint64_t minor = api.contains("minor") ? api["minor"].get<uint64_t>() : 0;
                if (major != DF_MOD_API_MAJOR || minor > DF_MOD_API_MINOR) throw std::runtime_error("Incompatible mod API (requires " + std::to_string(major) + "." + std::to_string(minor) + ")");
                const fs::path relative = j.at("libraries").at(Loader::platform()).get<std::string>();
                if (relative.empty() || relative == "." || relative.is_absolute()) throw std::runtime_error("Missing native library for " + Loader::platform());
                e.library = contained(path, path / relative);
                if (!fs::is_regular_file(e.library)) throw std::runtime_error("Native library not found");
                e.dependencies = j.value("dependencies", std::vector<std::string>{});
                for (const auto& id : e.dependencies) if (!validId(id)) throw std::runtime_error("Invalid dependency ID");
                e.config = (fs::u8path(root) / ".config" / e.info.id).u8string(); fs::create_directories(fs::u8path(e.config));
                e.compatible = true;
                e.info.status = e.desired ? "Restart to load" : "Disabled";
            } catch (const std::exception& error) { e.info.status = error.what(); }
            entries.push_back(std::move(entry));
        }
    }
    bool load(Entry& e, std::set<Entry*>& visiting) {
        if (e.object) return e.info.loaded;
        if (!e.compatible || !e.desired) return false;
        if (!visiting.insert(&e).second) { e.info.status = "Dependency cycle"; return false; }
        for (const auto& id : e.dependencies) {
            auto dep = std::find_if(entries.begin(), entries.end(), [&](const auto& other) { return other->info.id == id; });
            if (dep == entries.end() || !load(**dep, visiting)) { e.info.status = "Dependency unavailable: " + id; visiting.erase(&e); return false; }
        }
        visiting.erase(&e);
        try {
            fs::path library = e.library;
#ifdef __ANDROID__
            const char* internal = SDL_GetAndroidInternalStoragePath();
            if (!internal) throw std::runtime_error("Android private storage is unavailable");
            // External/media storage is not executable. Stage this mod's native
            // directory privately; keep config/assets in its user-visible folder.
            const auto privateDir = fs::path(internal) / "native_mods" / e.info.id;
            fs::remove_all(privateDir); fs::create_directories(privateDir);
            for (const auto& file : fs::directory_iterator(e.library.parent_path()))
                if (file.is_regular_file() && file.path().extension() == ".so") {
                    const auto target = privateDir / file.path().filename();
                    fs::copy_file(file.path(), target, fs::copy_options::overwrite_existing);
                    fs::permissions(target, fs::perms::owner_read | fs::perms::owner_exec);
                }
            library = privateDir / e.library.filename();
#endif
#if defined(PLATFORMER_IOS) && PLATFORMER_IOS
            throw std::runtime_error("External native mods are unavailable on iOS");
#endif
            e.object = SDL_LoadObject(library.u8string().c_str());
            if (!e.object) throw std::runtime_error(SDL_GetError());
            const auto entry = reinterpret_cast<DFModLoad>(SDL_LoadFunction(e.object, "df_mod_load"));
            if (!entry) throw std::runtime_error("Missing df_mod_load export");
            e.callbacks.struct_size = sizeof(e.callbacks);
            e.context = {sizeof(DFModContext), &host, e.directory.c_str(), e.config.c_str()};
            if (!entry(&e.context, &e.callbacks) || e.callbacks.struct_size < offsetof(DFModCallbacks, userdata) + sizeof(void*)) throw std::runtime_error("Mod initialization failed");
            if (e.callbacks.struct_size < offsetof(DFModCallbacks, on_level_start) + sizeof(e.callbacks.on_level_start)) e.callbacks.on_level_start = nullptr;
            if (e.callbacks.struct_size < offsetof(DFModCallbacks, on_update) + sizeof(e.callbacks.on_update)) e.callbacks.on_update = nullptr;
            if (e.callbacks.struct_size < offsetof(DFModCallbacks, on_unload) + sizeof(e.callbacks.on_unload)) e.callbacks.on_unload = nullptr;
            e.info.loaded = true; e.info.status = "Loaded"; loadOrder.push_back(&e); return true;
        } catch (const std::exception& error) { e.info.status = error.what(); }
        catch (...) { e.info.status = "Mod threw during initialization"; }
        SDL_Log("MOD %s: %s", e.info.id.c_str(), e.info.status.c_str());
        if (e.object) { SDL_UnloadObject(e.object); e.object = nullptr; }
        return false;
    }
};
Loader::Loader(const std::string& saveRoot) : impl(std::make_unique<Impl>(saveRoot)) {}
Loader::~Loader() { shutdown(); }
void Loader::start() {
    if (impl->started || Edition::lite) return;
#if defined(PLATFORMER_IOS) && PLATFORMER_IOS
    return;
#endif
    impl->started = true; impl->discover();
    for (auto& e : impl->entries) { std::set<Impl::Entry*> visiting; impl->load(*e, visiting); }
}
void Loader::shutdown() {
    impl->player = nullptr; impl->level = nullptr;
    for (auto it = impl->loadOrder.rbegin(); it != impl->loadOrder.rend(); ++it) {
        auto& e = **it;
        try { if (e.callbacks.on_unload) e.callbacks.on_unload(e.callbacks.userdata); } catch (...) {}
        if (e.object) SDL_UnloadObject(e.object);
        e.object = nullptr; e.info.loaded = false;
    }
    impl->loadOrder.clear();
}
void Loader::rescan() { impl->discover(); }
const std::string& Loader::folder() const { return impl->root; }
std::vector<Info> Loader::list() const { std::vector<Info> result; for (const auto& e : impl->entries) result.push_back(e->info); return result; }
bool Loader::toggle(const std::string& id, std::string& status) {
    for (auto& entry : impl->entries) if (entry->info.id == id && validId(id) && fs::u8path(entry->directory).filename().u8string() == id) {
        auto& e = *entry;
        try {
            auto prefs = impl->prefs; prefs[id] = !e.desired;
            auto temporary = impl->prefsPath; temporary += ".tmp";
            { std::ofstream out(temporary); out << prefs.dump(2); out.flush(); if (!out.good()) throw std::runtime_error("Could not save mod preferences"); }
            std::error_code ec;
#ifdef _WIN32
            // Existing preferences are retained until a complete replacement is ready.
            if (!MoveFileExW(temporary.c_str(), impl->prefsPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Could not replace mod preferences");
#else
            fs::rename(temporary, impl->prefsPath, ec); if (ec) throw std::runtime_error(ec.message());
#endif
            impl->prefs = std::move(prefs); e.desired = !e.desired; e.info.enabled = e.desired;
            status = "Saved. Restart the game to apply mod changes.";
            if (e.compatible) e.info.status = e.desired == e.info.loaded ? (e.info.loaded ? "Loaded" : "Disabled") : "Restart required";
            return true;
        } catch (const std::exception& error) { status = error.what(); return false; }
    }
    status = "This mod cannot be enabled. Check its API and native library."; return false;
}
void Loader::tick(double seconds, DFPlayerState& player, const DFLevelState& level, uint64_t generation) {
    impl->player = &player; impl->level = &level;
    const bool newLevel = impl->generation != generation; impl->generation = generation;
    for (auto* e : impl->loadOrder) if (e->info.loaded) {
        try {
            if (newLevel && e->callbacks.on_level_start) e->callbacks.on_level_start(e->callbacks.userdata, &level);
            if (e->callbacks.on_update) e->callbacks.on_update(e->callbacks.userdata, std::clamp(seconds, 0.0, 0.25));
        } catch (...) { e->info.loaded = false; e->info.status = "Disabled after callback exception; restart required"; }
    }
    impl->player = nullptr; impl->level = nullptr;
}
}
