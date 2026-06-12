#pragma once

// kcdx::behavior_catalog_loader — loads the engine behavior catalog
// (<kcdx-engine>/behavior-catalog/*.lua) as a builtin behavior pack.
//
// Each catalog file is a normal Lua source calling kcdx.behavior.declare(...)
// EXACTLY as a plugin would — a bare name + spec, zero hex (the disassembler
// test). The loader runs every file under an EngineCatalogScope so each
// declare stamps the reserved kcdx.behavior.<bare> root via the engine's own
// identity (behavior_registry::DeclareEngine), distinct from a user plugin's
// <author>.<plugin>.<bare>. The catalog registers under kcdx.behavior.*
// legitimately — through the engine identity, never by spoofing an author
// ([plugin].author = "kcdx" is hard-rejected by author validation).
//
// PIN-AHEAD ORDERING: RunCatalog is called from the first-update-tick block in
// hooks.cpp AFTER RegisterKcdxTable has made kcdx.behavior.* live and the VM is
// bound, and STRICTLY BEFORE lua_plugin_loader::RunAll(L) — so every
// kcdx.behavior.* name is declared before any user plugin's load-time set runs.
// The ordering guarantee is CALL-SITE PLACEMENT, not a priority/zone mechanism.
//
// Each file is loaded via luaL_loadfile + lua_pcall inside a kcdx::guard::Call
// SEH guard, so one malformed file cannot break the engine or the rest of the
// pack. A malformed file is a BOOT-TIME builtin-pack error: surfaced LOUD via
// the structured KV logger (it does NOT silently skip), exactly as any builtin
// failure is. Runs once per session (internal latch).

extern "C" {
#include "lua.h"
}

namespace kcdx::behavior_catalog_loader {

// Discover + run every <kcdx-engine>/behavior-catalog/*.lua file (sorted by
// filename for determinism) against `L`, each under an EngineCatalogScope so
// its declares stamp the reserved kcdx.behavior.<bare> root. Call ONCE, from
// the first-update-tick block, BEFORE lua_plugin_loader::RunAll(L). A null `L`,
// a missing catalog dir, or a malformed file each logs LOUD and does not abort
// the engine; the catalog is skipped only when there is nothing to load.
// Safe to call once per session; subsequent calls are no-ops (internal latch).
void RunCatalog(lua_State* L);

}  // namespace kcdx::behavior_catalog_loader
