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
// Ships get_by_path (the pure-read own-asset resolver) plus the four runtime
// verbs (get_by_name / declare / register / replace) against the runtime store.
// The shared resolution helpers (declared below in this header) back BOTH this
// Lua surface and the C++ kcdxAssetInterface mirror.
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

// === Shared runtime-verb operations — ONE resolution path for BOTH surfaces ===
//
// The semantic core of declare / get_by_name / register / replace, factored to
// take the resolved (author, plugin) explicitly so the Lua binder (which reads
// the caller off the Lua callstack) AND the C++ kcdxAssetInterface thunks (which
// read the caller off the `self` handle) call the SAME function — no parallel
// C++ copy of the packing / classification / store-write logic (one shared
// resolution path; full Lua<->C++ parity, the other language's spelling). Each fills
// `outDisk` (the loadable path) + returns true on success, or fills `outErr`
// (the teaching message) + returns false on failure — the SAME (path | err)
// shapes both surfaces present (the Lua binder pushes them as (path) / (nil,
// err); the C++ thunk returns the path / nullptr-and-logs-the-err). On every
// failure path the operation ALSO emits the structured LOG_*_KV teaching line
// (category ASSET_GET / ASSET_RUNTIME) the dev log carries, so the C++ author
// (who gets no return-side err) still reads the cause in the log.

// declare(name, file): publish (author, plugin, bare) -> the resolved disk path
// of `file`, returning that path (the value a later GetPublishedAsset yields).
bool DeclareAsset(const std::string& author, const std::string& plugin,
                  const std::string& bare, const std::string& file,
                  std::string& outDisk, std::string& outErr);

// get_by_name(name): resolve (author, plugin, bare) in the published-name store.
bool GetPublishedAsset(const std::string& author, const std::string& plugin,
                       const std::string& bare, std::string& outDisk,
                       std::string& outErr);

// register(vpath, file): write the runtime overlay vpath -> file; return file's
// loadable path.
bool RegisterAsset(const std::string& author, const std::string& plugin,
                   const std::string& vpath, const std::string& file,
                   std::string& outDisk, std::string& outErr);

// replace(target, with): write the runtime overlay keyed by `target` — a vanilla
// vpath, OR a packed cross-mod published name ("<author>.<plugin>.<bare>") routed
// through ResolvePublishedVpath (design §5.3); return `with`'s loadable path.
bool ReplaceAsset(const std::string& author, const std::string& plugin,
                  const std::string& target, const std::string& with,
                  std::string& outDisk, std::string& outErr);

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
