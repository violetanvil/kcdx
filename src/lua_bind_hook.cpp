// kcdx.hook — Lua-facing function-interception registration.
//
// Part of the manifest-only restructure. This is a game-mod
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
// signature/hex is needed (the disassembler test — the name carries
// address AND ABI). ADVANCED/EXPERT locators exist for targets the library can't
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
#include "declared_targets.h"  // smart-resolver presence/kind probe (author-declared store)
#include "hook_chain.h"
#include "hook_payload.h"
#include "hook_signature.h"
#include "load_order.h"
#include "log.h"
#include "lua_bind_helpers.h"  // FindUnknownKey (shared unknown-key gate)
#include "lua_memory.h"
#include "lua_registry.h"
#include "patch_engine.h"
#include "plugin_loader.h"     // g_runtimeGameVersionString (declared-store lookup arg) + HandleOf (refdb CallerContext attribution)
#include "refdb.h"             // smart-resolver kind probe (engine seed)
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
// lightuserdata (exact VA — preferred for pointer-magnitude values, since
// LUA_NUMBER is float), or an integer VA. Returns 0 if absent
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

// --- Unknown-key rejection (fail loud, never silent-drop) ---------------
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
    // Routing branch: a payload
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
    // author's intent lost with no trace (fail loud, never silent-drop).
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
    // surfaces stay at parity (one model, two languages). Absent → default 0
    // (Marshal, degraded to Skip-with-warn-once in v1).
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
                    "(default), \"skip\", or \"error\" — got \"%s\". This "
                    "selects the per-hook off-thread routing policy.",
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
    // (self > engine > other precedence). Either field may be "" (anonymous caller
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
    // is a teaching error (errors teach, in the author's terms): it points
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
    // scope. Teach the fix rather than just rejecting (errors teach).
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
    // the COMMON path (the disassembler test — the engine carries both): the
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
    //      (the COMMON path — the name supplies the ABI). Used when
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
        // precedence in ResolveSignatureByName (self > engine > other):
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
        // Sig-mismatch gate (the name should carry the ABI; an explicit
        // override is detected, not silently accepted): the author named a target AND
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
            // FAIL-STATE INSTRUMENTATION (fail loud, never silently drop):
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
            // engine never invents one (the disassembler test).
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
                    "never invents one)"));
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

// =============================================================================
// Smart-resolver surface — kcdx.hook.<name>.<mode>(callback)
// =============================================================================
//
// kcdx.hook is registered as a TABLE with two metamethods:
//   __call  → forwards to the flat-table form (Lua_Hook) so the legacy
//             kcdx.hook{ target = "...", before = fn } shape keeps working
//             unchanged.
//   __index → smart-resolver: kcdx.hook.<name> probes the unified named-
//             target table (declared store + engine seed + cross-plugin
//             legacy author targets). Miss → returns nil (so .<mode>
//             throws "attempt to index a nil value" naming the typoed
//             slot). Hit → returns a verb-bound userdata that carries
//             (resolved name, owner identity, kind) baked in via a
//             metatable on which:
//                __index resolves the mode (.before/.after/.replace/
//                        .around/.mid) against the per-kind valid-mode
//                        set. Invalid mode for kind → returns nil (so
//                        the call throws "attempt to call a nil value").
//                        Valid mode → returns a closure carrying
//                        (name, mode, author, plugin) as upvalues.
//                __call  is rejected with a teaching error — "specify a
//                        mode like .before(fn)".
//
// The mode closure synthesizes a flat-table form on the Lua stack
// ({ target = "<name>", <mode> = <callback> }) and dispatches to the
// existing Lua_Hook C function via lua_call. The install codepath is
// thus identical to the flat-table form's — same signature resolution,
// same locator validation, same off_thread parsing, same conflict
// engine, same handle return. The smart resolver is a thin
// re-projection that pre-resolves "this name exists" and "this mode is
// valid for this kind" up-front, so the author gets fail-fast errors
// at .name / .mode access instead of generic install-time rejections.
//
// Out of scope here (kept on the flat-table form): mode="callsite",
// raw address= / pattern= / address_id= / target_symbol= /
// target_lua_cfunction= locators, captures= for mid mode. Those paths
// have no name to drive the resolver and stay on the explicit table
// form.

// (We are still inside the file-wide anonymous namespace opened above —
// no second `namespace {` here; the helpers below share the same TU-
// local linkage as Lua_Hook / ApplyHookEntry / the LuaTable* helpers.)

// Per-verb userdata payload. Carries the resolved name, the owner the
// closure must report as ITS owner when it dispatches install (the
// closure runs at .mode(callback) time, on the SAME call stack as the
// original kcdx.hook.<name>.<mode>(callback), so the owner is the same
// — but we capture it at __index time anyway so the closure does not
// re-walk the stack), and the kind string the kcdx.hook valid-mode
// table is indexed by. Plain POD; no std::string members (lua_newuserdata
// is bit-blittable storage with no destructor — placement-new + an __gc
// metamethod would be required for non-trivial members and there is no
// long-lived state to clean). The strings live on the userdata's
// envtable (set by Lua_HookIndex via lua_setfenv); the C struct only
// carries the lengths for cheap recovery, but actually the envtable
// alone is enough — keep the userdata size at one byte (the metatable
// identity is the lookup key).
//
// One per resolved kcdx.hook.<name> access. Discarded by the GC after
// the .mode call (the closure's upvalues are what survives).
struct ResolvedHookUd {
    char unused;
};

// The metatable's registry key for the verb-bound userdata. Identifies
// the userdata's type when the .mode __index metamethod walks the
// argument list.
constexpr const char* kHookResolvedMt = "kcdx.hook.resolved";

// Valid hook modes per resolved kind. Built by READING the existing
// Lua_Hook parser's kModeKeys[] set — that array is the single source
// of truth for the modes the flat-table form accepts; the smart
// resolver does not invent a parallel list. For kind == "function" the
// full set applies (the parser accepts all 5). Every other kind in the
// schema (vtable_index, callsite, data_slot, value, …) has NO valid
// hook mode today — the existing engine has no install path that turns
// a non-function name into a hook (a vtable hook engine, a data-slot
// watcher, etc. are tracked phases that have not landed). Returning
// false for every mode on those kinds is the correct kind-aware
// filtering: the smart resolver fails fast with "attempt to call a nil
// value" at the .mode slot, instead of pretending an install would
// succeed.
//
// When a future engine surface adds (e.g.) a vtable-index hook path,
// extending this gate by adding "vtable_index" → { "replace" } here +
// a corresponding install branch in Lua_Hook keeps the smart resolver
// surface aligned with the install path — same source-of-truth pattern
// the parser's kModeKeys[] follows today.
bool IsValidHookModeForKind(const std::string& kind, const std::string& mode) {
    if (kind == "function" || kind.empty()) {
        // kind.empty() covers legacy author targets (which carry no kind
        // metadata) and the declared-store default — both are treated as
        // function-shaped, matching how the flat-table form installs them.
        return (mode == "before"  || mode == "after"  ||
                mode == "around"  || mode == "replace" ||
                mode == "mid");
    }
    return false;
}

// Look up the resolved name's KIND. Probes the same population sources
// the install path reaches: the engine seed (refdb), then the author-
// declared store (with the calling plugin's namespace). Returns "" when
// no source carries a kind — the legacy author-target store is the
// catch-all, treated as "function" by IsValidHookModeForKind via the
// kind.empty() branch.
//
// NOT a presence probe — call this only after Probe* established the
// name resolves. The refdb branch fires its deduped SUPERSEDED/
// DEPRECATED/UNVERIFIED warn for the name. Refdb dedups per
// (pluginHandle, callType, name); attributing the ctx to the calling
// plugin + callType="kcdx.hook" collapses every kcdx.hook.<name>
// access by THIS plugin into one warn for the session, instead of
// one per __index keystroke. (The install pass itself doesn't reach
// this warn path — Lua_Hook resolves through address_library, which
// hits refdb via the single-arg ResolveAddrByName / SignatureByName
// helpers that don't emit the deduped warn — so the dedup defended
// here is across repeated smart-resolver accesses, not across
// access-then-install.) A bare engine-internal ctx would key the
// warn at a different slot than any plugin-attributed call ever
// reaches, double-firing on every keystroke against a state-flagged
// entity.
std::string KindForResolvedName(const std::string& name,
                                const std::string& author,
                                const std::string& plugin) {
    // 1. Engine seed via refdb (the curated cache built at refdb::Open).
    //    HasName is a pure presence check (no warn); ResolveByName fires
    //    the deduped state warn. We only ResolveByName when HasName hits,
    //    so a name that is solely in the declared store doesn't trigger a
    //    spurious refdb warn.
    if (kcdx::refdb::HasName(name)) {
        // Plugin-attributed ctx — see the dedup paragraph above. An
        // anonymous caller (empty plugin name) maps via HandleOf to
        // kcdxInvalidPluginHandle; refdb's dedup key accepts that as-is
        // and dedups the same way every other (handle, callType, name)
        // key does — no special-case logic.
        kcdx::refdb::CallerContext ctx;
        ctx.pluginHandle = ::kcdx::plugins::HandleOf(plugin.c_str());
        ctx.callType     = "kcdx.hook";
        kcdx::refdb::NameResolution r =
            kcdx::refdb::ResolveByName(name, ctx);
        if (r.found && !r.kind.empty()) return r.kind;
        return "function";  // engine seed with no kind column → function shape
    }
    // 2. Declared store (self tier — the only declared-store path the
    //    bare smart resolver consumes; a cross-plugin declared reference
    //    arrives as the 3-segment explicit form and would not reach the
    //    bare __index branch).
    if (!plugin.empty()) {
        kcdx::declared_targets::ResolvedDeclared d =
            kcdx::declared_targets::LookupForCaller(
                author, plugin, name,
                ::kcdx::plugins::g_runtimeGameVersionString);
        if (d.entry) {
            // Pattern: kindTag the author authored (default "function"
            // per declared_targets validation). Value: the entry has no
            // address and no hook-mode shape — return "value" so the
            // valid-mode gate rejects every hook mode.
            if (d.kind == kcdx::declared_targets::ResolvedDeclared::Kind::Value) {
                return "value";
            }
            if (!d.kindTag.empty()) return d.kindTag;
            return "function";
        }
    }
    // 3. Legacy author-target store — no kind metadata. Treated as
    //    "function" by the empty-string branch in IsValidHookModeForKind.
    return "";
}

// Probe whether `name` resolves to ANY population source the install
// path would consult. Owner identity feeds the self > engine > other
// precedence in the leaf-module resolver (same path the flat-table
// form's install reaches). Returns true when the name has a tier;
// false on a genuine miss (typo / un-declared name).
//
// The resolver may fire the once-per-session bare-collision warn —
// that is fine: the install path would fire the same warn at the same
// dedup key. Firing it at smart-resolver __index time (vs install
// time) makes it visible earlier, never duplicates.
bool ResolveProbe(const std::string& name,
                  const std::string& author,
                  const std::string& plugin) {
    // VA-bearing population (engine seed via refdb, declared store
    // Pattern with successful scan, Rva/AddressId legacy author target).
    if (kcdx::address_library::ResolveByName(
            name.c_str(), author.c_str(), plugin.c_str()) != 0) {
        return true;
    }
    // Non-VA author-target kinds (Pattern / TargetSymbol — the leaf
    // module can't turn them into a VA; the install path routes them
    // through hook_chain::ResolveLocator). The smart resolver counts
    // these as resolved — install will follow the same fallback.
    if (kcdx::address_library::FindResolvedAuthorTarget(
            name.c_str(), author.c_str(), plugin.c_str()) != nullptr) {
        return true;
    }
    // Declared-store entries that don't resolve to a VA in this leaf
    // (VersionMismatch — entry exists but no row matches the running
    // version; Value — no address). The namespace is occupied even
    // when no VA comes back, so the smart resolver surfaces this as a
    // hit and the install path emits the version-mismatch / value-vs-
    // hook teaching error from there.
    if (!plugin.empty()) {
        kcdx::declared_targets::ResolvedDeclared d =
            kcdx::declared_targets::LookupForCaller(
                author, plugin, name,
                ::kcdx::plugins::g_runtimeGameVersionString);
        if (d.kind != kcdx::declared_targets::ResolvedDeclared::Kind::NoEntry) {
            return true;
        }
    }
    return false;
}

// Forward decls — the metatable wiring is mutually recursive via the
// upvalues stamped on the closure.
int Lua_HookCall(lua_State* L);
int Lua_HookIndex(lua_State* L);
int Lua_HookResolvedIndex(lua_State* L);
int Lua_HookResolvedCall(lua_State* L);
int Lua_HookModeInstall(lua_State* L);

// The kcdx.hook table's __call metamethod. The metamethod receives the
// table as arg 1 and the author's positional args after. The flat-table
// form Lua_Hook expects its option table at arg 1, so we forward arg 2
// (the author's options table) AND every other arg the author passed,
// preserving the (nil, err) shape on parse failure.
//
//   kcdx.hook{ target = "...", before = fn }
//     → __call(<kcdx.hook table>, <options table>)
//     → forward <options table> as arg 1 of Lua_Hook.
int Lua_HookCall(lua_State* L) {
    // Forward args 2..N to Lua_Hook. (arg 1 is the kcdx.hook table
    // itself, supplied by Lua's __call dispatcher — not user-visible.)
    const int n = lua_gettop(L);
    lua_pushcfunction(L, Lua_Hook);
    for (int i = 2; i <= n; ++i) lua_pushvalue(L, i);
    // Lua_Hook returns either 1 (handle) or 2 (nil, err) — request
    // LUA_MULTRET and propagate exactly what it returned.
    lua_call(L, n - 1, LUA_MULTRET);
    return lua_gettop(L) - n;
}

// The kcdx.hook table's __index metamethod. arg 1 = the kcdx.hook
// table; arg 2 = the key the author accessed (the name they want to
// hook).
//
// Non-string keys (an author who wrote kcdx.hook[1] or similar) are
// passed through as a miss — return nil, so the next access errors
// loud rather than the metamethod itself doing something surprising.
int Lua_HookIndex(lua_State* L) {
    if (lua_type(L, 2) != LUA_TSTRING) {
        lua_pushnil(L);
        return 1;
    }
    const char* nameCStr = lua_tostring(L, 2);
    std::string name = nameCStr ? nameCStr : "";
    if (name.empty()) { lua_pushnil(L); return 1; }

    // Owner of the calling plugin — drives self > engine > other in
    // the unified resolver, AND is captured into the mode closure's
    // upvalues so the install dispatch at .mode(cb) time uses the
    // SAME (author, plugin) the __index probe used. Capturing at
    // __index means the closure is owner-pure with respect to the
    // call site that produced it (the install can never accidentally
    // mis-attribute when the same closure is moved across owners,
    // because the upvalues hold the original).
    std::string callSiteFile;
    int         callSiteLine = 0;
    kcdx::lua_registry::OwningPlugin owner =
        kcdx::lua_registry::OwningPluginForCurrentCall(
            L, callSiteFile, callSiteLine);

    if (!ResolveProbe(name, owner.author, owner.plugin)) {
        // The typo-fails-fast gate: the name doesn't resolve in any
        // population source. Return nil so the next access (e.g.
        // .before) raises Lua's "attempt to index a nil value"
        // naming this slot — the author sees WHICH name they typed
        // wrong without an opaque install-time message later.
        lua_pushnil(L);
        return 1;
    }

    // Kind probe — drives the .mode validity gate at the next level.
    // The legacy author-target case returns "" and is treated as
    // function-shaped by IsValidHookModeForKind.
    std::string kind = KindForResolvedName(name, owner.author, owner.plugin);

    // Allocate the verb-bound userdata. The payload is a one-byte
    // marker; the resolved facts (name, owner, kind) live on the
    // userdata's envtable (one Lua table per userdata, freed by GC
    // when the userdata becomes unreachable). The envtable carries
    // string fields cheaply — no placement-new / __gc dance for
    // std::string members on the C struct.
    auto* ud = static_cast<ResolvedHookUd*>(
        lua_newuserdata(L, sizeof(ResolvedHookUd)));
    ud->unused = 0;
    luaL_getmetatable(L, kHookResolvedMt);
    lua_setmetatable(L, -2);
    // envtable: { name, author, plugin, kind }
    lua_newtable(L);
    lua_pushstring(L, name.c_str());        lua_setfield(L, -2, "name");
    lua_pushstring(L, owner.author.c_str()); lua_setfield(L, -2, "author");
    lua_pushstring(L, owner.plugin.c_str()); lua_setfield(L, -2, "plugin");
    lua_pushstring(L, kind.c_str());         lua_setfield(L, -2, "kind");
    lua_setfenv(L, -2);
    return 1;
}

// The verb-bound userdata's __index — resolves the mode against the
// per-kind valid-mode table. arg 1 = the userdata; arg 2 = the mode
// name (the author wrote .before / .after / etc.).
//
// Returns nil for an invalid mode for this kind (so the next call
// raises "attempt to call a nil value"), or a closure for a valid
// one. Non-string keys (someone writing ud[1]) miss as nil.
int Lua_HookResolvedIndex(lua_State* L) {
    if (lua_type(L, 2) != LUA_TSTRING) {
        lua_pushnil(L);
        return 1;
    }
    const char* modeCStr = lua_tostring(L, 2);
    std::string mode = modeCStr ? modeCStr : "";

    // Recover (name, author, plugin, kind) from the userdata's envtable.
    lua_getfenv(L, 1);
    lua_getfield(L, -1, "kind");
    std::string kind = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
    lua_pop(L, 1);

    if (!IsValidHookModeForKind(kind, mode)) {
        // Kind-aware structural filter. Returning nil here is what
        // makes kcdx.hook.<vtable-only-name>.before(fn) fail at the
        // .before access with "attempt to call a nil value" instead
        // of producing a misleading install error later.
        lua_pop(L, 1);  // envtable
        lua_pushnil(L);
        return 1;
    }

    // Build the install closure. Upvalues:
    //   1: name (string)
    //   2: mode (string — matches one of kModeKeys')
    //   3: author (string — may be "")
    //   4: plugin (string — may be "")
    lua_getfield(L, -1, "name");   const std::string name   = lua_isstring(L, -1) ? lua_tostring(L, -1) : ""; lua_pop(L, 1);
    lua_getfield(L, -1, "author"); const std::string author = lua_isstring(L, -1) ? lua_tostring(L, -1) : ""; lua_pop(L, 1);
    lua_getfield(L, -1, "plugin"); const std::string plugin = lua_isstring(L, -1) ? lua_tostring(L, -1) : ""; lua_pop(L, 1);
    lua_pop(L, 1);  // envtable

    lua_pushstring(L, name.c_str());
    lua_pushstring(L, mode.c_str());
    lua_pushstring(L, author.c_str());
    lua_pushstring(L, plugin.c_str());
    lua_pushcclosure(L, Lua_HookModeInstall, 4);
    return 1;
}

// The verb-bound userdata's __call metamethod. Reached when the author
// calls the userdata WITHOUT going through a mode — kcdx.hook.IsInCombat(cb).
// For a multi-mode verb this is always an author error; surface the
// mode names so the fix is obvious.
int Lua_HookResolvedCall(lua_State* L) {
    lua_getfenv(L, 1);
    lua_getfield(L, -1, "name");
    std::string name = lua_isstring(L, -1) ? lua_tostring(L, -1) : "?";
    lua_pop(L, 2);
    lua_pushnil(L);
    lua_pushfstring(L,
        "kcdx.hook.%s(...) is not valid — specify a mode like "
        ".before(fn) / .after(fn) / .around(fn) / .replace(fn) / .mid(fn). "
        "(Or use the flat-table form: kcdx.hook{ target = \"%s\", before "
        "= fn }.)",
        name.c_str(), name.c_str());
    return 2;
}

// The closure returned by Lua_HookResolvedIndex. Upvalues 1..4 carry
// (name, mode, author, plugin); the author's call arg(s) carry the
// callback and any optional knobs.
//
// Builder-B dispatch: synthesize the flat-table form { target =
// <name>, <mode> = <callback>, ... } and call Lua_Hook through Lua's
// own call mechanism. Lua_Hook returns either 1 value (handle) or 2
// (nil, err); LUA_MULTRET propagates exactly what it returned.
//
// Author call shapes accepted:
//   .mode(callback)                  — bare callback, no extra options.
//   .mode(callback, { off_thread = ... })
//                                    — callback + optional knob table;
//                                      the knob table is shallow-merged
//                                      into the synthesized table after
//                                      the callback is set, so an
//                                      author-supplied target= /
//                                      <mode>= would override and is
//                                      rejected before merge.
//
// `captures = ...` on the knob table is intentionally NOT carried —
// the smart resolver routes hook modes only (mid uses captures, and
// IsValidHookModeForKind rejects mid for any non-function kind today;
// for function kind mid IS valid, and the knob table is the right
// home for captures — pass it through and let Lua_Hook's existing
// captures parser handle it).
int Lua_HookModeInstall(lua_State* L) {
    const char* nameCStr   = lua_tostring(L, lua_upvalueindex(1));
    const char* modeCStr   = lua_tostring(L, lua_upvalueindex(2));
    // author/plugin upvalues are reserved for future use (e.g. an
    // attribution stamp on the synthesized table if a divergence ever
    // matters); the install path re-walks the stack via
    // OwningPluginForCurrentCall, which returns the SAME owner this
    // closure was minted under because the closure runs on the same
    // call stack as the original kcdx.hook.<name>.<mode>(cb) call.
    (void)lua_upvalueindex(3);
    (void)lua_upvalueindex(4);
    std::string name = nameCStr ? nameCStr : "";
    std::string mode = modeCStr ? modeCStr : "";

    if (lua_gettop(L) < 1) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook.%s.%s(): missing required callback argument — "
            "pass a function (e.g. kcdx.hook.%s.%s(function(...) end)).",
            name.c_str(), mode.c_str(), name.c_str(), mode.c_str());
        return 2;
    }
    if (!lua_isfunction(L, 1)) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook.%s.%s(callback, [opts]): the first argument must "
            "be a function — got %s.",
            name.c_str(), mode.c_str(), luaL_typename(L, 1));
        return 2;
    }
    // Optional knob table. Reject a knob table that re-supplies target /
    // the chosen mode / a conflicting mode key — those are owned by the
    // closure, not the author.
    bool haveOpts = (lua_gettop(L) >= 2) && lua_istable(L, 2);
    if (haveOpts) {
        static const char* const kForbidden[] = {
            "target", "address", "address_id", "pattern",
            "target_symbol", "target_lua_cfunction",
            "before", "after", "around", "replace", "mid",
        };
        for (const char* fk : kForbidden) {
            lua_getfield(L, 2, fk);
            const bool present = !lua_isnil(L, -1);
            lua_pop(L, 1);
            if (present) {
                lua_pushnil(L);
                lua_pushfstring(L,
                    "kcdx.hook.%s.%s(callback, opts): the opts table "
                    "cannot supply '%s' — the locator and mode are "
                    "fixed by the kcdx.hook.<name>.<mode> form. Drop "
                    "the '%s' key, or switch to the flat-table form "
                    "(kcdx.hook{...}) if you need to override.",
                    name.c_str(), mode.c_str(), fk, fk);
                return 2;
            }
        }
    }

    // Synthesize the flat-table form: { target = name, <mode> = cb, ...opts }.
    lua_pushcfunction(L, Lua_Hook);
    lua_newtable(L);
    const int synthIdx = lua_gettop(L);
    lua_pushstring(L, name.c_str());
    lua_setfield(L, synthIdx, "target");
    lua_pushvalue(L, 1);                       // callback
    lua_setfield(L, synthIdx, mode.c_str());

    // Shallow-merge the optional knob table — every other key the
    // author put there flows into the synthesized table, so things
    // like off_thread / name / description / captures / signature
    // (for mid) ride through to Lua_Hook unchanged.
    if (haveOpts) {
        lua_pushnil(L);
        while (lua_next(L, 2) != 0) {
            // key at -2, value at -1.
            // Copy (key, value) onto the top, then assign into synthIdx.
            lua_pushvalue(L, -2);   // key
            lua_pushvalue(L, -2);   // value
            lua_settable(L, synthIdx);
            lua_pop(L, 1);          // pop value; keep key for next lua_next
        }
    }

    // Dispatch through Lua's own call mechanism so Lua_Hook's lua_*
    // return shape is preserved verbatim. Stack: [..., Lua_Hook, table].
    lua_call(L, 1, LUA_MULTRET);
    return lua_gettop(L) - (haveOpts ? 2 : 1);  // discount the args the author passed
}

// Install the per-verb metatables in LUA_REGISTRYINDEX. Idempotent
// (luaL_newmetatable is a no-op when the name is already registered).
void EnsureSmartResolverMetatables(lua_State* L) {
    // kcdx.hook table metatable — __call (forwards to flat-table form)
    // and __index (smart resolver).
    if (luaL_newmetatable(L, "kcdx.hook.verb") != 0) {
        lua_pushcfunction(L, Lua_HookCall);
        lua_setfield(L, -2, "__call");
        lua_pushcfunction(L, Lua_HookIndex);
        lua_setfield(L, -2, "__index");
        lua_pushstring(L, "kcdx.hook.verb");
        lua_setfield(L, -2, "__metatable");  // hide from pak Lua
    }
    lua_pop(L, 1);

    // Resolved-userdata metatable — __index (per-kind mode resolver)
    // and __call (multi-mode-verb misuse error).
    if (luaL_newmetatable(L, kHookResolvedMt) != 0) {
        lua_pushcfunction(L, Lua_HookResolvedIndex);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, Lua_HookResolvedCall);
        lua_setfield(L, -2, "__call");
        lua_pushstring(L, kHookResolvedMt);
        lua_setfield(L, -2, "__metatable");
    }
    lua_pop(L, 1);
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
    // Lua-surface wiring. The Kind::Hook apply handler is registered
    // earlier, at engine init, by RegisterHandlers() — by the time bind()
    // runs (first-update-tick) the handler is already in place.
    kcdx::lua_registry::EnsureHandleMetatable(L);
    EnsureSmartResolverMetatables(L);

    // kcdx.hook is a TABLE with two metamethods:
    //   __call  → forwards to the flat-table form (kcdx.hook{...}).
    //   __index → smart resolver (kcdx.hook.<name>.<mode>(cb)).
    // Both shapes coexist; existing test plugins that call kcdx.hook{...}
    // route through __call and reach the same Lua_Hook parser unchanged.
    int kcdx_idx = lua_gettop(L);
    lua_newtable(L);
    luaL_getmetatable(L, "kcdx.hook.verb");
    lua_setmetatable(L, -2);
    lua_setfield(L, kcdx_idx, "hook");
}

}  // namespace kcdx::lua_bind_hook
