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
// model), prefix-filtered enumeration, the recorded-value slot, and the
// applied flag. The apply-boundary pass, `set`, edges, and the post-load
// toggle path land on this same unit in later steps.
//
// VALUE MODEL: behavior values (default, recorded, implementation, revert)
// live in the engine-owned Lua VM as registry references (luaL_ref into
// LUA_REGISTRYINDEX) — NEVER copied out, NEVER a kcdx-side static-const
// sentinel (the dual-Lua sentinel hazard). This unit stores the integer refs
// + metadata only; it does not touch a lua_State. Callers (the Lua binder
// now; the C++ interface later) create and dereference the refs on the VM.
// kNoRef marks an absent ref (no revert; recorded value unset — nil is the
// engine's unset sentinel and is never stored as a value).
//
// INVARIANT (threading): all access is on the game main thread — every
// current caller is a Lua binder invoked from plugin scripts (main-thread by
// the callback-threading law), and the engine-tier declare path runs from the
// catalog load on the same thread. The C++ tier's thread contract keeps
// queries main-thread; when the command queue lands, the queued path
// marshals to the main thread before touching this registry. No lock here —
// adding one unasked would paper over a threading-contract violation instead
// of failing loud at the contract.

#include <cstddef>
#include <string>
#include <vector>

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
    // default). Written by `set` (a later step); the slot ships now so the
    // registry shape is the final one.
    int recordedRef = kNoRef;

    // Applied flag: the implementation has run for the current recorded
    // value. Written by the apply-boundary pass (a later step).
    bool applied = false;

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

}  // namespace kcdx::behavior_registry
