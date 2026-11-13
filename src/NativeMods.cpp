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
    DFRunState* run = nullptr;
    const TileAccess* tiles = nullptr;
    Menu* menu = nullptr;
    Entry* editing = nullptr;
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
        host.get_run_state = [](void* context, DFRunState* out) -> int32_t {
            auto& self = *static_cast<Impl*>(context);
            if (!self.run || !out || out->struct_size < sizeof(DFRunState)) return 0;
            *out = *self.run; return 1;
        };
        host.set_run_state = [](void* context, const DFRunState* in) -> int32_t {
            auto& self = *static_cast<Impl*>(context);
            if (!self.run || !in || in->struct_size < sizeof(DFRunState) || in->coins < 0 || in->coins > 1000000 ||
                in->lives < 0 || in->lives > 999 || in->score < 0) return 0;
            *self.run = *in; self.run->struct_size = sizeof(DFRunState); return 1;
        };
        host.get_tile = [](void* context, int32_t x, int32_t y, int32_t* out) -> int32_t {
            auto& self = *static_cast<Impl*>(context);
            if (!out || !self.tiles || !self.tiles->get || !self.level || x < 0 || y < 0 ||
                x >= self.level->width_tiles || y >= self.level->height_tiles) return 0;
            *out = self.tiles->get(x, y); return 1;
        };
        host.set_tile = [](void* context, int32_t x, int32_t y, int32_t id) -> int32_t {
            auto& self = *static_cast<Impl*>(context);
            if (!self.tiles || !self.tiles->set || !self.level || x < 0 || y < 0 ||
                x >= self.level->width_tiles || y >= self.level->height_tiles || id < 0 || id > 65535) return 0;
            self.tiles->set(x, y, id); return 1;
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
                // Bad/missing artwork never prevents an otherwise valid mod loading.
                try {
                    const auto icon = j.value("icon", std::string());
                    if (!icon.empty()) {
                        const auto image = contained(path, path / fs::u8path(icon));
                        if (fs::is_regular_file(image) && fs::file_size(image) <= 2 * 1024 * 1024) e.info.icon = image.u8string();
                    }
                } catch (...) {}
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
            if (e.callbacks.struct_size < offsetof(DFModCallbacks, on_menu_load) + sizeof(e.callbacks.on_menu_load)) e.callbacks.on_menu_load = nullptr;
            if (e.callbacks.struct_size < offsetof(DFModCallbacks, on_menu_action) + sizeof(e.callbacks.on_menu_action)) e.callbacks.on_menu_action = nullptr;
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
    impl->player = nullptr; impl->level = nullptr; impl->run = nullptr; impl->tiles = nullptr;
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
void Loader::tick(double seconds, DFPlayerState& player, const DFLevelState& level, uint64_t generation,
                  DFRunState* run, const TileAccess* tiles) {
    impl->player = &player; impl->level = &level; impl->run = run; impl->tiles = tiles;
    const bool newLevel = impl->generation != generation; impl->generation = generation;
    for (auto* e : impl->loadOrder) if (e->info.loaded) {
        try {
            if (newLevel && e->callbacks.on_level_start) e->callbacks.on_level_start(e->callbacks.userdata, &level);
            if (e->callbacks.on_update) e->callbacks.on_update(e->callbacks.userdata, std::clamp(seconds, 0.0, 0.25));
        } catch (...) { e->info.loaded = false; e->info.status = "Disabled after callback exception; restart required"; }
    }
    impl->player = nullptr; impl->level = nullptr; impl->run = nullptr; impl->tiles = nullptr;
}
void Loader::editMenu(Menu& menu) {
    if (Edition::lite) return;
    impl->menu = &menu;
    DFMenuAPI api{}; api.struct_size = sizeof(api); api.context = impl.get(); api.menu_id = menu.id.c_str();
    api.count = [](void* c) -> uint32_t { auto* self = static_cast<Impl*>(c); return self->menu ? static_cast<uint32_t>(self->menu->items.size()) : 0; };
    api.get = [](void* c, uint32_t index, DFMenuItem* out) -> int32_t {
        auto* self = static_cast<Impl*>(c);
        if (!self->menu || !out || out->struct_size < sizeof(DFMenuItem) || index >= self->menu->items.size()) return 0;
        const auto& row = self->menu->items[index]; *out = {}; out->struct_size = sizeof(*out); out->type = row.type;
        auto copy = [](char* to, size_t size, const std::string& from) { std::memcpy(to, from.data(), std::min(size - 1, from.size())); };
        copy(out->label, sizeof(out->label), row.label); copy(out->command, sizeof(out->command), row.command);
        copy(out->argument, sizeof(out->argument), row.argument); return 1;
    };
    auto row = [](Impl* self, const DFMenuItem* in, MenuItem& out) {
        if (!self->menu || !self->editing || !in || in->struct_size < sizeof(DFMenuItem) ||
            in->type < DF_MENU_TEXT || in->type > DF_MENU_SLIDER) return false;
        auto text = [](const char* p, size_t capacity, std::string& value) {
            const auto* end = static_cast<const char*>(std::memchr(p, 0, capacity));
            if (!end) return false; value.assign(p, end); return true;
        };
        out.type = in->type;
        if (!text(in->label, sizeof(in->label), out.label) || !text(in->command, sizeof(in->command), out.command) ||
            !text(in->argument, sizeof(in->argument), out.argument)) return false;
        if (out.command == "mod_action" && out.argument.find(':') == std::string::npos)
            out.argument = self->editing->info.id + ":" + out.argument;
        return true;
    };
    // Noncapturing functions share the validated converter through a local static.
    using Converter = bool (*)(Impl*, const DFMenuItem*, MenuItem&);
    static const Converter convert = row;
    api.set = [](void* c, uint32_t index, const DFMenuItem* in) -> int32_t {
        auto* self = static_cast<Impl*>(c); MenuItem item;
        if (!self->menu || index >= self->menu->items.size() || !convert(self, in, item)) return 0;
        item.originalIndex = self->menu->items[index].originalIndex;
        self->menu->items[index] = std::move(item); return 1;
    };
    api.insert = [](void* c, uint32_t index, const DFMenuItem* in) -> int32_t {
        auto* self = static_cast<Impl*>(c); MenuItem item;
        if (!self->menu || self->menu->items.size() >= 512 || index > self->menu->items.size() || !convert(self, in, item)) return 0;
        self->menu->items.insert(self->menu->items.begin() + index, std::move(item)); return 1;
    };
    api.remove = [](void* c, uint32_t index) -> int32_t {
        auto* self = static_cast<Impl*>(c);
        if (!self->menu || index >= self->menu->items.size()) return 0;
        self->menu->items.erase(self->menu->items.begin() + index); return 1;
    };
    api.set_title = [](void* c, const char* title) -> int32_t {
        auto* self = static_cast<Impl*>(c); if (!self->menu || !title) return 0;
        size_t length = 0; while (length < 256 && title[length]) ++length;
        if (length == 256) return 0; self->menu->title.assign(title, length); return 1;
    };
    for (auto* entry : impl->loadOrder) if (entry->info.loaded && entry->callbacks.on_menu_load) {
        impl->editing = entry;
        try { entry->callbacks.on_menu_load(entry->callbacks.userdata, &api); }
        catch (...) { entry->info.loaded = false; entry->info.status = "Disabled after menu callback exception"; }
    }
    impl->editing = nullptr; impl->menu = nullptr;
}
void Loader::menuAction(const std::string& menu, const std::string& action) {
    const auto separator = action.find(':'); if (separator == std::string::npos || Edition::lite) return;
    for (auto* entry : impl->loadOrder) if (entry->info.loaded && entry->info.id == action.substr(0, separator) && entry->callbacks.on_menu_action) {
        try { entry->callbacks.on_menu_action(entry->callbacks.userdata, menu.c_str(), action.substr(separator + 1).c_str()); }
        catch (...) { entry->info.loaded = false; entry->info.status = "Disabled after menu action exception"; }
        break;
    }
}

}
