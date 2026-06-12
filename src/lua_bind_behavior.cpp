// kcdx.behavior.* — named behaviors: declare / set / get / list.
//
//   kcdx.behavior.declare("hardcore_combat", {
//       description    = "lock fast-travel and timed saves while in combat",
//       default        = false,
//       implementation = function(value) ... end,
//       revert         = function(old_value) ... end,  -- optional
//   })
//   kcdx.behavior.set("redmoon.realism.hardcore_combat", true)
//   kcdx.behavior.get("hardcore_combat")          --> recorded value, else default
//   kcdx.behavior.list("redmoon.realism.")        --> array of entry tables
//
// The thin binder over behavior_registry (the one runtime registry both
// tiers share). THIS file owns the author-facing surface: argument shapes,
// namespace stamping (the engine derives <author>.<plugin> from the calling
// plugin's manifest — the author writes only the bare name), and the
// teaching errors. The registry owns storage, the duplicate rule, the
// precedence walk, enumeration, the set-edges, and the apply-boundary pass.
//
// Set window: a set RECORDS during plugin load (last-wins; one conflict warn
// when a second different plugin records a different value); the apply
// boundary invokes each set behavior's implementation once with the final
// value. A set that cannot resolve raises the DISCRIMINATING §6 error
// (reorder / failed-load / disabled / engine-rejected / absent / typo /
// bare-name), recording the consumer→declarer edge on the way out so the
// failed set still feeds ordering. A PLUGIN-tier set from an EARLY stop
// (before the main Lua wave) is OUT-OF-WINDOW (the window-law wall — built
// for the C++ early-stop caller in P2 + the deferred lua_before in P11; the
// Lua binder is itself a main-stop caller, so it never trips it). A post-load
// set (after the boundary completed, or on an already-applied behavior during
// the drain) dispatches the revert TOGGLE (design §5.4, main-thread inline):
// the binder pins the new value, the registry's ApplyPostLoadToggle runs
// revert(old)+implementation(new) for a `revert` declarer (skipping revert on
// a never-applied behavior), records the new value, and raises the revert-less
// teaching error otherwise. The off-thread command queue is a later step.
//
// Error contract: a wrong declare/set/get/list call RAISES a normal Lua
// error at the call site (luaL_error) with a teaching text — the calling
// plugin fails loudly, the load continues (standard plugin error handling).
// Each raise is also logged under category "BEHAVIOR" so the dev log greps
// the cause.
//
// Declare window: declares are a LOAD-TIME act. The post-load check gates on
// the registry's apply-boundary state (BoundaryCompleted) — the declare
// window closes at the apply boundary, SYMMETRIC with the SET window's
// post-load gate (both read BoundaryCompleted), NOT the init phase. The
// boundary completes BEFORE InputLoaded fires, so a declare self-fired from
// an InputLoaded handler — or issued mid-drain during the boundary — sees
// BoundaryCompleted()==true and trips the wall (raises the load-time-act
// teaching error), exactly the way the post-load set path works. (The SET
// window also reads the per-behavior `applied` flag for the mid-drain
// already-applied case; a declare has no such per-behavior state — no
// behavior exists yet at declare time — so the declare gate is the boundary
// flag alone.)
//
// Values: the spec's default / implementation / revert and the recorded
// value live in the engine-owned Lua VM as registry refs (luaL_ref into
// LUA_REGISTRYINDEX) — never copied out, never a kcdx-side static-const
// sentinel. nil is the engine's unset sentinel: a nil `default` is the
// missing-field error, and an unset recorded value is the absent ref, never
// a stored nil.

#include "lua_bind_behavior.h"

#include <cstdio>   // snprintf — the best-effort value stringifier
#include <string>
#include <vector>

extern "C" {
#include "lauxlib.h"
}

#include "behavior_registry.h"
#include "init_phase.h"        // init::Current — the set out-of-window (too-early) check
#include "load_order.h"        // IsPluginEnabled / RunsBefore — branch a/c discrimination
#include "log.h"               // LOG_ERROR_KV, ::kcdx::log::KV
#include "lua_bind_helpers.h"  // FindUnknownKey
#include "lua_plugin_loader.h" // DidScriptFail — branch b failed-declarer discrimination
#include "lua_registry.h"      // OwningPluginForCurrentCall
#include "plugin_loader.h"     // g_manifests — the discovered plugin set (branch c absent/disabled)
#include "zone_gate.h"         // RejectReason — branch c engine-rejected

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
    // Gates on the registry's apply-boundary state (BoundaryCompleted),
    // symmetric with the SET window's post-load gate (Lua_Set, below) — the
    // declare window closes at the apply boundary, not at an init phase. The
    // boundary completes BEFORE kcdxMessage_InputLoaded fires, so a declare
    // self-fired from an `input_loaded` handler sees BoundaryCompleted()==true
    // and trips this wall, exactly as a post-load set does. A declare issued
    // DURING the boundary drain (mid-drain, by an implementation) is likewise
    // post-load — consistent with §5.4 "declares are a load-time act".
    if (kcdx::behavior_registry::BoundaryCompleted()) {
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
// successful set, and a boundary raise clears it back to unset — get never
// reports a state the implementation did not (or will not) receive.
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

// Best-effort one-line description of the value at `idx` for the
// set-conflict warn. Type-tagged, never invokes a metamethod (a __tostring
// would run author code mid-set), strings truncated. Stack effect: 0.
std::string DescribeValue(lua_State* L, int idx) {
    switch (lua_type(L, idx)) {
        case LUA_TBOOLEAN:
            return lua_toboolean(L, idx) ? "true" : "false";
        case LUA_TNUMBER: {
            char buf[40];
            snprintf(buf, sizeof(buf), "%.14g",
                     static_cast<double>(lua_tonumber(L, idx)));
            return buf;
        }
        case LUA_TSTRING: {
            size_t len = 0;
            const char* s = lua_tolstring(L, idx, &len);
            std::string out = "\"";
            out.append(s, len > 48 ? 48 : len);
            if (len > 48) out += "...";
            out += "\"";
            return out;
        }
        default:
            return std::string("<") + lua_typename(L, lua_type(L, idx)) + ">";
    }
}

// "author.plugin" or "<anonymous>" for the conflict warn's actor names.
std::string SetterLabel(const std::string& author, const std::string& plugin) {
    if (plugin.empty()) return "<anonymous>";
    return author + "." + plugin;
}

// Split a name on '.' (an empty segment stays empty — the caller treats a
// dotted-edge form as not-prefixed). Used only to recognise the explicit
// 3-segment <author>.<plugin>.<bare> form for the discriminating set
// resolution errors; the registry owns real resolution.
std::vector<std::string> SplitName(const std::string& name) {
    std::vector<std::string> segs;
    std::string cur;
    for (char c : name) {
        if (c == '.') { segs.push_back(cur); cur.clear(); }
        else          { cur.push_back(c); }
    }
    segs.push_back(cur);
    return segs;
}

// True iff `<author>.<plugin>` names a plugin in the discovered set
// (g_manifests carries EVERY discovered plugin — enabled, disabled, or
// engine-rejected — so this is the "is it installed at all" question; the
// load_order/zone_gate state below answers disabled/rejected). Fills
// `pluginNameOut` with the matched [plugin].name (the load_order / script
// key) on a hit.
bool FindOwningPlugin(const std::string& author, const std::string& plugin,
                      std::string& pluginNameOut) {
    for (const auto& m : kcdx::plugins::g_manifests) {
        if (m.author == author && m.name == plugin) {
            pluginNameOut = m.name;
            return true;
        }
    }
    return false;
}

// True iff the registry carries ANY behavior stamped under the
// `<author>.<plugin>.` prefix (the declarer registered at least one
// behavior). Distinguishes "loaded, declares a typo'd-away name" (some
// behaviors present) from "declared nothing under this prefix yet"
// (later-in-order, or a failed/clean-but-empty declarer).
bool DeclarerHasAnyBehavior(const std::string& author,
                            const std::string& plugin) {
    std::vector<const kcdx::behavior_registry::Behavior*> rows;
    kcdx::behavior_registry::Enumerate(author + "." + plugin + ".", rows);
    return !rows.empty();
}

// Build + log + raise the DISCRIMINATING resolution error for a `set` whose
// name did not resolve to a registered behavior (design §6). `nameArg` is
// the author's argument; `caller` is the consumer (for the reorder wording
// + the failed-set edge). Records the consumer->declarer edge on the
// prefixed ordering-failed branches (a/b/c) before raising — a failed set
// still feeds s6 persistence + s7 auto-order (a bare name (d) / catalog miss
// has no declarer to point at, records nothing; anonymous setters record
// nothing, as the resolved path). NEVER returns (luaL_error longjmps).
int RaiseSetResolution(lua_State* L, const std::string& nameArg,
                       const kcdx::lua_registry::OwningPlugin& caller) {
    const std::vector<std::string> segs = SplitName(nameArg);
    const bool prefixed =
        segs.size() == 3 && !segs[0].empty() && !segs[1].empty() &&
        !segs[2].empty();

    std::string detail;
    const char* branch = nullptr;
    // The full stamped name the consumer→declarer edge records on a failed
    // prefixed set (the design: a failed set still records "this consumer
    // wanted this declarer", feeding ordering). A bare-name failure has no
    // declarer to point at, so it records no edge.
    std::string edgeFullName;

    // A 3-segment kcdx.behavior.<bare> name is the engine CATALOG tier, not a
    // plugin — it has no owning plugin to discriminate against. Resolution
    // already missed (the catalog does not carry this name; the catalog pack
    // ships in a later phase), so teach the catalog-miss directly rather than
    // reading "kcdx.behavior is not installed" off the reserved root.
    const bool catalogForm =
        prefixed && segs[0] == "kcdx" && segs[1] == "behavior";

    if (catalogForm) {
        const std::string& bare = segs[2];
        branch = "catalog_miss";
        detail = "kcdx.behavior.set('" + nameArg + "'): the engine catalog "
            "declares no behavior 'kcdx.behavior." + bare + "' — browse the "
            "catalog with kcdx.behavior.list(\"kcdx.behavior.\") (it is empty "
            "until the catalog pack ships; a plugin behavior is set by its "
            "full <author>.<plugin>.<bare> name).";
        // No edge: a catalog name has no declarer plugin to order against.
    } else if (prefixed) {
        const std::string& author = segs[0];
        const std::string& plugin = segs[1];
        const std::string& bare   = segs[2];
        const std::string owner   = author + "." + plugin;
        const std::string full    = nameArg;  // already the 3-seg full name
        edgeFullName = full;

        std::string ownerPluginName;
        const bool installed = FindOwningPlugin(author, plugin, ownerPluginName);

        if (!installed) {
            // Branch c — absent.
            branch = "owner_absent";
            detail = "kcdx.behavior.set('" + full + "'): '" + bare +
                "' belongs to '" + owner + "', which is not installed. "
                "Install that plugin (or check the <author>.<plugin> prefix "
                "for a typo); your set cannot resolve until its declarer "
                "loads.";
        } else if (!kcdx::load_order::IsPluginEnabled(ownerPluginName)) {
            // Branch c — disabled or engine-rejected (the engine knows
            // which: a non-empty zone_gate reject reason = engine-rejected,
            // else the user disabled it in load_order.toml).
            const std::string& reject =
                kcdx::zone_gate::RejectReason(owner);
            if (!reject.empty()) {
                branch = "owner_rejected";
                detail = "kcdx.behavior.set('" + full + "'): '" + bare +
                    "' belongs to '" + owner + "', which was rejected by the "
                    "engine (" + reject + "). Fix the cause of the rejection; "
                    "your set cannot resolve until its declarer loads.";
            } else {
                branch = "owner_disabled";
                detail = "kcdx.behavior.set('" + full + "'): '" + bare +
                    "' belongs to '" + owner + "', which is installed but "
                    "disabled (load_order.toml). Enable that plugin; your "
                    "set cannot resolve until its declarer loads.";
            }
        } else if (DeclarerHasAnyBehavior(author, plugin)) {
            // Branch b — loaded, declares behaviors, but NOT this bare name:
            // a typo, or a behavior the declarer's new version removed. No
            // reorder suggestion — none fixes a name that does not exist.
            branch = "no_such_bare";
            detail = "kcdx.behavior.set('" + full + "'): '" + owner +
                "' is loaded but declares no behavior '" + bare +
                "' — check the name against kcdx.behavior.list(\"" + owner +
                ".\") (a typo, or a behavior the declarer's new version "
                "removed).";
        } else if (kcdx::lua_plugin_loader::DidScriptFail(ownerPluginName)) {
            // Branch b — failed declarer: the owner's script ERRORED before
            // its declares ran, so it registered nothing. Consult the load
            // OUTCOME first (design §6) — no reorder fixes a load failure.
            branch = "owner_failed";
            detail = "kcdx.behavior.set('" + full + "'): '" + owner +
                "' failed to load — fix or remove it; your set cannot "
                "resolve until it loads (its script errored before its "
                "declares ran, so it registered no behaviors).";
        } else if (kcdx::load_order::RunsBefore(caller.plugin, ownerPluginName)) {
            // Branch a — the owning plugin loads LATER than you. The exact
            // reorder names both plugins + the direction. First-launch
            // wording is calibrated to what the engine KNOWS at this point:
            // the prefix's plugin loads later, not (yet) that it declares
            // this specific name. SECOND-LAUNCH UPGRADE (design §6): if a
            // persisted edge from a PRIOR launch confirms this consumer set
            // exactly this behavior, the engine KNOWS the declaration exists —
            // so the error upgrades to the discriminating form that names the
            // behavior confidently (no longer the hedged "the prefix's plugin
            // loads later"). On a miss (first launch / no edge) the
            // first-launch wording stands unchanged.
            branch = "owner_later";
            if (kcdx::load_order::PriorLaunchEdgeConfirms(
                    caller.author, caller.plugin, full)) {
                branch = "owner_later_confirmed";
                detail = "kcdx.behavior.set('" + full + "'): '" + owner +
                    "' DECLARES the behavior '" + bare + "' but loads AFTER "
                    "you (a prior launch confirmed this dependency) — move '" +
                    caller.author + "." + caller.plugin + "' below '" + owner +
                    "' (in load_order.toml) so its declares run before your "
                    "set, or run the behavior auto-order method (it writes the "
                    "corrected order for the next launch).";
            } else {
                detail = "kcdx.behavior.set('" + full + "'): '" + owner +
                    "' loads after you — move '" + caller.author + "." +
                    caller.plugin + "' below it (in load_order.toml) so its "
                    "declares run before your set, or run the behavior "
                    "auto-order method (it writes the corrected order for the "
                    "next launch).";
            }
        } else {
            // Owner loaded earlier (or same-keyed), enabled, did not fail,
            // and declared nothing under its prefix: the named behavior does
            // not exist on a clean-but-empty declarer. Same surface as the
            // typo branch — no reorder fixes a name with no declarer.
            branch = "no_such_bare";
            detail = "kcdx.behavior.set('" + full + "'): '" + owner +
                "' is loaded but declares no behavior '" + bare +
                "' — check the name against kcdx.behavior.list(\"" + owner +
                ".\") (a typo, or the declarer declares no behaviors).";
        }
    } else {
        // Branch d — a bare name (or any non-3-segment form) with no
        // declarer found: there is no <author>.<plugin> prefix to
        // discriminate with on a FIRST launch. SECOND-LAUNCH UPGRADE
        // (design §6): a persisted edge from a PRIOR launch upgrades this to
        // the discriminating form — if this consumer set a behavior whose bare
        // component matches this bare name in a prior launch, the engine
        // recorded the FULL <author>.<plugin>.<bare> it resolved to back then,
        // so the error names that declarer confidently instead of the generic
        // "use the full name" hint. On a miss (first launch / no edge) the
        // first-launch wording stands unchanged.
        std::string priorFull;
        if (kcdx::load_order::PriorLaunchEdgeForBare(
                caller.author, caller.plugin, nameArg, priorFull)) {
            branch = "bare_confirmed";
            detail = "kcdx.behavior.set('" + nameArg + "'): a prior launch "
                "resolved this bare name to '" + priorFull + "' — that "
                "declarer is not loaded so far this launch (it is missing, "
                "disabled, or loads after you). Use the full name '" +
                priorFull + "' and make sure its plugin loads before you "
                "(check load_order.toml), or browse kcdx.behavior.list().";
        } else {
            branch = "bare_no_declarer";
            detail = "kcdx.behavior.set('" + nameArg + "'): no plugin loaded so "
                "far declares '" + nameArg + "'. If it belongs to another "
                "plugin, use its full <author>.<plugin>.<bare> name; browse "
                "kcdx.behavior.list() to see what is declared.";
        }
    }

    // Edge recording on the prefixed ordering-failed branches (a/b/c): a
    // failed set still records "this consumer wanted this declarer" (feeds s6
    // persistence + s7 auto-order). The prefixed branches carry the explicit
    // full name; the bare branch has no declarer to point at (no edge).
    // Anonymous setters (empty plugin) record nothing — RecordEdge skips
    // them, as the resolved path does.
    if (!edgeFullName.empty()) {
        kcdx::behavior_registry::RecordEdge(
            caller.author, caller.plugin, edgeFullName);
    }

    LOG_ERROR_KV(kCat, "set_unresolved",
        ::kcdx::log::KV("branch",  branch),
        ::kcdx::log::KV("name",    nameArg),
        ::kcdx::log::KV("author",  caller.author),
        ::kcdx::log::KV("plugin",  caller.plugin),
        ::kcdx::log::KV("detail",  detail));
    luaL_error(L, "%s", detail.c_str());
    return 0;  // unreachable (luaL_error longjmps)
}

// kcdx.behavior.set(name, value) — record a value for the apply boundary.
//
// Load-window semantics: the value is RECORDED (last-wins); the behavior's
// implementation runs ONCE at the apply boundary with the final recorded
// value. nil is the unset sentinel, never a value (§4). Resolution: the
// shared ResolveForCaller, and on a miss the DISCRIMINATING §6 set-error
// (RaiseSetResolution) — a prefixed name keys off the owning plugin's load
// outcome + order, a bare name points at the full-name form; the failed set
// records the consumer→declarer edge. The window-law wall rejects a
// plugin-tier set from an early stop (catalog-tier passes from any stop). A
// post-load set (the boundary completed, or the behavior already applied
// mid-drain) dispatches the revert TOGGLE — revert(old)+implementation(new)
// for a `revert` declarer (skipping revert on a never-applied behavior),
// recording the new value; a revert-less behavior raises the teaching error.
// Main-thread inline; the off-thread command queue is a later step.
int Lua_Set(lua_State* L) {
    if (lua_type(L, 1) != LUA_TSTRING) {
        const char* detail =
            "kcdx.behavior.set(name, value): `name` (arg 1) must be a "
            "string — a bare name (resolved self > engine > other) or a "
            "full <author>.<plugin>.<bare> name.";
        LOG_ERROR_KV(kCat, "set_bad_arg",
            ::kcdx::log::KV("detail", detail));
        return luaL_error(L, "%s", detail);
    }
    const std::string nameArg = lua_tostring(L, 1);

    // nil (or a missing value) is the §4 teaching error — nil is the
    // engine's unset sentinel, never a value.
    const int valueType = lua_type(L, 2);
    if (valueType == LUA_TNIL || valueType == LUA_TNONE) {
        const std::string detail =
            "kcdx.behavior.set('" + nameArg + "', nil): nil is the engine's "
            "UNSET sentinel, never a value — to leave a behavior unset, "
            "don't set it (get() then answers the declarer's default). Any "
            "other Lua value (false included) is a valid setting.";
        LOG_ERROR_KV(kCat, "set_nil_value",
            ::kcdx::log::KV("name", nameArg),
            ::kcdx::log::KV("detail", detail));
        return luaL_error(L, "%s", detail.c_str());
    }

    // Resolve the caller identity first — both the discriminating
    // resolution error (the reorder branch names the consumer) and the
    // conflict warn / edge below need it.
    std::string callSiteFile;
    int callSiteLine = 0;
    kcdx::lua_registry::OwningPlugin owner =
        kcdx::lua_registry::OwningPluginForCurrentCall(
            L, callSiteFile, callSiteLine);

    // Resolve. On a miss, the DISCRIMINATING set-resolution error (design
    // §6's five branches) — NOT the generic get-style "no declarer" text:
    // a prefixed name keys off the owning plugin's load OUTCOME + order
    // (reorder / failed-load / disabled / engine-rejected / absent / typo);
    // a bare name with no declarer points at the full-name form. The failed
    // set also records the consumer→declarer edge (ordering feedback).
    const kcdx::behavior_registry::Behavior* resolved =
        kcdx::behavior_registry::ResolveForCaller(
            owner.author, owner.plugin, nameArg);
    if (!resolved) {
        return RaiseSetResolution(L, nameArg, owner);  // never returns
    }
    kcdx::behavior_registry::Behavior* b =
        kcdx::behavior_registry::LookupMutable(resolved->fullName);

    // The window law (design §6). A PLUGIN-tier behavior resolves only at
    // the MAIN stop — its declares come into existence when the declaring
    // plugin's main entry runs (the game-thread first-tick Lua wave, phase
    // >= EngineSubsystemsInit). A `set` from an EARLIER stop (a C++
    // kcdxPlugin_Load on the worker, phase < EngineSubsystemsInit; a future
    // lua_before slot) against a plugin-tier behavior is OUT-OF-WINDOW — a
    // loud teaching error, the timeline's own out-of-window law. CATALOG-tier
    // (kcdx.behavior.*) names are settable from ANY stop (the engine pack
    // declares them before any plugin runs), so this gate is plugin-tier
    // only. NOTE: the Lua binder is itself a MAIN-stop caller (this code runs
    // in the first-tick wave), so it never trips this gate today; the gate's
    // live trippers are the C++ early-stop caller (kcdxBehaviorInterface in
    // P2) and the deferred Lua early-stop (lua_before, P11 P5 / TD-0013). The
    // wall is built here so the law is enforced the moment an early-stop
    // caller exists; the post-boundary (too-LATE) case is the separate gate
    // below.
    if (resolved->tier == kcdx::behavior_registry::Tier::Plugin &&
        static_cast<int>(kcdx::init::Current()) <
            static_cast<int>(kcdx::init::InitPhase::EngineSubsystemsInit)) {
        const std::string detail =
            "kcdx.behavior.set('" + nameArg + "'): plugin behaviors resolve "
            "at the main stop — set '" + b->fullName + "' from your main "
            "entry (plugin.lua / lua_after / kcdxPlugin_PostGameLoad), not "
            "from an early stop (a C++ kcdxPlugin_Load). The declarer's "
            "behaviors do not exist yet at the early stop; engine catalog "
            "names (kcdx.behavior.*) are the only behaviors settable that "
            "early.";
        LOG_ERROR_KV(kCat, "set_out_of_window",
            ::kcdx::log::KV("name",   b->fullName),
            ::kcdx::log::KV("phase",  kcdx::init::Name(kcdx::init::Current())),
            ::kcdx::log::KV("detail", detail));
        return luaL_error(L, "%s", detail.c_str());
    }

    // The set window. After the boundary completed — or on a behavior the
    // boundary already applied (a mid-drain set from another implementation)
    // — this is a POST-LOAD set, the revert toggle contract (design §5.4),
    // main-thread inline (the Lua binder runs on the game main thread; the
    // off-thread command queue is a later step). The registry owns the
    // revert/implementation dispatch + the record update; the binder pins
    // the new value into a ref and hands it over (nil was already rejected
    // above, so this is always a real ref). The registry consumes the ref on
    // EVERY path — records it on success, releases it on any failure — so the
    // binder never releases it here.
    if (kcdx::behavior_registry::BoundaryCompleted() || b->applied) {
        lua_pushvalue(L, 2);
        const int newValueRef = luaL_ref(L, LUA_REGISTRYINDEX);
        std::string toggleErr;
        const bool ok = kcdx::behavior_registry::ApplyPostLoadToggle(
            L, b, newValueRef, toggleErr);
        if (ok) {
            return 0;  // toggled: revert(old)+implementation(new), recorded.
        }
        if (!toggleErr.empty()) {
            // A teaching-error disposition (a revert-less behavior): the
            // registry left the record untouched + released the new ref. Log
            // + raise at the author's set site (the consumer-misuse case).
            LOG_ERROR_KV(kCat, "set_post_load_no_revert",
                ::kcdx::log::KV("name", b->fullName),
                ::kcdx::log::KV("detail", toggleErr));
            return luaL_error(L, "%s", toggleErr.c_str());
        }
        // A declarer-code raise (revert/implementation): the registry already
        // logged it attributed to the DECLARER (not the setting consumer) and
        // applied the disposition. The set does not re-raise at the consumer's
        // call site — the failure is the declarer's, surfaced in the log.
        return 0;
    }

    // (caller identity `owner` was resolved above, before resolution.)

    // Conflict warn — a SECOND DIFFERENT plugin recording a DIFFERENT
    // value over a standing record: one teaching warn naming both plugins,
    // both values, and who won (the later — last-wins). A plugin re-setting
    // its own record, or a different plugin recording an equal value
    // (lua_rawequal — value identity, no metamethods), records silently.
    if (b->recordedRef != kcdx::behavior_registry::kNoRef) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, b->recordedRef);  // old at -1
        const std::string prevDesc = DescribeValue(L, -1);
        lua_pushvalue(L, 2);                                // new at -1
        const bool sameValue = lua_rawequal(L, -1, -2) != 0;
        lua_pop(L, 2);
        const bool samePlugin = (b->setterAuthor == owner.author &&
                                 b->setterPlugin == owner.plugin);
        if (!samePlugin && !sameValue) {
            LOG_WARN_KV(kCat, "set_conflict",
                ::kcdx::log::KV("behavior", b->fullName),
                ::kcdx::log::KV("earlier_plugin",
                    SetterLabel(b->setterAuthor, b->setterPlugin)),
                ::kcdx::log::KV("earlier_value", prevDesc),
                ::kcdx::log::KV("later_plugin",
                    SetterLabel(owner.author, owner.plugin)),
                ::kcdx::log::KV("later_value", DescribeValue(L, 2)),
                ::kcdx::log::KV("winner",
                    SetterLabel(owner.author, owner.plugin)),
                ::kcdx::log::KV("note",
                    "two plugins set the same behavior to different values; "
                    "the LATER plugin in load order wins (last-wins) and "
                    "its value is the one the apply boundary applies"));
        }
    }

    // Record: last-wins. Pin the new value, release the old record.
    lua_pushvalue(L, 2);
    const int newRef = luaL_ref(L, LUA_REGISTRYINDEX);
    if (b->recordedRef != kcdx::behavior_registry::kNoRef) {
        luaL_unref(L, LUA_REGISTRYINDEX, b->recordedRef);
    }
    b->recordedRef  = newRef;
    b->setterAuthor = owner.author;
    b->setterPlugin = owner.plugin;

    // The engine-tracked set-edge: consumer plugin -> the behavior it set
    // (in-memory; feeds the ordering errors + persistence of later steps).
    kcdx::behavior_registry::RecordEdge(
        owner.author, owner.plugin, b->fullName);
    return 0;
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
    lua_pushcfunction(L, Lua_Set);
    lua_setfield(L, -2, "set");
    lua_pushcfunction(L, Lua_Get);
    lua_setfield(L, -2, "get");
    lua_pushcfunction(L, Lua_List);
    lua_setfield(L, -2, "list");
    lua_setfield(L, kcdx_idx, "behavior");
}

}  // namespace kcdx::lua_bind_behavior
