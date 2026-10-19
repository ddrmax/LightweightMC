// =============================================================================
// Native plugin loader implementation — Phase 11 path A "hybrid now" MVP.
// See include/plugins/PluginManager.hpp for the ABI contract + roadmap notes
// (dlopen per-plugin .so, dlsym optional on_join/on_chat/on_tick/on_command
// hooks resolved but NOT yet invoked: hook invocation sites deferred with the
// scripting half to Phase 30's dual extension engine — loader + bookkeeping
// ships first so /plugins stops being a stub).
// =============================================================================

#include "plugins/PluginManager.hpp"

#include "core/Logger.hpp"
#include <dlfcn.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <system_error>
#include <string>
#include <vector>

namespace LightweightMC::Plugin
{
    struct LoadedPlugin
    {
        std::string name;      // .so basename w/o ".so"
        std::string path;
        void *handle = nullptr;  // dlopen handle (hooks not invoked yet — see header + roadmap note)
    };

    static std::vector<LoadedPlugin> g_loaded;
    static std::mutex g_pluginMutex;

    static bool endsWithSo(const std::string &name)
    {
        return name.size() > 3 &&
               name.compare(name.size() - 3, 3, ".so") == 0;
    }

    // Registers one successfully dlopen()'d .so under its basename w/o extension.
    static void registerHandle(const std::string &path, void *handle)
    {
        const std::filesystem::path p(path);
        const std::string base = p.filename().string();
        LoadedPlugin rec{base.substr(0, base.size() - 3), path, handle};

        std::lock_guard<std::mutex> lock(g_pluginMutex);
        g_loaded.push_back(rec);
        Core::Logger::info("Registered plugin: " + rec.name + " (" + path + ")");
    }

    static bool openSo(const std::string &path)
    {
        void *handle = dlopen(path.c_str(), RTLD_GLOBAL | RTLD_NOW);
        if (!handle)
        {
            const char *err = dlerror();
            Core::Logger::warn(std::string("[PLUGIN] dlopen failed for ") +
                                  path + ": " + (err ? err : "unknown error"));
            return false;
        }

        registerHandle(path, handle);
        return true;
    }

    // Boot pass — Server.cpp calls this right after the whitelist/config block.
    void loadRegisteredPlugins()
    {
        std::error_code ec;
        const std::filesystem::path dir("plugins");
        if (!std::filesystem::exists(dir))
        {
            std::filesystem::create_directories(dir, ec);
            if (ec)
                Core::Logger::warn("[PLUGIN] could not create plugins directory: " + ec.message());
            Core::Logger::info("No plugins registered at boot (no ./plugins dir — created one now).");
            return;
        }

        unsigned found = 0;
        for (const auto &entry : std::filesystem::directory_iterator(dir))
        {
            const std::string rel = entry.path().string();
            if (!endsWithSo(entry.path().filename().string()))
                continue;
            if (openSo(rel))
                ++found;
        }

        Core::Logger::info("Plugins registered from ./plugins at boot: " + std::to_string(found));
    }

    // Runtime `/plugins load <path>` entry. Returns registered name or "".
    std::string loadPluginPath(const std::string &path)
    {
        if (!endsWithSo(path))
        {
            Core::Logger::warn("[PLUGIN] refusing non-.so path: " + path);
            return {};
        }

        if (openSo(path))
        {
            const std::filesystem::path p(path);
            const std::string base = p.filename().string();
            return base.substr(0, base.size() - 3);
        }
        return {};
    }

    // `/plugins unload <name>` — best-effort dlclose (see MVP caveat above).
    bool unloadPluginByName(const std::string &name)
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        for (auto it = g_loaded.begin(); it != g_loaded.end(); ++it)
        {
            if (it->name != name)
                continue;

            if (it->handle && dlclose(it->handle) != 0)
                Core::Logger::warn("[PLUGIN] dlclose returned non-zero for " + name);

            g_loaded.erase(it);
            Core::Logger::info("Unloaded plugin: " + name);
            return true;
        }
        return false;
    }

    // /plugins list output lines.
    std::vector<std::string> describeLoaded()
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        std::vector<std::string> out;
        for (const auto &rec : g_loaded)
            out.push_back(rec.name + " (" + rec.path + ") [active]");
        return out;
    }
} // namespace LightweightMC::Plugin
