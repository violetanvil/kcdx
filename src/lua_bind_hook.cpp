// kcdx.hook — Lua-facing function-interception registration.
//
// Phase 2b sub-3 of the manifest-only restructure (see
// docs/outstanding-work/restructure-plan.md). This is a game-mod
// authoring surface: a plugin's plugin.lua declares "when the game
// calls this function, run my Lua callback" by calling kcdx.hook{...}.
// It succeeds the v0.1 [[hook]] + [[mid_hook]] TOML schemas and the
// runtime kcdx.memory.dynamic_hook helper, collapsing all three into
// one function call.
//
//   local h = kcdx.hook{
//       name      = "outfit_gate",
//       target    = "lua_pcall",     -- THE common locator: name the
//                                    -- function; the engine resolves its
//                                    -- address AND verified signature.
//       -- attach the callback under the MODE NAME itself. Exactly one of
//       -- before / after / around / replace per call:
//       before = function(self, szApp, flags)
//           if szApp:find(":") then szApp = (szApp:gsub(":", " -")) end
//           return self, szApp, flags          -- returned values flow into the original
//       end,
//   }
//   -- h:applied() -> nil (Pending) | true (Applied) | false (Failed)
//   -- h:reason()  -> string (when Failed)
//   -- h:name()    -> string
//
// Locators: the common path is `target = "<name>"` (above) — the name
// supplies BOTH address and verified signature, so no hand-written
// signature/hex is needed (the disassembler test — cornerstones.md /
// AP12). ADVANCED/EXPERT locators exist for targets the library can't
// name yet — exactly one of `address` (raw VA/pointer), `pattern` (AOB),
// `address_id` (numeric id), `target_symbol`, `target_lua_cfunction`, or
// `mode = "callsite"` with a `target_callsite` sub-locator. These carry
// hex/ABI burden and are the escape hatch, never the path a normal
// author is funneled down. When you use one, supply `signature = "..."`
// yourself (the engine has no name to carry the ABI from).
//
// Callback surface (the author writes the target function, in Lua):
//   - Params arrive as POSITIONAL callback arguments, named by the
//     author's own function(...) list. The signature string supplies
//     the TYPES; the parameter names are the author's choice.
//   - Mutation is by RETURN. "What you return is what flows forward."
//       before  : return nothing -> original runs with unchanged args;
//                 return N values -> they replace the args. before
//                 ALWAYS runs the original (it massages inputs only).
//       after   : receives the return value; returns the (possibly
//                 changed) return value.
//       replace : original never runs; the return is the result. An
//                 empty replace = function() end suppresses the call.
//       around  : receives the original as a callable first parameter;
//                 calls it 0/1/N times, returns the result. The full
//                 wrap; the only mode that can conditionally skip.
//
// SCOPE: this file builds the queued payload, parses the signature,
// validates the locator + the mode-as-key rule (exactly one of
// before/after/around/replace), takes a GC-safe ref to the callback,
// and enqueues. The deferred apply pass (Kind::Hook handler ->
// hook_chain::Add) installs the interception in unified load order.
// mid / callsite modes are NOT accepted here yet (their own sub-commits;
// the payload carries the enum values so those subs are additive).
//
// Validation runs IMMEDIATELY (table shape, locator exclusivity, mode
// name, signature parse) so the caller gets (nil, err) in straight-line
// code. Per the plan §"Confirmed design decisions" #2, the install
// itself waits for the end-of-zone apply pass so conflict resolution
// sees every plugin's intent before any byte changes.

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
#include "lua_bind_helpers.h"  // FindUnknownKey (shared unknown-key gate)
#include "lua_memory.h"
#include "lua_registry.h"
#include "patch_engine.h"
#include "scripting.h"

namespace kcdx::lua_bind_hook {

namespace {

// --- Lua-table helpers (kept TU-local; same shape as lua_bind_bytes) ---

std::string LuaTableString(lua_State* L, int tableIdx, const char* key,
                           const char* fallback = "") {
    lua_getfield(L, tableIdx, key);
    std::string out = fallback;
    if (lua_isstring(L, -1)) out = lua_tostring(L, -1);
    lua_pop(L, 1);
    return out;
}

int LuaTableInt(lua_State* L, int tableIdx, const char* key, int fallback) {
    lua_getfield(L, tableIdx, key);
    int out = fallback;
    if (lua_isnumber(L, -1)) out = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    return out;
}

// Read a `key` that may be a kcdx.memory.pointer userdata, a raw
// lightuserdata (exact VA — preferred for pointer-magnitude values; see
// .claude/rules/lua-precision.md), or an integer VA. Returns 0 if absent
// / wrong type. Same target shape as kcdx.memory.dynamic_hook's `target`.
uintptr_t LuaTableAddress(lua_State* L, int tableIdx, const char* key) {
    lua_getfield(L, tableIdx, key);
    uintptr_t out = 0;
    if (lua_islightuserdata(L, -1)) {
        out = reinterpret_cast<uintptr_t>(lua_touserdata(L, -1));
    } else if (lua_isnumber(L, -1)) {
        out = static_cast<uintptr_t>(lua_tointeger(L, -1));
    } else if (lua_isuserdata(L, -1)) {
        if (lua_getmetatable(L, -1)) {
            luaL_getmetatable(L, kcdx::lua_memory::kPointerMetatable);
            if (lua_rawequal(L, -1, -2)) {
                lua_pop(L, 2);
                auto* p = static_cast<kcdx::lua_memory::pointer*>(
                    lua_touserdata(L, -1));
                if (p) out = static_cast<uintptr_t>(p->get_address());
            } else {
                lua_pop(L, 2);
            }
        }
    }
    lua_pop(L, 1);
    return out;
}

uint64_t LuaTableU64(lua_State* L, int tableIdx, const char* key,
                     uint64_t fallback) {
    lua_getfield(L, tableIdx, key);
    uint64_t out = fallback;
    if (lua_isnumber(L, -1)) out = static_cast<uint64_t>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    return out;
}

// Pull `key` from the table as a Lua function and stash it in the Lua
// registry so the GC won't collect the closure between registration
// and the deferred apply pass. Returns the luaL_ref handle, or
// LUA_NOREF if the field is absent / not a function.
int TakeCallbackRef(lua_State* L, int tableIdx, const char* key) {
    lua_getfield(L, tableIdx, key);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return LUA_NOREF;
    }
    // luaL_ref pops the value off the stack and returns a stable int key
    // into LUA_REGISTRYINDEX. Released by the apply pass / teardown via
    // luaL_unref when the hook is uninstalled or fails to apply.
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

// --- Unknown-key rejection (fail-state-logging.md / AP14) ---------------
//
// The recognized option-key set for kcdx.hook. The binder reads only the
// keys it knows via lua_getfield and historically IGNORED any sibling key —
// so a typo'd `signagure=` / `target_calsite=` vanished, the author's intent
// gone with no trace. The shared kcdx::lua_bind_helpers::FindUnknownKey
// iterates the top-level options table and validates STRING keys against
// this set; integer/array keys are NOT checked (a captures list {"rax", ...}
// is freeform). This list stays local because the key set belongs to this
// binder.
static const char* kKnown[] = {
    // identity / metadata
    "name", "description", "module", "offset", "off_thread", "priority",
    // scope selector
    "mode",
    // behavior-as-key (the callback lands under its mode name)
    "before", "after", "around", "replace", "mid",
    // locators (common + advanced hatch)
    "address_id", "target", "target_symbol", "target_lua_cfunction",
    "address", "pattern", "context", "anchor_string",
    // mode-specific sub-tables / signature
    "target_callsite", "captures", "signature",
};

// --- Apply handler ------------------------------------------------------
//
// Registered for Kind::Hook. Runs in the end-of-zone apply pass
// (lua_registry::ApplyZone), in unified load order. Installs the hook by
// resolving its locator + appending to the per-target chain via
// hook_chain::Add. On success the handle goes Applied; on conflict /
// resolution failure it goes Failed with a clear reason. On failure the
// callback registry ref is released (no live hook will use it).
bool ApplyHookEntry(kcdx::lua_registry::Entry& entry,
                    std::string& reason_out) {
    auto sp = std::static_pointer_cast<kcdx::hook_payload::HookPayload>(
                  entry.payload);
    kcdx::hook_payload::HookPayload* p = sp.get();
    if (!p) {
        reason_out = "internal error: hook entry payload is null";
        return false;
    }

    // Effective load-order priority for chain ordering. Anonymous
    // entries (no owning plugin yet) fall back to the entry's own
    // priority field (default 50).
    int priority = entry.priority;
    if (!entry.pluginName.empty()) {
        priority = kcdx::load_order::Of(entry.pluginName).priority;
    }

    // L is captured by hook_chain at first-tick (SetLuaState) and again
    // on first Add; pass nullptr here — Add uses the already-bound state.
    // handleId is the registry id of THIS entry — threaded into the
    // ChainEntry so a later kcdx.hook_chain::Uninstall(handleId) can find
    // and remove it.
    //
    // Routing branch (Phase 3 sub-1 step 5-main chunks 3+4): a payload
    // built by the kcdxHookInterface C thunks (src/hook_interface.cpp)
    // carries cFn != nullptr and routes through hook_chain::AddC.
    // Lua-built payloads leave cFn null and route through Add. The two
    // surfaces are mutex by construction — fail loud (log::ErrorF +
    // Failed reason) on violation. Bare assert() would be a debug-only
    // no-op in Release; this surface MUST trip in shipped builds when
    // a bug stamps both fields. The corrupted entry flips to Failed
    // status with the reason; the apply pass continues with other
    // entries.
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
        // Release the callback ref — this hook will never fire.
        if (p->callbackRef != LUA_NOREF) {
            lua_State* gs = kcdx::scripting::lua_state();
            if (gs) luaL_unref(gs, LUA_REGISTRYINDEX, p->callbackRef);
            p->callbackRef = LUA_NOREF;
        }
        return false;
    }
    return true;
}

// --- Locator validation -------------------------------------------------
//
// Exactly one function-entry locator must be set for the default
// (function-entry) scope. The COMMON locator is `target = "<name>"`,
// which lands in addressName and is counted below. For mode = "callsite",
// the callsite sub-locator carries the patch target, so the
// function-entry-locator count must be zero. Returns an empty string on
// success or a ready-to-return diagnostic on failure.
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
            return "mode='callsite' requires a target_callsite table "
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
        // mode='callsite' locates the patch target via target_callsite;
        // no function-entry locator may also be set.
        if (entryLocatorCount > 0) {
            return "mode='callsite' uses target_callsite for the patch "
                   "target; do not also set target/pattern/address_id/"
                   "target_symbol/target_lua_cfunction";
        }
        return "";
    }

    // Non-callsite modes: exactly one function-entry locator. The common
    // path is `target = "<name>"`; the raw locators are an advanced hatch.
    if (entryLocatorCount == 0) {
        return "specify target=\"<name>\" to hook a function by name — the "
               "engine resolves the address and ABI for you. (Advanced: a "
               "raw address=, pattern=, address_id=, target_symbol=, or "
               "target_lua_cfunction= for targets the library can't name "
               "yet.)";
    }
    if (entryLocatorCount > 1) {
        return "set exactly ONE locator — normally target=\"<name>\". "
               "(Advanced locators address/pattern/address_id/target_symbol/"
               "target_lua_cfunction are mutually exclusive with each other "
               "and with target.)";
    }
    if (p.callsite.has_value()) {
        return "target_callsite is only valid with mode='callsite'";
    }
    return "";
}

// --- mid captures parsing ----------------------------------------------
//
// A capture entry is a register/memory EXPRESSION with an OPTIONAL
// `:type` suffix:  "rax"  "[rcx+0x10]:i32"  "xmm0:f32".  The expression
// is the make_jit_midfunc param_capture; the type is its param_type
// (default "i64"). Authors write these straight off a disassembler, so
// the syntax mirrors the disassembly 1:1.
//
// Splitting rule: the `:type` suffix is the LAST `:` whose tail is a
// recognized width token. A memory expr like "[rcx+0x10]" contains no
// `:`, so the common case is unambiguous; we only peel a suffix when the
// tail is a known type, leaving exprs that happen to contain a colon
// (none today, but future-proof) intact.

// Recognized capture type tokens — must match the make_jit_midfunc /
// type_info_t string matcher (i8..u64, ptr, f32/f64 and aliases). Tokens
// the JIT understands; anything else is treated as part of the expr.
bool IsKnownCaptureType(const std::string& tok) {
    static const char* kTypes[] = {
        "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64",
        "ptr", "f32", "f64", "float", "double", "bool",
    };
    for (const char* t : kTypes) if (tok == t) return true;
    return false;
}

// Split "expr:type" -> (expr, type). type defaults to "i64" when absent.
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

// Read the `captures` field into the payload's parallel capture vectors.
// Two accepted shapes (mode == mid only):
//   positional list:  captures = {"rax", "[rcx+0x10]:i32"}
//                       -> captureNames[i] = "" (callback table keyed 1..N)
//   name map:         captures = {hp = "rax", x = "[rcx+0x10]:i32"}
//                       -> captureNames[i] = "hp" (callback table keyed by name)
// A table with ANY string key is treated as the name-map form; a pure
// array (1..N integer keys) is the list form. Returns "" on success or a
// ready-to-return diagnostic. Leaves the vectors empty when `captures` is
// absent (validated as required for mid by the caller).
std::string ReadCaptures(lua_State* L, int tableIdx,
                         kcdx::hook_payload::HookPayload& p) {
    lua_getfield(L, tableIdx, "captures");
    if (lua_isnil(L, -1)) { lua_pop(L, 1); return ""; }
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return "captures must be a table (a list {\"rax\", \"[rcx+0x10]:i32\"} "
               "or a name map {hp=\"rax\", x=\"[rcx]:i32\"})";
    }
    const int capIdx = lua_gettop(L);

    // Detect form: any non-integer (string) key => name-map form.
    bool nameMap = false;
    lua_pushnil(L);
    while (lua_next(L, capIdx) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING) nameMap = true;
        lua_pop(L, 1);  // pop value, keep key for next
        if (nameMap) { lua_pop(L, 1); break; }  // pop the key; decided
    }

    if (nameMap) {
        // Iterate string keys. Lua table order over string keys is
        // unspecified, but capture slot order is irrelevant — the
        // callback addresses each handle BY NAME, not position.
        lua_pushnil(L);
        while (lua_next(L, capIdx) != 0) {
            // key at -2, value at -1
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
            lua_pop(L, 1);  // pop value, keep key
        }
    } else {
        const int n = static_cast<int>(lua_objlen(L, capIdx));
        for (int i = 1; i <= n; ++i) {
            lua_rawgeti(L, capIdx, i);
            if (!lua_isstring(L, -1)) {
                lua_pop(L, 1);
                lua_pop(L, 1);  // captures table
                return "captures list entries must be \"expr:type\" strings "
                       "(e.g. \"rax\" or \"[rcx+0x10]:i32\")";
            }
            std::string raw = lua_tostring(L, -1);
            std::string expr, type;
            SplitCapture(raw, expr, type);
            p.captureExprs.push_back(std::move(expr));
            p.captureTypes.push_back(std::move(type));
            p.captureNames.push_back("");  // positional: keyed 1..N
            lua_pop(L, 1);
        }
    }

    lua_pop(L, 1);  // pop the captures table
    return "";
}

// Build the optional callsite sub-locator from a nested
// `target_callsite = {...}` table, if present. Leaves `out` as
// nullopt when the field is absent. Returns "" on success, a
// diagnostic otherwise. Pattern parse errors surface here.
std::string ReadCallsite(lua_State* L, int tableIdx,
                         std::optional<kcdx::hook_payload::CallsiteLocator>& out) {
    lua_getfield(L, tableIdx, "target_callsite");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return "";
    }
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return "target_callsite must be a table";
    }
    const int csIdx = lua_gettop(L);
    kcdx::hook_payload::CallsiteLocator cs;
    cs.offset    = LuaTableInt(L, csIdx, "offset", 0);
    cs.addressId = LuaTableU64(L, csIdx, "address_id", 0);
    cs.rva       = LuaTableString(L, csIdx, "rva");
    const std::string csPattern = LuaTableString(L, csIdx, "pattern");
    lua_pop(L, 1);  // pop the target_callsite table

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

int Lua_Hook(lua_State* L) {
    if (!lua_istable(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "kcdx.hook: expected a single table argument");
        return 2;
    }

    // Reject an unrecognized option key BEFORE reading anything — a typo'd
    // key (signagure=, target_calsite=) would otherwise vanish silently, the
    // author's intent lost with no trace (fail-state-logging.md / AP14).
    {
        std::string bad = kcdx::lua_bind_helpers::FindUnknownKey(
            L, 1, kKnown, sizeof(kKnown) / sizeof(kKnown[0]));
        if (!bad.empty()) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.hook: unrecognized option key '%s' — not a recognized "
                "kcdx.hook option (check for a typo).",
                bad.c_str());
            return 2;
        }
    }

    auto p = std::make_shared<kcdx::hook_payload::HookPayload>();
    p->name        = LuaTableString(L, 1, "name", "lua_hook");
    p->description = LuaTableString(L, 1, "description");
    p->module      = LuaTableString(L, 1, "module", "WHGame.dll");
    p->offset      = LuaTableInt(L, 1, "offset", 0);

    // off_thread = "marshal" (default) / "skip" / "error" — per-hook
    // off-thread routing policy. Mirrors kcdxHookOffThread_* on the
    // C++ side (include/kcdx/Interfaces.h:1362-1365) so the Lua + C++
    // surfaces stay at parity (lua-api-surface.md). Absent → default 0
    // (Marshal, degraded to Skip-with-warn-once per Outcome P in v1).
    {
        lua_getfield(L, 1, "off_thread");
        if (lua_type(L, -1) == LUA_TSTRING) {
            const char* s = lua_tostring(L, -1);
            if      (std::string(s) == "marshal") p->offThread = 0;
            else if (std::string(s) == "skip")    p->offThread = 1;
            else if (std::string(s) == "error")   p->offThread = 2;
            else {
                lua_pop(L, 1);
                lua_pushnil(L);
                lua_pushfstring(L,
                    "kcdx.hook '%s': off_thread must be \"marshal\" "
                    "(default), \"skip\", or \"error\" — got \"%s\". See "
                    ".claude/rules/lua-callback-threading.md for the "
                    "per-hook off-thread routing model.",
                    p->name.c_str(), s);
                return 2;
            }
        } else if (!lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.hook '%s': off_thread must be a string "
                "(\"marshal\" / \"skip\" / \"error\")",
                p->name.c_str());
            return 2;
        }
        lua_pop(L, 1);
    }

    // Fetch the owning plugin identity NOW (not at append-time below) so
    // both namespace components are in hand for the name-resolution path
    // — the target signature lookup (ResolveSignatureByName) and, at
    // apply, ResolveLocator's ResolveByName — which take the
    // (author, plugin) pair for the self > engine > other precedence
    // (naming-namespaces.md). Either field may be "" (anonymous caller
    // → both empty; or a plugin whose [plugin].author is not yet
    // populated → author empty); pass through, never error. The same
    // callSiteFile/Line + owner are reused for the registry Entry below
    // (no second stack-walk; the struct return is the reason).
    std::string callSiteFile;
    int         callSiteLine = 0;
    kcdx::lua_registry::OwningPlugin owner =
        kcdx::lua_registry::OwningPluginForCurrentCall(
            L, callSiteFile, callSiteLine);
    p->owningAuthor = owner.author;
    p->owningPlugin = owner.plugin;

    // Entry-level 'priority' is no longer honored — cross-plugin order
    // comes from the plugin's [load_order].priority (kcdx.toml);
    // intra-plugin order is registration order in plugin.lua. Mirror
    // the kcdx.bytes once-per-session INFO so authors notice the dead
    // knob. (See restructure-plan §"Confirmed design decisions".)
    lua_getfield(L, 1, "priority");
    if (!lua_isnil(L, -1)) {
        static bool warnedOnce = false;
        if (!warnedOnce) {
            warnedOnce = true;
            log::Info("kcdx.hook: entry-level 'priority' field is no "
                      "longer honored. Cross-plugin ordering comes from "
                      "the plugin's [load_order].priority (set in "
                      "kcdx.toml). Intra-plugin ordering is the "
                      "registration order in your plugin.lua. This "
                      "warning fires once per session.");
        }
    }
    lua_pop(L, 1);

    // --- Optional `mode` string: SCOPE selector ONLY ---
    // `mode` selects the hook's SCOPE — never its behavior. The only scope
    // value today is:
    //   "callsite"  — redirect ONE call instruction (vs the default
    //                 function-entry scope, selected by omitting `mode`).
    // The BEHAVIOR (before/after/around/replace/mid) is ALWAYS attached
    // under its own key, never named in `mode`. A behavior name in `mode`
    // is a teaching error (lua-api-surface.md §"errors teach"): it points
    // the author at the behavior key, it does not just reject. We capture
    // the string here; a behavior-name `mode` is diagnosed after the
    // behavior key is known (so the message can name the actual key).
    // Absent `mode` = default function-entry scope.
    std::string modeStr;
    bool        haveModeStr = false;
    {
        lua_getfield(L, 1, "mode");
        if (lua_type(L, -1) == LUA_TSTRING) {
            modeStr = lua_tostring(L, -1);
            haveModeStr = true;
            if (modeStr == "callsite") p->callsiteScope = true;
        } else if (!lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.hook '%s': `mode` must be a string — \"callsite\" "
                "(the only scope selector today) or omitted (default: hook "
                "the function entry).",
                p->name.c_str());
            return 2;
        }
        lua_pop(L, 1);
    }

    // --- Behavior-as-key + callback ---
    // The author attaches the callback under the behavior name itself:
    //   kcdx.hook{ ..., before = function(...) ... end }
    //   kcdx.hook{ ..., mid = function(c) ... end, offset =, captures = }
    //   kcdx.hook{ ..., mode="callsite", around = function(orig, ...) end }
    // Exactly ONE of before / after / around / replace / mid must hold a
    // function (one behavior per call — chain multiple behaviors on a
    // target by making multiple kcdx.hook calls). The detected behavior
    // is stored in p->mode; the callsite SCOPE (if any) rides
    // p->callsiteScope alongside it. `mid` is NOT a valid callsite
    // behavior (a call-site redirect wraps the called function — there is
    // no mid-instruction capture at a call site).
    struct ModeKey { const char* key; kcdx::hook_payload::Mode mode; };
    static const ModeKey kModeKeys[] = {
        {"before",  kcdx::hook_payload::Mode::Before},
        {"after",   kcdx::hook_payload::Mode::After},
        {"around",  kcdx::hook_payload::Mode::Around},
        {"replace", kcdx::hook_payload::Mode::Replace},
        {"mid",     kcdx::hook_payload::Mode::Mid},
    };
    int    modeCount = 0;
    bool   haveMode  = false;
    for (const auto& mk : kModeKeys) {
        lua_getfield(L, 1, mk.key);
        const bool isFn = lua_isfunction(L, -1);
        lua_pop(L, 1);
        if (isFn) {
            ++modeCount;
            if (!haveMode) { p->mode = mk.mode; haveMode = true; }
        }
    }
    if (modeCount == 0) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook '%s': must attach exactly one callback under a "
            "behavior key (before, after, around, replace, or mid), e.g. "
            "before = function(...) ... end",
            p->name.c_str());
        return 2;
    }
    if (modeCount > 1) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook '%s': attach exactly ONE behavior per call; found "
            "multiple of before/after/around/replace/mid. Use separate "
            "kcdx.hook calls to put more than one behavior on a target.",
            p->name.c_str());
        return 2;
    }
    // mode = "callsite" wraps the called function, so it accepts only the
    // function-wrapping behaviors (before/after/around/replace) — not mid.
    if (p->callsiteScope && p->mode == kcdx::hook_payload::Mode::Mid) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook '%s': mode = \"callsite\" cannot use the `mid` "
            "behavior. A call-site redirect wraps the CALLED function; use "
            "before / after / around / replace. (For a mid-instruction "
            "capture, drop mode=\"callsite\" and hook the function "
            "directly with mid.)",
            p->name.c_str());
        return 2;
    }

    // `mode` selects SCOPE, not behavior. The only valid scope string is
    // "callsite" (handled above → callsiteScope). ANY other string is an
    // author mistake: either they echoed the behavior name into `mode`
    // (the behavior key already declares it) or they typed an unknown
    // scope. Teach the fix rather than just rejecting (lua-api-surface.md
    // §"errors teach").
    if (haveModeStr && !p->callsiteScope) {
        const char* attached = kcdx::hook_payload::ModeToken(p->mode);
        // Did they name the behavior they actually attached? Point them
        // straight at the redundant key to remove.
        if (modeStr == attached) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.hook '%s': mode selects SCOPE, not behavior. The `%s "
                "= function...` key already declares the %s behavior — "
                "remove `mode = \"%s\"`. (mode is only for scope, e.g. mode "
                "= \"callsite\" to redirect one call site.)",
                p->name.c_str(), attached, attached, modeStr.c_str());
            return 2;
        }
        // mode names a known behavior, but a DIFFERENT one than the key
        // attached — still steer them to set behavior via the key.
        if (modeStr == "before" || modeStr == "after" ||
            modeStr == "around" || modeStr == "replace" || modeStr == "mid") {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.hook '%s': mode selects SCOPE, not behavior. Set the "
                "behavior by attaching it under its own key, e.g. before = "
                "function... ; mode is only for scope (e.g. mode = "
                "\"callsite\").",
                p->name.c_str());
            return 2;
        }
        // Unknown scope string — list the only valid scope value(s).
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook '%s': mode must be \"callsite\" (the only scope "
            "selector today) or omitted (default: hook the function entry).",
            p->name.c_str());
        return 2;
    }

    // --- Function-entry locator fields ---
    // address_id accepts EITHER a human-readable Address Library name
    // (string → addressName) OR a numeric id (number → addressId). One
    // field, both forms — authors can write the readable name they see
    // in kcdx.addr.* instead of memorizing the opaque number. Dispatch on
    // the actual Lua type (LUA_TSTRING vs LUA_TNUMBER), not lua_isstring/
    // lua_isnumber which cross-coerce in Lua 5.1.
    lua_getfield(L, 1, "address_id");
    if (lua_type(L, -1) == LUA_TSTRING) {
        p->addressName = lua_tostring(L, -1);
    } else if (lua_type(L, -1) == LUA_TNUMBER) {
        p->addressId = static_cast<uint64_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);

    // `target = "<name>"` — the name-based locator that resolves BOTH the
    // address AND the verified signature from the Address Library. This is
    // the COMMON path (the disassembler test — cornerstones.md / AP12): the
    // author names the function and the engine carries its ABI, so no
    // hand-written signature is needed. Implemented as the same name slot
    // address_id="name" already feeds (p->addressName → ResolveLocator's
    // ResolveByName); the signature carry happens at the signature gate
    // below (target's entry signature fills in when no explicit signature=
    // is given). The remaining locators (address/pattern/address_id/
    // target_symbol/target_lua_cfunction) are the labeled advanced/expert
    // hatch for targets the library can't name yet — never the common path.
    std::string targetName;
    bool        haveTarget = false;
    {
        lua_getfield(L, 1, "target");
        if (lua_type(L, -1) == LUA_TSTRING) {
            targetName  = lua_tostring(L, -1);
            haveTarget  = true;
        } else if (!lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.hook '%s': `target` must be a string — the Address "
                "Library name of the function to hook (e.g. target = "
                "\"CGame_per_frame_ui_pump\"). The name resolves both the "
                "address and the verified signature.",
                p->name.c_str());
            return 2;
        }
        lua_pop(L, 1);
    }
    if (haveTarget) {
        // target shares the name slot with address_id="name". Setting both
        // a string address_id AND target would be two names for one locator
        // — reject with a clear steer rather than silently letting one win.
        if (!p->addressName.empty()) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.hook '%s': set EITHER target = \"%s\" OR address_id = "
                "\"%s\", not both — they are the same name-based locator. "
                "Prefer `target` (the common path; it also supplies the "
                "verified signature).",
                p->name.c_str(), targetName.c_str(), p->addressName.c_str());
            return 2;
        }
        p->addressName = targetName;
    }
    p->targetSymbol       = LuaTableString(L, 1, "target_symbol");
    p->targetLuaCfunction = LuaTableString(L, 1, "target_lua_cfunction");
    p->address            = LuaTableAddress(L, 1, "address");
    const std::string patternStr = LuaTableString(L, 1, "pattern");
    const std::string contextStr = LuaTableString(L, 1, "context");
    const std::string anchorStr  = LuaTableString(L, 1, "anchor_string");

    try {
        if (!patternStr.empty()) {
            p->pattern = kcdx::patch::ParsePattern(patternStr);
        }
        if (!contextStr.empty()) {
            p->context = kcdx::patch::ParsePattern(contextStr);
        }
        if (!anchorStr.empty()) {
            p->anchor = kcdx::patch::AnchorString{anchorStr};
        }
    } catch (const std::exception& ex) {
        lua_pushnil(L);
        lua_pushfstring(L, "kcdx.hook '%s': %s", p->name.c_str(), ex.what());
        return 2;
    }

    // --- Callsite sub-locator (mode == callsite) ---
    {
        std::string csErr = ReadCallsite(L, 1, p->callsite);
        if (!csErr.empty()) {
            lua_pushnil(L);
            lua_pushfstring(L, "kcdx.hook '%s': %s",
                            p->name.c_str(), csErr.c_str());
            return 2;
        }
    }

    // --- Locator exclusivity / completeness ---
    {
        std::string locErr = ValidateLocator(*p);
        if (!locErr.empty()) {
            lua_pushnil(L);
            lua_pushfstring(L, "kcdx.hook '%s': %s",
                            p->name.c_str(), locErr.c_str());
            return 2;
        }
    }

    // --- mid mode: captures, not a function signature ---
    // mid intercepts a single instruction at `offset` inside the
    // function and reads/writes named register/memory captures — it does
    // NOT know (or need) the function's signature. So mid takes `captures`
    // where the other modes take `signature`. Parse the captures now and
    // skip the signature requirement entirely. (The payload's
    // hasSignature stays false; the apply pass dispatches on mode.)
    if (p->mode == kcdx::hook_payload::Mode::Mid) {
        std::string capErr = ReadCaptures(L, 1, *p);
        if (!capErr.empty()) {
            lua_pushnil(L);
            lua_pushfstring(L, "kcdx.hook '%s': %s",
                            p->name.c_str(), capErr.c_str());
            return 2;
        }
        if (p->captureExprs.empty()) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.hook '%s': mode='mid' requires a 'captures' table — "
                "the register/memory values to read/write at `offset` "
                "(e.g. captures = {\"rax\", \"[rcx+0x10]:i32\"} or "
                "captures = {hp = \"rax\"})",
                p->name.c_str());
            return 2;
        }
        // offset is where inside the function the captured instruction
        // lives. 0 is legal (capture at the entry) but unusual; warn-free.
    } else {
    // --- Signature DSL (before/after/around/replace) ---
    // The engine needs the ABI to marshal arguments + return value to/from
    // the callback. TWO sources, in precedence order:
    //   1. An explicit `signature = "..."` the author wrote (EXPERT path /
    //      override — always WINS when present, even if target also carries
    //      one; the author may know better than the seed, or be hooking a
    //      target the library can't name yet).
    //   2. The verified signature the Address Library carries for `target`
    //      (the COMMON path — the name supplies the ABI; AP12). Used when
    //      the author gave NO explicit signature=.
    // Parse now so the apply pass never re-parses. If target resolved an
    // address but has NO verified signature AND no explicit signature= was
    // given, that's a teaching error (the engine can't know the ABI).
    const std::string sigStr = LuaTableString(L, 1, "signature");

    // The signature string actually parsed: explicit wins; else target's.
    std::string effectiveSig = sigStr;
    bool        sigFromTarget = false;
    if (effectiveSig.empty() && haveTarget) {
        // (owningAuthor, owningPlugin) drive the self > engine > other
        // precedence in ResolveSignatureByName (naming-namespaces.md):
        // the signature comes from the SAME row the address resolves to
        // (self target, else engine seed, else another plugin's target).
        // Launch-time binder pass only. The empty-author transition has
        // retired at this call site (step 4 of the 2-dot namespace
        // refactor); the real (author, plugin) comes from the
        // OwningPluginForCurrentCall struct above.
        const char* entrySig =
            kcdx::address_library::ResolveSignatureByName(
                targetName.c_str(), owner.author.c_str(),
                owner.plugin.c_str());
        if (entrySig && entrySig[0] != '\0') {
            effectiveSig  = entrySig;
            sigFromTarget = true;
        }
    } else if (!effectiveSig.empty() && haveTarget) {
        // Sig-mismatch gate (AP12/AP13): the author named a target AND
        // hand-wrote an explicit signature=. The explicit one WINS (the
        // deliberate-override case stays authoritative — effectiveSig is
        // untouched), but if the name ALSO carries a verified library ABI
        // and the two are NOT compatible, the silent trust is a footgun —
        // a wrong explicit sig mis-marshals with no diagnostic. Consult the
        // verified ABI to DETECT the conflict (not to override) and emit a
        // teaching WARN naming both signatures + that the explicit one is
        // used as-authored. Mirrors hook_interface.cpp ResolveSignature.
        const char* verifiedSig =
            kcdx::address_library::ResolveSignatureByName(
                targetName.c_str(), owner.author.c_str(),
                owner.plugin.c_str());
        if (verifiedSig && verifiedSig[0] != '\0') {
            auto explicitParse = kcdx::hook_signature::Parse(effectiveSig);
            auto verifiedParse = kcdx::hook_signature::Parse(verifiedSig);
            // Only compare when BOTH parse — a malformed explicit sig is
            // caught by the parse below; a malformed verified seed is a
            // seed bug surfaced there too. The gate catches a clean-but-
            // wrong explicit sig vs a clean verified ABI.
            //
            // Resolution is UNCHANGED either way: effectiveSig stays the
            // explicit one — the expert override is honored, the install
            // proceeds (behavior-(c)). ONLY the log severity changes, split
            // by ClassifyConflict (shared with hook_interface.cpp's gate so
            // the two surfaces cannot drift):
            //   Hard (arg-count / return-width delta) → ERROR: a frame
            //     mis-description on a live engine function is a KNOWN CRASH
            //     RISK (the cap-38 / 0xC8 case).
            //   Soft (same shape, per-slot type nuance)  → WARN as before.
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
                            "the game crashes in or after this hook, this is "
                            "the cause."));
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
            // FAIL-STATE INSTRUMENTATION (fail-state-logging.md / AP14):
            // ResolveSignatureByName returns "" for TWO distinct cases — a
            // name that resolved to an address but carries no verified ABI
            // (the genuine "supply a signature" case), AND a name that does
            // not resolve at all (a typo / unknown / un-declared target). The
            // historic message wrongly told a typo'd name to "supply a
            // signature". DISTINGUISH by asking whether the name resolves to
            // anything: a nonzero ResolveByName VA, OR a winning author target
            // (Pattern / TargetSymbol kinds resolve via FindResolvedAuthorTarget,
            // not ResolveByName — DECLARED but no VA in this leaf module).
            // Mirrors hook_interface.cpp ResolveSignature.
            const uintptr_t addr = kcdx::address_library::ResolveByName(
                targetName.c_str(), owner.author.c_str(), owner.plugin.c_str());
            const bool declared =
                addr != 0 ||
                kcdx::address_library::FindResolvedAuthorTarget(
                    targetName.c_str(), owner.author.c_str(),
                    owner.plugin.c_str()) != nullptr;
            if (!declared) {
                LOG_DEBUG_KV("HOOK", "target_not_found",
                    log::KV("target", targetName.c_str()),
                    log::KV("plugin", owner.plugin.c_str()),
                    log::KV::BareStr("detail",
                        "target name resolved to NO address and is not a "
                        "declared author target — unknown / typo'd / not "
                        "declared, NOT a known-but-no-ABI target"));
                lua_pushfstring(L,
                    "kcdx.hook '%s': target '%s' did not resolve — no engine "
                    "seed, no declared author target by that name. This is an "
                    "UNKNOWN target name (a typo, or a target you have not "
                    "declared). Check the name against kcdx.addr.* or your "
                    "declared [[target]] rows; if you meant an un-named site, "
                    "declare a [[target]] with a pattern/rva + signature=, or "
                    "supply signature= here with an explicit locator (advanced).",
                    p->name.c_str(), targetName.c_str());
                return 2;
            }
            // The target resolved a NAME (a seed entry OR an author-declared
            // target) but carries no ABI — a hook NEEDS a signature, and the
            // engine never invents one (AP2 / the disassembler test, AP12).
            // This is the folded pattern/rva-no-signature case: an author
            // target whose locator is a raw pattern or RVA can't carry an ABI
            // on its own, so its targets.toml row must add signature=. Teach
            // both fixes, don't just reject.
            LOG_DEBUG_KV("HOOK", "target_no_abi",
                log::KV("target", targetName.c_str()),
                log::KV("plugin", owner.plugin.c_str()),
                log::KV::BareStr("detail",
                    "target resolved to an address but carries no verified "
                    "ABI — the author must supply a signature (the engine "
                    "never invents one, AP2)"));
            lua_pushfstring(L,
                "kcdx.hook '%s': target '%s' resolved to an address but has "
                "no signature — a hook needs an ABI. If '%s' is your own "
                "author-declared target, add signature=\"...\" to its "
                "targets.toml row (so every plugin that hooks it by name gets "
                "the ABI for free); otherwise supply signature= here "
                "(advanced), or use a name whose ABI the engine already knows.",
                p->name.c_str(), targetName.c_str(), targetName.c_str());
        } else {
            lua_pushfstring(L,
                "kcdx.hook '%s': a 'signature' is required (e.g. "
                "\"void (ptr self, wstr szApp)\") so the engine knows the "
                "function's argument + return types — or use target = "
                "\"<name>\" to have the engine supply it.",
                p->name.c_str());
        }
        return 2;
    }
    {
        auto sr = kcdx::hook_signature::Parse(effectiveSig);
        if (!sr.ok) {
            lua_pushnil(L);
            // A target-supplied signature that fails to parse is a SEED bug
            // (a malformed signature string in the Address Library), not an
            // author mistake — say so, since the author didn't write it.
            if (sigFromTarget) {
                lua_pushfstring(L,
                    "kcdx.hook '%s': the Address Library signature for "
                    "target '%s' (\"%s\") failed to parse: %s. This is a "
                    "kcdx seed bug — report it; meanwhile you can override "
                    "with an explicit signature=.",
                    p->name.c_str(), targetName.c_str(), effectiveSig.c_str(),
                    sr.error.c_str());
            } else if (sr.errorColumn > 0) {
                lua_pushfstring(L,
                    "kcdx.hook '%s': signature parse error at column %d: %s",
                    p->name.c_str(), sr.errorColumn, sr.error.c_str());
            } else {
                lua_pushfstring(L,
                    "kcdx.hook '%s': signature parse error: %s",
                    p->name.c_str(), sr.error.c_str());
            }
            return 2;
        }
        p->signature    = std::move(sr.sig);
        p->hasSignature = true;
    }
    }

    // --- Callback closure ---
    // Take a GC-safe registry ref to the function attached under the
    // chosen mode key, so the closure survives until the apply pass.
    p->callbackRef = TakeCallbackRef(
        L, 1, kcdx::hook_payload::ModeToken(p->mode));
    if (p->callbackRef == LUA_NOREF) {
        // Shouldn't happen — mode detection above already confirmed a
        // function under this key — but guard defensively.
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook '%s': internal error: mode '%s' callback vanished "
            "between detection and ref",
            p->name.c_str(), kcdx::hook_payload::ModeToken(p->mode));
        return 2;
    }

    // --- Queue the registration ---
    kcdx::lua_registry::Entry e;
    e.kind    = kcdx::lua_registry::Kind::Hook;
    e.name    = p->name;
    e.payload = p;  // shared_ptr<HookPayload> stored as shared_ptr<void>
    // Reuse the owner + call site fetched at the top (single stack-walk);
    // the same plugin name is now also on p->owningPlugin (and the
    // author on p->owningAuthor) for the resolvers.
    e.pluginName   = owner.plugin;
    e.callSiteFile = callSiteFile;
    e.callSiteLine = callSiteLine;

    std::string err;
    uint64_t handleId = kcdx::lua_registry::Append(std::move(e), &err);
    if (handleId == 0) {
        // Append failed; the entry was not enqueued, so release the
        // callback ref we took above (no apply pass will free it).
        if (p->callbackRef != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, p->callbackRef);
            p->callbackRef = LUA_NOREF;
        }
    }
    return kcdx::lua_registry::PushHandleOrError(L, handleId, err);
}

}  // namespace

// Register the Kind::Hook deferred-apply handler. ENGINE state, not
// Lua-surface state — it makes Kind::Hook appliable regardless of which
// surface (Lua kcdx.hook or the C++ kcdxHookInterface) queued the entry.
// Called at engine init (dllmain.cpp, before DiscoverAndLoad), NOT from
// bind(): bind() runs at first-update-tick, too LATE for a C++ plugin's
// kcdxPlugin_Load (DllMain-phase) whose kcdxHookInterface thunk queues a
// Kind::Hook entry — lua_registry::Append rejects any Kind with no handler.
// ApplyHookEntry is the TU-local static above; RegisterHandlers() sees it
// from the same TU. Registers exactly once (no double-register: the call
// MOVED out of bind(), it is not duplicated there).
void RegisterHandlers() {
    kcdx::lua_registry::RegisterApplyHandler(
        kcdx::lua_registry::Kind::Hook, &ApplyHookEntry);
}

void bind(lua_State* L) {
    // Lua-surface wiring ONLY. The Kind::Hook apply handler is registered
    // earlier, at engine init, by RegisterHandlers() — by the time bind()
    // runs (first-update-tick) the handler is already in place.
    kcdx::lua_registry::EnsureHandleMetatable(L);

    lua_pushcfunction(L, Lua_Hook);
    lua_setfield(L, -2, "hook");
}

}  // namespace kcdx::lua_bind_hook
