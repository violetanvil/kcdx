// kcdx::declare_interface — engine-side impl of kcdxDeclareInterface.
//
// Mirrors the Lua kcdx.declare / kcdx.declared binder
// (src/lua_bind_declare.cpp) — same DeclaredEntry payload, same
// register-time validation contract, same LookupForCaller read path, same
// 1-segment SELF / 3-segment explicit name forms on Get — but takes raw C
// inputs from a C++ DLL plugin via the kcdxDeclareInterface vtable instead
// of a Lua table, and gets owner identity from
// kcdx::plugins::AuthorForHandle / NameForHandle off
// opts.owningPlugin instead of a Lua-stack walk (there is no Lua stack
// here — a C++ DLL plugin calls directly).
//
// Validation is layered the same way as the Lua side:
//   - Binder-layer rejects (null/empty module, null/empty bareName, null
//     entries, count == 0, unattributed handle, malformed kcdxDeclareEntry
//     field) are caught HERE and log under category
//     "DECLARED_TARGET_BIND" so the two layers are greppable separately
//     from the store-layer rejects.
//   - Store-layer rejects (name charset, version-key syntax, pattern-
//     without-signature, all-or-nothing partial acceptance) are caught by
//     declared_targets::Register and log under "DECLARED_TARGET".
// Both produce structured KV log lines so the author reads the cause in
// the dev log; the boolean return surfaces the accept/reject outcome to
// the calling DLL.

#include "declare_interface.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "declared_targets.h"  // DeclaredEntry, VersionEntry, Register, LookupForCaller, ResolvedDeclared
#include "log.h"               // LOG_*_KV, ::kcdx::log::KV
#include "plugin_loader.h"     // AuthorForHandle / NameForHandle / g_runtimeGameVersionString

namespace kcdx::declare_interface {

namespace {

// Build the (author, plugin) pair for self > engine > other-plugin
// precedence from owningPlugin. Empty fields for kcdxInvalidPluginHandle /
// unknown handle — matches the existing hook_interface.cpp / bytes_interface
// .cpp OwnerFromHandle discipline.
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

// Push a binder-layer teaching error to the engine log. Mirrors the shape
// lua_bind_declare's LogBinderReject uses (author / plugin / name / reason /
// detail) so the two surfaces grep alike; category "DECLARED_TARGET_BIND"
// separates the binder layer from the store layer's "DECLARED_TARGET".
void LogBinderReject(const std::string& author,
                     const std::string& plugin,
                     const std::string& name,
                     const char*        reason,
                     const std::string& detail) {
    LOG_ERROR_KV("DECLARED_TARGET_BIND", "register_rejected",
        ::kcdx::log::KV("author", author),
        ::kcdx::log::KV("plugin", plugin),
        ::kcdx::log::KV("name",   name),
        ::kcdx::log::KV("reason", reason),
        ::kcdx::log::KV("detail", detail));
}

// Translate one kcdxDeclareEntry into a declared_targets::VersionEntry.
// kindTag is propagated AS-AUTHORED (the store does not auto-derive). Any
// null string is treated as empty (the store's contract for unset). Returns
// "" on success or a teaching error on a malformed POD (today: only the
// missing-versionKey case — every other field is optional at this layer; the
// store catches the deeper validation per declared_targets::Register's
// contract).
std::string BuildVersionEntry(const kcdxDeclareEntry& src,
                              const std::string& callDesc,
                              declared_targets::VersionEntry& out) {
    if (!src.versionKey || !src.versionKey[0]) {
        return callDesc +
            ": entry has empty versionKey — every kcdxDeclareEntry must "
            "carry an exact ('1.5.1164953') or wildcard ('1.5.*') version "
            "key the engine matches against the running game version.";
    }
    out.versionKey = src.versionKey;
    if (src.patternStr && src.patternStr[0]) {
        out.isPattern = true;
        out.patternStr = src.patternStr;
        if (src.signatureStr && src.signatureStr[0]) {
            out.signatureStr = src.signatureStr;
        }
        if (src.kindTag && src.kindTag[0]) {
            out.kindTag = src.kindTag;
        }
    } else {
        out.isPattern = false;
        out.valueIsString = src.valueIsString;
        if (src.valueIsString) {
            out.valueStr = src.valueStr ? src.valueStr : "";
        } else {
            out.valueInt = src.valueInt;
        }
        if (src.kindTag && src.kindTag[0]) {
            out.kindTag = src.kindTag;
        }
    }
    return "";
}

// ----------------------------------------------------------------------
// Declare thunk — the write surface.
// ----------------------------------------------------------------------

bool Thunk_Declare(const char* module,
                   const char* bareName,
                   const kcdxDeclareEntry* entries,
                   size_t count,
                   kcdxPluginHandle owningPlugin) {
    Owner owner = OwnerFromHandle(owningPlugin);

    if (!module || !module[0]) {
        LogBinderReject(owner.author, owner.plugin,
            bareName ? bareName : "",
            "bad_arg_module",
            "kcdxDeclareInterface::Declare(module, bareName, entries, count, "
            "owningPlugin): `module` must be a non-null, non-empty string "
            "(no default — a defaulted module silently misroutes when "
            "secondary modules become a concern).");
        return false;
    }
    if (!bareName || !bareName[0]) {
        LogBinderReject(owner.author, owner.plugin, "",
            "bad_arg_bareName",
            "kcdxDeclareInterface::Declare(...): `bareName` must be a "
            "non-null, non-empty string — the bare name you are declaring. "
            "The engine stamps it as <author>.<plugin>.<bareName> from your "
            "[plugin] manifest.");
        return false;
    }
    const std::string moduleStr   = module;
    const std::string bareNameStr = bareName;
    const std::string callDesc =
        "kcdxDeclareInterface::Declare('" + moduleStr + "', '" +
        bareNameStr + "')";

    if (owningPlugin == kcdxInvalidPluginHandle ||
        owner.author.empty() || owner.plugin.empty()) {
        LogBinderReject(owner.author, owner.plugin, bareNameStr,
            "unattributed",
            callDesc +
            ": owningPlugin handle is invalid or unattributed — declared "
            "targets are identified by <author>.<plugin>.<bareName> from "
            "your manifest. Pass the handle from "
            "api->GetPluginHandle(\"<[plugin].name>\").");
        return false;
    }

    if (!entries || count == 0) {
        LogBinderReject(owner.author, owner.plugin, bareNameStr,
            "missing_versions",
            callDesc +
            ": no entries supplied — pass a non-null kcdxDeclareEntry "
            "array and count > 0. A declared name with no payload is a "
            "no-op the engine cannot resolve.");
        return false;
    }

    std::vector<declared_targets::VersionEntry> versions;
    versions.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        declared_targets::VersionEntry ve;
        std::string err = BuildVersionEntry(entries[i], callDesc, ve);
        if (!err.empty()) {
            LogBinderReject(owner.author, owner.plugin, bareNameStr,
                "bad_version_entry", err);
            return false;
        }
        versions.push_back(std::move(ve));
    }

    declared_targets::DeclaredEntry entry;
    entry.declaringAuthor = owner.author;
    entry.declaringPlugin = owner.plugin;
    entry.name            = bareNameStr;
    entry.module          = moduleStr;
    entry.versions        = std::move(versions);

    // Hand off to the store. Register runs name-charset, version-key
    // syntax, and pattern-without-signature validation; on reject it
    // writes its own structured KV line (category "DECLARED_TARGET") and
    // returns false. The interface propagates the boolean unchanged so
    // the calling DLL can short-circuit on failure.
    return declared_targets::Register(entry);
}

// ----------------------------------------------------------------------
// Get thunk — the read surface for VALUE entries.
// ----------------------------------------------------------------------

kcdxDeclaredValue Thunk_Get(const char* name, kcdxPluginHandle owningPlugin) {
    kcdxDeclaredValue out;
    out.found       = false;
    out.isString    = false;
    out.intValue    = 0;
    out.stringValue = nullptr;

    if (!name || !name[0]) {
        return out;
    }
    const std::string nameArg = name;

    // Parse the 3-segment explicit form `<author>.<plugin>.<bare>` if
    // present, mirroring lua_bind_declare::Lua_Declared's split. Only the
    // 1-segment SELF form and the 3-segment explicit form are meaningful
    // for declared-value reads — any other dot count returns a miss.
    std::string lookupAuthor;
    std::string lookupPlugin;
    std::string lookupBare;

    std::vector<std::string> segs;
    {
        std::string cur;
        for (char c : nameArg) {
            if (c == '.') {
                segs.push_back(std::move(cur));
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        segs.push_back(std::move(cur));
    }
    if (segs.size() == 3 &&
        !segs[0].empty() && !segs[1].empty() && !segs[2].empty()) {
        lookupAuthor = segs[0];
        lookupPlugin = segs[1];
        lookupBare   = segs[2];
    } else if (segs.size() == 1 && !segs[0].empty()) {
        Owner owner = OwnerFromHandle(owningPlugin);
        lookupAuthor = owner.author;
        lookupPlugin = owner.plugin;
        lookupBare   = segs[0];
    } else {
        // 2-segment / 4+-segment / empty-segment / 0-segment — no SELF or
        // explicit interpretation exists for declared-value reads.
        return out;
    }

    // Anonymous SELF call (no owning plugin / unknown handle) — mirror
    // the Lua binder's "no owner = no self tier" semantics: return a miss
    // rather than silently looking up under ("", "", name).
    if (lookupAuthor.empty() || lookupPlugin.empty()) {
        return out;
    }

    const declared_targets::ResolvedDeclared rd =
        declared_targets::LookupForCaller(
            lookupAuthor, lookupPlugin, lookupBare,
            kcdx::plugins::g_runtimeGameVersionString);

    if (rd.kind != declared_targets::ResolvedDeclared::Kind::Value) {
        // Pattern, VersionMismatch, NoEntry all surface as a miss on this
        // accessor. Pattern entries are consumed by name through the hook
        // / bytes verbs (the resolved address path); this accessor is for
        // value entries only.
        return out;
    }

    out.found = true;
    if (rd.valueIsString) {
        out.isString    = true;
        out.stringValue = nullptr;
        // stringValue aliases the store's owned std::string; valid until the owning plugin re-Declares this same name (cross-triple Declares from any plugin do not invalidate it — deque node-stability).
        if (rd.entry) {
            const declared_targets::VersionEntry* picked =
                declared_targets::FindPickedVersionEntry(
                    *rd.entry, kcdx::plugins::g_runtimeGameVersionString);
            if (picked && picked->valueIsString && !picked->valueStr.empty()) {
                out.stringValue = picked->valueStr.c_str();
            }
        }
    } else {
        out.isString = false;
        out.intValue = rd.valueInt;
    }
    return out;
}

// ----------------------------------------------------------------------
// Vtable instance. Order MATCHES the kcdxDeclareInterface struct field
// order in include/kcdx/Interfaces.h byte-for-byte (append-only ABI;
// fixed offsets). DO NOT reorder.
// ----------------------------------------------------------------------

kcdxDeclareInterface g_declareInterface = {
    /*Declare=*/ Thunk_Declare,
    /*Get=*/     Thunk_Get,
};

}  // namespace

const kcdxDeclareInterface* GetInterface() {
    return &g_declareInterface;
}

}  // namespace kcdx::declare_interface
