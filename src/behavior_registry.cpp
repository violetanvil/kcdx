// kcdx behavior registry — mechanism only. The unit contract is the header's
// doc-comment; the author-facing teaching errors are the binder's
// (lua_bind_behavior.cpp), except the duplicate-declare text, which lives
// here because the registry is the one place that knows the standing
// declarer.

#include "behavior_registry.h"

#include <algorithm>
#include <map>
#include <set>

extern "C" {
#include "lua.h"      // the boundary pass invokes implementation refs
#include "lauxlib.h"  // LUA_NOREF static_assert + luaL_unref on a raise
}

#include "address_library.h"  // ResolveAlias — the kcdx.alias substitution
#include "log.h"              // LOG_*_KV, ::kcdx::log::KV

namespace kcdx::behavior_registry {

static_assert(kNoRef == LUA_NOREF,
              "behavior_registry::kNoRef must mirror LUA_NOREF");

namespace {

// One ordered store, both tiers — stamped full name -> Behavior. Ordered so
// Enumerate is deterministic and prefix scans are a lower_bound walk.
// Function-local static: no init-order dependence on other TUs.
std::map<std::string, Behavior>& Store() {
    static std::map<std::string, Behavior> s;
    return s;
}

// Bare names already warned for a tier collision (warn once per session, per
// colliding name). Deliberately THIS surface's own dedup set — never shared
// with the address surface's: per-surface per-name-per-session is the law.
std::set<std::string>& WarnedBareCollisions() {
    static std::set<std::string> s;
    return s;
}

constexpr const char* kEngineRoot = "kcdx.behavior.";

// The recorded set-edges (consumer plugin -> behavior), first-recorded
// order, deduplicated at insert. Function-local static like Store().
std::vector<SetEdge>& EdgeStore() {
    static std::vector<SetEdge> s;
    return s;
}

// Flipped exactly once, at the end of RunApplyBoundary.
bool& BoundaryCompletedFlag() {
    static bool s = false;
    return s;
}

bool RegisterOne(Behavior&& b, std::string& errOut) {
    auto& store = Store();
    auto it = store.find(b.fullName);
    if (it != store.end()) {
        // Duplicate stamped full name: the FIRST declaration stands; the
        // SECOND declare is the error (a teaching text the binder raises at
        // the second declare's call site). Only producible as an
        // intra-plugin authoring bug or a catalog QA miss — the prefix is
        // engine-derived, so a plugin cannot declare under another plugin's
        // prefix.
        errOut = "'" + b.fullName + "' is already declared (by " +
                 it->second.DeclarerLabel() +
                 ") — duplicate declare of the same stamped full name. The "
                 "FIRST declaration stands and this second declare is "
                 "rejected; declare each behavior once.";
        return false;
    }
    // Declare sequence: ascending registration order IS declaring-plugin
    // load order (plugins execute sequentially in unified load order; a
    // plugin's declares are contiguous) — the boundary drain's order.
    static uint64_t nextSeq = 0;
    b.declareSeq = ++nextSeq;
    std::string key = b.fullName;  // copy the key BEFORE moving the value
    store.emplace(std::move(key), std::move(b));
    return true;
}

// Split `name` on '.' into segments (an empty segment stays an empty
// string — the caller rejects those).
std::vector<std::string> SplitDots(const std::string& name) {
    std::vector<std::string> segs;
    std::string cur;
    for (char c : name) {
        if (c == '.') {
            segs.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    segs.push_back(cur);
    return segs;
}

}  // namespace

std::string Behavior::DeclarerLabel() const {
    if (tier == Tier::Engine) return "kcdx";
    return declaringAuthor + "." + declaringPlugin;
}

bool DeclarePlugin(const std::string& author,
                   const std::string& plugin,
                   const std::string& bareName,
                   const std::string& description,
                   int defaultRef,
                   int implementationRef,
                   int revertRef,
                   std::string& errOut) {
    Behavior b;
    b.fullName         = author + "." + plugin + "." + bareName;
    b.bareName         = bareName;
    b.tier             = Tier::Plugin;
    b.declaringAuthor  = author;
    b.declaringPlugin  = plugin;
    b.description      = description;
    b.defaultRef       = defaultRef;
    b.implementationRef = implementationRef;
    b.revertRef        = revertRef;
    return RegisterOne(std::move(b), errOut);
}

bool DeclareEngine(const std::string& bareName,
                   const std::string& description,
                   int defaultRef,
                   int implementationRef,
                   int revertRef,
                   std::string& errOut) {
    Behavior b;
    b.fullName         = kEngineRoot + bareName;
    b.bareName         = bareName;
    b.tier             = Tier::Engine;
    b.description      = description;
    b.defaultRef       = defaultRef;
    b.implementationRef = implementationRef;
    b.revertRef        = revertRef;
    return RegisterOne(std::move(b), errOut);
}

const Behavior* Lookup(const std::string& fullName) {
    auto& store = Store();
    auto it = store.find(fullName);
    return (it == store.end()) ? nullptr : &it->second;
}

Behavior* LookupMutable(const std::string& fullName) {
    auto& store = Store();
    auto it = store.find(fullName);
    return (it == store.end()) ? nullptr : &it->second;
}

const Behavior* ResolveForCaller(const std::string& callerAuthor,
                                 const std::string& callerPlugin,
                                 const std::string& nameArg) {
    if (nameArg.empty()) return nullptr;

    // Alias substitution FIRST (the caller's kcdx.alias local handles, via
    // the address-library alias store) — the same top-of-resolution contract
    // the address surface uses: a matching handle substitutes its full
    // target, which then resolves as a normal name (a substituted
    // <author>.<plugin>.<bare> triple takes the 3-segment exact-lookup path
    // below).
    const std::string aliased = kcdx::address_library::ResolveAlias(
        callerAuthor.c_str(), callerPlugin.c_str(), nameArg.c_str());
    const std::string& name = aliased.empty() ? nameArg : aliased;

    const std::vector<std::string> segs = SplitDots(name);
    for (const std::string& s : segs) {
        if (s.empty()) return nullptr;  // a ".."/leading/trailing-dot form
    }

    if (segs.size() == 3) {
        // Explicit form — exact stamped-name lookup. Covers both
        // <author>.<plugin>.<bare> and the engine kcdx.behavior.<bare>.
        return Lookup(name);
    }
    if (segs.size() != 1) {
        // 2-segment / 4+-segment: no behavior interpretation exists.
        return nullptr;
    }

    // Bare 1-segment: self > engine > other (first other in stamped-name
    // order). ALL THREE tier hits are computed BEFORE any return so a bare
    // name occupying >=2 of {self, engine, other} warns regardless of which
    // tier wins — the canonical shared-namespace collision model (the
    // address surface's MaybeWarnCollision). Once per session per name;
    // resolution proceeds by precedence regardless of the warn.
    const std::string& bare = segs[0];

    const Behavior* self = nullptr;
    if (!callerAuthor.empty() && !callerPlugin.empty()) {
        self = Lookup(callerAuthor + "." + callerPlugin + "." + bare);
    }
    const Behavior* engine = Lookup(kEngineRoot + bare);
    const Behavior* other = nullptr;
    for (const auto& kv : Store()) {
        const Behavior& b = kv.second;
        if (b.bareName != bare) continue;
        if (b.tier == Tier::Engine) continue;  // the engine tier, above
        if (b.declaringAuthor == callerAuthor &&
            b.declaringPlugin == callerPlugin) {
            continue;  // the self tier, above
        }
        other = &b;  // first in stamped-name order — the tier's winner
        break;
    }

    const int occupants =
        (self ? 1 : 0) + (engine ? 1 : 0) + (other ? 1 : 0);
    if (occupants >= 2 && WarnedBareCollisions().insert(bare).second) {
        const Behavior* winner = self ? self : (engine ? engine : other);
        const char* winnerTier =
            self ? "self" : (engine ? "engine" : "other");
        std::string shadowed;
        auto append = [&shadowed](const std::string& s) {
            if (!shadowed.empty()) shadowed += ", ";
            shadowed += s;
        };
        if (self) {  // self won — engine and/or other are shadowed
            if (engine) append("kcdx (engine catalog)");
            if (other)  append(other->DeclarerLabel());
        } else if (engine) {  // engine won — other is shadowed (self absent)
            if (other) append(other->DeclarerLabel());
        }
        LOG_WARN_KV("BEHAVIOR", "bare_name_collision",
            ::kcdx::log::KV("name", bare),
            ::kcdx::log::KV("resolved_to", winnerTier),
            ::kcdx::log::KV("winner", winner->DeclarerLabel()),
            ::kcdx::log::KV("shadowed", shadowed),
            ::kcdx::log::KV("fix",
                "a bare behavior name that exists in more than one of {your "
                "plugin, the engine catalog, another plugin} resolves self > "
                "engine > other; use the full <author>.<plugin>.<bare> form "
                "(or kcdx.behavior.<bare> for the engine catalog) to pick "
                "one explicitly and silence this warning"));
    }

    if (self)   return self;
    if (engine) return engine;
    return other;
}

void Enumerate(const std::string& prefix,
               std::vector<const Behavior*>& out) {
    auto& store = Store();
    auto it = prefix.empty() ? store.begin() : store.lower_bound(prefix);
    for (; it != store.end(); ++it) {
        if (!prefix.empty() &&
            it->first.compare(0, prefix.size(), prefix) != 0) {
            break;  // ordered map: past the prefix range
        }
        out.push_back(&it->second);
    }
}

size_t Count() {
    return Store().size();
}

void RecordEdge(const std::string& consumerAuthor,
                const std::string& consumerPlugin,
                const std::string& behaviorFullName) {
    if (consumerPlugin.empty()) return;  // anonymous setter — no consumer
    auto& edges = EdgeStore();
    for (const SetEdge& e : edges) {
        if (e.consumerAuthor == consumerAuthor &&
            e.consumerPlugin == consumerPlugin &&
            e.behaviorFullName == behaviorFullName) {
            return;  // already recorded (a re-set adds no new edge)
        }
    }
    edges.push_back(SetEdge{consumerAuthor, consumerPlugin, behaviorFullName});
}

const std::vector<SetEdge>& Edges() {
    return EdgeStore();
}

bool BoundaryCompleted() {
    return BoundaryCompletedFlag();
}

void RunApplyBoundary(lua_State* L) {
    auto& store = Store();
    size_t appliedCount = 0;
    size_t raisedCount  = 0;
    size_t passCount    = 0;

    // Once-per-boundary invocation guard. `applied` alone cannot carry it:
    // a raise clears `applied` (the truthfulness rule), and without this
    // guard two mutually-setting raising implementations could re-pend each
    // other forever. Once invoked, a behavior is never invoked again this
    // boundary — the invariant that makes the drain terminate.
    std::set<const Behavior*> invoked;

    for (;;) {
        // Snapshot this pass's pending set — set-but-not-applied, not yet
        // invoked — in declaring-plugin load order (ascending declareSeq).
        // Late entries pended by implementations land in the NEXT snapshot.
        std::vector<Behavior*> pass;
        for (auto& kv : store) {
            Behavior& b = kv.second;
            if (b.recordedRef != kNoRef && !b.applied &&
                invoked.find(&b) == invoked.end()) {
                pass.push_back(&b);
            }
        }
        if (pass.empty()) break;
        std::sort(pass.begin(), pass.end(),
                  [](const Behavior* a, const Behavior* b) {
                      return a->declareSeq < b->declareSeq;
                  });
        ++passCount;

        for (Behavior* b : pass) {
            // Defensive re-check with NO current trigger: a raise clears only
            // the raising behavior's OWN record, so nothing in this step can
            // invalidate a sibling entry mid-pass. Kept because the s5
            // post-load toggle paths will mutate records around drains.
            if (b->recordedRef == kNoRef || b->applied) continue;

            invoked.insert(b);
            // Applied flips BEFORE the invoke: a mid-call set on this
            // behavior (incl. a self-set from its own implementation)
            // follows the post-load rules, and a success keeps the recorded
            // value as exactly the value the implementation received.
            b->applied = true;

            const int top0 = lua_gettop(L);
            lua_rawgeti(L, LUA_REGISTRYINDEX, b->implementationRef);
            lua_rawgeti(L, LUA_REGISTRYINDEX, b->recordedRef);
            const int status = lua_pcall(L, 1, 0, 0);
            if (status != 0) {
                const char* msg = lua_tostring(L, -1);
                // Boundary-raise disposition (design §5.3): attributed to
                // the DECLARING plugin; recorded value AND applied flag
                // clear to unset so get() answers the default (truthful —
                // the intended state was not applied); the drain CONTINUES.
                LOG_ERROR_KV("BEHAVIOR", "implementation_raised",
                    ::kcdx::log::KV("behavior", b->fullName),
                    ::kcdx::log::KV("declarer", b->DeclarerLabel()),
                    ::kcdx::log::KV("error", msg ? msg : "<non-string error>"),
                    ::kcdx::log::KV("disposition",
                        "recorded value cleared to unset (get() returns the "
                        "default); remaining behaviors still apply"));
                lua_settop(L, top0);
                luaL_unref(L, LUA_REGISTRYINDEX, b->recordedRef);
                b->recordedRef = kNoRef;
                b->applied = false;
                b->setterAuthor.clear();
                b->setterPlugin.clear();
                ++raisedCount;
                continue;
            }
            lua_settop(L, top0);
            ++appliedCount;
        }
    }

    // A record re-pended onto a raise-cleared behavior (only another
    // implementation can produce it) has no remaining boundary slot —
    // clear it so get() never carries a value no implementation received.
    for (auto& kv : store) {
        Behavior& b = kv.second;
        if (b.recordedRef != kNoRef && !b.applied) {
            LOG_WARN_KV("BEHAVIOR", "post_raise_set_dropped",
                ::kcdx::log::KV("behavior", b.fullName),
                ::kcdx::log::KV("setter",
                    b.setterAuthor + "." + b.setterPlugin),
                ::kcdx::log::KV("detail",
                    "set during the boundary drain on a behavior whose "
                    "implementation had already raised this boundary; the "
                    "value is cleared (the implementation is invoked at "
                    "most once per boundary) — get() returns the default"));
            luaL_unref(L, LUA_REGISTRYINDEX, b.recordedRef);
            b.recordedRef = kNoRef;
            b.setterAuthor.clear();
            b.setterPlugin.clear();
        }
    }

    BoundaryCompletedFlag() = true;
    // One lifecycle info line (logging.md): the boundary completed.
    LOG_INFO_KV("BEHAVIOR", "apply_boundary_complete",
        ::kcdx::log::KV("applied", appliedCount),
        ::kcdx::log::KV("raised", raisedCount),
        ::kcdx::log::KV("passes", passCount),
        ::kcdx::log::KV("declared", store.size()));
}

}  // namespace kcdx::behavior_registry
