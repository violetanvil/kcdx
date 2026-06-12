#pragma once

// kcdx.behavior.* — the named-behavior Lua domain. See lua_bind_behavior.cpp
// for the surface contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_behavior {

// Register the kcdx.behavior domain table (declare / get / list) on the kcdx
// table at the top of the Lua stack — a GROUPED capability domain, built like
// kcdx.cvar.* / kcdx.assets.*. Stack effect: 0.
void bind(lua_State* L);

// Engine-catalog declare scope — RAII. While alive, a kcdx.behavior.declare
// call routes to the ENGINE-identity declare path (behavior_registry::
// DeclareEngine, stamping the reserved kcdx.behavior.<bare> root) instead of
// the plugin path (DeclarePlugin, <author>.<plugin>.<bare>). The catalog
// loader (src/behavior_catalog_loader.cpp) wraps each catalog .lua file's
// execution in one of these so the file's declares — authored EXACTLY as a
// plugin would write them, a bare name + spec — stamp under the engine root
// via the engine's identity, never by spoofing an author. Main-thread only
// (catalog files run on the game main thread in the first-tick block, the same
// thread as RunAll); the flag is a plain bool, not atomic. Nestable is not
// required (one file at a time), but the guard restores the PRIOR value so a
// stray nested scope is still correct.
class EngineCatalogScope {
public:
    EngineCatalogScope();
    ~EngineCatalogScope();
    EngineCatalogScope(const EngineCatalogScope&)            = delete;
    EngineCatalogScope& operator=(const EngineCatalogScope&) = delete;
private:
    bool prior_;
};

}  // namespace kcdx::lua_bind_behavior
