// kcdx::bytes_interface — engine-side impl of kcdxBytesInterface.
//
// Mirrors the Lua kcdx.bytes binder
// (src/lua_bind_bytes.cpp Lua_Bytes) — same PatchEntry payload, same
// exactly-one-locator rule, same target="<name>" name-resolution, same
// Kind::Bytes lua_registry::Entry + deferred apply pass — but takes raw C
// inputs from a C++ DLL plugin via the kcdxBytesInterface vtable instead of a
// Lua table, and gets owner identity from OwnerFromHandle(opts->owningPlugin)
// instead of a Lua-stack walk (there's no Lua stack — a C++ DLL plugin calls
// directly, so there's no call-site file/line either).
//
// SHAPE — ONE operation (Register), not sub-verbs (Interfaces.h:1656). A byte
// rewrite has a single operation: write `replacement` at a located site. The
// query quartet (IsApplied/GetReason/GetName/Uninstall) walks the registry by
// handleId — the SAME Find() / SetStatus() pair the Lua metatable methods use
// (src/lua_registry.cpp). Uninstall is the one divergence from hook_interface:
// bytes has NO revert path (by design; Interfaces.h:1805-1810), so
// it returns false + logs a teaching line rather than routing any uninstall.

#include "bytes_interface.h"

#include <memory>
#include <string>

#include "address_library.h"  // ResolveByName / FindResolvedAuthorTarget
#include "log.h"
#include "lua_registry.h"
#include "patch_engine.h"     // patch::PatchEntry, ParsePattern, ParseBytes
#include "plugin_loader.h"    // NameForHandle / AuthorForHandle

namespace kcdx::bytes_interface {

namespace {

// Build the (author, plugin) pair for self > engine > other precedence from
// opts->owningPlugin. Both fields empty for kcdxInvalidPluginHandle / unknown
// handle — same discipline as hook_interface.cpp's OwnerFromHandle (no
// mis-attribution; the resolver falls through to the anonymous engine-seed
// path).
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

// Push a teaching error to the engine log + the calling plugin's log (the
// auto-loud-on-failure contract documented at Interfaces.h:1667-1668 /
// 1774-1780). Mirrors hook_interface.cpp's LogTeachingError — stable category
// tag "BYTES_INTERFACE" (logging.md). kcdxInvalidPluginHandle falls back to
// engine-only.
void LogTeachingError(kcdxPluginHandle owningPlugin,
                      const std::string& msg) {
    LOG_ERROR_KV("BYTES_INTERFACE", "register_failed",
        log::KV("plugin", kcdx::plugins::NameForHandle(owningPlugin).c_str()),
        log::KV("reason", msg.c_str()));
    if (owningPlugin != kcdxInvalidPluginHandle) {
        LOG_PLUGIN_ERROR(owningPlugin, "BYTES_INTERFACE",
            "kcdxBytesInterface::Register — %s", msg.c_str());
    }
}

// Resolve target="<name>" into the PatchEntry's locator field — COPIED IN
// STRUCTURE from lua_bind_bytes.cpp:262-345. ResolveByName -> p->resolvedVa for
// VA-bearing names (engine seed, Rva/AddressId author-target); else
// FindResolvedAuthorTarget routes a Pattern author-target through p->pattern or
// a TargetSymbol author-target through p->targetSymbol; a VA-bearing
// author-target that did NOT resolve, an unsupported kind, or a genuine miss
// each produce a teaching error. Returns "" on success or a ready-to-log
// diagnostic on failure (caller logs + returns 0).
std::string ResolveTargetName(const std::string& targetName,
                              const Owner& owner,
                              kcdx::patch::PatchEntry& p) {
    const uintptr_t va = kcdx::address_library::ResolveByName(
        targetName.c_str(), owner.author.c_str(), owner.plugin.c_str());
    if (va) {
        // Engine seed, Rva author-target, or AddressId author-target — the
        // name resolved straight to a VA. Carry it; Resolve uses it directly.
        p.resolvedVa = va;
        return "";
    }

    // ResolveByName returned 0 — either a Pattern / TargetSymbol author-target
    // (no VA in this leaf module) or a genuine miss. FindResolvedAuthorTarget
    // disambiguates with the same precedence + the same real (author, plugin).
    const kcdx::address_library::AuthorTarget* at =
        kcdx::address_library::FindResolvedAuthorTarget(
            targetName.c_str(), owner.author.c_str(), owner.plugin.c_str());
    if (!at) {
        // Genuine miss — no seed, no author target won the precedence.
        return std::string("target '") + targetName +
               "' did not resolve (unknown name, wrong game version, "
               "unverified row, or a typo). Check the name against "
               "kcdx.addr.* or your declared [[target]] rows.";
    }

    using Kind = kcdx::address_library::AuthorLocatorKind;
    switch (at->kind) {
        case Kind::Pattern:
            // Registry stores the AOB un-parsed; parse it here so the apply
            // pass resolves it through the SAME pattern path a directly-set
            // pattern= uses. A malformed AOB is an author error in the
            // target's row — teach it, don't throw.
            try {
                p.pattern = kcdx::patch::ParsePattern(at->locatorStr);
            } catch (const std::exception& ex) {
                return std::string("target '") + targetName +
                       "' (author-declared pattern) has a malformed AOB: " +
                       ex.what();
            }
            return "";
        case Kind::TargetSymbol:
            p.targetSymbol = at->locatorStr;
            return "";
        case Kind::Rva:
        case Kind::AddressId:
            // VA-bearing kinds should have resolved via ResolveByName above.
            // Reaching here means the name table disagreed with itself;
            // surface it rather than silently mis-resolving.
            return std::string("target '") + targetName +
                   "' is a VA-bearing author-target but did not resolve to an "
                   "address (unverified row or game-version mismatch). Check "
                   "the target's row.";
        default:
            return std::string("target '") + targetName +
                   "' has an unsupported author-target kind.";
    }
}

// -----------------------------------------------------------------------
// Register thunk — the single install method (Interfaces.h:1781).
// -----------------------------------------------------------------------

kcdxBytesHandle Thunk_Register(const kcdxBytesOptions* opts) {
    if (!opts) {
        LogTeachingError(kcdxInvalidPluginHandle,
            "opts is null — pass a non-null kcdxBytesOptions* describing the "
            "rewrite (target/pattern/addressId/targetSymbol + replacement).");
        return 0;
    }

    kcdxPluginHandle owningHandle = opts->owningPlugin;
    Owner owner = OwnerFromHandle(owningHandle);

    // Build the patch entry from opts. Field defaults mirror the Lua path
    // (lua_bind_bytes.cpp:132-162): name "cpp_bytes" (Interfaces.h:1713),
    // module "WHGame.dll", idempotent true. priority is engine-internal +
    // ignored everywhere (cross-plugin order = plugin [load_order].priority).
    auto p = std::make_shared<kcdx::patch::PatchEntry>();
    p->sourceFile  = "<cpp>";
    p->name        = (opts->name && opts->name[0]) ? opts->name : "cpp_bytes";
    if (opts->description) p->description = opts->description;
    p->priority    = 50;   // engine-internal default; ignored everywhere
    p->module      = (opts->module && opts->module[0]) ? opts->module
                                                       : "WHGame.dll";
    p->offset      = opts->offset;
    p->idempotent  = opts->idempotent;
    p->addressId   = opts->addressId;
    if (opts->targetSymbol) p->targetSymbol = opts->targetSymbol;

    const std::string targetName  = opts->target       ? opts->target       : "";
    const std::string patternStr  = opts->pattern      ? opts->pattern      : "";
    const std::string replacementStr =
        opts->replacement ? opts->replacement : "";
    const std::string originalStr = opts->original     ? opts->original     : "";
    const std::string contextStr  = opts->context      ? opts->context      : "";
    const std::string anchorStr   = opts->anchorString ? opts->anchorString : "";

    // Exactly-one-locator rule (lua_bind_bytes.cpp:196-218). `target` is the
    // common path; pattern/addressId/targetSymbol are the expert hatch.
    const int locatorCount =
        (!patternStr.empty()       ? 1 : 0) +
        (p->addressId != 0         ? 1 : 0) +
        (!p->targetSymbol.empty()  ? 1 : 0) +
        (!targetName.empty()       ? 1 : 0);
    if (locatorCount == 0) {
        LogTeachingError(owningHandle,
            std::string("bytes '") + p->name + "': must specify exactly one "
            "locator (target, pattern, addressId, or targetSymbol). The common "
            "path is target = \"<name>\" — a name the engine resolves to an "
            "address; pattern/addressId/targetSymbol are the expert hatch.");
        return 0;
    }
    if (locatorCount > 1) {
        LogTeachingError(owningHandle,
            std::string("bytes '") + p->name + "': locators are mutually "
            "exclusive (set exactly one of target, pattern, addressId, "
            "targetSymbol).");
        return 0;
    }
    if (replacementStr.empty()) {
        LogTeachingError(owningHandle,
            std::string("bytes '") + p->name + "': missing required field "
            "'replacement' (the bytes to write, e.g. \"90 90 90\").");
        return 0;
    }

    // target = "<name>" → resolve the NAME into a locator field. Same WHERE-
    // resolution as kcdx.hook's `target` (the disassembler test — the name
    // resolves address AND ABI, the author writes no hex);
    // launch-time (registration pass), never a hot path.
    if (!targetName.empty()) {
        std::string err = ResolveTargetName(targetName, owner, *p);
        if (!err.empty()) {
            LogTeachingError(owningHandle,
                std::string("bytes '") + p->name + "': " + err);
            return 0;
        }
    }

    // Parse pattern / replacement / original / context / anchor. Mirrors
    // lua_bind_bytes.cpp:347-361 — try/catch -> teaching log + return 0.
    try {
        if (!patternStr.empty()) {
            p->pattern = kcdx::patch::ParsePattern(patternStr);
        }
        p->replacement = kcdx::patch::ParseBytes(replacementStr);
        if (!originalStr.empty()) {
            p->original = kcdx::patch::ParseBytes(originalStr);
        }
        if (!contextStr.empty()) {
            p->context = kcdx::patch::ParsePattern(contextStr);
        }
        if (!anchorStr.empty()) {
            p->anchor = kcdx::patch::AnchorString{anchorStr};
        }
    } catch (const std::exception& ex) {
        LogTeachingError(owningHandle,
            std::string("bytes '") + p->name + "': " + ex.what());
        return 0;
    }

    // original-length == replacement-length (lua_bind_bytes.cpp:363-371).
    if (!p->original.empty() && p->original.size() != p->replacement.size()) {
        LogTeachingError(owningHandle,
            std::string("bytes '") + p->name + "': original length (" +
            std::to_string(p->original.size()) + ") != replacement length (" +
            std::to_string(p->replacement.size()) + ").");
        return 0;
    }

    // Stamp the registry Entry. No Lua call site (C++ DLL plugin calls
    // directly) — leave callSiteFile empty / callSiteLine 0, mirroring how
    // hook_interface.cpp's QueueHookEntry handles it.
    kcdx::lua_registry::Entry e;
    e.kind     = kcdx::lua_registry::Kind::Bytes;
    e.name     = p->name;
    e.priority = p->priority;
    e.payload  = p;  // shared_ptr<PatchEntry> stored as shared_ptr<void>
    e.pluginName = owner.plugin;
    e.callSiteFile.clear();
    e.callSiteLine = 0;
    // The author threads through verbatim; pluginName falls back like the Lua
    // path (lua_bind_bytes.cpp:390-395) so patch_engine log lines have
    // attribution even for an anonymous (no owning plugin) registration.
    p->pluginAuthor = owner.author;
    p->pluginName   = e.pluginName.empty() ? std::string("<cpp>")
                                           : e.pluginName;

    std::string err;
    uint64_t handleId = kcdx::lua_registry::Append(std::move(e), &err);
    if (handleId == 0) {
        LOG_ERROR_KV("BYTES_INTERFACE", "append_failed",
            log::KV("plugin", owner.plugin.c_str()),
            log::KV("reason", err.c_str()));
    }
    return handleId;
}

// -----------------------------------------------------------------------
// Query thunks (4) — mirror hook_interface.cpp's quartet. handleId 0 /
// unknown / non-Bytes entries flow through the documented sentinel returns.
// EXCEPTION: Uninstall has no revert path for bytes (by design).
// -----------------------------------------------------------------------

bool Thunk_IsApplied(kcdxBytesHandle h) {
    if (h == 0) return false;
    const kcdx::lua_registry::Entry* e = kcdx::lua_registry::Find(h);
    if (!e) return false;
    return e->status.load(std::memory_order_acquire) ==
           kcdx::lua_registry::Status::Applied;
}

const char* Thunk_GetReason(kcdxBytesHandle h) {
    if (h == 0) return nullptr;
    const kcdx::lua_registry::Entry* e = kcdx::lua_registry::Find(h);
    if (!e) return nullptr;
    // Per Interfaces.h:1793-1798 — return null when the handle is valid AND
    // applied; otherwise return the stored reason. Find() returns a pointer
    // into the node-stable container, so the c_str() lifetime equals the
    // registry entry's lifetime (process lifetime; entries are append-only).
    if (e->status.load(std::memory_order_acquire) ==
        kcdx::lua_registry::Status::Applied) {
        return nullptr;
    }
    if (e->reason.empty()) return nullptr;
    return e->reason.c_str();
}

const char* Thunk_GetName(kcdxBytesHandle h) {
    if (h == 0) return nullptr;
    const kcdx::lua_registry::Entry* e = kcdx::lua_registry::Find(h);
    if (!e) return nullptr;
    return e->name.c_str();
}

bool Thunk_Uninstall(kcdxBytesHandle h) {
    // A byte rewrite has NO revert path — the original bytes are not retained
    // for restore, so a rewrite is permanent for the session (by design;
    // Interfaces.h:1805-1810). Do NOT route through any chain
    // uninstall (that's the Hook path). Return false + teach. Route the
    // teaching line to the owning plugin's log when we can find the entry by
    // handleId (matches GetName's lookup); engine-log always.
    const kcdx::lua_registry::Entry* e =
        (h != 0) ? kcdx::lua_registry::Find(h) : nullptr;
    const char* name = e ? e->name.c_str() : "<unknown>";

    LOG_WARN_KV("BYTES_INTERFACE", "uninstall_unsupported",
        log::KV("handle", static_cast<uint64_t>(h)),
        log::KV("name", name),
        log::KV("reason",
            "kcdx.bytes has no revert path — a byte rewrite is permanent for "
            "the session; uninstall is not supported for bytes registrations"));
    return false;
}

// -----------------------------------------------------------------------
// Vtable instance. Order MATCHES include/kcdx/Interfaces.h:1767-1816
// byte-for-byte (append-only ABI; fixed offsets). DO NOT reorder.
// -----------------------------------------------------------------------

kcdxBytesInterface g_bytesInterface = {
    /*Register=*/  Thunk_Register,
    /*IsApplied=*/ Thunk_IsApplied,
    /*GetReason=*/ Thunk_GetReason,
    /*GetName=*/   Thunk_GetName,
    /*Uninstall=*/ Thunk_Uninstall,
};

}  // namespace

const kcdxBytesInterface* GetInterface() {
    return &g_bytesInterface;
}

}  // namespace kcdx::bytes_interface
