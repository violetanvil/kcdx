// kcdx behavior registry — mechanism only. The unit contract is the header's
// doc-comment; the author-facing teaching errors are the binder's
// (lua_bind_behavior.cpp), except the duplicate-declare text, which lives
// here because the registry is the one place that knows the standing
// declarer.

#include "behavior_registry.h"

#include <map>
#include <set>

extern "C" {
#include "lauxlib.h"  // LUA_NOREF (static_assert below only — no state use)
}

#include "address_library.h"  // ResolveAlias — the kcdx.alias substitution
#include "log.h"              // LOG_WARN_KV, ::kcdx::log::KV

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

}  // namespace kcdx::behavior_registry
