#pragma once

// kcdx.assets.* — the asset authoring DOMAIN. See lua_bind_assets.cpp for the
// surface contract.

extern "C" {
#include "lua.h"
}

#include <string>

namespace kcdx::lua_bind_assets {

// Register the `kcdx.assets` capability DOMAIN (a sub-table, like kcdx.cvar.* /
// kcdx.plugin.*) on the kcdx table at the top of the Lua stack. Stack effect: 0.
//
// Ships get_by_path (the pure-read own-asset resolver). The four runtime verbs
// (get_by_name / declare / register / replace) are a later step — they need the
// runtime store; their contract is pinned by NYI docs + deliberately-failing
// matrix rows, not by a stub here.
void bind(lua_State* L);

// Resolve a plugin-relative asset path to its on-disk loadable path for the
// plugin identified by (author, plugin). The shared resolver behind BOTH the
// own-asset form (kcdx.assets.get_by_path, author/plugin = the calling plugin)
// AND the cross-plugin form (kcdx.plugin.<a>.<p>.assets.get_by_path, author/
// plugin from the navigated handle). REUSES the same folderPath /
// assetsEntrypointRel join + '..'-traversal reject + is_regular_file check the
// asset_sidecar resolver uses — the loadable path is the absolute disk path the
// HOOK-2 open serves, NOT a parallel resolution.
//
// Returns true and fills `outDisk` (absolute) on success. On failure returns
// false and fills `outErr` with a teaching message naming the missing path (the
// author's terms) — the caller turns that into the (nil, err) Lua return.
// `author` may be empty (the legacy 1-dot tier — the lookup matches on `plugin`
// alone then); `plugin` empty is always a no-such-plugin failure.
bool ResolveAssetPath(const std::string& author, const std::string& plugin,
                      const std::string& relPath, std::string& outDisk,
                      std::string& outErr);

// The .assets leaf on a kcdx.plugin.<author>.<plugin> handle. Pushed by
// lua_bind_plugin's plugin-handle __index when the accessed key is "assets":
// returns a userdata whose own __index exposes get_by_path bound to the
// navigated (author, plugin). Keeps the cross-plugin asset surface owned HERE
// (asset concern) while lua_bind_plugin owns the navigation chain. `author` and
// `plugin` are the resolved segments off the handle's envtable.
//
// Stack: pushes ONE value (the assets-domain userdata) and returns 1, matching
// the __index contract lua_bind_plugin's handle resolver expects.
int PushPluginAssetsDomain(lua_State* L, const std::string& author,
                           const std::string& plugin);

}  // namespace kcdx::lua_bind_assets
