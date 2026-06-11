// kcdx.behavior.* — named behaviors, the read side + declare.
//
//   kcdx.behavior.declare("hardcore_combat", {
//       description    = "lock fast-travel and timed saves while in combat",
//       default        = false,
//       implementation = function(value) ... end,
//       revert         = function(old_value) ... end,  -- optional
//   })
//   kcdx.behavior.get("hardcore_combat")          --> recorded value, else default
//   kcdx.behavior.list("redmoon.realism.")        --> array of entry tables
//
// The thin binder over behavior_registry (the one runtime registry both
// tiers share). THIS file owns the author-facing surface: argument shapes,
// namespace stamping (the engine derives <author>.<plugin> from the calling
// plugin's manifest — the author writes only the bare name), and the
// teaching errors. The registry owns storage, the duplicate rule, the
// precedence walk, and enumeration.
//
// Error contract: a wrong declare/get/list call RAISES a normal Lua error at
// the call site (luaL_error) with a teaching text — the calling plugin fails
// loudly, the load continues (standard plugin error handling). Each raise is
// also logged under category "BEHAVIOR" so the dev log greps the cause.
//
// Declare window: declares are a LOAD-TIME act. The post-load check gates on
// the init-phase model — the last load-wave phase (AfterGameApply, advanced
// at the end of the first-update-tick load block) is the closest queryable
// "the load waves finished" state until the apply boundary lands and owns
// the window precisely.
//
// Values: the spec's default / implementation / revert and the recorded
// value live in the engine-owned Lua VM as registry refs (luaL_ref into
// LUA_REGISTRYINDEX) — never copied out, never a kcdx-side static-const
// sentinel. nil is the engine's unset sentinel: a nil `default` is the
// missing-field error, and an unset recorded value is the absent ref, never
// a stored nil.

#include "lua_bind_behavior.h"

#include <string>
#include <vector>

extern "C" {
#include "lauxlib.h"
}

#include "behavior_registry.h"
#include "init_phase.h"        // init::Current — the declare-window check
#include "log.h"               // LOG_ERROR_KV, ::kcdx::log::KV
#include "lua_bind_helpers.h"  // FindUnknownKey
#include "lua_registry.h"      // OwningPluginForCurrentCall

namespace kcdx::lua_bind_behavior {

namespace {

constexpr const char* kCat = "BEHAVIOR";

// The spec-table key allowlist (fail loud on a typo'd key, never a silent
// drop).
static const char* kSpecKeys[] = {
    "description", "default", "implementation", "revert",
};
constexpr size_t kSpecKeyCount = sizeof(kSpecKeys) / sizeof(kSpecKeys[0]);

// The one-line spec-shape reminder appended to declare's teaching errors.
constexpr const char* kSpecShape =
    "The spec shape: { description = \"one human line\", default = <any "
    "non-nil Lua value>, implementation = function(value) ... end, revert = "
    "function(old_value) ... end --[[optional]] }.";

// Log the reject (greppable in the dev log), then raise the teaching error
// at the author's call site. Never returns.
int RejectDeclare(lua_State* L, const std::string& author,
                  const std::string& plugin, const std::string& name,
                  const char* reason, const std::string& detail) {
    LOG_ERROR_KV(kCat, "declare_rejected",
        ::kcdx::log::KV("author", author),
        ::kcdx::log::KV("plugin", plugin),
        ::kcdx::log::KV("name",   name),
        ::kcdx::log::KV("reason", reason),
        ::kcdx::log::KV("detail", detail));
    return luaL_error(L, "%s", detail.c_str());
}

// kcdx.behavior.declare(name, spec)
//
// name: positional bare string — the engine stamps <author>.<plugin>.<bare>
// from the calling plugin's manifest. spec: REQUIRED description (string) +
// default (any non-nil value) + implementation (function); OPTIONAL revert
// (function). Field validation runs BEFORE any ref is created; the registry
// rejects a duplicate stamped full name (refs released before the raise), so
// the FIRST declaration always stands and a reject leaks nothing.
int Lua_Declare(lua_State* L) {
    std::string callSiteFile;
    int callSiteLine = 0;
    kcdx::lua_registry::OwningPlugin owner =
        kcdx::lua_registry::OwningPluginForCurrentCall(
            L, callSiteFile, callSiteLine);

    // --- arg 1: the bare name ---
    if (lua_type(L, 1) != LUA_TSTRING) {
        return RejectDeclare(L, owner.author, owner.plugin, "",
            "bad_arg_name",
            "kcdx.behavior.declare(name, spec): `name` (arg 1) must be a "
            "string — the BARE behavior name you are declaring. The engine "
            "stamps it as <author>.<plugin>.<name> from your [plugin] "
            "manifest. Call shape: kcdx.behavior.declare(\"hardcore_combat\", "
            "{ description = ..., default = ..., implementation = "
            "function(value) ... end }).");
    }
    const std::string bareName = lua_tostring(L, 1);
    if (bareName.empty()) {
        return RejectDeclare(L, owner.author, owner.plugin, bareName,
            "empty_name",
            "kcdx.behavior.declare(name, spec): `name` is empty — write the "
            "bare behavior name (e.g. \"hardcore_combat\").");
    }
    if (bareName.find('.') != std::string::npos) {
        return RejectDeclare(L, owner.author, owner.plugin, bareName,
            "dotted_name",
            "kcdx.behavior.declare('" + bareName + "'): write the BARE name "
            "(no dots) — the engine derives the <author>.<plugin> prefix "
            "from your manifest and stamps the full name for you; never "
            "type your own prefix.");
    }

    // --- the declare window: declares are a load-time act ---
    if (kcdx::init::Current() >= kcdx::init::InitPhase::AfterGameApply) {
        return RejectDeclare(L, owner.author, owner.plugin, bareName,
            "post_load_declare",
            "kcdx.behavior.declare('" + bareName + "'): declares are a "
            "load-time act — this call arrived after the plugin load waves "
            "finished. Declare from your plugin's load entry (plugin.lua or "
            "lua_after), not from a post-load callback or the console.");
    }

    // --- the declarer identity (the namespace stamp) ---
    if (owner.plugin.empty()) {
        // Truly no owning plugin (console / pak Lua / anonymous caller).
        return RejectDeclare(L, owner.author, owner.plugin, bareName,
            "anonymous_declarer",
            "kcdx.behavior.declare('" + bareName + "'): no owning plugin "
            "found for this call site — behaviors are declared by plugins, "
            "and the engine stamps <author>.<plugin>.<bare> from the "
            "declaring plugin's manifest. Declare from a plugin's own "
            "script (plugin.lua / lua_after).");
    }
    if (owner.author.empty()) {
        // Plugin known but [plugin].author empty — the tolerated legacy
        // manifest state (the 1-dot tier the resolvers carry through the
        // namespace refactor). A declare needs the full 2-dot stamp, so it
        // teaches the manifest fix instead of reading as "no plugin".
        return RejectDeclare(L, owner.author, owner.plugin, bareName,
            "missing_author",
            "kcdx.behavior.declare('" + bareName + "'): your plugin '" +
            owner.plugin + "' has no [plugin].author in its manifest — the "
            "engine stamps every behavior as <author>.<plugin>.<bare>, and "
            "the author component is missing. Add [plugin].author to your "
            "manifest (kcdx.toml), then declare again.");
    }

    // --- arg 2: the spec table ---
    if (lua_type(L, 2) != LUA_TTABLE) {
        return RejectDeclare(L, owner.author, owner.plugin, bareName,
            "bad_arg_spec",
            "kcdx.behavior.declare('" + bareName + "', spec): `spec` (arg 2) "
            "must be a table. " + kSpecShape);
    }
    const std::string unknown = kcdx::lua_bind_helpers::FindUnknownKey(
        L, 2, kSpecKeys, kSpecKeyCount);
    if (!unknown.empty()) {
        return RejectDeclare(L, owner.author, owner.plugin, bareName,
            "unknown_spec_key",
            "kcdx.behavior.declare('" + bareName + "'): unrecognised spec "
            "key '" + unknown + "'. The spec fields are: description "
            "(string, required), default (non-nil, required), "
            "implementation (function, required), revert (function, "
            "optional).");
    }

    // description — required string.
    lua_getfield(L, 2, "description");
    if (lua_type(L, -1) != LUA_TSTRING) {
        lua_pop(L, 1);
        return RejectDeclare(L, owner.author, owner.plugin, bareName,
            "missing_description",
            "kcdx.behavior.declare('" + bareName + "'): spec.description is "
            "missing or not a string (required — one human line, surfaced "
            "by kcdx.behavior.list()). " + kSpecShape);
    }
    const std::string description = lua_tostring(L, -1);
    lua_pop(L, 1);

    // default — required, any non-nil value (nil is the unset sentinel).
    lua_getfield(L, 2, "default");
    const bool defaultPresent = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (!defaultPresent) {
        return RejectDeclare(L, owner.author, owner.plugin, bareName,
            "missing_default",
            "kcdx.behavior.declare('" + bareName + "'): spec.default is "
            "missing — `default` is required and must be a NON-NIL Lua "
            "value (nil is the engine's unset sentinel, never a value); it "
            "is what kcdx.behavior.get() returns while the behavior was "
            "never set. " + kSpecShape);
    }

    // implementation — required function.
    lua_getfield(L, 2, "implementation");
    const bool implOk = (lua_type(L, -1) == LUA_TFUNCTION);
    lua_pop(L, 1);
    if (!implOk) {
        return RejectDeclare(L, owner.author, owner.plugin, bareName,
            "missing_implementation",
            "kcdx.behavior.declare('" + bareName + "'): spec.implementation "
            "is missing or not a function (required — function(value), "
            "invoked once at the apply boundary with the final settled "
            "value). " + kSpecShape);
    }

    // revert — optional function.
    lua_getfield(L, 2, "revert");
    const int revertType = lua_type(L, -1);
    lua_pop(L, 1);
    if (revertType != LUA_TNIL && revertType != LUA_TFUNCTION) {
        return RejectDeclare(L, owner.author, owner.plugin, bareName,
            "bad_revert",
            "kcdx.behavior.declare('" + bareName + "'): spec.revert, if "
            "present, must be a function — function(old_value); its "
            "presence makes the behavior runtime-togglable. " + kSpecShape);
    }

    // --- all valid: pin the values into the VM (registry refs) and
    // register. The registry rejects a duplicate stamped full name (the
    // FIRST declaration stands); on that reject the freshly-created refs
    // are released before the raise, so a rejected declare leaks nothing. ---
    lua_getfield(L, 2, "default");
    const int defaultRef = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_getfield(L, 2, "implementation");
    const int implementationRef = luaL_ref(L, LUA_REGISTRYINDEX);
    int revertRef = kcdx::behavior_registry::kNoRef;
    if (revertType == LUA_TFUNCTION) {
        lua_getfield(L, 2, "revert");
        revertRef = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    std::string err;
    const bool ok = kcdx::behavior_registry::DeclarePlugin(
        owner.author, owner.plugin, bareName, description,
        defaultRef, implementationRef, revertRef, err);
    if (!ok) {
        // Duplicate same-full-name: release the refs, then raise the
        // registry's teaching text (it names the standing declarer) against
        // THIS second declare. The first declaration stands; the load
        // continues (a normal Lua error in the declaring script).
        luaL_unref(L, LUA_REGISTRYINDEX, defaultRef);
        luaL_unref(L, LUA_REGISTRYINDEX, implementationRef);
        if (revertRef != kcdx::behavior_registry::kNoRef) {
            luaL_unref(L, LUA_REGISTRYINDEX, revertRef);
        }
        return RejectDeclare(L, owner.author, owner.plugin, bareName,
            "duplicate_declare",
            "kcdx.behavior.declare('" + bareName + "'): " + err);
    }
    return 0;
}

// Resolve `nameArg` for the calling plugin or raise the teaching error.
// Shared by get (and by list's future per-name forms if any).
const kcdx::behavior_registry::Behavior* ResolveOrRaise(
    lua_State* L, const char* verb, const std::string& nameArg) {
    std::string callSiteFile;
    int callSiteLine = 0;
    kcdx::lua_registry::OwningPlugin owner =
        kcdx::lua_registry::OwningPluginForCurrentCall(
            L, callSiteFile, callSiteLine);

    const kcdx::behavior_registry::Behavior* b =
        kcdx::behavior_registry::ResolveForCaller(
            owner.author, owner.plugin, nameArg);
    if (b) return b;

    const std::string detail =
        std::string("kcdx.behavior.") + verb + "('" + nameArg + "'): no "
        "declared behavior matches this name so far. Browse "
        "kcdx.behavior.list() (or kcdx.behavior.list(\"<author>.<plugin>.\")) "
        "to see what is declared; to reach another plugin's behavior, use "
        "its full <author>.<plugin>.<bare> name.";
    LOG_ERROR_KV(kCat, "name_unresolved",
        ::kcdx::log::KV("verb",   verb),
        ::kcdx::log::KV("name",   nameArg),
        ::kcdx::log::KV("author", owner.author),
        ::kcdx::log::KV("plugin", owner.plugin),
        ::kcdx::log::KV("detail", detail));
    luaL_error(L, "%s", detail.c_str());
    return nullptr;  // unreachable (luaL_error longjmps)
}

// kcdx.behavior.get(name) — the current recorded value, else the spec's
// default. Truthful by construction: the recorded slot is written only by a
// successful set (a later step), so today every get answers the default.
int Lua_Get(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        const char* detail =
            "kcdx.behavior.get(name): `name` (arg 1) must be a string — a "
            "bare name (resolved self > engine > other) or a full "
            "<author>.<plugin>.<bare> name.";
        LOG_ERROR_KV(kCat, "get_bad_arg",
            ::kcdx::log::KV("detail", detail));
        return luaL_error(L, "%s", detail);
    }
    const std::string nameArg = lua_tostring(L, 1);
    const kcdx::behavior_registry::Behavior* b =
        ResolveOrRaise(L, "get", nameArg);

    const int ref = (b->recordedRef != kcdx::behavior_registry::kNoRef)
                        ? b->recordedRef
                        : b->defaultRef;
    lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
    return 1;
}

// kcdx.behavior.list([prefix]) — every registered behavior (both tiers, one
// registry), each entry { name, description, default, current, declarer };
// `current` is the recorded value, else the default (what get() answers).
// Optional string prefix filters on the stamped full name
// (list("kcdx.behavior.") = the engine catalog; list("redmoon.") = redmoon's).
int Lua_List(lua_State* L) {
    std::string prefix;
    const int argType = lua_type(L, 1);
    if (argType == LUA_TSTRING) {
        prefix = lua_tostring(L, 1);
    } else if (argType != LUA_TNONE && argType != LUA_TNIL) {
        const char* detail =
            "kcdx.behavior.list([prefix]): `prefix`, if present, must be a "
            "string — a stamped-name prefix filter like \"redmoon.\" or "
            "\"redmoon.realism.\".";
        LOG_ERROR_KV(kCat, "list_bad_arg",
            ::kcdx::log::KV("detail", detail));
        return luaL_error(L, "%s", detail);
    }

    std::vector<const kcdx::behavior_registry::Behavior*> rows;
    kcdx::behavior_registry::Enumerate(prefix, rows);

    lua_createtable(L, static_cast<int>(rows.size()), 0);
    int idx = 0;
    for (const auto* b : rows) {
        lua_createtable(L, 0, 5);

        lua_pushstring(L, b->fullName.c_str());
        lua_setfield(L, -2, "name");
        lua_pushstring(L, b->description.c_str());
        lua_setfield(L, -2, "description");
        lua_rawgeti(L, LUA_REGISTRYINDEX, b->defaultRef);
        lua_setfield(L, -2, "default");
        const int curRef =
            (b->recordedRef != kcdx::behavior_registry::kNoRef)
                ? b->recordedRef
                : b->defaultRef;
        lua_rawgeti(L, LUA_REGISTRYINDEX, curRef);
        lua_setfield(L, -2, "current");
        lua_pushstring(L, b->DeclarerLabel().c_str());
        lua_setfield(L, -2, "declarer");

        lua_rawseti(L, -2, ++idx);
    }
    return 1;
}

}  // namespace

void bind(lua_State* L) {
    // kcdx.behavior.* — a GROUPED capability domain (NOT a top-level verb).
    // Built exactly like kcdx.cvar.* / kcdx.assets.*: a lua_newtable, per-fn
    // lua_pushcfunction/lua_setfield, then one lua_setfield onto the kcdx
    // table at the top of the stack. Stack-balanced for the next binder.
    const int kcdx_idx = lua_gettop(L);
    lua_newtable(L);
    lua_pushcfunction(L, Lua_Declare);
    lua_setfield(L, -2, "declare");
    lua_pushcfunction(L, Lua_Get);
    lua_setfield(L, -2, "get");
    lua_pushcfunction(L, Lua_List);
    lua_setfield(L, -2, "list");
    lua_setfield(L, kcdx_idx, "behavior");
}

}  // namespace kcdx::lua_bind_behavior
