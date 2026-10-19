#ifndef PLUGIN_MANAGER_HPP
#define PLUGIN_MANAGER_HPP
// =============================================================================
// Native plugin ABI loader — Phase 11 path A (dlopen-based C++ plugins).
// Roadmap pivots recorded in phases.md §Phase 11: "Hybrid now" maintainer pick
// ships this minimal boot-time loader + /plugins backend first, deferring the
// embedded-scripting half (B. Lua via Sol/QuickJS) to Phase 30's dual engine.
//
// Contract: plugins live in ./plugins/*.so next to the server binary (created
// dir at boot if absent). A plugin .so exports extern "C" symbols:
//   on_join(const char*), on_chat(const char*), on_tick(), on_command(const char*)
// Hooks are looked up via dlsym after dlopen; missing optionals stay silent.
// ABI is LightweightMC's own (no Bukkit source compat — that path needs a JVM
// or the scripting layer, see phases.md).
// =============================================================================

#include <string>
#include <vector>
namespace LightweightMC::Plugin
{
    // Boot: scan ./plugins for *.so, dlopen each, log `Registered plugin: NAME (path)`.
    void loadRegisteredPlugins();

    // Reload one .so by absolute/relative path at runtime (`/plugins load <path>`);
    // returns the registered plugin name on success, empty string on failure.
    std::string loadPluginPath(const std::string &path);

    // dlclose + forget a loaded plugin by name; false if unknown/not loaded.
    bool unloadPluginByName(const std::string &name);

    // Display lines "NAME (path) [active]" for /plugins list output.
    std::vector<std::string> describeLoaded();
}

#endif