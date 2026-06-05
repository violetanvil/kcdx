// kcdx::asset_interface — engine-side impl of kcdxAssetInterface.
//
// The C++ mirror of the Lua kcdx.assets.* binder (src/lua_bind_assets.cpp) —
// same five verbs, same engine-side resolution, same runtime stores. Each thunk
// resolves the calling plugin's (author, plugin) off opts.self (via
// kcdx::plugins::AuthorForHandle / NameForHandle, the same handle->owner lookup
// declare_interface.cpp uses), validates the arguments, then calls the IDENTICAL
// shared operation the Lua binder calls (lua_bind_assets::DeclareAsset /
// GetPublishedAsset / RegisterAsset / ReplaceAsset / ResolveAssetPath). There is
// NO parallel C++ resolution / packing / classification / store-write logic —
// the shared ops own all of it: one shared resolution path (the Lua binder and
// this C++ mirror both call the shared helpers), never a parallel copy; and full
// Lua<->C++ parity (the C++ author gets every capability the Lua author does, the
// other language's spelling), so a C++ verb produces the SAME result as its Lua peer.
//
// RETURN SHAPE (design Option A): every method returns the resolved loadable
// PATH as a const char* on success, nullptr on failure. The teaching error is
// LOGGED by the shared op (the SAME LOG_*_KV lines the Lua binder emits — the
// shared ops log on every failure path), NOT handed back in code — the path is
// used directly by the C++ author; the error teaches via the dev log (the C++
// author's native channel, mirroring kcdxConsoleInterface::GetCVar* and
// kcdxDeclareInterface). A nullptr return with a dev-log line is the loud
// failure; never a silent empty string.
//
// RETURN-POINTER LIFETIME: the success path is a freshly-resolved std::string
// (a pure read for GetByPath; the store's owned copy for the others). It is
// pinned in a thread_local std::string and the const char* into THAT is
// returned — stable until the next kcdxAssetInterface call on the same thread
// (the standard const char*-return contract: use the path immediately, the same
// convention as kcdxConsoleInterface::GetArg). No allocation crosses the ABI;
// the author copies the path if they need it past the next call.

#include "asset_interface.h"

#include <string>

#include "log.h"               // LOG_*_KV, ::kcdx::log::KV
#include "lua_bind_assets.h"   // the shared runtime-verb operations + ResolveAssetPath
#include "plugin_loader.h"     // AuthorForHandle / NameForHandle

namespace kcdx::asset_interface {

namespace {

// Stable log category for the binder-layer rejects this interface adds on top of
// the shared ops' own logging (a null/empty arg or an unattributed handle — the
// arg never reaches the shared op, so this layer logs it). Greppable separately
// from the shared ASSET_GET / ASSET_RUNTIME lines.
constexpr const char* kCat = "ASSET_GET";

// Per-thread pin for the returned loadable path. The thunk fills it from the
// shared op's outDisk and returns its c_str() — stable until the next
// kcdxAssetInterface call on this thread (the const char*-return contract).
thread_local std::string t_returnPin;

// Resolve (author, plugin) off the calling plugin's handle — the C++ analogue of
// the Lua binder's ResolveCaller (which walks the Lua callstack). Empty fields
// for kcdxInvalidPluginHandle / unknown handle; matches the OwnerFromHandle
// discipline in declare_interface.cpp / hook_interface.cpp.
struct Owner {
    std::string author;
    std::string plugin;
};

Owner OwnerFromHandle(kcdxPluginHandle h) {
    Owner o;
    o.author = kcdx::plugins::AuthorForHandle(h);
    o.plugin = kcdx::plugins::NameForHandle(h);
    return o;
}

// Log + return nullptr for a binder-layer reject (a bad arg / unattributed self
// caught HERE, before the shared op runs). The shared ops log their own rejects;
// this only covers the cases the arg never reaches them.
const char* BinderReject(const std::string& author, const std::string& plugin,
                         const char* verb, const char* reason,
                         const std::string& detail) {
    LOG_WARN_KV(kCat, "rejected",
        ::kcdx::log::KV("surface", std::string("cpp")),
        ::kcdx::log::KV("verb",    std::string(verb)),
        ::kcdx::log::KV("author",  author),
        ::kcdx::log::KV("plugin",  plugin),
        ::kcdx::log::KV("reason",  std::string(reason)),
        ::kcdx::log::KV("detail",  detail));
    return nullptr;
}

// Pin `disk` per-thread and return a stable const char* into it.
const char* Pin(const std::string& disk) {
    t_returnPin = disk;
    return t_returnPin.c_str();
}

// ----------------------------------------------------------------------
// GetByPath — pure read of the caller's own asset.
// ----------------------------------------------------------------------
const char* Thunk_GetByPath(kcdxPluginHandle self, const char* path) {
    Owner o = OwnerFromHandle(self);
    if (o.plugin.empty()) {
        return BinderReject(o.author, o.plugin, "get_by_path", "unattributed",
            "kcdxAssetInterface::GetByPath: `self` is invalid or unattributed — "
            "pass your own handle from api->GetPluginHandle(\"<[plugin].name>\"). "
            "GetByPath resolves YOUR OWN asset; an unattributed caller has no "
            "assets/ folder to resolve against.");
    }
    if (!path || !path[0]) {
        return BinderReject(o.author, o.plugin, "get_by_path", "bad_arg_path",
            "kcdxAssetInterface::GetByPath(self, path): `path` must be a non-null, "
            "non-empty string — the path to your asset relative to your assets/ "
            "folder (e.g. \"icons/my_icon.dds\").");
    }
    // The SAME pure-read resolver the Lua get_by_path uses (it logs the teaching
    // error naming the missing path on failure — ASSET_GET).
    std::string disk, err;
    if (!kcdx::lua_bind_assets::ResolveAssetPath(o.author, o.plugin, path, disk,
                                                 err)) {
        return nullptr;  // ResolveAssetPath already logged the teaching error.
    }
    return Pin(disk);
}

// ----------------------------------------------------------------------
// GetByName — resolve a name the caller published.
// ----------------------------------------------------------------------
const char* Thunk_GetByName(kcdxPluginHandle self, const char* name) {
    Owner o = OwnerFromHandle(self);
    if (o.plugin.empty()) {
        return BinderReject(o.author, o.plugin, "get_by_name", "unattributed",
            "kcdxAssetInterface::GetByName: `self` is invalid or unattributed — "
            "pass your own handle. GetByName resolves YOUR OWN published name.");
    }
    if (!name || !name[0]) {
        return BinderReject(o.author, o.plugin, "get_by_name", "bad_arg_name",
            "kcdxAssetInterface::GetByName(self, name): `name` must be a "
            "non-null, non-empty string — a name you published with Declare.");
    }
    std::string disk, err;
    if (!kcdx::lua_bind_assets::GetPublishedAsset(o.author, o.plugin, name, disk,
                                                  err)) {
        return nullptr;  // GetPublishedAsset already logged the teaching error.
    }
    return Pin(disk);
}

// ----------------------------------------------------------------------
// Declare — publish a name; return the declared file's loadable path.
// ----------------------------------------------------------------------
const char* Thunk_Declare(kcdxPluginHandle self, const char* name,
                          const char* file) {
    Owner o = OwnerFromHandle(self);
    if (o.plugin.empty()) {
        return BinderReject(o.author, o.plugin, "declare", "unattributed",
            "kcdxAssetInterface::Declare: `self` is invalid or unattributed — "
            "declare publishes a name in YOUR OWN namespace; pass your own "
            "handle. An unattributed caller has no namespace to publish into.");
    }
    if (!name || !name[0] || !file || !file[0]) {
        return BinderReject(o.author, o.plugin, "declare", "bad_arg",
            "kcdxAssetInterface::Declare(self, name, file): `name` and `file` "
            "must both be non-null, non-empty — a stable published name and the "
            "asset it names (relative to your assets/ folder).");
    }
    std::string disk, err;
    if (!kcdx::lua_bind_assets::DeclareAsset(o.author, o.plugin, name, file, disk,
                                             err)) {
        return nullptr;  // DeclareAsset already logged the teaching error.
    }
    return Pin(disk);
}

// ----------------------------------------------------------------------
// Register — write a runtime overlay; return the file's loadable path.
// ----------------------------------------------------------------------
const char* Thunk_Register(kcdxPluginHandle self, const char* vpath,
                           const char* file) {
    Owner o = OwnerFromHandle(self);
    if (o.plugin.empty()) {
        return BinderReject(o.author, o.plugin, "register", "unattributed",
            "kcdxAssetInterface::Register: `self` is invalid or unattributed — "
            "register adds an overlay from YOUR OWN asset; pass your own handle.");
    }
    if (!vpath || !vpath[0] || !file || !file[0]) {
        return BinderReject(o.author, o.plugin, "register", "bad_arg",
            "kcdxAssetInterface::Register(self, vpath, file): `vpath` and `file` "
            "must both be non-null, non-empty — the virtual path the game opens "
            "and the asset that serves it (relative to your assets/ folder).");
    }
    std::string disk, err;
    if (!kcdx::lua_bind_assets::RegisterAsset(o.author, o.plugin, vpath, file,
                                              disk, err)) {
        return nullptr;  // RegisterAsset already logged the teaching error.
    }
    return Pin(disk);
}

// ----------------------------------------------------------------------
// Replace — write a runtime replacement (vanilla vpath OR packed cross-mod
// name); return the replacement file's loadable path.
// ----------------------------------------------------------------------
const char* Thunk_Replace(kcdxPluginHandle self, const char* target,
                          const char* file) {
    Owner o = OwnerFromHandle(self);
    if (o.plugin.empty()) {
        return BinderReject(o.author, o.plugin, "replace", "unattributed",
            "kcdxAssetInterface::Replace: `self` is invalid or unattributed — "
            "replace serves YOUR OWN file; pass your own handle.");
    }
    if (!target || !target[0] || !file || !file[0]) {
        return BinderReject(o.author, o.plugin, "replace", "bad_arg",
            "kcdxAssetInterface::Replace(self, target, file): `target` and "
            "`file` must both be non-null, non-empty — the asset to replace (a "
            "vanilla path or a packed \"<author>.<plugin>.<bare>\" name) and the "
            "file that replaces it (relative to your assets/ folder).");
    }
    std::string disk, err;
    if (!kcdx::lua_bind_assets::ReplaceAsset(o.author, o.plugin, target, file,
                                             disk, err)) {
        return nullptr;  // ReplaceAsset already logged the teaching error.
    }
    return Pin(disk);
}

// ----------------------------------------------------------------------
// Vtable instance. Order MATCHES the kcdxAssetInterface struct field order in
// include/kcdx/Interfaces.h byte-for-byte (append-only ABI; fixed offsets).
// DO NOT reorder.
// ----------------------------------------------------------------------
kcdxAssetInterface g_assetInterface = {
    /*GetByPath=*/ Thunk_GetByPath,
    /*GetByName=*/ Thunk_GetByName,
    /*Declare=*/   Thunk_Declare,
    /*Register=*/  Thunk_Register,
    /*Replace=*/   Thunk_Replace,
};

}  // namespace

const kcdxAssetInterface* GetInterface() {
    return &g_assetInterface;
}

}  // namespace kcdx::asset_interface
