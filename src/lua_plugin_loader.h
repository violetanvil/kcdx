#pragma once

// kcdx::lua_plugin_loader — executes each enabled plugin's
// [entrypoints].lua files against the live game lua_State.
//
// Phase 2b sub-4 of the manifest-only restructure (see
// docs/outstanding-work/restructure-plan.md). The manifest parser
// (config.cpp) records each plugin's lua entrypoints into
// PluginManifest::luaEntrypointsRel, but until this module nothing
// RAN them. RunAll() is called from the first-update-tick orchestration
// in hooks.cpp, after RegisterKcdxTable has made the kcdx.* surface
// live and the VM is bound, and BEFORE the deferred-apply pass
// (lua_registry::ApplyZone) — so a plugin.lua's kcdx.hook/.bytes calls
// queue their intent, then the apply pass installs everything in
// unified load order.
//
// Each file is loaded via luaL_loadfile + lua_pcall inside a
// kcdx::guard::Call SEH guard. A plugin.lua that errors (syntax error,
// runtime error, fault) is logged and skipped — it cannot take down the
// engine or other plugins (the error-isolation contract from the plan's
// "Error isolation" section). Before each file runs, the loader
// registers its script path → owning plugin name with
// lua_registry::RegisterScriptOwner so kcdx.* calls made from that file
// attribute to the right plugin's load-order row.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_plugin_loader {

// Run every enabled plugin's [entrypoints].lua files against `L`, in
// the order plugins appear in kcdx::plugins::g_plugins (the post-topo-
// sort order). Registration order within a plugin follows the declared
// luaEntrypointsRel order. Safe to call once per session; subsequent
// calls are no-ops (guarded by an internal latch) so the first-tick
// path can call it unconditionally.
//
// NOTE: cross-plugin hook CHAIN order is decided later, at apply time,
// by lua_registry::ApplyZone's load-order sort — not by the order
// plugin.lua files happen to run here. This function only needs to get
// every plugin's registrations queued with correct attribution.
void RunAll(lua_State* L);

}  // namespace kcdx::lua_plugin_loader
