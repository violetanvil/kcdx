#include "address_library.h"

#include <windows.h>  // GetModuleHandleW for the author-RVA WHGame base lookup

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "init_phase.h"     // KCDX_REQUIRE_PHASE — refdb-ready observability guard
#include "log.h"
#include "refdb.h"          // engine-seed address + signature resolution lives in refdb now

// Address library — the plugin-precedence surface ONLY.
//
// The compiled-in seed table (the legacy kEntries[] array, the kGV_* constant
// and the linear-scan accessors) was removed when refdb took ownership of the
// curated cache. This translation unit now owns:
//   - per-plugin aliases (kcdx.alias)
//   - the author-declared targets registry
//   - name validation (plugin name, author name, namespace components)
//   - bare-name collision warning
//   - the shared-name precedence walk (self > engine > other) and the
//     equivalent walk on signature lookup
//
// The engine-seed tier of every walk delegates to refdb:
//   - presence:  refdb::HasName(name)
//   - address:   refdb::ResolveAddrByName(name, ctx)
//   - signature: refdb::SignatureByName(name, ctx)
//
// The curated facts (per-version rva, per-version verified signature,
// supersession state, etc.) live in `reference.sqlite`, are bulk-resolved at
// the running game version once inside refdb::Open(), and live in refdb's
// in-memory hash maps from then on. address_library NEVER speaks SQL.

namespace kcdx::address_library {

namespace {

bool StrEq(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        ++a; ++b;
    }
    return *a == *b;
}

// Resolve WHGame.dll's load base for composing author-supplied RVAs.
// Author-declared targets of kind Rva carry a literal RVA into WHGame.dll;
// this is the only spot in address_library that needs the base directly
// (refdb composes its own RVA→VA at ResolveAddrByName boundary). Cached
// after the first non-null hit. Zero when WHGame.dll is not mapped.
uintptr_t WhgameBase() {
    static uintptr_t cached = 0;
    if (cached) return cached;
    HMODULE m = GetModuleHandleW(L"WHGame.dll");
    cached = reinterpret_cast<uintptr_t>(m);
    return cached;
}

// ---------------------------------------------------------------------------
// Shared-name resolution: self > engine > other, with
// a warn-once-per-session-per-colliding-bare-name diagnostic.
//
// LAUNCH-TIME ONLY. Every helper below is reached exclusively from
// ResolveByName / ResolveSignatureByName, which run during the launch-time
// registration/apply pass and NEVER from a hook-fire / per-frame path (the
// resolved address is cached in the binding). The g_warnedCollisions dedup
// set is likewise touched only at launch. Do NOT call any of these from a
// hooked function or runtime tick — that would defeat the resident-registry
// invariant documented at g_authorTargets.
// ---------------------------------------------------------------------------

// Forward declaration of the author-target registry — its definition (with
// the resident / never-read-at-runtime invariant comment) lives in the
// registry section lower in this TU. Both blocks are in the SAME unnamed
// namespace, so this declaration binds to that definition (internal linkage).
extern std::vector<AuthorTarget> g_authorTargets;

// True iff the engine seed (= refdb's cache) declares this name at all,
// independent of game_version / status. Collision detection is about
// namespace OCCUPANCY (who claims the name), not whether the row currently
// resolves: an entity that exists but is unverified still occupies the
// engine namespace and must count as a shadowed owner, so the author is
// taught to prefix. refdb::HasName never fires a warning — pure presence.
bool SeedHasName(const char* name) {
    if (!name || !name[0]) return false;
    return kcdx::refdb::HasName(std::string(name));
}

// Resolve the engine-seed address for a name. Delegates to refdb (which
// applies the same closest-match version row + supersession walk + state
// derivation it already runs internally; refdb's own deduped WARN on
// SUPERSEDED/DEPRECATED/UNVERIFIED fires correctly here because the engine
// seed seam is engine-internal — pluginHandle defaults to 0).
uintptr_t SeedResolveAddr(const char* name) {
    if (!name || !name[0]) return 0;
    return kcdx::refdb::ResolveAddrByName(std::string(name));
}

// Find the author target owned by (`author`, `plugin`) with this bare name,
// or nullptr. Matching is on the full triple (author, plugin, bare) — under
// the 2-dot model the same (plugin, bare) pair may exist under different
// authors. During the transition `author` may be "" on the caller side; we
// match exact-equal (an empty-author lookup finds an empty-author registered
// row, never a populated one — keeping legacy 1-dot rows separate from new
// 2-dot rows).
const AuthorTarget* FindAuthorTarget(const std::string& author,
                                     const std::string& plugin,
                                     const char* bareName) {
    for (const AuthorTarget& t : g_authorTargets) {
        if (t.author == author && t.pluginName == plugin &&
            t.bareName == bareName) {
            return &t;
        }
    }
    return nullptr;
}

// Find the author target owned by `plugin` (any author) with this bare name,
// used by the 2-segment legacy "<plugin>.<bare>" explicit form. Returns the
// FIRST match — under the 2-dot model two authors could publish the same
// plugin name, but in practice this only fires for legacy 1-dot rows where
// author is "" and the (plugin, bare) pair is the de-facto unique key.
const AuthorTarget* FindAuthorTargetByPlugin(const std::string& plugin,
                                             const char* bareName) {
    for (const AuthorTarget& t : g_authorTargets) {
        if (t.pluginName == plugin && t.bareName == bareName) return &t;
    }
    return nullptr;
}

// Find the FIRST author target with this bare name owned by some namespace
// OTHER than (`excludeAuthor`, `excludePlugin`), or nullptr. The calling
// plugin's own target is the "self" tier, resolved separately and never the
// "other".
const AuthorTarget* FindOtherAuthorTarget(const std::string& excludeAuthor,
                                          const std::string& excludePlugin,
                                          const char* bareName) {
    for (const AuthorTarget& t : g_authorTargets) {
        if (t.author == excludeAuthor && t.pluginName == excludePlugin) continue;
        if (t.bareName == bareName) return &t;
    }
    return nullptr;
}

// Resolve the address an author target locates, for the kinds resolvable
// DIRECTLY in this leaf module: Rva (locatorNum is the rva relative to
// WHGame.dll) and AddressId (locatorNum is a kcdx_id → refdb cache lookup).
// Pattern / TargetSymbol are NOT resolved here — turning them into a VA
// requires the patch engine / symbol table, and this module must not depend
// on them (the dependency runs the other way; see
// address-library.h FindResolvedAuthorTarget). Returns 0 for those two kinds.
//
// How a by-name Pattern/TargetSymbol author target reaches a real address:
// hook_chain::ResolveLocator, on a 0 from ResolveByName for an addressName,
// calls FindResolvedAuthorTarget; if the winner is a Pattern/TargetSymbol
// author target it feeds that target's locatorStr (the pattern string / symbol
// name) into the SAME patch::Resolve / symbol pipeline it already runs for a
// directly-set pattern / target_symbol locator. The address comes from THAT
// pipeline, not from here. We never fabricate a VA.
uintptr_t ResolveAuthorTargetAddr(const AuthorTarget& t) {
    switch (t.kind) {
        case AuthorLocatorKind::Rva: {
            if (t.locatorNum == 0) return 0;
            uintptr_t base = WhgameBase();
            if (!base) return 0;
            return base + static_cast<uintptr_t>(t.locatorNum);
        }
        case AuthorLocatorKind::AddressId:
            // The author target's locatorNum is a kcdx_id in the refdb cache.
            // refdb composes WHGame base + rva at lookup time; on a miss
            // (unknown id, no rva on the row, module not mapped) it returns 0
            // and the caller's existing fail-loud path fires.
            return kcdx::refdb::ResolveAddrById(t.locatorNum);
        case AuthorLocatorKind::Pattern:
        case AuthorLocatorKind::TargetSymbol:
        default:
            return 0;
    }
}

// Already-warned bare names this session (warn-once-per-session-per-name).
// Shared by ResolveByName + ResolveSignatureByName so a
// bare name that collides warns ONCE total, not once per function. Launch-time
// only (see the block comment above).
std::set<std::string> g_warnedCollisions;

// Emit the once-per-session collision warning for a bare name. `winnerTier` /
// `winnerOwner` describe who won by precedence; `shadowed` lists the other
// owners. The line teaches the fix: prefix the name you didn't declare.
// A PREFIXED reference never reaches here (callers only call this on a bare
// reference that occupied >1 of {self, engine, other}).
void WarnBareCollisionOnce(const char* bareName,
                           const char* winnerTier,
                           const std::string& winnerOwner,
                           const std::string& shadowed) {
    if (g_warnedCollisions.count(bareName)) return;
    g_warnedCollisions.insert(bareName);
    LOG_WARN_KV("NAMESPACE", "bare_name_collision",
        log::KV("name", bareName),
        log::KV("resolved_to", winnerTier),
        log::KV("winner", winnerOwner),
        log::KV("shadowed", shadowed),
        log::KV("fix",
            "a bare name that exists in more than one of {your plugin, the "
            "engine, another plugin} resolves self > engine > other; prefix "
            "the one you did not declare as \"<plugin>.<name>\" (or \"kcdx."
            "<name>\" for the engine seed) to pick it explicitly and silence "
            "this warning."));
}

// Render the full 2-dot owner display string for an author target. When
// `author` is empty (legacy 1-dot row), falls back to the bare plugin name —
// the format every existing log line already prints.
std::string OwnerDisplay(const std::string& author,
                         const std::string& plugin) {
    if (author.empty()) return plugin;
    return author + "." + plugin;
}

// Detect a bare-name collision (the name occupies >1 of {self, engine, other})
// and warn once if so. Called by both resolvers AFTER they pick a winner, so
// the winner tier is known. `selfHit` = the calling plugin owns it;
// `engineHit` = the seed declares it; `otherHit` = some other plugin owns it.
// Resolution proceeds by precedence regardless of the warn.
void MaybeWarnCollision(const char* bareName,
                        const std::string& owningAuthor,
                        const std::string& owningPlugin,
                        bool selfHit, bool engineHit, bool otherHit,
                        const AuthorTarget* otherTarget) {
    int occupants = (selfHit ? 1 : 0) + (engineHit ? 1 : 0) + (otherHit ? 1 : 0);
    if (occupants < 2) return;

    // Winner tier + owner, by precedence (self > engine > other).
    const char* winnerTier = selfHit ? "self" : (engineHit ? "engine" : "other");
    std::string winnerOwner =
        selfHit ? OwnerDisplay(owningAuthor, owningPlugin)
                : (engineHit ? std::string("kcdx")
                             : OwnerDisplay(otherTarget->author,
                                            otherTarget->pluginName));

    // List the shadowed owners (everyone the winner displaced).
    std::string shadowed;
    auto append = [&shadowed](const std::string& s) {
        if (!shadowed.empty()) shadowed += ", ";
        shadowed += s;
    };
    if (selfHit) {  // self won — engine and/or other are shadowed
        if (engineHit) append("kcdx (engine seed)");
        if (otherHit)  append(OwnerDisplay(otherTarget->author,
                                           otherTarget->pluginName));
    } else if (engineHit) {  // engine won — other is shadowed (self absent)
        if (otherHit) append(OwnerDisplay(otherTarget->author,
                                          otherTarget->pluginName));
    }
    WarnBareCollisionOnce(bareName, winnerTier, winnerOwner, shadowed);
}

// Split a possibly-qualified shared name into its dot-separated segments.
// `segments[0..count-1]` carry the pieces; count is 1, 2, or 3 (an empty name
// returns count == 0; 4-or-more-segment names overflow and return count == 4
// so callers can reject them as malformed). The engine parses on the dot —
// it is semantic, not convention.
//
// Under the 2-dot model:
//   count == 1 → BARE name; resolves by self > engine > other precedence.
//   count == 2 → 1-dot explicit form: <kcdx>.<seedname> for an engine seed,
//                or legacy <plugin>.<bare> for an author target whose author
//                is still empty (the transition state).
//   count == 3 → 2-dot explicit form: <author>.<plugin>.<bare>, the new
//                model's full plugin-export reference.
struct QualifiedName {
    std::string segments[3];
    int         count = 0;     // 0 = empty input; 4 = overflow (malformed)
};

QualifiedName SplitQualified(const char* name) {
    QualifiedName q;
    if (!name || !*name) return q;
    const char* start = name;
    for (const char* p = name; ; ++p) {
        if (*p == '.' || *p == '\0') {
            if (q.count < 3) {
                q.segments[q.count].assign(start, p);
            }
            ++q.count;
            if (*p == '\0') break;
            start = p + 1;
        }
    }
    return q;
}

// What a bare name resolved to, by self > engine > other precedence. Computed
// ONCE by ResolveBareWinner so ResolveByName and FindResolvedAuthorTarget share
// the SAME precedence decision AND the SAME once-per-session collision warn
// (the warn fires inside ResolveBareWinner, keyed by name, so a name that
// already warned from one caller does not double-warn from the other).
// `winner` names the tier; `authorTarget` is the winning
// author target when the winner is Self/Other, nullptr when Engine/None.
struct BareResolution {
    enum class Tier { None, Self, Engine, Other } winner = Tier::None;
    const AuthorTarget* authorTarget = nullptr;  // non-null iff Self/Other won
};

// Resolve a BARE name (no dot) by self > engine > other precedence and emit the
// once-per-session collision warn if the name occupies >1 tier. Shared by
// ResolveByName (which turns the winner into a VA) and FindResolvedAuthorTarget
// (which hands the winning author target to hook_chain for pattern/symbol
// routing). Launch-time only.
//
// `owningAuthor` may be "" during the in-progress namespace refactor — the
// self tier then matches an author-target registered with empty author + this
// plugin name, which is the legacy 1-dot row (preserving the observable
// resolution order the existing corpus relies on).
BareResolution ResolveBareWinner(const char* name,
                                 const std::string& owningAuthor,
                                 const std::string& owningPlugin) {
    const AuthorTarget* selfTarget =
        owningPlugin.empty() ? nullptr
                             : FindAuthorTarget(owningAuthor, owningPlugin,
                                                name);
    bool engineHit = SeedHasName(name);
    const AuthorTarget* otherTarget =
        FindOtherAuthorTarget(owningAuthor, owningPlugin, name);

    bool selfHit  = (selfTarget != nullptr);
    bool otherHit = (otherTarget != nullptr);

    MaybeWarnCollision(name, owningAuthor, owningPlugin,
                       selfHit, engineHit, otherHit, otherTarget);

    BareResolution r;
    if (selfTarget) {
        r.winner = BareResolution::Tier::Self;
        r.authorTarget = selfTarget;
    } else if (engineHit) {
        r.winner = BareResolution::Tier::Engine;
    } else if (otherTarget) {
        r.winner = BareResolution::Tier::Other;
        r.authorTarget = otherTarget;
    }
    return r;
}

}  // namespace

uintptr_t ResolveByName(const char* name,
                        const char* owningAuthor,
                        const char* owningPlugin) {
    // OBSERVABILITY GUARD: the engine-seed branch reads refdb's cache, which
    // is built inside refdb::Open() at the RefdbOpened init phase. A
    // ResolveByName reached before then would silently miss every engine-seed
    // name; KCDX_REQUIRE_PHASE makes that loud.
    KCDX_REQUIRE_PHASE(::kcdx::init::InitPhase::RefdbOpened);
    if (!name || !name[0]) return 0;
    const std::string author = owningAuthor ? owningAuthor : "";
    const std::string plugin = owningPlugin ? owningPlugin : "";

    // --- ALIAS substitution (per-plugin local handles) — BEFORE the
    // self > engine > other walk. If the calling plugin declared an alias by
    // this bare name, substitute its full target and resolve THAT. An alias is
    // a pure local add-on: it only fires when this plugin owns it, so it can
    // never shadow an engine name or another plugin's bare name.
    std::string aliased = ResolveAlias(author.c_str(), plugin.c_str(), name);
    const char* eff = aliased.empty() ? name : aliased.c_str();

    QualifiedName q = SplitQualified(eff);
    if (q.count == 0 || q.count > 3) return 0;

    // --- 3-segment EXPLICIT plugin-export reference:
    //     "<author>.<plugin>.<bare>" — the 2-dot model's full form. Never
    //     warns; resolves directly to the matching author target.
    if (q.count == 3) {
        const AuthorTarget* t =
            FindAuthorTarget(q.segments[0], q.segments[1],
                             q.segments[2].c_str());
        if (!t) return 0;
        return ResolveAuthorTargetAddr(*t);
    }

    // --- 2-segment EXPLICIT reference (legacy 1-dot form):
    //     "kcdx.<seedname>" → engine seed by the unprefixed engine name.
    //     "<plugin>.<bare>" → legacy author target (the row was registered
    //                          with empty author + plugin name == segments[0]).
    //     During the transition both shapes are accepted so the existing
    //     corpus (which has not adopted the 2-dot prefix yet) keeps resolving;
    //     once every author target has a populated author, the second case
    //     becomes a teaching error and only "kcdx.<seed>" stays legal here.
    //     Prefixed references never warn.
    if (q.count == 2) {
        if (q.segments[0] == "kcdx") {
            return SeedResolveAddr(q.segments[1].c_str());
        }
        const AuthorTarget* t =
            FindAuthorTargetByPlugin(q.segments[0], q.segments[1].c_str());
        if (!t) return 0;
        return ResolveAuthorTargetAddr(*t);
    }

    // --- BARE reference: resolve self > engine > other (shared decision). --
    BareResolution res = ResolveBareWinner(eff, author, plugin);
    switch (res.winner) {
        case BareResolution::Tier::Self:
        case BareResolution::Tier::Other:
            // An author target won. Rva / AddressId become a VA here; Pattern /
            // TargetSymbol return 0 (the caller asks FindResolvedAuthorTarget
            // and routes them through the patch/symbol pipeline — see header).
            return ResolveAuthorTargetAddr(*res.authorTarget);
        case BareResolution::Tier::Engine:
            return SeedResolveAddr(eff);
        case BareResolution::Tier::None:
            return 0;
    }
    return 0;
}

const AuthorTarget* FindResolvedAuthorTarget(const char* name,
                                             const char* owningAuthor,
                                             const char* owningPlugin) {
    if (!name || !name[0]) return nullptr;
    const std::string author = owningAuthor ? owningAuthor : "";
    const std::string plugin = owningPlugin ? owningPlugin : "";

    // --- ALIAS substitution (per-plugin local handles) — same as
    // ResolveByName, BEFORE the dot-split / precedence walk. Local handle,
    // never displaces another tier.
    std::string aliased = ResolveAlias(author.c_str(), plugin.c_str(), name);
    const char* eff = aliased.empty() ? name : aliased.c_str();

    QualifiedName q = SplitQualified(eff);
    if (q.count == 0 || q.count > 3) return nullptr;

    // --- 3-segment "<author>.<plugin>.<bare>": resolves directly to the
    //     matching author target. Never warns.
    if (q.count == 3) {
        return FindAuthorTarget(q.segments[0], q.segments[1],
                                q.segments[2].c_str());
    }

    // --- 2-segment legacy/1-dot explicit form:
    //     "kcdx.<rest>" is the engine seed (NOT an author target) → nullptr;
    //     "<plugin>.<rest>" resolves directly to that plugin's legacy
    //     author target (empty author + plugin == segments[0]) during the
    //     transition.
    if (q.count == 2) {
        if (q.segments[0] == "kcdx") return nullptr;
        return FindAuthorTargetByPlugin(q.segments[0], q.segments[1].c_str());
    }

    // --- BARE reference: SAME precedence + SAME collision-warn dedup as
    // ResolveByName (ResolveBareWinner is the single shared decision point).
    // Return the winning author target when Self/Other won; nullptr when the
    // engine seed won (a seed row is not an author target) or nothing matched.
    BareResolution res = ResolveBareWinner(eff, author, plugin);
    return res.authorTarget;  // non-null iff Self/Other won
}

void WarnBareCollisionShared(const char*        bareName,
                             const char*        winnerTier,
                             const std::string& winnerOwner,
                             const std::string& shadowed) {
    // Delegates to the file-local once-per-session helper so the symbol table
    // shares the SAME g_warnedCollisions dedup as the address-name resolver:
    // a bare name that already warned from either surface does not double-warn.
    WarnBareCollisionOnce(bareName, winnerTier, winnerOwner, shadowed);
}

namespace {

// Fetch the engine seed's signature for `name`. refdb stores per-entity
// verified_signature in its in-memory cache; the view returned by
// SignatureByName points at the cached std::string and is NUL-terminated
// (std::string::data == c_str). The cache is process-lifetime — the c-string
// stays valid for the rest of the session.
//
// Returns nullptr when the name is not in the cache (so the caller can fall
// through to the next precedence tier); "" when the name is in the cache but
// the row carries no verified signature (we never invent one).
const char* SeedSignature(const char* name) {
    if (!name || !name[0]) return nullptr;
    if (!kcdx::refdb::HasName(name)) return nullptr;
    std::string_view sv = kcdx::refdb::SignatureByName(std::string(name));
    return sv.data();  // string_view over a std::string is NUL-terminated.
}

}  // namespace

const char* ResolveSignatureByName(const char* name,
                                   const char* owningAuthor,
                                   const char* owningPlugin) {
    if (!name || !name[0]) return "";
    const std::string author = owningAuthor ? owningAuthor : "";
    const std::string plugin = owningPlugin ? owningPlugin : "";

    // --- ALIAS substitution (per-plugin local handles) — same as
    // ResolveByName, so the signature resolves from the SAME row the address
    // does. Keep `aliased` alive for the function: subsequent calls take
    // pointers / views into it.
    std::string aliased = ResolveAlias(author.c_str(), plugin.c_str(), name);
    const char* eff = aliased.empty() ? name : aliased.c_str();

    QualifiedName q = SplitQualified(eff);
    if (q.count == 0 || q.count > 3) return "";

    // --- 3-segment "<author>.<plugin>.<bare>" — direct lookup.
    if (q.count == 3) {
        const AuthorTarget* t =
            FindAuthorTarget(q.segments[0], q.segments[1],
                             q.segments[2].c_str());
        return t ? t->signature.c_str() : "";
    }

    // --- 2-segment legacy/1-dot explicit form. "kcdx.<rest>" → seed
    // signature; "<plugin>.<rest>" → legacy author target signature.
    if (q.count == 2) {
        if (q.segments[0] == "kcdx") {
            const char* s = SeedSignature(q.segments[1].c_str());
            return s ? s : "";
        }
        const AuthorTarget* t =
            FindAuthorTargetByPlugin(q.segments[0], q.segments[1].c_str());
        return t ? t->signature.c_str() : "";
    }

    // --- BARE reference: SAME order as ResolveByName (self > engine > other).
    // The signature must come from the SAME row the address came from, so the
    // ABI matches the resolved function. Share the collision dedup with
    // ResolveByName: a bare name that already warned there does not double-warn
    // here (first warn this session wins, keyed by the name).
    const AuthorTarget* selfTarget =
        plugin.empty() ? nullptr : FindAuthorTarget(author, plugin, eff);
    const char* engineSig = SeedSignature(eff);
    bool engineHit = (engineSig != nullptr);
    const AuthorTarget* otherTarget =
        FindOtherAuthorTarget(author, plugin, eff);

    bool selfHit  = (selfTarget != nullptr);
    bool otherHit = (otherTarget != nullptr);

    MaybeWarnCollision(eff, author, plugin,
                       selfHit, engineHit, otherHit, otherTarget);

    // (1) self, (2) engine, (3) other — the signature from the winning row.
    if (selfTarget) return selfTarget->signature.c_str();
    if (engineHit)  return engineSig;
    if (otherTarget) return otherTarget->signature.c_str();
    return "";
}

// ===========================================================================
// Author-declared targets — runtime registry (storage + validation).
// ===========================================================================

namespace {

// The runtime registry of author-declared targets.
//
// INVARIANT — launch-time populate; resident; never read at runtime.
// This vector is POPULATED ONCE at launch, during plugin discovery, via
// RegisterAuthorTarget(). After discovery it is RESIDENT and READ-ONLY for
// the rest of the process lifetime. It must NEVER be consulted on a
// hook-fire / runtime-hot path: resolution happens exactly ONCE during the
// apply pass (a LATER step wires that read), the resolved address is cached
// in the binding, and the registry is never touched again while the game
// runs. Treat any read of this from a hooked function or per-frame tick as a
// bug.
std::vector<AuthorTarget> g_authorTargets;

// Per-namespace alias map: "<owningAuthor>.<owningPlugin>" key (or just
// "<owningPlugin>" when the author is empty, for the in-progress refactor's
// legacy 1-dot rows) -> (short handle -> full target name). Same launch-time-
// populate / resident / never-read-at-runtime invariant as g_authorTargets —
// populated once during discovery by RegisterAlias(), read only during the
// apply pass (ResolveAlias, called at the top of name resolution). An alias
// resolves ONLY in its declaring namespace, so it can never shadow an engine
// name or another plugin's bare name (aliases are per-plugin local handles).
std::map<std::string, std::map<std::string, std::string>> g_aliases;

// Build the per-namespace alias-map key from the owning author + plugin. An
// empty author falls back to just "<plugin>" so legacy 1-dot rows registered
// before the corpus declared [plugin].author keep resolving the aliases the
// existing tests + plugin authors wrote.
std::string AliasOwnerKey(const std::string& author,
                          const std::string& plugin) {
    if (author.empty()) return plugin;
    return author + "." + plugin;
}

// True iff `c` is a legal char for a shared-name component: [a-z0-9_].
// Uppercase, '.', '-', and everything else are rejected (the dot is the
// reserved canonical separator the engine parses on).
bool IsNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

// Validate one shared-name COMPONENT (a plugin name OR a bare target name):
// non-empty, length 2..32, every char in [a-z0-9_]. `what` names the field
// in the teaching error ("plugin name" / "target name"). Does NOT apply the
// reserved-"kcdx" rule — that's plugin-name-specific, layered on by the
// caller. Returns false + fills outError on any violation.
// Validate a single namespace component (a single dot-separated piece of a
// qualified name). `maxLen` caps the length: 32 by default (the standard short
// handle limit — aliases, bare target names, etc., which are typed by hand and
// should stay terse), with [plugin].name overriding to 128 (engine-prefix
// author families like kcdx_builtin_* push longer naturally, and the runtime
// cost of a longer cap is nil — one strlen at launch-time discovery).
bool ValidateNameComponent(const char* name, const char* what,
                           std::string& outError,
                           size_t maxLen = 32) {
    if (!name || name[0] == '\0') {
        outError = std::string(what) +
                   " is empty — must be at least 2 chars of [a-z0-9_].";
        return false;
    }
    size_t len = 0;
    for (const char* p = name; *p; ++p) {
        ++len;
        if (!IsNameChar(*p)) {
            outError = std::string(what) + " \"" + name +
                       "\" has an illegal character — only lowercase "
                       "[a-z0-9_] is allowed (no uppercase, '.', '-', or "
                       "spaces). The dot is the reserved namespace separator.";
            return false;
        }
    }
    if (len < 2) {
        outError = std::string(what) + " \"" + name +
                   "\" is too short — must be at least 2 chars.";
        return false;
    }
    if (len > maxLen) {
        outError = std::string(what) + " \"" + name +
                   "\" is too long — must be at most " +
                   std::to_string(maxLen) + " chars.";
        return false;
    }
    return true;
}

}  // namespace

bool ValidatePluginName(const char* name, std::string& outError) {
    // Charset + length. [plugin].name uses a 128-char cap (raised from the
    // original 32): runtime cost of a longer bound is nil (one strlen + bounds
    // compare at launch-time discovery only), and the engine-prefix author
    // family (e.g. kcdx_builtin) pushes longer names naturally — an artificial
    // 32 cap would force every kcdx_builtin_* plugin to truncate its plugin
    // name to fit the author prefix, distorting plugin names to fit prefix
    // length. Short components (aliases, bare target names) keep the default
    // 32 cap they were always at.
    constexpr size_t kPluginNameMaxLen = 128;
    if (!ValidateNameComponent(name, "[plugin].name", outError,
                               kPluginNameMaxLen)) {
        return false;
    }
    // Reserved engine root: the exact value "kcdx" is the engine namespace;
    // any name starting "kcdx." would squat under the reserved root. Both are
    // a hard rejection ("kcdx.*" is reserved for the
    // engine; [plugin].name = \"kcdx\" is rejected).
    //
    // Note: a literal "kcdx." can't actually reach here as a single component
    // because '.' fails the charset check above; we still guard the prefix
    // explicitly so the intent — and the teaching message — is unambiguous.
    if (StrEq(name, "kcdx") ||
        (name[0] == 'k' && name[1] == 'c' && name[2] == 'd' &&
         name[3] == 'x' && name[4] == '.')) {
        outError =
            "[plugin].name \"" + std::string(name) +
            "\" is reserved — the \"kcdx\" namespace (and any \"kcdx.\" "
            "prefix) belongs to the engine. Pick your own short lowercase "
            "id.";
        return false;
    }
    return true;
}

bool ValidateAuthorName(const char* name, std::string& outError) {
    // Charset + length. [plugin].author shares the 128-char cap with
    // [plugin].name — both are namespace-prefix components in the 2-dot
    // <author>.<plugin>.<bare> shared-namespace model;
    // engine-author families (kcdx_builtin_*, etc.) push longer values
    // naturally, and the runtime cost of a longer cap is nil (one strlen at
    // launch-time discovery only). Short components (aliases, bare target
    // names — typed by hand) keep the default 32 cap.
    constexpr size_t kAuthorNameMaxLen = 128;
    if (!ValidateNameComponent(name, "[plugin].author", outError,
                               kAuthorNameMaxLen)) {
        return false;
    }
    // Reserved engine root: the exact value "kcdx" is the engine namespace;
    // any name starting "kcdx." would squat under the reserved root. Same
    // rule as ValidatePluginName — author is the leading namespace component
    // and obeys the same reserved-root constraint.
    //
    // Note: a literal "kcdx." can't actually reach here as a single component
    // because '.' fails the charset check above; we still guard the prefix
    // explicitly so the intent — and the teaching message — is unambiguous.
    if (StrEq(name, "kcdx") ||
        (name[0] == 'k' && name[1] == 'c' && name[2] == 'd' &&
         name[3] == 'x' && name[4] == '.')) {
        outError =
            "[plugin].author \"" + std::string(name) +
            "\" is reserved — the \"kcdx\" namespace (and any \"kcdx.\" "
            "prefix) belongs to the engine. Pick your own short lowercase "
            "id.";
        return false;
    }
    return true;
}

bool RegisterAuthorTarget(const char*       author,
                          const char*       pluginName,
                          const char*       bareName,
                          AuthorLocatorKind kind,
                          const char*       locatorStr,
                          uint64_t          locatorNum,
                          const char*       signature,
                          std::string&      outError) {
    // Validate the author (when non-empty) as the leading namespace
    // component — same charset / length / reserved-root rules as the
    // plugin name. An empty author is accepted
    // during the in-progress refactor (legacy 1-dot row); step 4 of the
    // refactor wires the real author through every binder and once the
    // corpus migrates the field becomes required.
    if (author && author[0] != '\0') {
        if (!ValidateAuthorName(author, outError)) {
            return false;
        }
    }
    // Validate the owning plugin name as a namespace prefix (charset, length,
    // reserved-root) — a bad prefix corrupts every shared name this plugin
    // exports (a bad prefix is a hard manifest rejection).
    if (!ValidatePluginName(pluginName, outError)) {
        return false;
    }
    // Validate the bare name as the final segment of `<author>.<plugin>.<name>`
    // — same [a-z0-9_], 2-32 component rule (the reserved-"kcdx" check is
    // prefix-only, so it does NOT apply to the bare name).
    if (!ValidateNameComponent(bareName, "target name", outError)) {
        return false;
    }

    // Validated — append. (Storage layer only: collision handling is the
    // resolver's job, not the registry's.)
    AuthorTarget t;
    t.author     = author ? author : "";
    t.pluginName = pluginName;
    t.bareName   = bareName;
    t.kind       = kind;
    t.locatorStr = locatorStr ? locatorStr : "";
    t.locatorNum = locatorNum;
    t.signature  = signature ? signature : "";
    g_authorTargets.push_back(std::move(t));
    return true;
}

size_t AuthorTargetCount() {
    return g_authorTargets.size();
}

bool RegisterAlias(const char*  owningAuthor,
                   const char*  owningPlugin,
                   const char*  shortName,
                   const char*  target,
                   std::string& outError) {
    // An alias is scoped to a plugin; an anonymous caller has no space to
    // declare it in.
    if (!owningPlugin || owningPlugin[0] == '\0') {
        outError =
            "kcdx.alias can only be called from a plugin script — the calling "
            "Lua chunk did not attribute to a [plugin] (anonymous console / pak "
            "Lua cannot declare an alias). An alias is a per-plugin local "
            "handle.";
        return false;
    }
    // The owning plugin name must be a legal namespace prefix (also rejects
    // the reserved "kcdx" root — a plugin named "kcdx" can't reach here, but
    // validate for a teaching message anyway).
    if (!ValidatePluginName(owningPlugin, outError)) {
        return false;
    }
    // The owning author, when non-empty, must obey the same charset / length
    // / reserved-root rules. Empty author is accepted during the in-progress
    // refactor (legacy 1-dot scope).
    if (owningAuthor && owningAuthor[0] != '\0') {
        if (!ValidateAuthorName(owningAuthor, outError)) {
            return false;
        }
    }
    // The short handle is referenced exactly like a bare name, so it obeys the
    // same [a-z0-9_] 2-32 component charset.
    if (!ValidateNameComponent(shortName, "alias name", outError)) {
        return false;
    }
    // The target must be non-empty. It can be a bare name or an explicit
    // "<author>.<plugin>.<name>" / "<plugin>.<name>"; we do NOT further
    // validate its shape here — it is re-resolved through the normal name
    // pipeline at the apply pass, which reports an unknown / malformed target
    // with full context then.
    if (!target || target[0] == '\0') {
        outError =
            std::string("kcdx.alias(\"") + shortName +
            "\", target): the target name is empty — pass the full name to "
            "alias, e.g. kcdx.alias(\"inv\", \"redmoon.open_inventory\").";
        return false;
    }
    std::string key = AliasOwnerKey(owningAuthor ? owningAuthor : "",
                                    owningPlugin);
    g_aliases[key][shortName] = target;
    return true;
}

std::string ResolveAlias(const char* owningAuthor,
                         const char* owningPlugin,
                         const char* name) {
    if (!owningPlugin || owningPlugin[0] == '\0' || !name || name[0] == '\0') {
        return {};
    }
    std::string key = AliasOwnerKey(owningAuthor ? owningAuthor : "",
                                    owningPlugin);
    auto pit = g_aliases.find(key);
    if (pit == g_aliases.end()) return {};
    auto ait = pit->second.find(name);
    if (ait == pit->second.end()) return {};
    return ait->second;
}

size_t AliasCount() {
    size_t n = 0;
    for (const auto& [plugin, m] : g_aliases) n += m.size();
    return n;
}

}  // namespace kcdx::address_library
