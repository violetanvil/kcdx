// kcdx.assets.* — the asset authoring DOMAIN (Lua surface).
//
// A GROUPED capability DOMAIN (kcdx.assets.*, like kcdx.cvar.* / kcdx.plugin.*,
// NOT a top-level verb — asset operations are a capability domain, not one of
// the closed-set core registration verbs). design asset-replacement.md §5.
//
// All FIVE verbs are LIVE: get_by_path (pure read), get_by_name / declare /
// register / replace (against the §5.1 runtime stores in asset_namespace). The
// own-form verbs resolve the calling plugin; the §6 cross-plugin form
// (kcdx.plugin.<a>.<p>.assets.get_by_path / .get_by_name) resolves a navigated
// plugin. replace serves BOTH a vanilla-path target AND a packed cross-mod
// published name ("<author>.<plugin>.<bare>"): the cross-mod form resolves the
// name -> the publisher's serve-vpath (design §5.3) and keys the runtime-overlay
// store by THAT vpath, so B's `with` serves where A's published asset would. A
// packed target resolving to no published asset is an AP14 teaching error naming
// it, never a silent non-serve. The C++ mirror (kcdxAssetInterface) is a later
// step (docs/cpp NYI markers).
//
//   kcdx.assets.get_by_path(path) -> loadable path | (nil, err)
//
// get_by_path resolves the CALLING plugin's OWN asset (no owner prefix — the
// engine knows who you are, naming-namespaces.md "never type your own prefix")
// to a loadable on-disk path: the absolute disk path the asset-resolution seam
// (HOOK 2) opens to serve the file. It is a PURE READ — it mutates no store and
// depends on none of the §5.1 runtime store; it joins the calling plugin's
// assets/ root with the relative path and confirms the file exists, reusing the
// SAME resolution the sidecar parser uses (folderPath / assetsEntrypointRel /
// rel, '..'-traversal rejected, is_regular_file). A path to a file NOT in the
// plugin's assets/ returns a TEACHING ERROR naming the missing path (AP14),
// never a silent nil.
//
//   local icon = kcdx.assets.get_by_path("icons/my_icon.dds")
//   -- icon is a loadable path you hand to a game asset API.
//
// CROSS-PLUGIN form — kcdx.plugin.<author>.<plugin>.assets.get_by_path(path) —
// resolves through the step-6 navigable namespace (lua_bind_plugin.cpp). The
// .assets leaf on a resolved plugin handle is wired by PushPluginAssetsDomain
// (below): it binds get_by_path to the NAVIGATED (author, plugin), so the same
// resolver serves another mod's asset by path. The path stays a quoted string
// (it is data); the namespace is bare dotted (§6).
//
//   local shirt = kcdx.plugin.redmoon.outfit_swap.assets.get_by_path("male/shirt.dds")
//
// Lua bridge (lua-bridge.md): raw Lua C API only — lua_pushcfunction +
// lua_setfield, a registry-keyed metatable for the cross-plugin assets userdata.
// NO kcdx-side static-const sentinel (AP5); the frealloc canary stays zero.
// Returns a STRING (a path), never a pointer — the lua_Number=float precision
// hazard (lua-precision.md) does NOT apply.

#include "lua_bind_assets.h"

#include <filesystem>
#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "asset_namespace.h"  // the RUNTIME stores: RegisterRuntimeOverlay /
                              // PublishName / LookupPublishedName (the four
                              // runtime verbs read/write these — design §5.1)
#include "asset_overlay.h"    // NormalizeVPath — the shared key fold for register
#include "log.h"
#include "lua_registry.h"   // OwningPluginForCurrentCall — the calling-plugin seam
#include "plugin_loader.h"  // g_plugins, PluginManifest (folderPath / assetsEntrypointRel)

namespace fs = std::filesystem;

namespace kcdx::lua_bind_assets {

namespace {

// Stable log category for the resolution teaches (greppable in kcdx-dev.log;
// the agent reads these for cap-75's falsifiable rows).
constexpr const char* kCat = "ASSET_GET";

// Registry key for the cross-plugin assets-domain userdata metatable (the
// userdata kcdx.plugin.<a>.<p>.assets resolves to). Installed once, idempotent.
constexpr const char* kPluginAssetsMt = "kcdx.plugin.assets";

// Find the loaded plugin's manifest by (author, plugin). REUSES the
// match-on-both-components discipline lua_bind_plugin::HandleForAuthorPlugin
// uses — FindByName keys on [plugin].name ALONE, so it cannot disambiguate two
// authors reusing a plugin name; the (author, plugin) pair is the identity
// (naming-namespaces.md). An empty `author` is the legacy 1-dot tier (the
// corpus today; [plugin].author not yet declared everywhere) — match on
// `plugin` alone then. Returns nullptr on no match.
const plugins::PluginManifest* FindManifest(const std::string& author,
                                            const std::string& plugin) {
    if (plugin.empty()) return nullptr;
    for (const auto& p : kcdx::plugins::g_plugins) {
        if (p.manifest.name != plugin) continue;
        if (author.empty() || p.manifest.author == author) {
            return &p.manifest;
        }
    }
    return nullptr;
}

// Pack a (author, plugin, bare) triple into the published-name store key
// "<author>.<plugin>.<bare>" (naming-namespaces.md — the shared-name model). The
// legacy 1-dot tier (author empty) packs as "<plugin>.<bare>" so an own declare
// and an own get_by_name agree on the key even before [plugin].author is
// declared everywhere. ASCII-lowercased so a declare and a later get_by_name
// agree case-insensitively (the namespace is case-insensitive, like the vpath
// fold); the bare name and the prefix are author-typed text.
std::string PackName(const std::string& author, const std::string& plugin,
                     const std::string& bare) {
    std::string packed = author.empty() ? (plugin + "." + bare)
                                        : (author + "." + plugin + "." + bare);
    for (char& c : packed) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return packed;
}

// Lowercase an ALREADY-PACKED author-written cross-mod name ("a.b.bare") so it
// keys into the published-name store with the SAME ASCII-lowercase fold PackName
// applies to an own declare's packed key. The author types the packed target
// verbatim (e.g. "redmoon.outfit.belt"); the store key is case-insensitive (the
// namespace is, like the vpath fold), so a B-side replace target and the A-side
// declare's published key agree regardless of the author's case. NOT PackName
// (that builds a key from a (author,plugin,bare) triple + a derived prefix); this
// folds a name the author already wrote in full.
std::string LowerPackedName(const std::string& packed) {
    std::string out = packed;
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

// Is `target` a PACKED CROSS-MOD published name ("<author>.<plugin>.<bare>") vs
// a vanilla vpath (a file path)? MIRRORS the sidecar's classifier exactly
// (asset_sidecar.cpp `LooksLikePublishedName`, the authority for this same
// vanilla-vs-published disambiguation): a separator ('/' or '\\') means a path,
// not a name; a published name is <author>.<plugin>.<bare> → at least two dots.
// A bare-but-unslashed token with < 2 dots is a vanilla path (the engine's open
// is the arbiter). REUSED, not a third classifier — `LooksLikePublishedName`
// lives in asset_sidecar.cpp's anonymous namespace (not header-exposed), so this
// is its exact rule, cited. (naming-namespaces.md — the dot-separated
// <author>.<plugin>.<bare> shared-namespace shape.)
bool LooksLikePackedCrossModName(const std::string& s) {
    if (s.find('/') != std::string::npos ||
        s.find('\\') != std::string::npos) {
        return false;  // has a separator → a path, not a name
    }
    size_t dots = 0;
    for (char c : s) if (c == '.') ++dots;
    return dots >= 2;
}

// Resolve the CALLING plugin's (author, plugin) for the own-form runtime verbs
// (declare / get_by_name / register / replace). Walks the Lua callstack to the
// nearest attributed plugin script (OwningPluginForCurrentCall); an anonymous
// caller (console / pak Lua / ad-hoc) returns false — those verbs resolve the
// CALLER's own namespace, which an anonymous caller does not have. On success
// fills `outAuthor` / `outPlugin`. Mirrors Lua_AssetsGetByPath's owner resolve.
bool ResolveCaller(lua_State* L, std::string& outAuthor,
                   std::string& outPlugin) {
    std::string callSiteFile;
    int callSiteLine = 0;
    const kcdx::lua_registry::OwningPlugin owner =
        kcdx::lua_registry::OwningPluginForCurrentCall(L, callSiteFile,
                                                       callSiteLine);
    if (owner.plugin.empty()) return false;
    outAuthor = owner.author;
    outPlugin = owner.plugin;
    return true;
}

// kcdx.assets.declare(name, file) — publish the caller's <author>.<plugin>.<name>
// -> the resolved disk path of `file` (US-5; the programmatic peer of a sidecar
// `name`). `file` is resolved like get_by_path (the caller's assets/, '..'-reject
// + is_regular_file), so a declared name always points at a real loadable file.
// Two failure shapes, the kcdx-binder idiom: bad arg / no caller / unresolvable
// file -> (nil, teaching err), never a silent nil (AP14). On success returns the
// loadable path (the same value a later get_by_name yields) so the author can use
// it immediately AND publish it in one call.
int Lua_AssetsDeclare(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING || lua_type(L, 2) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.assets.declare(name, file): expects two string arguments — a "
            "stable published NAME (e.g. \"shirt\") and the FILE it names, "
            "relative to YOUR OWN assets/ folder (e.g. "
            "kcdx.assets.declare(\"shirt\", \"male/shirt.dds\")). The name "
            "publishes as <author>.<plugin>.shirt so other mods can reference "
            "it; you type only the bare name.");
        return 2;
    }
    size_t nlen = 0, flen = 0;
    const char* ns = lua_tolstring(L, 1, &nlen);
    const char* fs = lua_tolstring(L, 2, &flen);
    const std::string bare = (ns && nlen) ? std::string(ns, nlen) : std::string();
    const std::string file = (fs && flen) ? std::string(fs, flen) : std::string();
    if (bare.empty() || file.empty()) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.assets.declare(name, file): `name` and `file` must both be "
            "non-empty — a stable published name and the asset it names "
            "(relative to your assets/ folder).");
        return 2;
    }

    std::string author, plugin;
    if (!ResolveCaller(L, author, plugin)) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.assets.declare(name, file): no calling plugin — declare "
            "publishes a name in YOUR OWN namespace, so it must be called from "
            "a plugin's plugin.lua (or a file it require()s). An anonymous "
            "caller (console / ad-hoc Lua) has no namespace to publish into.");
        return 2;
    }

    // Resolve `file` to a loadable disk path through the caller's assets/ (the
    // SAME resolver get_by_path uses — a published name always points at a real
    // file). A bad file fails loud with the resolver's teaching error (AP14).
    std::string disk, err;
    if (!ResolveAssetPath(author, plugin, file, disk, err)) {
        lua_pushnil(L);
        lua_pushlstring(L, err.data(), err.size());
        return 2;
    }

    // Publish <author>.<plugin>.<bare> -> { disk, serve-vpath } into the runtime
    // published-name store (RCU write). A later get_by_name(bare) from this plugin
    // (or kcdx.plugin.<a>.<p>.assets.get_by_name(bare) from another) resolves the
    // disk path; a cross-mod replace("<a>.<p>.bare", with) resolves the SERVE-
    // VPATH (design §5.3). A runtime declare is a PURE ADD-NEW publish — its
    // serve-vpath is the asset's OWN add-new vpath = `file`'s path relative to the
    // caller's assets/ (the vpath the engine opens it at), which is exactly the
    // `file` arg the author named. Normalize it through the SHARED fold (the same
    // NormalizeVPath the overlay map + resolver use) so a B-side cross-mod replace
    // keys the overlay store with a key the resolver matches when the engine opens
    // that vpath. (If the author ALSO runtime-replaces this file at a vanilla
    // vpath, that is a separate replace call keyed independently; declare itself
    // publishes the add-new vpath — §5.3 settled reading.)
    const std::string packed = PackName(author, plugin, bare);
    const std::string serveVpath = asset_overlay::NormalizeVPath(file);
    kcdx::asset_namespace::PublishName(packed, disk, serveVpath);
    LOG_DEBUG_KV(kCat, "declared",
        kcdx::log::KV("name", packed),
        kcdx::log::KV("disk", disk),
        kcdx::log::KV("serve_vpath", serveVpath));

    lua_pushlstring(L, disk.data(), disk.size());
    return 1;
}

// kcdx.assets.get_by_name(name) — resolve the caller's OWN published name to a
// loadable path (US-5; the read peer of declare). Builds the caller's packed
// <author>.<plugin>.<name> and looks it up in the published-name store. A name
// the caller never declared -> a teaching error naming it (AP14), never a silent
// nil. The §6 cross-plugin form is the separate Lua_CrossPluginGetByName closure.
int Lua_AssetsGetByName(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.assets.get_by_name(name): expects a single string argument — "
            "a name YOU published with kcdx.assets.declare (e.g. "
            "kcdx.assets.get_by_name(\"shirt\")). Returns a loadable path. To "
            "resolve ANOTHER mod's published name, navigate the namespace: "
            "kcdx.plugin.<author>.<plugin>.assets.get_by_name(name).");
        return 2;
    }
    size_t nlen = 0;
    const char* ns = lua_tolstring(L, 1, &nlen);
    const std::string bare = (ns && nlen) ? std::string(ns, nlen) : std::string();
    if (bare.empty()) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.assets.get_by_name(name): `name` must be non-empty — a name "
            "you published with kcdx.assets.declare.");
        return 2;
    }

    std::string author, plugin;
    if (!ResolveCaller(L, author, plugin)) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.assets.get_by_name(name): no calling plugin — get_by_name "
            "resolves YOUR OWN published name, so it must be called from a "
            "plugin's plugin.lua (or a file it require()s). To resolve a "
            "specific plugin's published name use "
            "kcdx.plugin.<author>.<plugin>.assets.get_by_name(name).");
        return 2;
    }

    const std::string packed = PackName(author, plugin, bare);
    std::string disk;
    if (!kcdx::asset_namespace::LookupPublishedName(packed, disk)) {
        lua_pushnil(L);
        const std::string ident =
            author.empty() ? plugin : (author + "." + plugin);
        lua_pushstring(L,
            ("kcdx.assets.get_by_name(\"" + bare + "\"): no published name '" +
             bare + "' in plugin '" + ident + "' — publish it first with "
             "kcdx.assets.declare(\"" + bare + "\", \"<your asset path>\"). "
             "Only names you declared are resolvable; a typo is a loud error, "
             "never a silent nil.").c_str());
        return 2;
    }
    lua_pushlstring(L, disk.data(), disk.size());
    return 1;
}

// kcdx.assets.register(vpath, file) — add a runtime vpath -> file overlay (US-6;
// make a not-at-load asset available, taking effect for opens THEREAFTER). `file`
// is resolved like get_by_path (the caller's assets/), so the served file is
// always a real loadable file. Writes the runtime-overlay store (RCU) keyed by
// the NORMALIZED vpath; the resolver serves it the same way a build-time overlay
// is served. Bad arg / no caller / unresolvable file -> teaching error (AP14).
int Lua_AssetsRegister(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING || lua_type(L, 2) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.assets.register(vpath, file): expects two string arguments — "
            "the VIRTUAL PATH the game will open (e.g. "
            "\"Libs/UI/Textures/MyGen.dds\") and the FILE that serves it, "
            "relative to YOUR OWN assets/ folder. The asset becomes resolvable "
            "for opens AFTER this call.");
        return 2;
    }
    size_t vlen = 0, flen = 0;
    const char* vs = lua_tolstring(L, 1, &vlen);
    const char* fs = lua_tolstring(L, 2, &flen);
    const std::string vpath = (vs && vlen) ? std::string(vs, vlen) : std::string();
    const std::string file = (fs && flen) ? std::string(fs, flen) : std::string();
    if (vpath.empty() || file.empty()) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.assets.register(vpath, file): `vpath` and `file` must both be "
            "non-empty — the virtual path the game opens and the asset that "
            "serves it (relative to your assets/ folder).");
        return 2;
    }

    std::string author, plugin;
    if (!ResolveCaller(L, author, plugin)) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.assets.register(vpath, file): no calling plugin — register "
            "adds an overlay from YOUR OWN asset, so it must be called from a "
            "plugin's plugin.lua (or a file it require()s).");
        return 2;
    }

    std::string disk, err;
    if (!ResolveAssetPath(author, plugin, file, disk, err)) {
        lua_pushnil(L);
        lua_pushlstring(L, err.data(), err.size());
        return 2;
    }

    // Write the runtime-overlay store (RCU). Keyed by NORMALIZED vpath inside
    // RegisterRuntimeOverlay (the SAME fold the resolver uses), so a runtime open
    // of `vpath` hits regardless of the engine's case/separator form. Take-effect
    // = thereafter (§3 US-6) — an already-open handle is NOT re-resolved.
    kcdx::asset_namespace::RegisterRuntimeOverlay(vpath, disk, plugin);

    lua_pushlstring(L, disk.data(), disk.size());
    return 1;
}

// kcdx.assets.replace(target, with) — register a runtime REPLACEMENT keyed by the
// target (US-6; the conditional-replacement case). THIS STEP serves only the
// VANILLA-PATH `target` form (\"Libs/UI/Textures/KCDLogo.dds\"): it writes the
// runtime-overlay store keyed by the NORMALIZED vpath — the same store register
// writes; a vanilla replace is a register whose key is an EXISTING vpath rather
// than a new one. `with` is the replacement FILE, relative to the caller's
// assets/ (resolved like get_by_path). Bad arg / no caller / unresolvable file
// -> teaching error (AP14).
//
// The PACKED cross-mod `target` form (\"author.plugin.bare\", the §6 string-key
// peer of kcdx.plugin.<a>.<p>.*) RESOLVES cross-mod (design §5.3) — the two hops:
// (1) resolve the packed name -> the SERVE-VPATH the other mod's asset serves at
// (ResolvePublishedVpath, the published-name store carries it); (2) key the
// runtime-overlay store by THAT serve-vpath, so B's `with` wins when the engine
// opens the vpath A's asset serves at (B wins by load order — the runtime store's
// take-effect-thereafter is the runtime analogue of §4.4). A packed name that
// resolves to NO published asset is an AP14 teaching error naming the unresolved
// name (the publisher must declare it first, and before B's replace — the
// "thereafter" ordering of §3 US-6). The author writes A's NAME, never A's vpath
// (the disassembler test, cornerstones.md) — the engine carries the serve-vpath.
int Lua_AssetsReplace(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING || lua_type(L, 2) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.assets.replace(target, with): expects two string arguments — "
            "the TARGET to replace (a vanilla asset path like "
            "\"Libs/UI/Textures/KCDLogo.dds\", OR another mod's published name "
            "like \"redmoon.outfit.belt\") and the FILE that replaces it, "
            "relative to YOUR OWN assets/ folder. Takes effect for opens AFTER "
            "the call.");
        return 2;
    }
    size_t tlen = 0, wlen = 0;
    const char* ts = lua_tolstring(L, 1, &tlen);
    const char* ws = lua_tolstring(L, 2, &wlen);
    const std::string target =
        (ts && tlen) ? std::string(ts, tlen) : std::string();
    const std::string with =
        (ws && wlen) ? std::string(ws, wlen) : std::string();
    if (target.empty() || with.empty()) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.assets.replace(target, with): `target` and `with` must both "
            "be non-empty — the asset to replace and the file that replaces it "
            "(relative to your assets/ folder).");
        return 2;
    }

    // Resolve the CALLING plugin + the `with` file FIRST (shared by both target
    // forms — replace serves YOUR OWN file). The overlay KEY then depends on the
    // target form: a vanilla path keys by the path itself; a packed cross-mod name
    // keys by the serve-vpath it resolves to (the two-hop §5.3 resolution below).
    std::string author, plugin;
    if (!ResolveCaller(L, author, plugin)) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.assets.replace(target, with): no calling plugin — replace "
            "serves YOUR OWN file, so it must be called from a plugin's "
            "plugin.lua (or a file it require()s).");
        return 2;
    }

    std::string disk, err;
    if (!ResolveAssetPath(author, plugin, with, disk, err)) {
        lua_pushnil(L);
        lua_pushlstring(L, err.data(), err.size());
        return 2;
    }

    // Determine the overlay KEY the runtime-overlay store is keyed by.
    //   * PACKED cross-mod name ("<author>.<plugin>.<bare>") — the two-hop §5.3
    //     resolution: resolve the packed name -> the publisher's SERVE-VPATH
    //     (ResolvePublishedVpath), and key the overlay store by THAT vpath so B's
    //     `with` wins where A's published asset serves. A name resolving to NO
    //     published asset is an AP14 teaching error naming the unresolved name
    //     (A must declare it first, AND before B's replace — §3 US-6 "thereafter"
    //     ordering). The author writes A's NAME, never A's vpath (the disassembler
    //     test). (Classified by the sidecar's exact rule — LooksLikePackedCross-
    //     ModName mirrors asset_sidecar.cpp::LooksLikePublishedName.)
    //   * VANILLA path — key by the path itself (the dominant US-6 case, unchanged
    //     from step 8b). RegisterRuntimeOverlay normalizes via the shared fold.
    std::string overlayKey = target;  // vanilla form: the target IS the key
    if (LooksLikePackedCrossModName(target)) {
        const std::string packed = LowerPackedName(target);
        std::string serveVpath;
        if (!kcdx::asset_namespace::ResolvePublishedVpath(packed, serveVpath)) {
            // Hop 1 failed — no published asset under this name (AP14: fail LOUD
            // naming the unresolved name; the author must publish it first, and
            // before this replace runs — the take-effect-thereafter ordering of
            // §3 US-6). Never a silent store-write the resolver could never hit.
            lua_pushnil(L);
            lua_pushstring(L,
                ("kcdx.assets.replace: cross-mod replace target '" + target +
                 "' resolves to no published asset — the owning mod must publish "
                 "that name (with kcdx.assets.declare(\"<bare>\", \"<file>\") or "
                 "a sidecar `name`), and it must be published BEFORE this replace "
                 "runs (a cross-mod replace takes effect for opens after the "
                 "call). Check the <author>.<plugin>.<bare> spelling; a name "
                 "resolving to nothing is a loud error, never a silent no-op.")
                    .c_str());
            LOG_WARN_KV(kCat, "rejected",
                kcdx::log::KV("verb", std::string("replace")),
                kcdx::log::KV("reason",
                    std::string("packed cross-mod target resolves to no "
                                "published asset")),
                kcdx::log::KV("target", target));
            return 2;
        }
        // Hop 1 succeeded — key by the publisher's serve-vpath (already
        // normalized in the published-name store). RegisterRuntimeOverlay
        // normalizes again (idempotent on an already-normalized key).
        overlayKey = serveVpath;
        LOG_DEBUG_KV(kCat, "crossmod_resolved",
            kcdx::log::KV("target", target),
            kcdx::log::KV("serve_vpath", serveVpath),
            kcdx::log::KV("with_disk", disk),
            kcdx::log::KV("by", author.empty() ? plugin
                                               : (author + "." + plugin)));
    }

    // Hop 2: write the runtime-overlay store keyed by the resolved vpath (the
    // same store register writes — replace IS register against an existing
    // target). The engine requests that vpath and the resolver hits the runtime
    // overlay, serving B's `with`. RegisterRuntimeOverlay normalizes via the
    // shared fold. Take-effect = thereafter (§3 US-6).
    kcdx::asset_namespace::RegisterRuntimeOverlay(overlayKey, disk, plugin);

    lua_pushlstring(L, disk.data(), disk.size());
    return 1;
}

// kcdx.assets.get_by_path(path) — the own-asset reader. Resolves the CALLING
// plugin (OwningPluginForCurrentCall) and its asset path to a loadable disk
// path. Two failure shapes, mirroring the kcdx-binder idiom
// (lua_bind_cvar.cpp / lua_bind_plugin.cpp):
//   * BAD ARGUMENT (path not a string / missing / empty) -> (nil, teaching
//     error), return 2. The call shape itself was wrong; teach the author.
//   * RESOLUTION FAILURE (no calling plugin / path not in the plugin's
//     assets/) -> (nil, teaching error), return 2. A path to a file NOT in the
//     plugin's assets/ is a typo, NOT a silent nil (AP14 — fail loud naming the
//     missing path in the author's terms).
int Lua_AssetsGetByPath(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.assets.get_by_path(path): expects a single string argument — "
            "the path to YOUR OWN asset, relative to your plugin's assets/ "
            "folder (e.g. kcdx.assets.get_by_path(\"icons/my_icon.dds\")). "
            "Returns a loadable path you hand to a game asset API. To reference "
            "ANOTHER mod's asset, navigate the namespace: "
            "kcdx.plugin.<author>.<plugin>.assets.get_by_path(path).");
        return 2;
    }
    size_t len = 0;
    const char* s = lua_tolstring(L, 1, &len);
    const std::string relPath = (s && len) ? std::string(s, len) : std::string();
    if (relPath.empty()) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.assets.get_by_path(path): `path` must be non-empty — the path "
            "to your own asset relative to your plugin's assets/ folder "
            "(e.g. \"icons/my_icon.dds\").");
        return 2;
    }

    // Resolve the CALLING plugin (no owner prefix — the engine knows who you
    // are). OwningPluginForCurrentCall walks the Lua callstack to the nearest
    // attributed plugin script; {"",""} for an anonymous caller (console / pak
    // Lua / ad-hoc), which has no own assets/ to resolve against.
    std::string callSiteFile;  // unused here (the resolver carries its own teach)
    int callSiteLine = 0;
    const kcdx::lua_registry::OwningPlugin owner =
        kcdx::lua_registry::OwningPluginForCurrentCall(L, callSiteFile,
                                                       callSiteLine);
    if (owner.plugin.empty()) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.assets.get_by_path(path): no calling plugin — get_by_path "
            "resolves YOUR OWN asset, so it must be called from a plugin's "
            "plugin.lua (or a file it require()s). An anonymous caller "
            "(console / ad-hoc Lua) has no assets/ folder of its own; to reach "
            "a specific plugin's asset use "
            "kcdx.plugin.<author>.<plugin>.assets.get_by_path(path).");
        return 2;
    }

    std::string disk, err;
    if (!ResolveAssetPath(owner.author, owner.plugin, relPath, disk, err)) {
        lua_pushnil(L);
        lua_pushlstring(L, err.data(), err.size());
        return 2;
    }
    lua_pushlstring(L, disk.data(), disk.size());
    return 1;
}

// The cross-plugin get_by_path closure body. A C closure carrying the navigated
// (author, plugin) as upvalues (1 = author, 2 = plugin). It reads its OWN target
// identity from the upvalues — NOT OwningPluginForCurrentCall (that resolves the
// CALLER, the wrong plugin for a cross-plugin read). Same two failure shapes +
// teaching errors as the own-form Lua_AssetsGetByPath, against the bound plugin.
int Lua_CrossPluginGetByPath(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "assets.get_by_path(path): expects a single string argument — the "
            "path to the asset relative to that plugin's assets/ folder "
            "(e.g. ...assets.get_by_path(\"male/shirt.dds\")).");
        return 2;
    }
    size_t plen = 0;
    const char* ps = lua_tolstring(L, 1, &plen);
    const std::string relPath =
        (ps && plen) ? std::string(ps, plen) : std::string();
    if (relPath.empty()) {
        lua_pushnil(L);
        lua_pushstring(L,
            "assets.get_by_path(path): `path` must be non-empty — the asset "
            "path relative to that plugin's assets/ folder.");
        return 2;
    }
    const char* aC = lua_tostring(L, lua_upvalueindex(1));
    const char* pC = lua_tostring(L, lua_upvalueindex(2));
    const std::string author = aC ? aC : "";
    const std::string plugin = pC ? pC : "";

    std::string disk, err;
    if (!ResolveAssetPath(author, plugin, relPath, disk, err)) {
        lua_pushnil(L);
        lua_pushlstring(L, err.data(), err.size());
        return 2;
    }
    lua_pushlstring(L, disk.data(), disk.size());
    return 1;
}

// The cross-plugin get_by_name closure body (§6: kcdx.plugin.<a>.<p>.assets.
// get_by_name resolves ANOTHER plugin's published name). A C closure carrying the
// navigated (author, plugin) as upvalues (1 = author, 2 = plugin). It builds the
// NAVIGATED plugin's packed <author>.<plugin>.<name> from the upvalues — NOT
// ResolveCaller (that resolves the CALLER, the wrong plugin) — and looks it up in
// the published-name store. Same teaching-error idiom as the own-form
// Lua_AssetsGetByName, against the bound plugin.
int Lua_CrossPluginGetByName(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "assets.get_by_name(name): expects a single string argument — a "
            "name that plugin published (e.g. "
            "...assets.get_by_name(\"shirt\")).");
        return 2;
    }
    size_t nlen = 0;
    const char* ns = lua_tolstring(L, 1, &nlen);
    const std::string bare = (ns && nlen) ? std::string(ns, nlen) : std::string();
    if (bare.empty()) {
        lua_pushnil(L);
        lua_pushstring(L,
            "assets.get_by_name(name): `name` must be non-empty — a name that "
            "plugin published.");
        return 2;
    }
    const char* aC = lua_tostring(L, lua_upvalueindex(1));
    const char* pC = lua_tostring(L, lua_upvalueindex(2));
    const std::string author = aC ? aC : "";
    const std::string plugin = pC ? pC : "";

    const std::string packed = PackName(author, plugin, bare);
    std::string disk;
    if (!kcdx::asset_namespace::LookupPublishedName(packed, disk)) {
        lua_pushnil(L);
        const std::string ident =
            author.empty() ? plugin : (author + "." + plugin);
        lua_pushstring(L,
            ("assets.get_by_name(\"" + bare + "\"): plugin '" + ident +
             "' has not published a name '" + bare + "' — only names that "
             "plugin declared with kcdx.assets.declare are resolvable. A typo "
             "is a loud error, never a silent nil.").c_str());
        return 2;
    }
    lua_pushlstring(L, disk.data(), disk.size());
    return 1;
}

// __index on a cross-plugin assets-domain userdata. arg 1 = the userdata
// (carrying the navigated author+plugin on its envtable); arg 2 = the accessed
// key. Exposes the cross-plugin READ surface — get_by_path AND get_by_name (§6;
// both resolve the NAVIGATED plugin). register / declare / replace are NOT
// cross-plugin verbs (an author publishes/registers into their OWN namespace, not
// another's), so they are correctly absent here; any other key is a navigation
// miss -> nil (the kcdx.hook / step-6 navigation-miss idiom — the next access
// raises Lua's stock index error naming the bad surface segment).
int Lua_PluginAssetsIndex(lua_State* L) {
    if (lua_type(L, 2) != LUA_TSTRING) { lua_pushnil(L); return 1; }
    const char* keyC = lua_tostring(L, 2);
    const std::string key = keyC ? keyC : "";
    lua_CFunction closure = nullptr;
    if (key == "get_by_path") {
        closure = Lua_CrossPluginGetByPath;
    } else if (key == "get_by_name") {
        closure = Lua_CrossPluginGetByName;
    } else {
        lua_pushnil(L);
        return 1;
    }
    // Return the matched closure bound to the navigated (author, plugin) carried
    // on the userdata's envtable (as upvalues 1 = author, 2 = plugin).
    lua_getfenv(L, 1);
    lua_getfield(L, -1, "author");
    lua_getfield(L, -2, "plugin");
    // Stack: ... envtable, author, plugin. lua_pushcclosure pops author+plugin
    // as the closure's 2 upvalues; pop the leftover envtable after.
    lua_pushcclosure(L, closure, 2);
    // Stack now: ... envtable, closure. Tidy the envtable out from under it.
    lua_remove(L, -2);
    return 1;
}

// Install the cross-plugin assets-domain metatable. Idempotent
// (luaL_newmetatable no-ops when already registered). Hides itself from pak Lua
// via __metatable (the kcdx.hook / step-6 pattern — getmetatable() returns the
// string, plugin code can't tamper with the resolver wiring).
void EnsurePluginAssetsMetatable(lua_State* L) {
    if (luaL_newmetatable(L, kPluginAssetsMt) != 0) {
        lua_pushcfunction(L, Lua_PluginAssetsIndex);
        lua_setfield(L, -2, "__index");
        lua_pushstring(L, kPluginAssetsMt);
        lua_setfield(L, -2, "__metatable");
    }
    lua_pop(L, 1);
}

const luaL_Reg kFunctions[] = {
    {"get_by_path", Lua_AssetsGetByPath},
    {"get_by_name", Lua_AssetsGetByName},
    {"declare", Lua_AssetsDeclare},
    {"register", Lua_AssetsRegister},
    {"replace", Lua_AssetsReplace},
    {nullptr, nullptr},
};

}  // namespace

bool ResolveAssetPath(const std::string& author, const std::string& plugin,
                      const std::string& relPath, std::string& outDisk,
                      std::string& outErr) {
    const plugins::PluginManifest* m = FindManifest(author, plugin);
    if (!m) {
        const std::string ident =
            author.empty() ? plugin : (author + "." + plugin);
        outErr = "kcdx.assets.get_by_path: no loaded plugin '" + ident +
                 "' — the asset's owner is not a plugin loaded this session "
                 "(check the <author>.<plugin> in the namespace path).";
        LOG_WARN_KV(kCat, "rejected",
            kcdx::log::KV("reason", std::string("no such plugin")),
            kcdx::log::KV("author", author),
            kcdx::log::KV("plugin", plugin),
            kcdx::log::KV("path", relPath));
        return false;
    }

    // No assets/ entrypoint declared -> the plugin ships no asset tree, so no
    // path resolves. Teach with the plugin's identity.
    if (m->assetsEntrypointRel.empty()) {
        outErr = "kcdx.assets.get_by_path(\"" + relPath + "\"): plugin '" +
                 (m->author.empty() ? m->name : m->author + "." + m->name) +
                 "' declares no assets/ entrypoint (set [entrypoints].assets = "
                 "\"assets/\" in its kcdx.toml) — it has no asset folder to "
                 "resolve a path against.";
        LOG_WARN_KV(kCat, "rejected",
            kcdx::log::KV("reason", std::string("no assets entrypoint")),
            kcdx::log::KV("plugin", m->name),
            kcdx::log::KV("path", relPath));
        return false;
    }

    // Path safety (input-validation.md §Paths): reject a '..' segment so a read
    // cannot escape the plugin's assets/ subtree. SAME reject the sidecar
    // resolver applies (asset_sidecar.cpp ResolveDeclaringFile) — reused, not
    // re-derived.
    const fs::path relP = fs::path(relPath).lexically_normal();
    for (const auto& seg : relP) {
        if (seg == "..") {
            outErr = "kcdx.assets.get_by_path(\"" + relPath + "\"): the path "
                     "escapes the plugin's assets/ folder ('..' traversal is "
                     "rejected — name an asset inside assets/, not above it).";
            LOG_WARN_KV(kCat, "rejected",
                kcdx::log::KV("reason", std::string("'..' traversal")),
                kcdx::log::KV("plugin", m->name),
                kcdx::log::KV("path", relPath));
            return false;
        }
    }

    // Join: <plugin folderPath> / <assetsEntrypointRel> / <relPath>. The SAME
    // join the sidecar's assetsRoot uses (asset_sidecar.cpp ~358:
    // folderPath / assetsEntrypointRel). The result is the absolute disk path
    // the HOOK-2 open serves — the loadable path the author hands a game API.
    const fs::path assetsRoot = m->folderPath / m->assetsEntrypointRel;
    const fs::path disk = (assetsRoot / relP).lexically_normal();

    std::error_code ec;
    if (!fs::is_regular_file(disk, ec)) {
        // Missing-target teach (AP14): a path to a file NOT in the plugin's
        // assets/ is a typo, never a silent nil. Name the missing path in the
        // author's terms (the relative path they wrote AND where it was looked
        // for) so they can fix it without consulting docs.
        outErr = "kcdx.assets.get_by_path(\"" + relPath + "\"): no such asset "
                 "in plugin '" +
                 (m->author.empty() ? m->name : m->author + "." + m->name) +
                 "' — '" + relPath + "' is not a file under its assets/ folder "
                 "(looked for " + disk.string() + "). A file's presence makes "
                 "it referenceable; check the path against your assets/ tree — "
                 "a typo here is a loud error, never a silent nil.";
        LOG_WARN_KV(kCat, "rejected",
            kcdx::log::KV("reason", std::string("no such asset")),
            kcdx::log::KV("plugin", m->name),
            kcdx::log::KV("path", relPath),
            kcdx::log::KV("looked_for", disk.string()));
        return false;
    }

    outDisk = disk.string();
    return true;
}

int PushPluginAssetsDomain(lua_State* L, const std::string& author,
                           const std::string& plugin) {
    // A userdata carrying (author, plugin) on its envtable; its metatable's
    // __index (Lua_PluginAssetsIndex) exposes get_by_path bound to that pair.
    // The one-byte payload is the step-6 ResolvedHookUd pattern — the metatable
    // identity + the envtable do the work; no std::string members on the C
    // struct (no __gc dance).
    EnsurePluginAssetsMetatable(L);
    lua_newuserdata(L, 1);
    luaL_getmetatable(L, kPluginAssetsMt);
    lua_setmetatable(L, -2);
    lua_newtable(L);
    lua_pushstring(L, author.c_str()); lua_setfield(L, -2, "author");
    lua_pushstring(L, plugin.c_str()); lua_setfield(L, -2, "plugin");
    lua_setfenv(L, -2);
    return 1;
}

void bind(lua_State* L) {
    // kcdx.assets.* — a GROUPED capability domain (NOT a top-level verb). Built
    // exactly like kcdx.cvar.* (lua_bind_cvar.cpp): a lua_newtable, per-fn
    // lua_pushcfunction/lua_setfield, then one lua_setfield onto the kcdx table.
    // The kcdx table stays at kcdx_idx throughout (lua_setfield pops the
    // sub-table it consumes), leaving the stack balanced for the next binder.
    int kcdx_idx = lua_gettop(L);

    // Install the cross-plugin assets metatable up front so the leaf is ready
    // when kcdx.plugin.<a>.<p>.assets resolves it (lua_bind_plugin calls
    // PushPluginAssetsDomain, which also EnsurePluginAssetsMetatable's
    // idempotently — both orderings are safe).
    EnsurePluginAssetsMetatable(L);

    lua_newtable(L);
    for (const luaL_Reg* f = kFunctions; f->name; ++f) {
        lua_pushcfunction(L, f->func);
        lua_setfield(L, -2, f->name);
    }
    lua_setfield(L, kcdx_idx, "assets");
}

}  // namespace kcdx::lua_bind_assets
