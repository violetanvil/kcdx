// kcdx.hook — Lua-facing function-interception registration (sub-verb surface).
//
// A game-mod authoring surface: a plugin's plugin.lua declares "when the
// game calls this function, run my Lua callback" by calling a kcdx.hook
// SUB-VERB. kcdx.hook is a TABLE of sub-verbs, one per interception mode:
//
//   kcdx.hook.before(module, target, [locator], callback, [opts])
//   kcdx.hook.after (module, target, [locator], callback, [opts])
//   kcdx.hook.around(module, target, [locator], callback, [opts])
//   kcdx.hook.replace(module, target, [locator], callback, [opts])
//   kcdx.hook.insert_before(module, target, locator, callback, [opts])  -- locator REQUIRED
//   kcdx.hook.insert_after (module, target, locator, callback, [opts])  -- locator REQUIRED
//
//   -- the headline common path: name the function, the engine carries the
//   -- address AND verified signature (no hand-written ABI):
//   kcdx.hook.before("WHGame.dll", "IsInCombat", function(self) ... end)
//
//   -- or pass a kcdx.functions.* reference VALUE as the target (the engine
//   -- dispatches by arg-2 type: a string → name resolution; a reference
//   -- userdata → its resolved name + carried signature):
//   kcdx.hook.before("WHGame.dll", kcdx.functions.WHGame.IsInCombat, fn)
//
//   -- h:applied() -> nil (Pending) | true (Applied) | false (Failed)
//   -- h:reason()  -> string (when Failed) ;  h:name() -> string
//
// THE POSITIONAL CONTRACT (lua-api-surface.md rule 4 / 4a):
//   - `module` is the REQUIRED first positional on every sub-verb. No
//     default — the author types kcdx.hook.before("WHGame.dll", ...) every
//     time. Honest about multi-DLL coverage; an author copying an example
//     can't accidentally target the wrong module via a default.
//   - `target` (2nd positional) is a canonical name string / stable id /
//     Ghidra auto-name (resolved via the Address Library + refdb), OR a
//     kcdx.functions.* reference VALUE. The name carries address AND verified
//     signature — the disassembler test (cornerstones.md): no hand-written ABI
//     on the common path. ADVANCED locators (raw address / AOB pattern /
//     address_id / target_symbol) live in [opts] as the labeled escape hatch.
//   - `[locator]` (optional positional for before/after/around/replace;
//     REQUIRED for insert_before/after) is a kcdx.locator.* value. Omitted on
//     before/after/around/replace → the hook applies at the function entry
//     (the existing whole-function-hook behavior).
//   - `callback` — the Lua function. Mode semantics (before massages args,
//     original always runs; after transforms the return; replace suppresses
//     the original; around receives the original as a callable first param)
//     are UNCHANGED from the prior binder.
//   - `[opts]` — a trailing optional table for the non-positional knobs:
//     `name`, `description`, `signature` (for an advanced-locator / raw target
//     where the engine has no name to carry the ABI), `off_thread`, and the
//     advanced locators (`address` / `pattern` / `address_id` / `target_symbol`
//     / `target_lua_cfunction`). Required → positional, optional → trailing
//     table (lua-api-surface.md rule 4).
//
// TWO SUB-VERBS BEYOND THE DESIGN'S SIX (mid + callsite) — preserved because
// the regression suite's mid-capture (cap-04 / cap-21) and call-site-redirect
// (cap-22) coverage exercises engine machinery the six design verbs do not
// reach, and the suite must never go red across this surface migration. Their
// semantics are UNCHANGED from the prior binder:
//   kcdx.hook.mid(module, target, offset, captures, callback, [opts])
//     -- mid-function capture at `offset`: a named/positional captures table,
//        :get()/:set() on the handles, return "skip" to skip the instruction.
//   kcdx.hook.callsite(module, callsite, mode, callback, [opts])
//     -- redirect ONE call instruction; `callsite` is a target_callsite table
//        (rva / pattern / address_id); `mode` is the wrapping behavior
//        ("before"/"after"/"around"/"replace"). callsite needs a signature in
//        [opts] (the CALLED function's ABI). insert_before/after on a curated
//        statement supersede the design's intent here; this is the suite-green
//        carry-over until that path is wired (see SURFACED at file end).
//
// SCOPE: each sub-verb builds the queued payload, parses the signature,
// validates the locator + takes a GC-safe ref to the callback, and enqueues.
// The deferred apply pass (Kind::Hook handler -> hook_chain::Add) installs the
// interception in unified load order. Validation runs IMMEDIATELY (the caller
// gets (nil, err) in straight-line code) so the install itself waits for the
// end-of-zone apply pass (conflict resolution sees every plugin's intent before
// any byte changes).
//
// insert_before / insert_after: the sub-verb + registration shape are built
// here; the engine's statement-locator capture-thunk apply path is not yet
// wired, so an insert with a real statement locator is enqueued and rejected at
// apply with a teaching reason (NOT faked green). See SURFACED at file end.

#include "lua_bind_hook.h"

#include <memory>
#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "address_library.h"   // ResolveSignatureByName (target = "name")
#include "hook_chain.h"
#include "hook_payload.h"
#include "hook_signature.h"
#include "load_order.h"
#include "log.h"
#include "lua_bind_functions.h"  // ReadFunctionRef (arg-2-type dispatch: reference value target)
#include "lua_bind_helpers.h"    // FindUnknownKey (shared unknown-key gate)
#include "lua_bind_locator.h"    // ReadLocator (arg-type dispatch: locator positional)
#include "lua_memory.h"
#include "lua_registry.h"
#include "patch_engine.h"
#include "plugin_loader.h"       // g_runtimeGameVersionString + HandleOf (refdb CallerContext attribution)
#include "refdb.h"
#include "scripting.h"

namespace kcdx::lua_bind_hook {

namespace {

// =============================================================================
// The queued payload + apply handler (engine state, surface-independent).
// =============================================================================

// --- Apply handler (Kind::Hook) — UNCHANGED from the prior binder -----------
//
// Runs in the end-of-zone apply pass (lua_registry::ApplyZone), in unified load
// order. Installs the hook by resolving its locator + appending to the
// per-target chain via hook_chain::Add. On success the handle goes Applied; on
// conflict / resolution failure it goes Failed with a clear reason. On failure
// the callback registry ref is released (no live hook will use it).
bool ApplyHookEntry(kcdx::lua_registry::Entry& entry,
                    std::string& reason_out) {
    auto sp = std::static_pointer_cast<kcdx::hook_payload::HookPayload>(
                  entry.payload);
    kcdx::hook_payload::HookPayload* p = sp.get();
    if (!p) {
        reason_out = "internal error: hook entry payload is null";
        return false;
    }

    // insert_before / insert_after with a real statement locator: the engine
    // capture-thunk apply path against curated statement metadata is not yet
    // wired. Fail LOUD at apply (never a faked-green install) with a teaching
    // reason naming what is missing. A function-entry locator on an insert is
    // unreachable here (the binder rejects it earlier) — this branch fires only
    // for the statement-locator insert the binder enqueued with insertPending.
    if (p->insertPending) {
        reason_out =
            "kcdx.hook.insert_before/insert_after on a statement locator is "
            "not yet wired in the engine — the curated-statement capture-thunk "
            "apply path lands in a later step. The sub-verb + registration "
            "validate here; the install is deferred. (Use kcdx.hook.mid for an "
            "offset-based capture, or before/after/around/replace at the "
            "function entry, until then.)";
        return false;
    }

    int priority = entry.priority;
    if (!entry.pluginName.empty()) {
        priority = kcdx::load_order::Of(entry.pluginName).priority;
    }

    // A payload built by the kcdxHookInterface C thunks carries cFn != nullptr
    // and routes through hook_chain::AddC; Lua-built payloads leave cFn null
    // and route through Add. The two are mutex by construction — fail loud on
    // a payload carrying both (a binder bug; would be a debug-only no-op in
    // Release if left to assert()).
    if (p->cFn && p->callbackRef != LUA_NOREF) {
        log::ErrorF("hook_chain: D5 mutex violation on '%s' (plugin '%s'): "
                    "payload carries BOTH cFn (C thunk path) AND "
                    "callbackRef (Lua binder path) — exactly one must "
                    "be set. Refusing to apply this entry.",
                    entry.name.c_str(), entry.pluginName.c_str());
        reason_out = "internal: hook payload carries both cFn and "
                     "callbackRef (D5 mutex violation) — exactly one must "
                     "be set. Likely a binder bug — file a kcdx issue.";
        return false;
    }
    kcdx::hook_chain::AddResult r;
    if (p->cFn) {
        r = kcdx::hook_chain::AddC(
            *p, p->cFn, p->signature, entry.pluginName, priority,
            entry.name, entry.handleId);
    } else {
        r = kcdx::hook_chain::Add(
            /*L=*/nullptr, *p, p->callbackRef, entry.pluginName, priority,
            entry.name, entry.handleId);
    }

    if (!r.ok) {
        reason_out = r.reason;
        if (p->callbackRef != LUA_NOREF) {
            lua_State* gs = kcdx::scripting::lua_state();
            if (gs) luaL_unref(gs, LUA_REGISTRYINDEX, p->callbackRef);
            p->callbackRef = LUA_NOREF;
        }
        return false;
    }
    return true;
}

// --- Locator validation — UNCHANGED from the prior binder -------------------
//
// Exactly one function-entry locator must be set for the default
// (function-entry) scope. The COMMON locator is the resolved target name (lands
// in addressName). For callsite scope, the callsite sub-locator carries the
// patch target, so the function-entry-locator count must be zero.
std::string ValidateLocator(const kcdx::hook_payload::HookPayload& p) {
    const int entryLocatorCount =
        (!p.pattern.bytes.empty()      ? 1 : 0) +
        (p.addressId != 0              ? 1 : 0) +
        (!p.addressName.empty()        ? 1 : 0) +
        (!p.targetSymbol.empty()       ? 1 : 0) +
        (!p.targetLuaCfunction.empty() ? 1 : 0) +
        (p.address != 0                ? 1 : 0);

    if (p.callsiteScope) {
        if (!p.callsite.has_value()) {
            return "callsite requires a target_callsite table "
                   "(pattern, address_id, or rva)";
        }
        const auto& cs = *p.callsite;
        const int csLocatorCount =
            (!cs.pattern.bytes.empty() ? 1 : 0) +
            (cs.addressId != 0         ? 1 : 0) +
            (!cs.rva.empty()           ? 1 : 0);
        if (csLocatorCount != 1) {
            return "target_callsite must set exactly one of "
                   "pattern, address_id, or rva";
        }
        if (entryLocatorCount > 0) {
            return "callsite uses target_callsite for the patch target; do "
                   "not also set a function-entry target/locator";
        }
        return "";
    }

    if (entryLocatorCount == 0) {
        return "specify a target name (the 2nd positional) to hook a function "
               "by name — the engine resolves the address and ABI for you. "
               "(Advanced: a raw address=, pattern=, address_id=, "
               "target_symbol=, or target_lua_cfunction= in [opts] for targets "
               "the library can't name yet.)";
    }
    if (entryLocatorCount > 1) {
        return "set exactly ONE locator — normally the target name. (The "
               "advanced [opts] locators address/pattern/address_id/"
               "target_symbol/target_lua_cfunction are mutually exclusive with "
               "each other and with the target name.)";
    }
    if (p.callsite.has_value()) {
        return "target_callsite is only valid on kcdx.hook.callsite";
    }
    return "";
}

// --- mid captures parsing — UNCHANGED from the prior binder ------------------

bool IsKnownCaptureType(const std::string& tok) {
    static const char* kTypes[] = {
        "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
        "ptr", "f32", "f64", "float", "double", "bool",
    };
    for (const char* t : kTypes) if (tok == t) return true;
    return false;
}

void SplitCapture(const std::string& raw, std::string& expr, std::string& type) {
    type = "i64";
    expr = raw;
    const std::string::size_type colon = raw.find_last_of(':');
    if (colon != std::string::npos && colon + 1 < raw.size()) {
        std::string tail = raw.substr(colon + 1);
        if (IsKnownCaptureType(tail)) {
            expr = raw.substr(0, colon);
            type = tail;
        }
    }
}

// Read a `captures` TABLE at stack index `capIdx` into the payload's parallel
// capture vectors. Accepts the positional-list form {"rax", "[rcx]:i32"} and
// the name-map form {hp = "rax"}. Returns "" on success or a diagnostic. (The
// prior binder read this from the option table's `captures` key; here it reads
// the captures table passed as a mid positional.)
std::string ReadCapturesTable(lua_State* L, int capIdx,
                              kcdx::hook_payload::HookPayload& p) {
    if (!lua_istable(L, capIdx)) {
        return "captures must be a table (a list {\"rax\", \"[rcx+0x10]:i32\"} "
               "or a name map {hp=\"rax\", x=\"[rcx]:i32\"})";
    }

    bool nameMap = false;
    lua_pushnil(L);
    while (lua_next(L, capIdx) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING) nameMap = true;
        lua_pop(L, 1);
        if (nameMap) { lua_pop(L, 1); break; }
    }

    if (nameMap) {
        lua_pushnil(L);
        while (lua_next(L, capIdx) != 0) {
            if (lua_type(L, -2) != LUA_TSTRING || !lua_isstring(L, -1)) {
                lua_pop(L, 2);
                return "captures name-map entries must be name = \"expr:type\" "
                       "strings (e.g. hp = \"rax\")";
            }
            std::string name = lua_tostring(L, -2);
            std::string raw  = lua_tostring(L, -1);
            std::string expr, type;
            SplitCapture(raw, expr, type);
            p.captureExprs.push_back(std::move(expr));
            p.captureTypes.push_back(std::move(type));
            p.captureNames.push_back(std::move(name));
            lua_pop(L, 1);
        }
    } else {
        const int n = static_cast<int>(lua_objlen(L, capIdx));
        for (int i = 1; i <= n; ++i) {
            lua_rawgeti(L, capIdx, i);
            if (!lua_isstring(L, -1)) {
                lua_pop(L, 1);
                return "captures list entries must be \"expr:type\" strings "
                       "(e.g. \"rax\" or \"[rcx+0x10]:i32\")";
            }
            std::string raw = lua_tostring(L, -1);
            std::string expr, type;
            SplitCapture(raw, expr, type);
            p.captureExprs.push_back(std::move(expr));
            p.captureTypes.push_back(std::move(type));
            p.captureNames.push_back("");
            lua_pop(L, 1);
        }
    }
    return "";
}

// Build the callsite sub-locator from a target_callsite table at `csIdx`.
std::string ReadCallsiteTable(lua_State* L, int csIdx,
                              std::optional<kcdx::hook_payload::CallsiteLocator>& out) {
    if (!lua_istable(L, csIdx)) {
        return "callsite (the 2nd positional) must be a target_callsite table "
               "{ rva = ... } / { pattern = ... } / { address_id = ... }";
    }
    kcdx::hook_payload::CallsiteLocator cs;
    lua_getfield(L, csIdx, "offset");
    if (lua_isnumber(L, -1)) cs.offset = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    lua_getfield(L, csIdx, "address_id");
    if (lua_isnumber(L, -1)) cs.addressId = static_cast<uint64_t>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    lua_getfield(L, csIdx, "rva");
    if (lua_isstring(L, -1)) cs.rva = lua_tostring(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, csIdx, "pattern");
    std::string csPattern;
    if (lua_isstring(L, -1)) csPattern = lua_tostring(L, -1);
    lua_pop(L, 1);

    if (!csPattern.empty()) {
        try {
            cs.pattern = kcdx::patch::ParsePattern(csPattern);
        } catch (const std::exception& ex) {
            return std::string("target_callsite.pattern: ") + ex.what();
        }
    }
    out = std::move(cs);
    return "";
}

// =============================================================================
// The [opts] trailing table — the optional, non-positional knobs.
// =============================================================================
//
// Required args are positionals on the sub-verb; everything optional lives here
// (lua-api-surface.md rule 4). The recognized key set; an unknown string key
// fails loud (never a silent drop — a typo'd `signagure=` would otherwise
// vanish, the author's intent gone with no trace).
const char* kOptsKnown[] = {
    "name", "description", "off_thread",
    // advanced locators (the labeled escape hatch — never the common path)
    "address", "pattern", "address_id", "target_symbol",
    "target_lua_cfunction", "context", "anchor_string", "offset",
    // signature override / advanced-target ABI
    "signature",
};

std::string OptString(lua_State* L, int optsIdx, const char* key) {
    if (optsIdx == 0) return "";
    lua_getfield(L, optsIdx, key);
    std::string out;
    if (lua_isstring(L, -1)) out = lua_tostring(L, -1);
    lua_pop(L, 1);
    return out;
}

// Read a raw VA from the value at stack index `idx` — a kcdx.memory.pointer
// userdata, a raw lightuserdata, or an integer VA (NOT a lua number that lost
// precision — a pointer-magnitude integer is read as lua_Integer, exact on this
// build for the integer path). 0 if not an address-shaped value. Shared by the
// `address` opts locator and the mid sub-verb's raw-address target positional.
uintptr_t StackAddress(lua_State* L, int idx) {
    uintptr_t out = 0;
    if (lua_islightuserdata(L, idx)) {
        out = reinterpret_cast<uintptr_t>(lua_touserdata(L, idx));
    } else if (lua_type(L, idx) == LUA_TNUMBER) {
        out = static_cast<uintptr_t>(lua_tointeger(L, idx));
    } else if (lua_isuserdata(L, idx)) {
        if (lua_getmetatable(L, idx)) {
            luaL_getmetatable(L, kcdx::lua_memory::kPointerMetatable);
            if (lua_rawequal(L, -1, -2)) {
                lua_pop(L, 2);
                auto* p = static_cast<kcdx::lua_memory::pointer*>(
                    lua_touserdata(L, idx));
                if (p) out = static_cast<uintptr_t>(p->get_address());
            } else {
                lua_pop(L, 2);
            }
        }
    }
    return out;
}

// Read a raw-address locator from the `address` opts key. 0 if absent.
uintptr_t OptAddress(lua_State* L, int optsIdx) {
    if (optsIdx == 0) return 0;
    lua_getfield(L, optsIdx, "address");
    const uintptr_t out = StackAddress(L, -1);
    lua_pop(L, 1);
    return out;
}

// =============================================================================
// The build core — shared by every sub-verb.
// =============================================================================

// The resolved interception SCOPE/behaviour a sub-verb requests.
struct HookSpec {
    kcdx::hook_payload::Mode mode = kcdx::hook_payload::Mode::Before;
    bool callsiteScope = false;
    // For insert_before/after: the apply path is not yet wired, so a real
    // statement locator enqueues an entry that fails loud at apply.
    bool insertWithStatementLocator = false;
};

// Stack contract: `targetIdx` (0 for callsite — the callsite table carries the
// locator), `cbIdx` (the callback function), `optsIdx` (0 if no opts table),
// `csIdx`/`capIdx`/`offset` for callsite/mid (0 / unused otherwise). Builds the
// payload, validates, takes the GC-safe callback ref, enqueues. Returns
// PushHandleOrError's value (1 handle, or 2 nil+err) — the sub-verb returns it.
int BuildAndQueueHook(lua_State* L, const HookSpec& spec, const char* verb,
                      const std::string& module,
                      int targetIdx, int cbIdx, int optsIdx,
                      int csIdx, int capIdx, int midOffset) {
    // --- [opts] unknown-key gate (fail loud) ---
    if (optsIdx != 0) {
        if (!lua_istable(L, optsIdx)) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.hook.%s: the trailing [opts] argument must be a table "
                "(e.g. { name = \"...\", signature = \"...\" }).", verb);
            return 2;
        }
        std::string bad = kcdx::lua_bind_helpers::FindUnknownKey(
            L, optsIdx, kOptsKnown, sizeof(kOptsKnown) / sizeof(kOptsKnown[0]));
        if (!bad.empty()) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.hook.%s: unrecognized option key '%s' in [opts] — not a "
                "recognized kcdx.hook option (check for a typo).",
                verb, bad.c_str());
            return 2;
        }
    }

    auto p = std::make_shared<kcdx::hook_payload::HookPayload>();
    p->mode          = spec.mode;
    p->callsiteScope = spec.callsiteScope;
    p->insertPending = spec.insertWithStatementLocator;
    p->module        = module;
    p->name          = OptString(L, optsIdx, "name");
    if (p->name.empty()) p->name = std::string("lua_hook_") + verb;
    p->description = OptString(L, optsIdx, "description");

    // off_thread = "marshal" (default) / "skip" / "error".
    if (optsIdx != 0) {
        lua_getfield(L, optsIdx, "off_thread");
        if (lua_type(L, -1) == LUA_TSTRING) {
            const std::string s = lua_tostring(L, -1);
            if      (s == "marshal") p->offThread = 0;
            else if (s == "skip")    p->offThread = 1;
            else if (s == "error")   p->offThread = 2;
            else {
                lua_pop(L, 1);
                lua_pushnil(L);
                lua_pushfstring(L,
                    "kcdx.hook.%s '%s': off_thread must be \"marshal\" "
                    "(default), \"skip\", or \"error\" — got \"%s\".",
                    verb, p->name.c_str(), s.c_str());
                return 2;
            }
        } else if (!lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.hook.%s '%s': off_thread must be a string "
                "(\"marshal\" / \"skip\" / \"error\").",
                verb, p->name.c_str());
            return 2;
        }
        lua_pop(L, 1);
        // offset (advanced — applied after resolution).
        lua_getfield(L, optsIdx, "offset");
        if (lua_isnumber(L, -1)) p->offset = static_cast<int>(lua_tointeger(L, -1));
        lua_pop(L, 1);
    }

    // Owning plugin identity — drives self > engine > other in the resolver,
    // reused for the registry Entry below (single stack-walk).
    std::string callSiteFile;
    int         callSiteLine = 0;
    kcdx::lua_registry::OwningPlugin owner =
        kcdx::lua_registry::OwningPluginForCurrentCall(
            L, callSiteFile, callSiteLine);
    p->owningAuthor = owner.author;
    p->owningPlugin = owner.plugin;

    // --- Target resolution ---
    // For callsite scope the locator is the target_callsite table (csIdx); for
    // every other verb the target positional supplies the function locator.
    std::string targetName;   // the name we resolve a signature for (when by-name)
    bool        haveTarget = false;

    if (spec.callsiteScope) {
        std::string csErr = ReadCallsiteTable(L, csIdx, p->callsite);
        if (!csErr.empty()) {
            lua_pushnil(L);
            lua_pushfstring(L, "kcdx.hook.%s '%s': %s",
                            verb, p->name.c_str(), csErr.c_str());
            return 2;
        }
    } else {
        // arg-2-type dispatch: a kcdx.functions.* reference value → its name;
        // a string → name resolution; an advanced raw locator → [opts].
        kcdx::lua_bind_functions::FunctionRefView ref;
        if (kcdx::lua_bind_functions::ReadFunctionRef(L, targetIdx, ref)) {
            // A reference VALUE. A game reference carries a canonical name the
            // existing name path resolves (address + verified signature). A
            // by_id / plugin reference has no name string the name resolver can
            // take here — surface that (it routes through the not-yet-wired
            // reference-resolution path) rather than silently mis-resolving.
            if (!ref.name.empty()) {
                targetName = ref.name;
                haveTarget = true;
                p->addressName = targetName;
            } else {
                lua_pushnil(L);
                lua_pushfstring(L,
                    "kcdx.hook.%s '%s': the kcdx.functions reference passed as "
                    "the target carries no resolvable name (a by_id or "
                    "address-only reference). Pass a named reference "
                    "(kcdx.functions.<DLL>.<Name>) or the name string directly.",
                    verb, p->name.c_str());
                return 2;
            }
        } else if (lua_type(L, targetIdx) == LUA_TSTRING) {
            targetName = lua_tostring(L, targetIdx);
            haveTarget = true;
            p->addressName = targetName;
        } else if (lua_isnil(L, targetIdx)) {
            // No string target — the author is relying on an advanced [opts]
            // locator. Read those below; ValidateLocator enforces exactly-one.
        } else {
            // A raw-address target positional (a kcdx.memory.pointer / raw
            // lightuserdata / integer VA) — the advanced raw-VA locator passed
            // positionally rather than via [opts] address=. This is the common
            // mid shape (a kcdx.code stub pointer) and a legitimate advanced
            // form for the whole-function verbs. The VA IS the target.
            const uintptr_t rawVa = StackAddress(L, targetIdx);
            if (rawVa != 0) {
                p->address = rawVa;
            } else {
                lua_pushnil(L);
                lua_pushfstring(L,
                    "kcdx.hook.%s '%s': the target (2nd positional) must be a "
                    "function NAME string, a kcdx.functions.* reference value, "
                    "or a raw address (a kcdx.memory.pointer / VA) — got %s.",
                    verb, p->name.c_str(), luaL_typename(L, targetIdx));
                return 2;
            }
        }

        // Advanced [opts] locators (the labeled escape hatch).
        p->targetSymbol       = OptString(L, optsIdx, "target_symbol");
        p->targetLuaCfunction = OptString(L, optsIdx, "target_lua_cfunction");
        p->address            = OptAddress(L, optsIdx);
        const std::string patternStr = OptString(L, optsIdx, "pattern");
        const std::string contextStr = OptString(L, optsIdx, "context");
        const std::string anchorStr  = OptString(L, optsIdx, "anchor_string");
        // address_id opts key accepts a string (→ addressName) or a number.
        if (optsIdx != 0) {
            lua_getfield(L, optsIdx, "address_id");
            if (lua_type(L, -1) == LUA_TSTRING) {
                const std::string aidName = lua_tostring(L, -1);
                if (!p->addressName.empty()) {
                    lua_pop(L, 1);
                    lua_pushnil(L);
                    lua_pushfstring(L,
                        "kcdx.hook.%s '%s': set EITHER the target name OR "
                        "address_id = \"%s\", not both — they are the same "
                        "name-based locator.",
                        verb, p->name.c_str(), aidName.c_str());
                    return 2;
                }
                p->addressName = aidName;
                targetName = aidName;
                haveTarget = true;
            } else if (lua_type(L, -1) == LUA_TNUMBER) {
                p->addressId = static_cast<uint64_t>(lua_tointeger(L, -1));
            }
            lua_pop(L, 1);
        }

        try {
            if (!patternStr.empty()) p->pattern = kcdx::patch::ParsePattern(patternStr);
            if (!contextStr.empty()) p->context = kcdx::patch::ParsePattern(contextStr);
            if (!anchorStr.empty())  p->anchor  = kcdx::patch::AnchorString{anchorStr};
        } catch (const std::exception& ex) {
            lua_pushnil(L);
            lua_pushfstring(L, "kcdx.hook.%s '%s': %s",
                            verb, p->name.c_str(), ex.what());
            return 2;
        }
    }

    // --- Locator exclusivity / completeness ---
    {
        std::string locErr = ValidateLocator(*p);
        if (!locErr.empty()) {
            lua_pushnil(L);
            lua_pushfstring(L, "kcdx.hook.%s '%s': %s",
                            verb, p->name.c_str(), locErr.c_str());
            return 2;
        }
    }

    // --- mid: captures, not a function signature ---
    if (p->mode == kcdx::hook_payload::Mode::Mid) {
        p->offset = midOffset;
        std::string capErr = ReadCapturesTable(L, capIdx, *p);
        if (!capErr.empty()) {
            lua_pushnil(L);
            lua_pushfstring(L, "kcdx.hook.%s '%s': %s",
                            verb, p->name.c_str(), capErr.c_str());
            return 2;
        }
        if (p->captureExprs.empty()) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.hook.%s '%s': mid requires a non-empty captures table — "
                "the register/memory values to read/write at the offset "
                "(e.g. {\"rax\", \"[rcx+0x10]:i32\"} or {hp = \"rax\"}).",
                verb, p->name.c_str());
            return 2;
        }
    } else {
        // --- Signature DSL (before/after/around/replace/callsite/insert) ---
        // The engine needs the ABI to marshal args + return. Precedence:
        //   1. an explicit signature= in [opts] (expert / override — wins);
        //   2. the verified signature the Address Library carries for the name.
        const std::string sigStr = OptString(L, optsIdx, "signature");
        std::string effectiveSig = sigStr;
        bool        sigFromTarget = false;

        if (effectiveSig.empty() && haveTarget) {
            const char* entrySig =
                kcdx::address_library::ResolveSignatureByName(
                    targetName.c_str(), owner.author.c_str(),
                    owner.plugin.c_str());
            if (entrySig && entrySig[0] != '\0') {
                effectiveSig  = entrySig;
                sigFromTarget = true;
            }
        } else if (!effectiveSig.empty() && haveTarget) {
            // Sig-mismatch gate: explicit wins, but if the name ALSO carries a
            // verified ABI and the two are NOT compatible, emit a teaching
            // diagnostic (Hard → ERROR, a known crash risk; Soft → WARN).
            const char* verifiedSig =
                kcdx::address_library::ResolveSignatureByName(
                    targetName.c_str(), owner.author.c_str(),
                    owner.plugin.c_str());
            if (verifiedSig && verifiedSig[0] != '\0') {
                auto explicitParse = kcdx::hook_signature::Parse(effectiveSig);
                auto verifiedParse = kcdx::hook_signature::Parse(verifiedSig);
                if (explicitParse.ok && verifiedParse.ok) {
                    const auto kind = kcdx::hook_signature::ClassifyConflict(
                        explicitParse.sig, verifiedParse.sig);
                    if (kind == kcdx::hook_signature::SignatureConflictKind::Hard) {
                        LOG_ERROR_KV("HOOK_SIG_GATE",
                            "explicit_overrides_verified_hard",
                            log::KV("target",       targetName.c_str()),
                            log::KV("plugin",       owner.plugin.c_str()),
                            log::KV("explicit_sig", effectiveSig.c_str()),
                            log::KV("verified_sig", verifiedSig),
                            log::KV("used",         "explicit"),
                            log::KV("severity",     "hard"),
                            log::KV("crash_risk",   "true"),
                            log::KV("note",
                                "explicit signature used AS-AUTHORED (expert "
                                "override honored); arg-count or return-width "
                                "differs from the verified ABI — a frame "
                                "mis-description on a live engine function. If "
                                "the game crashes in or after this hook, this "
                                "is the cause."));
                    } else if (kind ==
                               kcdx::hook_signature::SignatureConflictKind::Soft) {
                        LOG_WARN_KV("HOOK_SIG_GATE", "explicit_overrides_verified",
                            log::KV("target",       targetName.c_str()),
                            log::KV("plugin",       owner.plugin.c_str()),
                            log::KV("explicit_sig", effectiveSig.c_str()),
                            log::KV("verified_sig", verifiedSig),
                            log::KV("used",         "explicit"),
                            log::KV("severity",     "soft"));
                    }
                }
            }
        }

        if (effectiveSig.empty()) {
            lua_pushnil(L);
            if (haveTarget) {
                const uintptr_t addr = kcdx::address_library::ResolveByName(
                    targetName.c_str(), owner.author.c_str(), owner.plugin.c_str());
                const bool declared =
                    addr != 0 ||
                    kcdx::address_library::FindResolvedAuthorTarget(
                        targetName.c_str(), owner.author.c_str(),
                        owner.plugin.c_str()) != nullptr;
                if (!declared) {
                    lua_pushfstring(L,
                        "kcdx.hook.%s '%s': target '%s' did not resolve — no "
                        "engine seed, no declared author target by that name. "
                        "This is an UNKNOWN target name (a typo, or a target "
                        "you have not declared). Check the name against "
                        "kcdx.addr.* or your declared targets; if you meant an "
                        "un-named site, declare a target with a pattern/rva + "
                        "signature, or supply signature= in [opts] with an "
                        "advanced locator (advanced).",
                        verb, p->name.c_str(), targetName.c_str());
                } else {
                    lua_pushfstring(L,
                        "kcdx.hook.%s '%s': target '%s' resolved to an address "
                        "but has no signature — a hook needs an ABI. If '%s' is "
                        "your own author-declared target, add signature=\"...\" "
                        "to its targets.toml row; otherwise supply signature= "
                        "in [opts] (advanced), or use a name whose ABI the "
                        "engine already knows.",
                        verb, p->name.c_str(), targetName.c_str(), targetName.c_str());
                }
            } else {
                lua_pushfstring(L,
                    "kcdx.hook.%s '%s': a signature= is required in [opts] (e.g. "
                    "\"void (ptr self, wstr szApp)\") so the engine knows the "
                    "function's argument + return types — or pass a target NAME "
                    "(2nd positional) to have the engine supply it.",
                    verb, p->name.c_str());
            }
            return 2;
        }
        {
            auto sr = kcdx::hook_signature::Parse(effectiveSig);
            if (!sr.ok) {
                lua_pushnil(L);
                if (sigFromTarget) {
                    lua_pushfstring(L,
                        "kcdx.hook.%s '%s': the Address Library signature for "
                        "target '%s' (\"%s\") failed to parse: %s. This is a "
                        "kcdx seed bug — report it; meanwhile you can override "
                        "with an explicit signature= in [opts].",
                        verb, p->name.c_str(), targetName.c_str(),
                        effectiveSig.c_str(), sr.error.c_str());
                } else if (sr.errorColumn > 0) {
                    lua_pushfstring(L,
                        "kcdx.hook.%s '%s': signature parse error at column "
                        "%d: %s", verb, p->name.c_str(), sr.errorColumn,
                        sr.error.c_str());
                } else {
                    lua_pushfstring(L,
                        "kcdx.hook.%s '%s': signature parse error: %s",
                        verb, p->name.c_str(), sr.error.c_str());
                }
                return 2;
            }
            p->signature    = std::move(sr.sig);
            p->hasSignature = true;
        }
    }

    // --- Callback closure (GC-safe registry ref) ---
    lua_pushvalue(L, cbIdx);
    p->callbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
    if (p->callbackRef == LUA_NOREF) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook.%s '%s': internal error: callback ref failed",
            verb, p->name.c_str());
        return 2;
    }

    // --- Queue the registration ---
    kcdx::lua_registry::Entry e;
    e.kind         = kcdx::lua_registry::Kind::Hook;
    e.name         = p->name;
    e.payload      = p;
    e.pluginName   = owner.plugin;
    e.callSiteFile = callSiteFile;
    e.callSiteLine = callSiteLine;

    std::string err;
    uint64_t handleId = kcdx::lua_registry::Append(std::move(e), &err);
    if (handleId == 0) {
        if (p->callbackRef != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, p->callbackRef);
            p->callbackRef = LUA_NOREF;
        }
    }
    return kcdx::lua_registry::PushHandleOrError(L, handleId, err);
}

// =============================================================================
// The sub-verbs — each parses its positionals and calls the build core.
// =============================================================================

// Read the REQUIRED `module` first positional. Returns false + leaves a
// (nil, err) on the stack when missing/non-string.
bool ReadModule(lua_State* L, const char* verb, std::string& out) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook.%s(module, target, ...): `module` (the 1st positional) "
            "is REQUIRED — the DLL the target lives in (e.g. \"WHGame.dll\"). "
            "There is no default module.", verb);
        return false;
    }
    out = lua_tostring(L, 1);
    return true;
}

// Shared body for before/after/around/replace:
//   kcdx.hook.<verb>(module, target, [locator], callback, [opts])
// The optional locator floats between target and callback: a function value at
// the 3rd slot is the callback (no locator); a kcdx.locator.* value at the 3rd
// slot is the locator and the callback shifts to the 4th. (lua-api-surface.md
// rule 4 — optional positional disambiguated by type.)
int WholeFunctionVerb(lua_State* L, kcdx::hook_payload::Mode mode,
                      const char* verb) {
    std::string module;
    if (!ReadModule(L, verb, module)) return 2;

    // target = arg 2.
    // arg 3 is EITHER the callback (function) OR a locator (kcdx.locator value).
    int cbIdx, optsIdx;
    {
        kcdx::lua_bind_locator::LocatorView lv;
        const bool arg3IsLocator =
            kcdx::lua_bind_locator::ReadLocator(L, 3, lv);
        if (arg3IsLocator) {
            // A non-function-entry locator on a whole-function verb is a
            // statement-level locator the function-entry path cannot honor —
            // teach the author toward insert_before/after (statement work) or
            // omitting the locator (function entry). function_entry() IS the
            // default, so accept it as a no-op locator.
            if (!lv.is_function_entry) {
                lua_pushnil(L);
                lua_pushfstring(L,
                    "kcdx.hook.%s: a statement-level locator "
                    "(kcdx.locator.%s) selects a point INSIDE the function — "
                    "kcdx.hook.%s hooks the whole function (its entry). For "
                    "statement-level interception use kcdx.hook.insert_before "
                    "/ insert_after with this locator; omit the locator (or "
                    "pass kcdx.locator.function_entry()) to hook the entry.",
                    verb, lv.kind_label.c_str(), verb);
                return 2;
            }
            cbIdx   = 4;   // locator consumed slot 3; callback is slot 4.
            optsIdx = lua_istable(L, 5) ? 5 : 0;
        } else {
            cbIdx   = 3;   // no locator; callback is slot 3.
            optsIdx = lua_istable(L, 4) ? 4 : 0;
        }
    }

    if (!lua_isfunction(L, cbIdx)) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook.%s(module, target, [locator], callback, [opts]): the "
            "callback (a function) is REQUIRED — got %s at the callback "
            "position. Call shape: kcdx.hook.%s(\"WHGame.dll\", \"IsInCombat\", "
            "function(...) end).",
            verb, luaL_typename(L, cbIdx), verb);
        return 2;
    }

    HookSpec spec;
    spec.mode = mode;
    return BuildAndQueueHook(L, spec, verb, module,
                             /*targetIdx=*/2, cbIdx, optsIdx,
                             /*csIdx=*/0, /*capIdx=*/0, /*midOffset=*/0);
}

int Lua_Before (lua_State* L) { return WholeFunctionVerb(L, kcdx::hook_payload::Mode::Before,  "before");  }
int Lua_After  (lua_State* L) { return WholeFunctionVerb(L, kcdx::hook_payload::Mode::After,   "after");   }
int Lua_Around (lua_State* L) { return WholeFunctionVerb(L, kcdx::hook_payload::Mode::Around,  "around");  }
int Lua_Replace(lua_State* L) { return WholeFunctionVerb(L, kcdx::hook_payload::Mode::Replace, "replace"); }

// insert_before / insert_after — locator REQUIRED:
//   kcdx.hook.insert_before(module, target, locator, callback, [opts])
// The callback receives captures as a named table (the same return-flow shape
// as `before`). The sub-verb + registration validate here; the engine
// statement-capture apply path is not yet wired (a real statement locator
// enqueues an entry that fails loud at apply — see ApplyHookEntry).
int InsertVerb(lua_State* L, kcdx::hook_payload::Mode mode, const char* verb) {
    std::string module;
    if (!ReadModule(L, verb, module)) return 2;

    // locator = arg 3 (REQUIRED).
    kcdx::lua_bind_locator::LocatorView lv;
    if (!kcdx::lua_bind_locator::ReadLocator(L, 3, lv)) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook.%s(module, target, locator, callback, [opts]): the "
            "`locator` (3rd positional) is REQUIRED — a kcdx.locator.* value "
            "naming the statement to insert at (e.g. "
            "kcdx.locator.first_call_to(\"IsInCombat\")). \"Insert before what?\" "
            "has no default.", verb);
        return 2;
    }
    if (!lua_isfunction(L, 4)) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook.%s(module, target, locator, callback, [opts]): the "
            "callback (4th positional, a function) is REQUIRED — got %s.",
            verb, luaL_typename(L, 4));
        return 2;
    }
    const int optsIdx = lua_istable(L, 5) ? 5 : 0;

    HookSpec spec;
    spec.mode = mode;
    // function_entry() as the insert locator is a degenerate "the entry" — the
    // capture-thunk path is still unwired, so even that enqueues the
    // not-yet-wired entry; a real statement locator does too.
    spec.insertWithStatementLocator = true;
    return BuildAndQueueHook(L, spec, verb, module,
                             /*targetIdx=*/2, /*cbIdx=*/4, optsIdx,
                             /*csIdx=*/0, /*capIdx=*/0, /*midOffset=*/0);
}

int Lua_InsertBefore(lua_State* L) { return InsertVerb(L, kcdx::hook_payload::Mode::Before, "insert_before"); }
int Lua_InsertAfter (lua_State* L) { return InsertVerb(L, kcdx::hook_payload::Mode::After,  "insert_after");  }

// mid — preserved for the regression suite (cap-04 / cap-21):
//   kcdx.hook.mid(module, target, offset, captures, callback, [opts])
// The target is typically a raw address ([opts] address=) at a kcdx.code stub;
// `offset` is the instruction offset inside the function/region; `captures` is
// the named/positional capture table. The author may pass the address as the
// `target` positional (a kcdx.memory.pointer) OR via [opts] address=. For mid,
// the target positional accepts a pointer/lightuserdata/integer VA directly.
int Lua_Mid(lua_State* L) {
    const char* verb = "mid";
    std::string module;
    if (!ReadModule(L, verb, module)) return 2;

    // offset = arg 3 (REQUIRED integer).
    if (lua_type(L, 3) != LUA_TNUMBER) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook.mid(module, target, offset, captures, callback, "
            "[opts]): `offset` (3rd positional, an integer) is REQUIRED — the "
            "byte offset of the captured instruction inside the function.");
        return 2;
    }
    const int midOffset = static_cast<int>(lua_tointeger(L, 3));
    // captures = arg 4 (REQUIRED table).
    if (!lua_istable(L, 4)) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook.mid(module, target, offset, captures, callback, "
            "[opts]): `captures` (4th positional, a table) is REQUIRED — the "
            "register/memory values to read/write at the offset "
            "({\"rax\"} or {hp=\"rax\"}).");
        return 2;
    }
    // callback = arg 5 (REQUIRED function).
    if (!lua_isfunction(L, 5)) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook.mid(module, target, offset, captures, callback, "
            "[opts]): the callback (5th positional, a function) is REQUIRED — "
            "got %s.", luaL_typename(L, 5));
        return 2;
    }
    const int optsIdx = lua_istable(L, 6) ? 6 : 0;

    // mid resolves its target the same way as the whole-function verbs (a name,
    // a reference value, or a raw address). The target positional (arg 2) may
    // be a kcdx.memory.pointer / lightuserdata / integer VA, which BuildAndQueue
    // does NOT read as a function target — so for the common mid case (a raw
    // pointer at a kcdx.code stub), surface the address via the build core's
    // raw-address handling by reading arg 2 as the address here.
    HookSpec spec;
    spec.mode = kcdx::hook_payload::Mode::Mid;
    return BuildAndQueueHook(L, spec, verb, module,
                             /*targetIdx=*/2, /*cbIdx=*/5, optsIdx,
                             /*csIdx=*/0, /*capIdx=*/4, midOffset);
}

// callsite — preserved for the regression suite (cap-22):
//   kcdx.hook.callsite(module, callsite, mode, callback, [opts])
// `callsite` is a target_callsite table (rva / pattern / address_id); `mode` is
// the wrapping behavior ("before"/"after"/"around"/"replace"). [opts] carries
// the signature (the CALLED function's ABI).
int Lua_Callsite(lua_State* L) {
    const char* verb = "callsite";
    std::string module;
    if (!ReadModule(L, verb, module)) return 2;

    // callsite table = arg 2.
    if (!lua_istable(L, 2)) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook.callsite(module, callsite, mode, callback, [opts]): "
            "`callsite` (2nd positional, a table) is REQUIRED — the call site "
            "to redirect ({ rva = ... } / { pattern = ... } / "
            "{ address_id = ... }).");
        return 2;
    }
    // mode string = arg 3.
    if (lua_type(L, 3) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook.callsite(module, callsite, mode, callback, [opts]): "
            "`mode` (3rd positional, a string) is REQUIRED — the wrapping "
            "behavior \"before\" / \"after\" / \"around\" / \"replace\".");
        return 2;
    }
    kcdx::hook_payload::Mode mode;
    {
        const std::string modeStr = lua_tostring(L, 3);
        if      (modeStr == "before")  mode = kcdx::hook_payload::Mode::Before;
        else if (modeStr == "after")   mode = kcdx::hook_payload::Mode::After;
        else if (modeStr == "around")  mode = kcdx::hook_payload::Mode::Around;
        else if (modeStr == "replace") mode = kcdx::hook_payload::Mode::Replace;
        else {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.hook.callsite: `mode` must be \"before\" / \"after\" / "
                "\"around\" / \"replace\" — got \"%s\". (A call-site redirect "
                "wraps the CALLED function; mid is not a callsite behavior.)",
                modeStr.c_str());
            return 2;
        }
    }
    if (!lua_isfunction(L, 4)) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook.callsite(module, callsite, mode, callback, [opts]): the "
            "callback (4th positional, a function) is REQUIRED — got %s.",
            luaL_typename(L, 4));
        return 2;
    }
    const int optsIdx = lua_istable(L, 5) ? 5 : 0;

    HookSpec spec;
    spec.mode = mode;
    spec.callsiteScope = true;
    return BuildAndQueueHook(L, spec, verb, module,
                             /*targetIdx=*/0, /*cbIdx=*/4, optsIdx,
                             /*csIdx=*/2, /*capIdx=*/0, /*midOffset=*/0);
}

}  // namespace

// Register the Kind::Hook deferred-apply handler. ENGINE state, not Lua-surface
// state — makes Kind::Hook appliable regardless of which surface (Lua kcdx.hook
// or the C++ kcdxHookInterface) queued the entry. Called at engine init
// (dllmain.cpp, before DiscoverAndLoad), NOT from bind() (bind runs at
// first-update-tick, too LATE for a C++ plugin's kcdxPlugin_Load whose thunk
// queues a Kind::Hook entry — Append rejects a Kind with no handler).
void RegisterHandlers() {
    kcdx::lua_registry::RegisterApplyHandler(
        kcdx::lua_registry::Kind::Hook, &ApplyHookEntry);
}

void bind(lua_State* L) {
    kcdx::lua_registry::EnsureHandleMetatable(L);

    // kcdx.hook is a TABLE of sub-verb functions (lua-api-surface.md rule 4a:
    // discrete behavioral variants are sub-verbs, not table keys). Each
    // sub-verb is its own registered C function carrying its mode.
    int kcdx_idx = lua_gettop(L);
    lua_newtable(L);
    const int hookTbl = lua_gettop(L);

    static const luaL_Reg kVerbs[] = {
        {"before",        Lua_Before},
        {"after",         Lua_After},
        {"around",        Lua_Around},
        {"replace",       Lua_Replace},
        {"insert_before", Lua_InsertBefore},
        {"insert_after",  Lua_InsertAfter},
        // Beyond the design's six — preserved for the regression suite (see
        // the file header + the SURFACED note).
        {"mid",           Lua_Mid},
        {"callsite",      Lua_Callsite},
        {nullptr, nullptr},
    };
    for (const luaL_Reg* v = kVerbs; v->name; ++v) {
        lua_pushcfunction(L, v->func);
        lua_setfield(L, hookTbl, v->name);
    }

    lua_setfield(L, kcdx_idx, "hook");
}

}  // namespace kcdx::lua_bind_hook
