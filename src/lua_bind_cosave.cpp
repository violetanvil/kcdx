// kcdx.cosave.* — Lua-side save/load persistence.
//
// A GROUPED capability DOMAIN per .claude/rules/lua-api-surface.md
// (kcdx.cosave.*, like kcdx.log.* / kcdx.console.*, NOT a top-level
// verb). A thin Lua binder over the EXISTING, proven engine cosave
// path (kcdxSerializationInterface, Version 2 — the C++ CAP-12 plugin
// drives the same interface): the engine cosave code
// (src/serialization.{h,cpp}) is NOT touched; this brings the Lua
// surface to parity with the already-shipped C++ mirror. Lua values are
// serialized by the standalone codec (src/lua_cosave_serial.{h,cpp});
// neither the interface nor the serializer is modified here.
//
//   -- the save body runs INSIDE the engine's open writer window
//   kcdx.cosave.on_save(function()
//       kcdx.cosave.write("counter", 1, my_counter)
//       kcdx.cosave.write("state",   1, { hp = 100, name = "Henry" })
//   end)
//   kcdx.cosave.on_load(function()
//       for tag, ver, val in kcdx.cosave.records() do
//           if tag == "counter" then my_counter = val end
//       end
//   end)
//   -- expert override (rare): pin a UID to match an already-shipped save format
//   kcdx.cosave.set_uid(0xC0FFEE01)
//
// Methods return true (or the iterator) on success, (nil, teaching
// error) on a bad call (the standard kcdx-binder error idiom).
//
// ===========================================================================
// DESIGN LOCKS (three prior user decisions; build exactly this)
// ===========================================================================
//
// WHY on_save/on_load AND NOT kcdx.on('save_game') (locked Decision 1):
//   The engine's writer window (g_currentWriter — where OpenRecordNamed /
//   WriteRecordData work) is live ONLY inside serialization::RunSaveCallbacks
//   (serialization.cpp:303), which runs synchronously in
//   serialization::OnEngineMessage from messaging::FireEngineMessage
//   (messaging.cpp:243) and FLUSHES TO DISK (BuildAndWriteCosave) BEFORE the
//   kcdx.on lifecycle bridge fires (messaging.cpp:267). So a write() driven
//   from a kcdx.on('save_game') handler would hit a CLOSED window and persist
//   nothing. The fix: this binder registers its OWN C SaveCallback /
//   LoadCallback via SetSaveCallback / SetLoadCallback; the author's Lua
//   on_save / on_load body runs from INSIDE that C trampoline, where
//   g_currentWriter (resp. the reader cursor) is open. This mirrors CAP-12
//   exactly (SetSaveCallback -> OpenRecord inside it) and what the value
//   serializer's own header documents ("Serialize from a SaveCallback,
//   Deserialize from a LoadCallback").
//
// AUTO-UID from the stable plugin name (locked Decision, the headline UX —
// the disassembler test: the engine carries identity from the NAME, the
// author hand-packs no FourCC):
//   Common path = NO set_uid. Resolve the calling plugin via
//   lua_registry::OwningPluginForCurrentCall -> plugins::HandleOf (the
//   mechanism kcdx.command/publish/on/hook use), hash the plugin NAME with
//   the SAME single-sourced kcdx::serialization::HashTag from step 1 (FNV-1a
//   32-bit; reused so a Lua plugin and a C++ plugin that pick the same name
//   land in the same section), and SetUniqueID(handle, hash). serialization
//   gates persistence on uid != 0 (serialization.cpp:318 — a 0 uid silently
//   drops the section), so a 0 hash is mapped to a fixed non-zero sentinel
//   (kZeroHashUid). The uid is applied at on_save/on_load registration AND at
//   first write() and at set_uid — whatever path the author touches first,
//   the uid is non-zero before any OpenRecordNamed (see EnsureUid).
//
// set_uid(uid) — the EXPERT OVERRIDE (advanced; "pin a UID only to match an
// already-shipped save format"): validate a positive integer, reject 0 with a
// teaching error, SetUniqueID, and mark the plugin uid-explicitly-set so the
// auto-derive never overrides it.
//
// tag is a STRING (locked Decision 2): write() takes a human-readable string
// tag and goes through OpenRecordNamed (the step-1 named thunk — hashes +
// collision-detects + stores the string for the read side).
//
// ===========================================================================
// Threading (AP6): RunSaveCallbacks / RunLoadCallbacks fire on the MAIN
// THREAD. They run synchronously inside serialization::OnEngineMessage, which
// messaging::FireEngineMessage invokes inline on the calling thread; the
// SaveGame / PostLoadGame messages are fired from the main-thread save/load
// path (the interface header documents the callbacks as main-thread —
// Interfaces.h:1146). So lua_pcall'ing the stored Lua fn from inside the
// trampoline races nothing on the single shared VM. The trampoline is
// pcall-isolated: a throwing on_save/on_load logs loud (structured) and does
// NOT propagate out into the engine's save/load frame.
//
// Lua bridge (lua-bridge.md, AP5): the on_save/on_load Lua fns are stored as
// luaL_refs into LUA_REGISTRYINDEX; the per-plugin {saveRef, loadRef, uid,
// uidExplicit} mapping is an engine-side C++ std::unordered_map keyed by
// kcdxPluginHandle (NOT a Lua slot / sentinel). records() is a plain
// lua_pushcclosure C closure (NO static-const sentinel). PROBE Q stays zero.

#include "lua_bind_cosave.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "log.h"
#include "lua_cosave_serial.h"  // Serialize / Deserialize for record values
#include "lua_registry.h"       // OwningPluginForCurrentCall
#include "plugin_loader.h"      // plugins::HandleOf
#include "scripting.h"          // scripting::lua_state() — the live VM the
                                // main-thread trampoline pcalls against
#include "serialization.h"      // GetInterface(), HashTag()

namespace kcdx::lua_bind_cosave {

namespace {

// Per-plugin cosave state. Engine-side C++ (NOT a Lua slot / sentinel —
// lua-bridge.md, AP5), keyed by kcdxPluginHandle. The single save/load C
// trampoline identifies WHICH plugin fired from its handle arg and looks up
// the matching Lua ref here. SetSaveCallback/SetLoadCallback take the handle,
// so the trampoline's argument is authoritative.
struct CosaveState {
    int      saveRef     = LUA_NOREF;   // luaL_ref to the on_save Lua fn
    int      loadRef     = LUA_NOREF;   // luaL_ref to the on_load Lua fn
    bool     saveCbWired = false;       // C SaveCallback registered once
    bool     loadCbWired = false;       // C LoadCallback registered once
    uint32_t uid         = 0;           // 0 = not yet derived/applied
    bool     uidExplicit = false;       // set_uid() pinned it; don't auto-derive
};

std::mutex g_mu;
std::unordered_map<kcdxPluginHandle, CosaveState> g_state;

// A non-zero stand-in for the rare case HashTag(name) == 0 — serialization
// gates persistence on uid != 0 (serialization.cpp:318), so a 0 uid would
// silently drop the plugin's whole section. FNV-1a almost never yields 0, but
// the guard makes "auto-UID is always non-zero" a hard guarantee, not a hope.
constexpr uint32_t kZeroHashUid = 1u;

// Resolve the calling plugin's NAME -> handle (the kcdx.command/publish/on
// mechanism). callSiteFile/Line are filled for the teaching error / log.
kcdxPluginHandle ResolveOwner(lua_State* L, std::string& owner,
                              std::string& callSiteFile, int& callSiteLine) {
    // Cosave identifies its owner by [plugin].name only — UIDs are
    // derived from the plugin name's FNV-1a hash. The author component
    // is not consulted here.
    owner = kcdx::lua_registry::OwningPluginForCurrentCall(
        L, callSiteFile, callSiteLine).plugin;
    return kcdx::plugins::HandleOf(owner.empty() ? "" : owner.c_str());
}

// Derive + apply a non-zero UID from the plugin NAME if this plugin doesn't
// already have one (and hasn't pinned one via set_uid). Called from every
// path that touches cosave for a plugin (on_save / on_load registration AND
// the first write), so the uid is non-zero before any OpenRecordNamed —
// whichever path the author reaches first wins, and re-deriving the same name
// is idempotent. Caller holds g_mu. Returns the live uid.
uint32_t EnsureUidLocked(CosaveState& st, kcdxPluginHandle handle,
                         const std::string& owner) {
    if (st.uid != 0) return st.uid;  // already derived or pinned

    uint32_t hash = kcdx::serialization::HashTag(
        owner.empty() ? "" : owner.c_str());
    if (hash == 0) hash = kZeroHashUid;  // never persist under uid 0
    st.uid = hash;

    const kcdxSerializationInterface* ser = kcdx::serialization::GetInterface();
    if (ser) ser->SetUniqueID(handle, st.uid);

    log::InfoF("kcdx.cosave: auto-derived uid=0x%08X for plugin='%s' "
               "(from plugin name; no set_uid)",
               st.uid, owner.empty() ? "<anon>" : owner.c_str());
    return st.uid;
}

// ---------------------------------------------------------------------------
// Save/load C trampolines — ONE each (not per-plugin C functions). The handle
// arg identifies the plugin; look up its saveRef / loadRef here. Registered
// once per plugin via SetSaveCallback / SetLoadCallback. Runs INSIDE
// RunSaveCallbacks / RunLoadCallbacks (window open, main thread). pcall-
// isolated: a throwing Lua body logs loud and does NOT escape into the
// engine's save/load frame.
// ---------------------------------------------------------------------------

void RunRefIsolated(kcdxPluginHandle plugin, int ref, const char* which) {
    if (ref == LUA_NOREF || ref == LUA_REFNIL) return;  // nothing registered

    lua_State* L = kcdx::scripting::lua_state();
    if (!L) {
        log::ErrorF("[kcdx.cosave] %s trampoline fired for plugin handle=%u "
                    "but no live lua_State; dropping the callback",
                    which, plugin);
        return;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        log::ErrorF("[kcdx.cosave] %s callback ref for plugin handle=%u is not "
                    "a function (internal inconsistency)",
                    which, plugin);
        return;
    }

    // pcall-isolated: the body runs in-window; a throw logs loud and does NOT
    // propagate out into the engine's save/load frame.
    int status = lua_pcall(L, /*nargs=*/0, /*nresults=*/0, /*errfunc=*/0);
    if (status != 0) {
        const char* msg = lua_tostring(L, -1);
        log::ErrorF("[kcdx.cosave] %s body for plugin handle=%u threw: %s",
                    which, plugin, msg ? msg : "(no message)");
        lua_pop(L, 1);
    }
}

void SaveTrampoline(kcdxPluginHandle plugin) {
    int ref = LUA_NOREF;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        auto it = g_state.find(plugin);
        if (it != g_state.end()) ref = it->second.saveRef;
    }
    RunRefIsolated(plugin, ref, "on_save");
}

void LoadTrampoline(kcdxPluginHandle plugin) {
    int ref = LUA_NOREF;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        auto it = g_state.find(plugin);
        if (it != g_state.end()) ref = it->second.loadRef;
    }
    RunRefIsolated(plugin, ref, "on_load");
}

// ---------------------------------------------------------------------------
// on_save(fn) / on_load(fn) — register the Lua save/load body.
// ---------------------------------------------------------------------------

// Shared body for on_save / on_load: validate fn, ref it (replacing any
// prior), wire the C trampoline once, and ensure the uid is non-zero before
// any write the trampoline will do. `isSave` selects which slot/callback.
int RegisterBody(lua_State* L, bool isSave) {
    const char* verb = isSave ? "on_save" : "on_load";

    if (!lua_isfunction(L, 1)) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.cosave.%s(fn): expects a single function argument — the body "
            "that runs inside the cosave %s window. Call shape: "
            "kcdx.cosave.%s(function() ... end)",
            verb, isSave ? "write" : "read", verb);
        return 2;
    }

    const kcdxSerializationInterface* ser = kcdx::serialization::GetInterface();
    if (!ser) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.cosave.%s(fn): the cosave interface isn't available "
            "(internal error — see kcdx.log).",
            verb);
        return 2;
    }

    std::string owner, callSiteFile;
    int callSiteLine = 0;
    kcdxPluginHandle handle = ResolveOwner(L, owner, callSiteFile, callSiteLine);
    if (owner.empty()) {
        // Anonymous Lua (console / pak script) has no stable name to derive a
        // UID from and no plugin section to persist into. Refuse with a
        // teaching error rather than silently writing under uid 0 / an invalid
        // handle (which serialization would drop anyway).
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.cosave.%s(fn): cosave needs an owning plugin to derive a "
            "save identity, but this call isn't attributed to one (it ran from "
            "the console or an anonymous script, site=%s:%d). Call "
            "kcdx.cosave.%s from a plugin's plugin.lua.",
            verb, callSiteFile.empty() ? "?" : callSiteFile.c_str(),
            callSiteLine, verb);
        return 2;
    }

    // Ref the fn (arg 1). lua_pushvalue copies it to the top so luaL_ref pops
    // the copy and leaves the caller's argument untouched.
    lua_pushvalue(L, 1);
    int newRef = luaL_ref(L, LUA_REGISTRYINDEX);
    if (newRef == LUA_NOREF || newRef == LUA_REFNIL) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.cosave.%s(fn): internal error — failed to retain the "
            "callback (see kcdx.log).",
            verb);
        return 2;
    }

    {
        std::lock_guard<std::mutex> lock(g_mu);
        CosaveState& st = g_state[handle];

        // Re-registering replaces: unref the old fn so it can be collected.
        int& slot = isSave ? st.saveRef : st.loadRef;
        if (slot != LUA_NOREF && slot != LUA_REFNIL) {
            luaL_unref(L, LUA_REGISTRYINDEX, slot);
        }
        slot = newRef;

        // Wire the C trampoline ONCE per plugin per side (a second on_save just
        // replaces the ref above; don't re-register the C callback).
        bool& wired = isSave ? st.saveCbWired : st.loadCbWired;
        if (!wired) {
            if (isSave) ser->SetSaveCallback(handle, &SaveTrampoline);
            else        ser->SetLoadCallback(handle, &LoadTrampoline);
            wired = true;
        }

        // Touching cosave -> give the plugin its identity now, so the uid is
        // non-zero before the trampoline's first OpenRecordNamed. No-op if a
        // prior on_save/on_load/write/set_uid already set it.
        EnsureUidLocked(st, handle, owner);
    }

    log::InfoF("kcdx.cosave: registered %s for plugin='%s' site=%s:%d (ref=%d)",
               verb, owner.c_str(),
               callSiteFile.empty() ? "?" : callSiteFile.c_str(),
               callSiteLine, newRef);

    lua_pushboolean(L, 1);
    return 1;
}

int Lua_OnSave(lua_State* L) { return RegisterBody(L, /*isSave=*/true); }
int Lua_OnLoad(lua_State* L) { return RegisterBody(L, /*isSave=*/false); }

// ---------------------------------------------------------------------------
// set_uid(uid) — the EXPERT OVERRIDE.
// ---------------------------------------------------------------------------
int Lua_SetUid(lua_State* L) {
    if (lua_type(L, 1) != LUA_TNUMBER) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.cosave.set_uid(uid): expects a positive integer — the cosave "
            "section id. This is an ADVANCED override; the common path is to "
            "OMIT set_uid and let kcdx derive a stable id from your plugin "
            "name. Pin a uid only to match a save format you already shipped.");
        return 2;
    }
    lua_Number n = lua_tonumber(L, 1);
    // uid is a u32; reject non-integers, negatives, 0, and out-of-range.
    if (n != static_cast<lua_Number>(static_cast<int64_t>(n)) || n <= 0 ||
        n > static_cast<lua_Number>(0xFFFFFFFFu)) {
        lua_pushnil(L);
        if (n == 0) {
            lua_pushstring(L,
                "kcdx.cosave.set_uid(0): uid 0 means no cosave section — the "
                "engine drops it silently. Pick a non-zero id, or omit set_uid "
                "to let kcdx derive one from your plugin name.");
        } else {
            lua_pushstring(L,
                "kcdx.cosave.set_uid(uid): uid must be a positive integer in "
                "[1, 0xFFFFFFFF] (a 32-bit cosave section id). Omit set_uid to "
                "let kcdx derive one from your plugin name.");
        }
        return 2;
    }
    uint32_t uid = static_cast<uint32_t>(static_cast<int64_t>(n));

    const kcdxSerializationInterface* ser = kcdx::serialization::GetInterface();
    if (!ser) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.cosave.set_uid(uid): the cosave interface isn't available "
            "(internal error — see kcdx.log).");
        return 2;
    }

    std::string owner, callSiteFile;
    int callSiteLine = 0;
    kcdxPluginHandle handle = ResolveOwner(L, owner, callSiteFile, callSiteLine);
    if (owner.empty()) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.cosave.set_uid(uid): cosave needs an owning plugin, but this "
            "call isn't attributed to one (console / anonymous script, "
            "site=%s:%d). Call kcdx.cosave.set_uid from a plugin's plugin.lua.",
            callSiteFile.empty() ? "?" : callSiteFile.c_str(), callSiteLine);
        return 2;
    }

    {
        std::lock_guard<std::mutex> lock(g_mu);
        CosaveState& st = g_state[handle];
        st.uid = uid;
        st.uidExplicit = true;  // auto-derive must never override this
        ser->SetUniqueID(handle, uid);
    }

    log::InfoF("kcdx.cosave: set_uid(0x%08X) for plugin='%s' (explicit override)",
               uid, owner.c_str());

    lua_pushboolean(L, 1);
    return 1;
}

// ---------------------------------------------------------------------------
// write(tag, version, value) — string tag + serializer + named record.
// ONLY valid inside the on_save trampoline (window open).
// ---------------------------------------------------------------------------
int Lua_Write(lua_State* L) {
    // tag (string, required)
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.cosave.write(tag, version, value): `tag` (string) is required "
            "— a human-readable record name (e.g. \"counter\"). Call shape: "
            "kcdx.cosave.write(\"counter\", 1, my_value)");
        return 2;
    }
    std::string tag = lua_tostring(L, 1);

    // version (positive integer, required)
    if (lua_type(L, 2) != LUA_TNUMBER) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.cosave.write(\"%s\", version, value): `version` (positive "
            "integer) is required — your per-tag schema version (start at 1, "
            "bump it when you change what this tag stores).",
            tag.c_str());
        return 2;
    }
    {
        lua_Number vn = lua_tonumber(L, 2);
        if (vn != static_cast<lua_Number>(static_cast<int64_t>(vn)) || vn <= 0 ||
            vn > static_cast<lua_Number>(0xFFFFFFFFu)) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.cosave.write(\"%s\", version, value): `version` must be a "
                "positive integer in [1, 0xFFFFFFFF].",
                tag.c_str());
            return 2;
        }
    }
    uint32_t version =
        static_cast<uint32_t>(static_cast<int64_t>(lua_tonumber(L, 2)));

    // value (any serializable Lua value, required at index 3). The serializer
    // rejects a top-level nil (and functions/userdata/threads/cycles) with a
    // teaching message; a missing 3rd arg is nil and is rejected there.
    std::vector<uint8_t> bytes;
    std::string serErr;
    if (!kcdx::lua_cosave_serial::Serialize(L, 3, bytes, serErr)) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.cosave.write(\"%s\", ...): %s",
            tag.c_str(), serErr.c_str());
        return 2;
    }

    const kcdxSerializationInterface* ser = kcdx::serialization::GetInterface();
    if (!ser) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.cosave.write(\"%s\", ...): the cosave interface isn't "
            "available (internal error — see kcdx.log).",
            tag.c_str());
        return 2;
    }

    // OpenRecordNamed (step-1 named thunk: hashes + collision-detects + stores
    // the string). Returns false for either of two reasons — and the engine
    // has ALREADY logged which (a hash collision names both tags + the plugin;
    // a closed window / missing uid logs its own line). The common author
    // cause is calling write() OUTSIDE an on_save body, so the teaching error
    // leads with that; it points at kcdx.log for the collision case (which the
    // engine distinguished there).
    if (!ser->OpenRecordNamed(tag.c_str(), version)) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.cosave.write(\"%s\", ...): could not open the record. The "
            "usual cause is calling write() outside a kcdx.cosave.on_save(...) "
            "body — write() only works inside the save window. (It also fails "
            "if your string tag collides with another tag's hash in this save; "
            "if so, kcdx.log names both tags.)",
            tag.c_str());
        return 2;
    }

    if (!ser->WriteRecordData(bytes.data(),
                              static_cast<uint32_t>(bytes.size()))) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.cosave.write(\"%s\", ...): the record opened but writing its "
            "bytes failed (see kcdx.log).",
            tag.c_str());
        return 2;
    }

    lua_pushboolean(L, 1);
    return 1;
}

// ---------------------------------------------------------------------------
// records() — iterator over this plugin's records (load side).
// ONLY valid inside the on_load trampoline (reader window open). Outside, the
// first GetNextRecordInfo returns false and the iterator yields nothing.
//
// Implemented as a single stateful C closure (lua_pushcclosure — NOT a
// static-const sentinel, AP5): each call advances the cursor and returns
// (tagString, version, value), or nil when GetNextRecordInfo is false. A
// record whose bytes fail to Deserialize (corrupt / incompatible) is SKIPPED
// with a loud warning naming the tag — one bad record never aborts the
// author's whole `for ... in records()` loop.
// ---------------------------------------------------------------------------
int RecordsIter(lua_State* L) {
    const kcdxSerializationInterface* ser = kcdx::serialization::GetInterface();
    if (!ser) return 0;  // no interface -> iteration ends

    for (;;) {
        uint32_t tagHash = 0, version = 0, len = 0;
        if (!ser->GetNextRecordInfo(&tagHash, &version, &len)) {
            return 0;  // no more records (or outside the reader window) -> stop
        }

        // The human-readable string tag for an OpenRecordNamed chunk ("" for a
        // numeric chunk or a pre-named-format cosave). Copy before any further
        // GetNextRecordInfo/ReadRecordData call (the pointer is transient).
        const char* tagNameC = ser->GetRecordTagName();
        std::string tagName = tagNameC ? tagNameC : "";

        std::vector<uint8_t> buf(len);
        if (len > 0 && !ser->ReadRecordData(buf.data(), len)) {
            log::WarnF("kcdx.cosave.records: failed to read %u bytes for record "
                       "tag='%s' — skipping it.",
                       len, tagName.empty() ? "<numeric>" : tagName.c_str());
            continue;  // advance to the next record
        }

        // Push the two scalar return values FIRST (tag, version), THEN
        // Deserialize pushes the value on top. The stack then holds, top-down:
        // value (top), version, tag — i.e. left-to-right tag, version, value,
        // exactly the generic-for `for tag, ver, val in ...` order. No
        // reordering needed.
        lua_pushstring(L, tagName.c_str());        // [..., tag]
        lua_pushinteger(L, (lua_Integer)version);  // [..., tag, version]

        std::string deErr;
        if (!kcdx::lua_cosave_serial::Deserialize(
                L, buf.data(), buf.size(), deErr)) {
            // Corrupt / incompatible record. Loud warning + SKIP. Deserialize
            // pushed nothing on the false path, so pop the tag + version we
            // pushed above before advancing to the next record (don't abort
            // the author's whole loop over one bad record).
            lua_pop(L, 2);
            log::WarnF("kcdx.cosave.records: could not deserialize record "
                       "tag='%s' (%s) — skipping it.",
                       tagName.empty() ? "<numeric>" : tagName.c_str(),
                       deErr.c_str());
            continue;  // advance to the next record
        }

        // Stack: ..., tag, version, value (value on top). Return all three.
        return 3;  // tag, version, value
    }
}

// records() — return the iterator closure. The generic-for protocol accepts a
// single stateful function that returns multiple values and nil to stop; we
// have no per-iteration state to thread (the engine's cursor IS the state), so
// state + control are nil.
int Lua_Records(lua_State* L) {
    lua_pushcclosure(L, RecordsIter, /*nupvalues=*/0);
    return 1;
}

}  // namespace

void bind(lua_State* L) {
    // kcdx.cosave.* — a GROUPED capability domain (NOT a top-level verb).
    // Built exactly like kcdx.console.* (lua_bind_command.cpp) / kcdx.log.*:
    // lua_newtable + per-fn lua_pushcfunction/lua_setfield, then lua_setfield
    // onto the kcdx table. The kcdx table is at the top of the stack on entry;
    // lua_setfield pops the sub-table it consumes, leaving the stack balanced
    // for the next sub-binder.
    int kcdx_idx = lua_gettop(L);

    lua_newtable(L);
    lua_pushcfunction(L, Lua_OnSave);
    lua_setfield(L, -2, "on_save");
    lua_pushcfunction(L, Lua_OnLoad);
    lua_setfield(L, -2, "on_load");
    lua_pushcfunction(L, Lua_Write);
    lua_setfield(L, -2, "write");
    lua_pushcfunction(L, Lua_Records);
    lua_setfield(L, -2, "records");
    lua_pushcfunction(L, Lua_SetUid);
    lua_setfield(L, -2, "set_uid");
    lua_setfield(L, kcdx_idx, "cosave");
}

}  // namespace kcdx::lua_bind_cosave
