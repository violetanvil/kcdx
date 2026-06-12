#pragma once

// kcdx behavior registry — the ONE runtime registry both behavior tiers share.
//
// UNIT CONTRACT (this header is the unit's reference doc):
//
// A *behavior* is a named, settable unit of intent: a value plus the
// declarer's implementation that reconfigures the game to match it, under the
// engine's apply contract. Two tiers register through this one registry and
// one code path:
//   - plugin-declared:  stamped `<author>.<plugin>.<bare>` (the engine derives
//     the prefix from the declaring plugin's manifest — the author never types
//     their own prefix);
//   - engine catalog:   stamped `kcdx.behavior.<bare>` (the reserved engine
//     root; the catalog loader is the only caller of the engine-tier declare
//     path — structurally present now, nothing calls it until the catalog
//     phase lands).
//
// This unit OWNS, per behavior: the declare registration (with
// duplicate-same-full-name rejection — the FIRST declaration stands, the
// second is the error), exact full-name lookup, bare/prefixed name resolution
// (self > engine > other precedence, dot-semantic — the shared-namespace
// model), prefix-filtered enumeration, the recorded-value slot, the applied
// flag, the in-memory set-edges (consumer plugin -> behavior, feeding the
// ordering errors + persistence of later steps), the APPLY-BOUNDARY pass
// (RunApplyBoundary — the one worklist drain that invokes each set
// behavior's implementation once with the final recorded value), and the
// POST-LOAD TOGGLE path (ApplyPostLoadToggle — the revert/implementation
// dispatch for a `revert` declarer, main-thread inline; the off-thread
// command queue is a later step).
//
// VALUE MODEL: behavior values (default, recorded, implementation, revert)
// live in the engine-owned Lua VM as registry references (luaL_ref into
// LUA_REGISTRYINDEX) — NEVER copied out, NEVER a kcdx-side static-const
// sentinel (the dual-Lua sentinel hazard). This unit stores the integer refs
// + metadata; storage never dereferences a ref. Callers (the Lua binder
// now; the C++ interface later) create and dereference the refs on the VM —
// with TWO exceptions: RunApplyBoundary takes the caller's lua_State to
// invoke each implementation ref (the boundary pass is this unit's job),
// and on a boundary raise it releases the cleared recorded ref; and
// ApplyPostLoadToggle takes both the caller's lua_State and a caller-pinned
// new-value ref to pcall the revert/implementation refs, releases the OLD
// recorded ref it replaces, and on a clearing failure disposition releases
// the cleared ref (the toggle path is this unit's job too). kNoRef
// marks an absent ref (no revert; recorded value unset — nil is the
// engine's unset sentinel and is never stored as a value).
//
// INVARIANT (threading): all access is on the game main thread — every
// current caller is a Lua binder invoked from plugin scripts (main-thread by
// the callback-threading law), the apply boundary runs inside the first-tick
// block (game main thread), and the engine-tier declare path runs from the
// catalog load on the same thread. The C++ tier's thread contract keeps
// queries main-thread; when the command queue lands, the queued path
// marshals to the main thread before touching this registry. No lock here —
// adding one unasked would paper over a threading-contract violation instead
// of failing loud at the contract.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Forward declaration so this header stays free of Lua includes; only
// RunApplyBoundary takes a state (see the value-model note above).
struct lua_State;

namespace kcdx::behavior_registry {

// Mirrors LUA_NOREF (static_assert'd in the .cpp) so this header stays free
// of Lua includes — the registry stores refs, it never dereferences them.
constexpr int kNoRef = -2;

enum class Tier {
    Plugin,  // declared by a plugin; full name <author>.<plugin>.<bare>
    Engine,  // declared by the engine catalog; full name kcdx.behavior.<bare>
};

struct Behavior {
    std::string fullName;    // the stamped name — the registry key
    std::string bareName;    // the declarer-written bare component
    Tier        tier = Tier::Plugin;

    // Declarer identity. Plugin tier: the manifest (author, plugin) pair.
    // Engine tier: both empty; DeclarerLabel() reads "kcdx".
    std::string declaringAuthor;
    std::string declaringPlugin;

    std::string description;  // one human line; surfaced by list()

    // Lua-registry refs (LUA_REGISTRYINDEX). default/implementation are
    // always real refs; revert is kNoRef when the spec omitted it.
    int defaultRef        = kNoRef;
    int implementationRef = kNoRef;
    int revertRef         = kNoRef;

    // The recorded-value slot. kNoRef = never set (get() answers with the
    // default). Written by the binder's load-window `set` (last-wins); a
    // boundary raise clears it back to kNoRef (get() answers the default —
    // truthful: the intended state was not applied).
    int recordedRef = kNoRef;

    // Applied flag: the implementation has run for the current recorded
    // value. Flipped by the apply-boundary pass BEFORE the implementation
    // is invoked (so a mid-call set on this behavior follows the post-load
    // rules and a success keeps the recorded value as the applied value);
    // cleared back to false on a boundary raise.
    bool applied = false;

    // Last-setter identity (the load-window record path; both empty =
    // never set, or an anonymous setter). Feeds the set-conflict warn.
    std::string setterAuthor;
    std::string setterPlugin;

    // Monotonic registration order. Plugin entries execute sequentially in
    // unified load order and a plugin's declares are contiguous, so
    // ascending declareSeq IS declaring-plugin load order — the boundary
    // drain's invocation order (design §5.3). Assigned at declare.
    uint64_t declareSeq = 0;

    // Value generation — bumped EVERY time the value a get() would answer
    // changes (the load-window set, the post-load toggle success, the
    // boundary-raise clear). The C++ value-handle model (design §8) reads it:
    // a handle minted by Get() captures (fullName, generation); an accessor
    // whose behavior's generation has since advanced is STALE — a
    // generation-checked teaching error, never a dangle into a replaced ref.
    // Monotonic, never reset; starts at 1 so a never-minted 0 handle is always
    // stale-by-construction.
    uint64_t valueGeneration = 1;

    // "author.plugin" for the plugin tier; "kcdx" for the engine catalog.
    std::string DeclarerLabel() const;
};

// Register a plugin-tier declare. The registry stamps
// `<author>.<plugin>.<bare>` and stores the behavior. Returns false + a
// teaching error in `errOut` on a duplicate stamped full name — the FIRST
// declaration stands, the registry is unchanged, and the caller raises the
// error against the SECOND declare. The caller validates the spec fields and
// owns the refs it passes (on a false return the caller releases them).
bool DeclarePlugin(const std::string& author,
                   const std::string& plugin,
                   const std::string& bareName,
                   const std::string& description,
                   int defaultRef,
                   int implementationRef,
                   int revertRef,  // kNoRef when absent
                   std::string& errOut);

// Engine-tier declare: stamps `kcdx.behavior.<bare>`. Same duplicate
// contract as DeclarePlugin. Structurally present for the catalog loader;
// nothing calls it until the catalog phase.
bool DeclareEngine(const std::string& bareName,
                   const std::string& description,
                   int defaultRef,
                   int implementationRef,
                   int revertRef,
                   std::string& errOut);

// Exact stamped-full-name lookup. nullptr on a miss. The returned pointer is
// stable for the session (behaviors are never removed).
const Behavior* Lookup(const std::string& fullName);

// Mutable exact lookup for the write path (the binder's set record, the
// boundary pass). Same stable-pointer contract as Lookup.
Behavior* LookupMutable(const std::string& fullName);

// Resolve a name argument for the calling plugin (callerAuthor/callerPlugin
// may be empty for an anonymous caller). Alias substitution runs FIRST (the
// caller's kcdx.alias local handles, via the address-library alias store): a
// matching handle substitutes its full target, which then resolves as a
// normal name. Then:
//   - 1 segment (bare): self (<caller>.<bare>) > engine (kcdx.behavior.<bare>)
//     > other (any other plugin's <bare>, first in stamped-name order) — the
//     shared-namespace precedence. All three tier hits are computed before
//     resolving; a bare name occupying >=2 of {self, engine, other} warns
//     once per session per name (the warn names the winner tier + owner,
//     the shadowed owners, and teaches the prefixed form); resolution still
//     returns the precedence winner.
//   - 3 segments: the explicit form — an exact full-name lookup (covers both
//     `<author>.<plugin>.<bare>` and the engine `kcdx.behavior.<bare>`).
//   - any other segment count / an empty segment: no interpretation — nullptr.
// nullptr when nothing matches.
const Behavior* ResolveForCaller(const std::string& callerAuthor,
                                 const std::string& callerPlugin,
                                 const std::string& nameArg);

// Enumerate behaviors (both tiers, one registry) whose stamped full name
// starts with `prefix` (empty prefix = all), in stamped-name order.
void Enumerate(const std::string& prefix,
               std::vector<const Behavior*>& out);

// Number of registered behaviors (both tiers).
size_t Count();

// One engine-tracked set-edge: a resolved set's consumer plugin -> the
// behavior it set (the declarer is derivable from the behavior). In-memory
// this step; feeds the ordering errors + the persisted-edge store of later
// steps.
struct SetEdge {
    std::string consumerAuthor;
    std::string consumerPlugin;
    std::string behaviorFullName;
};

// Record a consumer->declarer set-edge (deduplicated; an anonymous setter —
// empty plugin — records nothing, there is no consumer plugin to order).
void RecordEdge(const std::string& consumerAuthor,
                const std::string& consumerPlugin,
                const std::string& behaviorFullName);

// The recorded set-edges, in first-recorded order.
const std::vector<SetEdge>& Edges();

// True once the apply boundary has completed. The binder's set window gate:
// a set after this point — like a set on an already-applied behavior during
// the drain — follows the post-load rules (ApplyPostLoadToggle: a `revert`
// declarer toggles; a revert-less behavior gets the teaching error).
bool BoundaryCompleted();

// The APPLY BOUNDARY — the one worklist drain, run ONCE per session from the
// first-tick block (game main thread — every implementation runs on the
// thread the callback-threading law requires; no marshal needed), after
// RunPostGameLoad returns and before kcdxMessage_InputLoaded fires:
//
//   - Every set-but-not-applied behavior's implementation is invoked ONCE
//     with the final recorded value (lua_pcall), in declaring-plugin load
//     order (ascending declareSeq). Never-set behaviors are SKIPPED (the
//     default is a get() answer, not an applied state).
//   - The drain LOOPS until no pending entry remains: an implementation
//     setting a not-yet-applied behavior updates its pending value
//     (last-wins continues); one setting a behavior whose slot already
//     passed pends it for a follow-up pass.
//   - EACH BEHAVIOR IS INVOKED AT MOST ONCE PER BOUNDARY. The applied flag
//     flips BEFORE the invoke, so a mid-drain set on an applied behavior
//     follows the post-load rules; a raise-cleared behavior is never
//     re-invoked this boundary (the once-per-boundary invariant is what
//     makes the drain terminate), and a record re-pended onto it after its
//     raise is cleared at drain end so get() never carries an unapplied
//     value.
//   - A raise: LOG_ERROR attributed to the DECLARING plugin, that
//     behavior's recorded value AND applied flag cleared to unset (get()
//     answers the default — truthful), the drain CONTINUES.
//
// Completion emits one lifecycle info line (applied/raised counts) and
// flips BoundaryCompleted().
void RunApplyBoundary(lua_State* L);

// The POST-LOAD TOGGLE — a `set` that arrives AFTER the apply boundary (or on
// an already-applied behavior mid-drain) on the GAME MAIN THREAD. The binder
// resolves + validates the name, rejects nil, then pins the NEW value into a
// registry ref and hands it here; this unit owns the revert/implementation
// dispatch + the record update. `newValueRef` is a real ref the binder created
// (never kNoRef — nil was rejected upstream); on EVERY return path this unit
// either records it (success) or releases it (a failure that does not record),
// so the binder transfers ownership of the ref and never releases it itself.
//
// Returns true on a successful toggle (recorded value now == the new value,
// applied flag stays true). Returns false on a teaching-error disposition (a
// revert-less behavior — the recorded value is UNCHANGED, the new ref
// released, `errOut` carries the teaching text the binder raises) OR a
// declarer-code raise (the record cleared or kept per the two failure rules
// below, the error logged HERE attributed to the declarer, `errOut` empty —
// the binder does not re-raise a declarer-code failure at the consumer's
// call site, mirroring the boundary-raise's attribute-to-declarer rule).
// The binder distinguishes the two false cases by `errOut`: non-empty = raise
// it at the set site; empty = the declarer-code failure already logged, the
// set returns without raising.
//
//   - `revert` declarer + behavior WAS applied → revert(old_value) then
//     implementation(new_value), then record the new value (get() tracks).
//   - `revert` declarer + behavior NEVER applied (applied == false: nothing
//     set it at load, so the boundary skipped it) → SKIP revert, call
//     implementation(new_value) only, then record. revert is never handed a
//     state the implementation did not create.
//   - NO `revert` declarer (revertRef == kNoRef) → a teaching error; the
//     recorded value does NOT change (get() never lies). `errOut` carries it.
//
// Failure dispositions (logged HERE, attributed to the DECLARER):
//   - revert(old) succeeds, implementation(new) raises → recorded value AND
//     applied flag cleared to unset (the world is in the reverted state, get()
//     returns the default — truthful), the OLD ref released.
//   - revert(old) ITSELF raises → recorded value and applied state stay AS
//     THEY WERE (the engine cannot know how far a failed revert got, so the
//     standing record is the least-lying one); the NEW ref released.
bool ApplyPostLoadToggle(lua_State* L,
                         Behavior* b,
                         int newValueRef,
                         std::string& errOut);

// === The C++ value-handle seam (behavior design §8) ===
//
// The C++ interface (src/behavior_interface.cpp) calls INTO these. The Lua
// binder is the main-stop surface that owns its own record path inline; the
// C++ interface routes its load-window record + its value reads through the
// registry so the value model (refs on the one VM, the generation counter) has
// ONE owner. None of these MARSHALS a value out — the C++ side derefs the ref
// returned here ON the live VM (main thread), the registry never copies a value.

// Record a load-window set from the C++ surface (last-wins), the registry-owned
// mirror of the binder's inline load record. `newValueRef` is a real ref the
// caller pinned (nil rejected upstream); the registry releases the OLD recorded
// ref it replaces, stores the new one (transferring ownership), records the
// setter identity, and BUMPS valueGeneration (so any outstanding C++ value
// handle on this behavior goes stale). The caller has already resolved the
// window law + the post-load/boundary gate (a load-window set only). Records no
// edge — the caller records the consumer->declarer edge (it owns the consumer
// identity).
void RecordLoadSet(lua_State* L,
                   Behavior* b,
                   int newValueRef,
                   const std::string& setterAuthor,
                   const std::string& setterPlugin);

// The ref a get() would answer for `b` right now — the recorded value if set,
// else the declarer's default. NEVER kNoRef (default is always a real ref). The
// C++ Get path mints a handle against (b->fullName, b->valueGeneration) + this
// ref; the accessor re-reads the generation to detect staleness, then
// lua_rawgeti's this ref on the live VM. A pure read — no mutation, no generation
// bump.
int CurrentValueRef(const Behavior* b);

// True once the apply boundary completed AND we are past the load waves — the
// C++ query thread-wall's "post-load" determinant (design §8). The C++ interface
// pairs this with the live-VM + main-thread checks to decide whether an
// off-thread query is the out-of-window teaching-error case. Mirror of
// BoundaryCompleted() (named for the query-wall caller's intent).
inline bool PostLoad() { return BoundaryCompleted(); }

}  // namespace kcdx::behavior_registry
