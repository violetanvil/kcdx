// kcdx.command{...} — Lua-side console-command registration.
//
// A core authoring verb per .claude/rules/lua-api-surface.md: top-level
// (like kcdx.hook / kcdx.on), configuring -> {named table}. A thin Lua
// binder over the EXISTING, proven C++ console path (CAP-13): the engine
// console code (console.{h,cpp}, kcdxConsoleInterface) is NOT touched —
// this brings the Lua surface to parity with the already-shipped C++
// mirror, so no C++ console work is owed.
//
//   kcdx.command{
//       name        = "outfit_dump",
//       description = "Dump outfit state to log.",
//       callback    = function(args)
//           kcdx.log.info("CMD", "argc=%d", #args)   -- args is an array
//           if args[1] then
//               kcdx.log.info("CMD", "first=%s raw=%s", args[1], args.raw)
//           end
//       end,
//   }
//
// Returns true on success, (nil, teaching error) on failure (the standard
// kcdx-binder error idiom).
//
// DESIGN LOCKS:
//   * REGISTER IMMEDIATELY at the call (NOT via lua_registry's deferred
//     Entry/apply-pass). IConsole is resolved at boot (console::Init runs
//     on the worker thread before plugins load) and all Lua runs after the
//     game is up, so console::RegisterCommand is live from kcdxPlugin_Load
//     onward. Commands have NO conflict-resolution semantics (CryEngine +
//     the kcdx console layer already refuse duplicate names), so the
//     deferred-apply machinery buys nothing. Mirrors the C++ path which
//     registers immediately. No lua_registry Kind::Command.
//   * CALLBACK ARG SHAPE: callback = function(args). `args` is a Lua table
//     that is BOTH an ARRAY of the user-supplied argument strings
//     (args[1..#args], EXCLUDING GetArg(0) which is the command name) AND
//     carries a `raw` FIELD (args.raw == the full GetCommandLine() string).
//     The common case (args[1], #args) is zero-ceremony; the raw line is
//     there but invisible until reached for.
//   * OWNER IDENTITY via lua_registry::OwningPluginForCurrentCall (the same
//     mechanism kcdx.publish/on/hook use). The resolved owner NAME is mapped
//     to a kcdxPluginHandle via plugins::HandleOf. An anonymous caller
//     (resolves to "") -> HandleOf("") misses -> kcdxInvalidPluginHandle,
//     which console::RegisterCommand accepts (it stores the handle only for
//     dispatch logging; it does not require a valid handle). We warn so the
//     anonymous registration is observable — matching how kcdx.publish
//     surfaces the anonymous case.
//
// Threading (AP6): the console callback fires on the MAIN THREAD (CryEngine
// dispatches console commands during the game loop — Interfaces.h:1022). So
// lua_pcall'ing the stored Lua callback from inside the C thunk is
// main-thread-safe. The thunk is pcall-isolated: a throwing Lua callback
// logs loud (structured KV) and does not propagate out to the engine.
//
// Lua bridge (lua-bridge.md): the callback is stored as a luaL_ref into
// LUA_REGISTRYINDEX; the command-name -> ref mapping is an engine-side C++
// std::unordered_map (NOT a Lua sentinel — AP5 / PROBE Q stays zero).

#include "lua_bind_command.h"

#include <mutex>
#include <string>
#include <unordered_map>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "console.h"
#include "log.h"
#include "plugin_loader.h"
#include "scripting.h"      // scripting::lua_state() — the live VM the
                            // main-thread thunk pcalls against.
#include "lua_registry.h"

// One of the headers above transitively includes <windows.h>, which defines
// the object-like macro `GetCommandLine` -> `GetCommandLineA`. That rewrites
// our call to the kcdxConsoleInterface member `GetCommandLine` into
// `GetCommandLineA` (which the interface has no member named). Drop the macro
// for this TU — we only ever call the kcdx interface member, never the Win32
// GetCommandLine. (#undef of an undefined macro is a no-op, so this is safe
// even if the include graph changes.)
#undef GetCommandLine

namespace kcdx::lua_bind_command {

namespace {

// command name -> Lua registry ref for its callback. Engine-side C++ (NOT
// a Lua slot / sentinel — lua-bridge.md, AP5). The single C thunk
// (TheThunk) reads the fired command's name via GetArg(0) and looks the
// ref up here. Names are unique across the process (console::RegisterCommand
// refuses dups), so the key is unambiguous.
std::mutex g_mu;
std::unordered_map<std::string, int> g_commandRefs;

// One C thunk for EVERY kcdx.command-registered command. It identifies WHICH
// command fired by reading GetArg(0) (the command name) and looks up the
// matching Lua callback ref. Fires on the main thread (CryEngine console
// dispatch), so pcall'ing the stored Lua ref against the live VM is safe
// (AP6). pcall-isolated: a throwing callback logs loud and does NOT
// propagate out to the engine.
void TheThunk(const kcdxConsoleCmdArgs* args) {
    const kcdxConsoleInterface* console = kcdx::console::GetInterface();
    if (!console) {
        log::Error("[kcdx.command] thunk fired but console interface is null");
        return;
    }

    // GetArg(0) is the command name itself (CryEngine convention).
    const char* cmdNameC = console->GetArg(args, 0);
    if (!cmdNameC) {
        log::Error("[kcdx.command] thunk fired with no command name (GetArg(0) "
                   "returned null)");
        return;
    }
    std::string cmdName = cmdNameC;

    // Look up the callback ref for this command.
    int ref = LUA_NOREF;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        auto it = g_commandRefs.find(cmdName);
        if (it != g_commandRefs.end()) ref = it->second;
    }
    if (ref == LUA_NOREF || ref == LUA_REFNIL) {
        log::ErrorF("[kcdx.command] command '%s' fired but no callback ref is "
                    "registered (internal inconsistency)",
                    cmdName.c_str());
        return;
    }

    // The live VM — the same single shared lua_State the binder registered
    // against (scripting::lua_state()), retrieved the same way the other
    // C-thunk dispatchers (lua_lifecycle) get it. Main-thread.
    lua_State* L = kcdx::scripting::lua_state();
    if (!L) {
        log::ErrorF("[kcdx.command] command '%s' fired but no live lua_State; "
                    "dropping the callback",
                    cmdName.c_str());
        return;
    }

    // Retrieve the stored callback function.
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        log::ErrorF("[kcdx.command] command '%s' callback ref is not a function "
                    "(internal inconsistency)",
                    cmdName.c_str());
        return;
    }

    // Build the single `args` table the callback receives: an ARRAY of the
    // user-supplied argument strings (GetArg(1..argc-1) -> args[1..]) PLUS a
    // `raw` field (the full GetCommandLine() string). A table with both
    // integer keys and a string key is set with lua_rawseti for the array
    // elements and lua_setfield for the raw field on the SAME table.
    lua_newtable(L);
    int argc = console->GetArgCount(args);  // 1 + N (GetArg(0) is the name)
    int outIdx = 0;
    for (int i = 1; i < argc; ++i) {
        const char* a = console->GetArg(args, i);
        if (!a) continue;          // never push a NULL string into Lua
        lua_pushstring(L, a);      // copies into the VM; we don't retain it
        lua_rawseti(L, -2, ++outIdx);
    }
    // args.raw — the full command line. GetCommandLine may return null
    // (degrade to no `raw` rather than pushing a NULL string). The
    // <windows.h> GetCommandLine macro is #undef'd at the top of this TU.
    const char* rawLine = console->GetCommandLine(args);
    if (rawLine) {
        lua_pushstring(L, rawLine);
        lua_setfield(L, -2, "raw");
    }

    // pcall-isolated: a throwing callback logs loud and does NOT escape into
    // the engine's console-dispatch frame.
    int status = lua_pcall(L, /*nargs=*/1, /*nresults=*/0, /*errfunc=*/0);
    if (status != 0) {
        const char* msg = lua_tostring(L, -1);
        log::ErrorF("[kcdx.command] command '%s' callback threw: %s",
                    cmdName.c_str(), msg ? msg : "(no message)");
        lua_pop(L, 1);  // pop the error message
    }
}

// kcdx.command{ name=, description=, callback= }
//
//   name        (string, required)  : the console command name (unique
//                                     across the process).
//   description (string, optional)  : help text shown for `help <name>`.
//   callback    (function, required): runs on the main thread when the
//                                     command fires; receives a single
//                                     `args` table (array of arg strings +
//                                     `args.raw`).
//
// On success returns true. On a bad field, or if console::RegisterCommand
// refuses the registration (dup name / IConsole not ready), returns
// (nil, teaching error) per the kcdx-binder error convention.
int Lua_Command(lua_State* L) {
    // --- Validate arg 1 is a table ---
    if (lua_type(L, 1) != LUA_TTABLE) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.command{...}: expects a single table argument with fields "
            "`name` (string), `callback` (function), and optional "
            "`description` (string). Call shape: kcdx.command{ name = "
            "\"my_cmd\", callback = function(args) ... end }");
        return 2;
    }

    // --- name (string, required) ---
    lua_getfield(L, 1, "name");
    if (lua_type(L, -1) != LUA_TSTRING) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.command{...}: `name` (string) is required — the console "
            "command name runnable from the ~ console (e.g. name = "
            "\"outfit_dump\").");
        return 2;
    }
    std::string name = lua_tostring(L, -1);
    lua_pop(L, 1);

    // --- description (string, optional; defaults to "") ---
    std::string description;
    lua_getfield(L, 1, "description");
    if (lua_type(L, -1) == LUA_TSTRING) {
        description = lua_tostring(L, -1);
    } else if (!lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.command{ name = \"%s\" }: `description`, if present, must be "
            "a string — the help text shown for `help %s`.",
            name.c_str(), name.c_str());
        return 2;
    }
    lua_pop(L, 1);

    // --- callback (function, required) ---
    lua_getfield(L, 1, "callback");
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.command{ name = \"%s\" }: `callback` (function) is required "
            "— the function to run when the command fires. It receives a "
            "single `args` table: args[1], args[2], ... are the typed "
            "arguments, #args is the count, and args.raw is the full command "
            "line.",
            name.c_str());
        return 2;
    }
    // luaL_ref pops the value off the stack — it's already the field we
    // pushed via lua_getfield, so ref it directly (no extra copy needed; we
    // don't disturb the caller's table argument, which is at index 1).
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    if (ref == LUA_NOREF || ref == LUA_REFNIL) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.command{ name = \"%s\" }: internal error — failed to retain "
            "the callback (see kcdx.log)",
            name.c_str());
        return 2;
    }

    // --- Resolve owner identity (same mechanism as kcdx.publish/on/hook) ---
    // Map the owner NAME to a kcdxPluginHandle via plugins::HandleOf. An
    // anonymous caller (console / pak Lua) resolves to "" -> HandleOf("")
    // misses -> kcdxInvalidPluginHandle, which console::RegisterCommand
    // accepts (the handle is stored only for dispatch logging). We warn so
    // the anonymous registration stays observable.
    std::string callSiteFile;
    int callSiteLine = 0;
    std::string owner = kcdx::lua_registry::OwningPluginForCurrentCall(
        L, callSiteFile, callSiteLine);

    kcdxPluginHandle ownerHandle =
        kcdx::plugins::HandleOf(owner.empty() ? "" : owner.c_str());
    if (owner.empty()) {
        log::WarnF("kcdx.command: anonymous registrant (no attributed plugin) "
                   "for command '%s' at site=%s:%d — registering under an "
                   "invalid plugin handle (the command still works).",
                   name.c_str(),
                   callSiteFile.empty() ? "?" : callSiteFile.c_str(),
                   callSiteLine);
    }

    // --- Record the name -> ref mapping BEFORE registering, so the thunk can
    // resolve the callback the instant CryEngine dispatches it. If
    // RegisterCommand then refuses, we roll the mapping + ref back. ---
    {
        std::lock_guard<std::mutex> lock(g_mu);
        // Guard against a kcdx.command dup of an existing kcdx.command name:
        // RegisterCommand also refuses it, but rolling back a clobbered ref
        // map entry would orphan the prior command's ref. Refuse here too.
        if (g_commandRefs.find(name) != g_commandRefs.end()) {
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.command{ name = \"%s\" }: a command with this name is "
                "already registered. Command names must be unique across the "
                "process.",
                name.c_str());
            return 2;
        }
        g_commandRefs[name] = ref;
    }

    const kcdxConsoleInterface* console = kcdx::console::GetInterface();
    bool ok = console && console->RegisterCommand(
        ownerHandle, name.c_str(),
        description.empty() ? "" : description.c_str(), &TheThunk);

    if (!ok) {
        // Roll back: drop the ref + map entry so a retry under a different
        // name (or after IConsole comes up) isn't blocked by a stale entry.
        {
            std::lock_guard<std::mutex> lock(g_mu);
            g_commandRefs.erase(name);
        }
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.command{ name = \"%s\" }: registration was refused by the "
            "console (a duplicate name across plugins, no free command slots, "
            "or IConsole not yet ready). See kcdx.log for the specific reason.",
            name.c_str());
        return 2;
    }

    log::InfoF("kcdx.command: registered '%s' for plugin='%s' site=%s:%d "
               "(ref=%d, handle=%u)",
               name.c_str(), owner.empty() ? "<anon>" : owner.c_str(),
               callSiteFile.empty() ? "?" : callSiteFile.c_str(),
               callSiteLine, ref, ownerHandle);

    lua_pushboolean(L, 1);
    return 1;
}

}  // namespace

void bind(lua_State* L) {
    // kcdx.command is a TOP-LEVEL verb (like kcdx.hook / kcdx.on), NOT a
    // sub-table — the kcdx table is at the top of the stack; register the
    // function directly on it.
    int kcdx_idx = lua_gettop(L);
    lua_pushcfunction(L, Lua_Command);
    lua_setfield(L, kcdx_idx, "command");
}

}  // namespace kcdx::lua_bind_command
