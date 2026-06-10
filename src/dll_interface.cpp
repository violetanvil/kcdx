// kcdx::dll_interface — engine-side impl of kcdxDllInterface.
//
// Mirrors the Lua kcdx.dll.declare binder (src/lua_bind_functions.cpp) — the
// SAME declared-function store, written through the SAME public seam
// (lua_bind_functions::DeclareFunction), so a C++ Declare and a Lua
// kcdx.dll.declare populate ONE store and resolve identically through both
// PluginByName (C++) and kcdx.functions["<ns>"] (Lua).
//
// One Declare thunk: validate the namespace + every entry, reject a malformed
// entry with a logged teaching diagnostic (the C++ peer of the Lua call's
// raised error) and return false (NO partial accept — validate the whole batch
// before writing any of it), then write every accepted entry through the seam.
// `signature` is REQUIRED on each entry (a callback hook needs the ABI the
// compiled DLL does not carry).

#include "dll_interface.h"

#include <string>

#include "log.h"                 // LOG_*_KV, ::kcdx::log::KV
#include "lua_bind_functions.h"  // DeclareFunction

namespace kcdx::dll_interface {

namespace {

// Push a teaching error to the engine log. Mirrors the shape the Lua binder's
// raised error carries (namespace / name / reason / detail) so the two surfaces
// grep alike; category "DLL_DECLARE" matches the Lua binder's success-line
// category so a reader filters the whole surface by one tag.
void LogDeclareReject(const std::string& ns,
                      const std::string& name,
                      const char*        reason,
                      const std::string& detail) {
    LOG_ERROR_KV("DLL_DECLARE", "declare_rejected",
        ::kcdx::log::KV("namespace", ns),
        ::kcdx::log::KV("name",      name),
        ::kcdx::log::KV("reason",    reason),
        ::kcdx::log::KV("detail",    detail));
}

bool Thunk_Declare(const char* pluginNamespace,
                   const kcdxDeclaredFn* fns, int count) {
    const std::string ns = (pluginNamespace && pluginNamespace[0])
                               ? pluginNamespace : "";

    if (ns.empty()) {
        LogDeclareReject("", "", "bad_arg_namespace",
            "kcdxDllInterface::Declare(pluginNamespace, fns, count): "
            "`pluginNamespace` (string) is required — your plugin's "
            "<author>.<plugin> namespace (e.g. \"redmoon.outfit_mod\"). The "
            "declared functions land under "
            "kcdx.functions[\"<pluginNamespace>\"].*.");
        return false;
    }

    if (!fns || count <= 0) {
        LogDeclareReject(ns, "", "missing_entries",
            "kcdxDllInterface::Declare(\"" + ns + "\", fns, count): no entries "
            "supplied — pass a non-null kcdxDeclaredFn array and count > 0. A "
            "declaration with no functions is a no-op the engine cannot "
            "resolve.");
        return false;
    }

    // Validate the WHOLE batch BEFORE writing any of it (no partial accept):
    // every entry must carry a non-null/non-empty name AND signature. A
    // malformed entry fails the whole Declare loud — never a silent drop of one
    // author-declared function while the others land.
    for (int i = 0; i < count; ++i) {
        const kcdxDeclaredFn& e = fns[i];
        const bool hasName = e.name && e.name[0];
        const bool hasSig  = e.signature && e.signature[0];
        if (!hasName) {
            LogDeclareReject(ns, "", "bad_entry_name",
                "kcdxDllInterface::Declare(\"" + ns + "\", ...): entry #" +
                std::to_string(i) + " has a null/empty `name` — every entry "
                "must carry the bare function name you are declaring. The whole "
                "Declare is rejected (no partial accept).");
            return false;
        }
        if (!hasSig) {
            LogDeclareReject(ns, e.name, "bad_entry_signature",
                "kcdxDllInterface::Declare(\"" + ns + "\", ...): function `" +
                std::string(e.name) + "` has a null/empty `signature` — a "
                "callback hook needs the function's ABI from your source (the "
                "engine cannot read it from a compiled DLL). Supply it, e.g. "
                "{ \"" + std::string(e.name) + "\", \"bool (ptr self)\" }. The "
                "whole Declare is rejected (no partial accept).");
            return false;
        }
    }

    // Every entry is valid — write each through the shared seam (the ONE
    // declared-store insert the Lua kcdx.dll.declare binder also uses).
    int declared = 0;
    for (int i = 0; i < count; ++i) {
        const kcdxDeclaredFn& e = fns[i];
        if (kcdx::lua_bind_functions::DeclareFunction(ns, e.name, e.signature)) {
            ++declared;
        }
    }

    LOG_INFO_KV("DLL_DECLARE", "declared",
        ::kcdx::log::KV("namespace", ns),
        ::kcdx::log::KV("functions", declared),
        ::kcdx::log::KV("source", "cpp"));
    return true;
}

// -----------------------------------------------------------------------------
// Vtable instance. Order MATCHES the kcdxDllInterface struct field order in
// include/kcdx/Interfaces.h byte-for-byte (append-only ABI; fixed offsets).
// DO NOT reorder.
// -----------------------------------------------------------------------------

kcdxDllInterface g_dllInterface = {
    /*Declare=*/ Thunk_Declare,
};

}  // namespace

const kcdxDllInterface* GetInterface() {
    return &g_dllInterface;
}

}  // namespace kcdx::dll_interface
