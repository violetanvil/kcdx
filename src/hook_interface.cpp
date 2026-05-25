// kcdx::hook_interface — engine-side impl of kcdxHookInterface.
//
// Phase 3 sub-1 step 5-main chunks 3+4. Mirrors the Lua kcdx.hook.*
// binder (src/lua_bind_hook.cpp) — same payload shape, same load-order
// rules, same ApplyHookEntry routing — but takes raw C inputs from a
// C++ DLL plugin via the kcdxHookInterface vtable.
//
// Six sub-verb installers (Before/After/Around/Replace/Mid/Callsite)
// each:
//   1. validate target + callback per the kcdxHookOptions contract
//      (Interfaces.h:1402-1474),
//   2. build a hook_payload::HookPayload from (target, callback, opts),
//   3. resolve the signature (explicit opts->signature parse OR
//      address_library::ResolveSignatureByName by target name; Mid
//      treats signature as optional),
//   4. set payload.cFn so ApplyHookEntry's branch routes through
//      hook_chain::AddC (not hook_chain::Add — that's the Lua path),
//   5. synthesize a Kind::Hook lua_registry::Entry and Append; the
//      returned handleId is the kcdxHookHandle the author gets back.
//
// Four query thunks (IsApplied/GetReason/GetName/Uninstall) walk the
// registry by handleId — the SAME Find() / SetStatus() pair the Lua
// metatable methods use (src/lua_registry.cpp:146-193).

#include "hook_interface.h"

#include <memory>
#include <string>

#include "address_library.h"  // ResolveSignatureByName
#include "hook_chain.h"       // hook_chain::Uninstall
#include "hook_payload.h"
#include "hook_signature.h"
#include "log.h"
#include "lua_registry.h"
#include "patch_engine.h"     // patch::ParsePattern, patch::AnchorString
#include "plugin_loader.h"    // NameForHandle / AuthorForHandle

namespace kcdx::hook_interface {

namespace {

// Translate a kcdxHookCallsiteBehavior macro value into a payload Mode.
// Returns true on success; false (and *out untouched) on an unknown
// behavior value — the caller logs a teaching error and bails.
bool MapCallsiteBehavior(kcdxHookCallsiteBehavior b,
                         kcdx::hook_payload::Mode& out) {
    switch (b) {
        case kcdxHookCallsiteBehavior_Before:
            out = kcdx::hook_payload::Mode::Before;  return true;
        case kcdxHookCallsiteBehavior_After:
            out = kcdx::hook_payload::Mode::After;   return true;
        case kcdxHookCallsiteBehavior_Around:
            out = kcdx::hook_payload::Mode::Around;  return true;
        case kcdxHookCallsiteBehavior_Replace:
            out = kcdx::hook_payload::Mode::Replace; return true;
        default:
            return false;
    }
}

// Build the (author, plugin) pair for self > engine > other-plugin
// precedence from opts->owningPlugin. Both fields empty for
// kcdxInvalidPluginHandle / unknown handle — matches the existing
// Thunk_ResolveAddressByNameAs discipline (no mis-attribution; the
// resolver falls through to the anonymous engine-seed-only path).
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

// Push a teaching error to the engine log + the calling plugin's log
// (the auto-loud-on-failure contract documented at Interfaces.h:1507-
// 1509). The caller's plugin handle drives the per-plugin routing;
// kcdxInvalidPluginHandle falls back to engine-only.
void LogTeachingError(kcdxPluginHandle owningPlugin,
                      const char* verb,
                      const std::string& msg) {
    // Engine log: structured KV for grep + diagnosis.
    LOG_ERROR_KV("HOOK_INTERFACE", "install_failed",
        log::KV("verb",   verb),
        log::KV("plugin", kcdx::plugins::NameForHandle(owningPlugin).c_str()),
        log::KV("reason", msg.c_str()));
    // Per-plugin log: only if the handle is real.
    if (owningPlugin != kcdxInvalidPluginHandle) {
        LOG_PLUGIN_ERROR(owningPlugin, "HOOK_INTERFACE",
            "kcdxHookInterface::%s — %s", verb, msg.c_str());
    }
}

// Validate target + callback + (for Callsite) the callsite sub-locator.
// `target` may be null/"" only when an [advanced] locator is supplied in
// opts; `callback` must be non-null. For Callsite the caller has already
// validated callsite sub-locator below; this helper covers the
// installer-shared baseline.
//
// Returns an empty string on success or a ready-to-log diagnostic on
// failure (the caller logs + returns 0).
std::string ValidateBaseline(const char* target,
                             void*       callback,
                             const kcdxHookOptions* opts,
                             bool        isCallsite) {
    if (!callback) {
        return "callback is null — every install method requires a non-null "
               "function pointer (cast your free function or static member to "
               "void*)";
    }

    const bool haveTarget = (target && target[0] != '\0');
    const bool haveAdvancedLocator =
        opts && (
            (opts->pattern             && opts->pattern[0]) ||
             opts->addressId                                ||
            (opts->targetSymbol        && opts->targetSymbol[0]) ||
            (opts->targetLuaCfunction  && opts->targetLuaCfunction[0]) ||
             opts->address);

    if (!haveTarget && !haveAdvancedLocator) {
        return "specify `target` = the Address Library NAME of the function "
               "to hook (the engine resolves address + ABI for you). "
               "Advanced: pass an opts.pattern / .addressId / .targetSymbol "
               "/ .targetLuaCfunction / .address for a target the library "
               "can't name yet (then supply opts.signature too — there's no "
               "name to carry the ABI from).";
    }

    if (isCallsite) {
        const bool haveCsLoc = opts && (
            (opts->callsitePattern   && opts->callsitePattern[0]) ||
             opts->callsiteAddressId                              ||
            (opts->callsiteRva       && opts->callsiteRva[0]));
        if (!haveCsLoc) {
            return "Callsite requires an opts.callsitePattern / "
                   ".callsiteAddressId / .callsiteRva — the CALL "
                   "instruction inside the target function to redirect.";
        }
    }

    return "";
}

// Apply the shared opts → payload threading common to every install
// method. Caller has already done baseline validation. Returns "" on
// success; a diagnostic on a pattern/anchor parse error.
std::string ThreadOptsToPayload(const char* target,
                                const kcdxHookOptions* opts,
                                kcdx::hook_payload::HookPayload& p) {
    // Identity (defaults are set in HookPayload's struct member inits).
    if (opts && opts->name && opts->name[0]) {
        p.name = opts->name;
    } else if (target && target[0]) {
        // Engine-synthesized default per Interfaces.h:1408 — the binder
        // already does the equivalent for the Lua path
        // (lua_bind_hook.cpp:432). The :<handleId> portion is appended
        // by the loader at registration time; here we tag with target
        // so a no-name install is still greppable.
        p.name = std::string("<cppsynth>:") + target;
    } else {
        p.name = "<cppsynth>";
    }
    if (opts && opts->description) p.description = opts->description;

    // Module (engine substitutes "WHGame.dll" when null per Interfaces.h:1426).
    if (opts && opts->module && opts->module[0]) {
        p.module = opts->module;
    }

    if (opts) {
        p.offset = opts->offset;
        if (opts->maxAnchorDistance) {
            p.maxAnchorDistance = opts->maxAnchorDistance;
        }
        if (opts->context && opts->context[0]) {
            try {
                p.context = kcdx::patch::ParsePattern(opts->context);
            } catch (const std::exception& ex) {
                return std::string("opts.context parse failed: ") + ex.what();
            }
        }
        if (opts->anchorString && opts->anchorString[0]) {
            p.anchor = kcdx::patch::AnchorString{opts->anchorString};
        }

        // Function-entry locator (mutually exclusive with `target` —
        // the binder's ValidateLocator equivalent is in the per-mode
        // path; here we just thread whatever's set so ResolveLocator
        // reaches the right branch).
        if (opts->pattern && opts->pattern[0]) {
            try {
                p.pattern = kcdx::patch::ParsePattern(opts->pattern);
            } catch (const std::exception& ex) {
                return std::string("opts.pattern parse failed: ") + ex.what();
            }
        }
        if (opts->addressId)                p.addressId          = opts->addressId;
        if (opts->targetSymbol)             p.targetSymbol       = opts->targetSymbol;
        if (opts->targetLuaCfunction)       p.targetLuaCfunction = opts->targetLuaCfunction;
        if (opts->address)                  p.address            = opts->address;

        p.offThread = static_cast<uint8_t>(opts->offThread);
    }

    // The COMMON path — name supplies address + ABI. `target` arg lands
    // in addressName, the same slot Lua's `target="<name>"` feeds.
    if (target && target[0]) {
        p.addressName = target;
    }

    return "";
}

// Build the callsite sub-locator on the payload from opts (Callsite only).
// Returns "" on success or a diagnostic on a pattern-parse failure.
std::string ThreadCallsiteToPayload(const kcdxHookOptions* opts,
                                    kcdx::hook_payload::HookPayload& p) {
    p.callsiteScope = true;
    kcdx::hook_payload::CallsiteLocator cs;
    cs.offset    = opts->callsiteOffset;
    cs.addressId = opts->callsiteAddressId;
    if (opts->callsiteRva && opts->callsiteRva[0]) cs.rva = opts->callsiteRva;
    if (opts->callsitePattern && opts->callsitePattern[0]) {
        try {
            cs.pattern = kcdx::patch::ParsePattern(opts->callsitePattern);
        } catch (const std::exception& ex) {
            return std::string("opts.callsitePattern parse failed: ") +
                   ex.what();
        }
    }
    p.callsite = std::move(cs);
    return "";
}

// Resolve the signature for the payload: explicit opts->signature wins;
// else the verified ABI from the Address Library for `target`'s name.
// For Mid the signature is optional (the Lua path also accepts a no-sig
// mid — make_jit_midfunc keys on capture types, not function ABI).
//
// Returns "" on success or a diagnostic on a parse / no-ABI failure.
std::string ResolveSignature(const char* target,
                             const kcdxHookOptions* opts,
                             const Owner& owner,
                             bool isMid,
                             kcdx::hook_payload::HookPayload& p) {
    std::string sigStr;
    if (opts && opts->signature && opts->signature[0]) {
        sigStr = opts->signature;
    }
    if (sigStr.empty() && target && target[0]) {
        const char* entrySig = kcdx::address_library::ResolveSignatureByName(
            target, owner.author.c_str(), owner.plugin.c_str());
        if (entrySig && entrySig[0]) sigStr = entrySig;
    } else if (!sigStr.empty() && target && target[0]) {
        // Sig-mismatch gate (AP12/AP13): the author named a target AND
        // hand-wrote an explicit opts->signature. The explicit one WINS
        // (the deliberate-override case — the author may know better than
        // the seed, or be overriding a stale row), but if the name ALSO
        // carries a verified library ABI and the two are NOT compatible,
        // the silent-trust is a footgun — a wrong explicit sig mis-marshals
        // with no diagnostic. Consult the verified ABI to DETECT the
        // conflict (not to override), and emit a teaching WARN naming both
        // signatures + that the explicit one is used as-authored. The
        // resolution itself is unchanged: explicit sigStr proceeds.
        const char* verifiedSig = kcdx::address_library::ResolveSignatureByName(
            target, owner.author.c_str(), owner.plugin.c_str());
        if (verifiedSig && verifiedSig[0]) {
            auto explicitParse = kcdx::hook_signature::Parse(sigStr);
            auto verifiedParse = kcdx::hook_signature::Parse(verifiedSig);
            // Only compare when BOTH parse — a malformed explicit sig is
            // caught by the parse below; a malformed verified seed is a
            // seed bug surfaced elsewhere. The gate is about a clean-but-
            // wrong explicit sig vs a clean verified ABI.
            if (explicitParse.ok && verifiedParse.ok &&
                !kcdx::hook_signature::SignaturesCompatible(
                    explicitParse.sig, verifiedParse.sig)) {
                LOG_WARN_KV("HOOK_SIG_GATE", "explicit_overrides_verified",
                    log::KV("target",       target),
                    log::KV("plugin",
                            kcdx::plugins::NameForHandle(
                                opts ? opts->owningPlugin
                                     : kcdxInvalidPluginHandle).c_str()),
                    log::KV("explicit_sig", sigStr.c_str()),
                    log::KV("verified_sig", verifiedSig),
                    log::KV("used",         "explicit"));
            }
        }
    }
    if (sigStr.empty()) {
        if (isMid) {
            // Mid permits no signature (mirrors the Lua path).
            p.hasSignature = false;
            return "";
        }
        if (target && target[0]) {
            return std::string("target '") + target +
                   "' resolved to an address but has no signature — "
                   "kcdxHookInterface needs an ABI. Supply opts.signature, "
                   "or use a target name whose ABI the engine already "
                   "knows.";
        }
        return "non-Mid install requires opts.signature — there's no `target` "
               "name to carry the ABI from.";
    }
    auto sr = kcdx::hook_signature::Parse(sigStr);
    if (!sr.ok) {
        return std::string("signature parse failed: ") + sr.error;
    }
    p.signature    = std::move(sr.sig);
    p.hasSignature = true;
    return "";
}

// Shared body: build the payload + entry, append, return handleId. The
// caller has already set p->mode + p->callsiteScope + p->cFn.
uint64_t QueueHookEntry(const std::shared_ptr<kcdx::hook_payload::HookPayload>& p,
                        const Owner& owner) {
    kcdx::lua_registry::Entry e;
    e.kind       = kcdx::lua_registry::Kind::Hook;
    e.name       = p->name;
    e.payload    = p;
    e.pluginName = owner.plugin;
    e.callSiteFile.clear();
    e.callSiteLine = 0;

    std::string err;
    uint64_t handleId = kcdx::lua_registry::Append(std::move(e), &err);
    if (handleId == 0) {
        LOG_ERROR_KV("HOOK_INTERFACE", "append_failed",
            log::KV("plugin", owner.plugin.c_str()),
            log::KV("reason", err.c_str()));
    }
    return handleId;
}

// One installer body shared by Before/After/Around/Replace/Mid. The
// mode + isMid flags drive the small variations (Mid skips the
// signature-required gate; the others require a signature).
uint64_t InstallNonCallsite(const char* target, void* callback,
                            const kcdxHookOptions* opts,
                            kcdx::hook_payload::Mode mode,
                            const char* verbForLog) {
    kcdxPluginHandle owningHandle =
        opts ? opts->owningPlugin : kcdxInvalidPluginHandle;

    {
        std::string err = ValidateBaseline(target, callback, opts,
                                           /*isCallsite=*/false);
        if (!err.empty()) {
            LogTeachingError(owningHandle, verbForLog, err);
            return 0;
        }
    }

    Owner owner = OwnerFromHandle(owningHandle);
    auto p = std::make_shared<kcdx::hook_payload::HookPayload>();
    p->mode          = mode;
    p->owningAuthor  = owner.author;
    p->owningPlugin  = owner.plugin;

    {
        std::string err = ThreadOptsToPayload(target, opts, *p);
        if (!err.empty()) {
            LogTeachingError(owningHandle, verbForLog, err);
            return 0;
        }
    }

    {
        const bool isMid = (mode == kcdx::hook_payload::Mode::Mid);
        std::string err = ResolveSignature(target, opts, owner, isMid, *p);
        if (!err.empty()) {
            LogTeachingError(owningHandle, verbForLog, err);
            return 0;
        }
    }

    // Mid captures: thread opts->captures + captureCount into the
    // payload's parallel-vector form (lua_bind_hook.cpp:325-388
    // ReadCaptures equivalent). The kcdxHookCapture array carries
    // (expr, type, name) per capture; we drop them into the same three
    // vectors AddMid consumes.
    if (mode == kcdx::hook_payload::Mode::Mid) {
        if (opts && opts->captures && opts->captureCount > 0) {
            for (uint32_t i = 0; i < opts->captureCount; ++i) {
                const auto& c = opts->captures[i];
                p->captureExprs.emplace_back(c.expr  ? c.expr  : "");
                p->captureTypes.emplace_back(c.type  ? c.type  : "i64");
                p->captureNames.emplace_back(c.name  ? c.name  : "");
            }
        }
        if (p->captureExprs.empty()) {
            LogTeachingError(owningHandle, verbForLog,
                "Mid requires a non-empty opts.captures array — the "
                "register/memory values to read/write at opts.offset.");
            return 0;
        }
    }

    p->cFn = callback;

    return QueueHookEntry(p, owner);
}

uint64_t InstallCallsite(const char* target, void* callback,
                         const kcdxHookOptions* opts) {
    kcdxPluginHandle owningHandle =
        opts ? opts->owningPlugin : kcdxInvalidPluginHandle;

    {
        std::string err = ValidateBaseline(target, callback, opts,
                                           /*isCallsite=*/true);
        if (!err.empty()) {
            LogTeachingError(owningHandle, "Callsite", err);
            return 0;
        }
    }

    kcdx::hook_payload::Mode behaviorMode;
    if (!MapCallsiteBehavior(opts ? opts->callsiteBehavior
                                  : kcdxHookCallsiteBehavior_Before,
                             behaviorMode)) {
        LogTeachingError(owningHandle, "Callsite",
            "opts.callsiteBehavior must be one of "
            "kcdxHookCallsiteBehavior_Before / _After / _Around / _Replace "
            "(default 0 = Before). Mid is not a valid callsite behavior — "
            "a call-site redirect wraps the called function.");
        return 0;
    }

    Owner owner = OwnerFromHandle(owningHandle);
    auto p = std::make_shared<kcdx::hook_payload::HookPayload>();
    p->mode          = behaviorMode;
    p->owningAuthor  = owner.author;
    p->owningPlugin  = owner.plugin;

    {
        std::string err = ThreadOptsToPayload(target, opts, *p);
        if (!err.empty()) {
            LogTeachingError(owningHandle, "Callsite", err);
            return 0;
        }
    }
    {
        std::string err = ThreadCallsiteToPayload(opts, *p);
        if (!err.empty()) {
            LogTeachingError(owningHandle, "Callsite", err);
            return 0;
        }
    }
    {
        // Callsite uses the function-entry behaviors; a signature is
        // required (same as Add()'s non-Mid gate).
        std::string err = ResolveSignature(target, opts, owner,
                                           /*isMid=*/false, *p);
        if (!err.empty()) {
            LogTeachingError(owningHandle, "Callsite", err);
            return 0;
        }
    }

    p->cFn = callback;

    return QueueHookEntry(p, owner);
}

// -----------------------------------------------------------------------
// Sub-verb thunks (6) — one per kcdxHookInterface install method.
// -----------------------------------------------------------------------

kcdxHookHandle Thunk_Before(const char* target, void* callback,
                            const kcdxHookOptions* opts) {
    return InstallNonCallsite(target, callback, opts,
                              kcdx::hook_payload::Mode::Before, "Before");
}
kcdxHookHandle Thunk_After(const char* target, void* callback,
                           const kcdxHookOptions* opts) {
    return InstallNonCallsite(target, callback, opts,
                              kcdx::hook_payload::Mode::After, "After");
}
kcdxHookHandle Thunk_Around(const char* target, void* callback,
                            const kcdxHookOptions* opts) {
    return InstallNonCallsite(target, callback, opts,
                              kcdx::hook_payload::Mode::Around, "Around");
}
kcdxHookHandle Thunk_Replace(const char* target, void* callback,
                             const kcdxHookOptions* opts) {
    return InstallNonCallsite(target, callback, opts,
                              kcdx::hook_payload::Mode::Replace, "Replace");
}
kcdxHookHandle Thunk_Mid(const char* target, void* callback,
                         const kcdxHookOptions* opts) {
    return InstallNonCallsite(target, callback, opts,
                              kcdx::hook_payload::Mode::Mid, "Mid");
}
kcdxHookHandle Thunk_Callsite(const char* target, void* callback,
                              const kcdxHookOptions* opts) {
    return InstallCallsite(target, callback, opts);
}

// -----------------------------------------------------------------------
// Query thunks (4) — mirror H_uninstall's Hook-kind semantics from
// src/lua_registry.cpp:146-193. handleId 0 / unknown / non-Hook entries
// flow through the documented sentinel returns.
// -----------------------------------------------------------------------

bool Thunk_IsApplied(kcdxHookHandle h) {
    if (h == 0) return false;
    const kcdx::lua_registry::Entry* e = kcdx::lua_registry::Find(h);
    if (!e) return false;
    return e->status.load(std::memory_order_acquire) ==
           kcdx::lua_registry::Status::Applied;
}

const char* Thunk_GetReason(kcdxHookHandle h) {
    if (h == 0) return nullptr;
    const kcdx::lua_registry::Entry* e = kcdx::lua_registry::Find(h);
    if (!e) return nullptr;
    // Per Interfaces.h:1554 — return null when the handle is valid AND
    // applied; otherwise return the stored reason. Find() returns a
    // pointer into the node-stable std::deque (lua_registry.cpp:35), so
    // the c_str() lifetime equals the registry entry's lifetime (process
    // lifetime; entries are append-only).
    if (e->status.load(std::memory_order_acquire) ==
        kcdx::lua_registry::Status::Applied) {
        return nullptr;
    }
    if (e->reason.empty()) return nullptr;
    return e->reason.c_str();
}

const char* Thunk_GetName(kcdxHookHandle h) {
    if (h == 0) return nullptr;
    const kcdx::lua_registry::Entry* e = kcdx::lua_registry::Find(h);
    if (!e) return nullptr;
    return e->name.c_str();
}

bool Thunk_Uninstall(kcdxHookHandle h) {
    if (h == 0) return true;  // idempotent per Interfaces.h:1568-1570
    // hook_chain::Uninstall is idempotent at the engine layer (returns
    // true on unknown / already-handled ids); SetStatus is no-op on
    // unknown handle.
    bool ok = kcdx::hook_chain::Uninstall(h);
    kcdx::lua_registry::SetStatus(h, kcdx::lua_registry::Status::Removed);
    return ok;
}

// -----------------------------------------------------------------------
// Vtable instance. Order MATCHES include/kcdx/Interfaces.h:1515-1575
// byte-for-byte (AP11). DO NOT reorder.
// -----------------------------------------------------------------------

kcdxHookInterface g_hookInterface = {
    /*Before=*/   Thunk_Before,
    /*After=*/    Thunk_After,
    /*Around=*/   Thunk_Around,
    /*Replace=*/  Thunk_Replace,
    /*Mid=*/      Thunk_Mid,
    /*Callsite=*/ Thunk_Callsite,
    /*IsApplied=*/Thunk_IsApplied,
    /*GetReason=*/Thunk_GetReason,
    /*GetName=*/  Thunk_GetName,
    /*Uninstall=*/Thunk_Uninstall,
};

}  // namespace

const kcdxHookInterface* GetInterface() {
    return &g_hookInterface;
}

}  // namespace kcdx::hook_interface
