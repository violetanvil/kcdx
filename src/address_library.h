#pragma once

#include <cstdint>
#include <string>

// Address Library — id-to-RVA lookup compiled into kcdx.dll.
//
// Plugins call api->ResolveAddress(id) to get a runtime VA for a
// known function/data entry without hardcoding RVAs (which would
// break on every KCD2 patch). The Address Library decouples the
// stable plugin-facing IDs from the per-build RVAs.
//
// Each entry also carries a description string (provenance + signature
// + evidence trail) that plugin code can fetch via Describe()/
// DescribeByName(). The descriptions answer "what RVA did I just get?"
// without forcing authors to grep a separate CSV.
//
// Seed data + ID assignment policy + per-row provenance:
// data/address-library/.

namespace kcdx::address_library {

// Forward declaration — the full struct is defined in the author-declared
// targets section below. FindResolvedAuthorTarget (declared with ResolveByName)
// returns a pointer to one, so the type must be visible before that point.
struct AuthorTarget;

// Resolve a known address-library ID against the running KCD2 build.
// Returns the absolute VA (WHGame.dll base + RVA) on success, or 0
// when:
//   - id is unknown to this kcdx build's compiled-in database; OR
//   - the row for `id` exists but its game_version doesn't match
//     the running KCD2 (plugin needs an updated kcdx with a fresh
//     RVA for this game build); OR
//   - the row's status is anything other than "verified" (we don't
//     promise resolution for unverified rows even when the RVA is
//     present).
//
// Called from interfaces.cpp's Thunk_ResolveAddress.
uintptr_t Resolve(uint64_t id);

// Diagnostic: count of compiled-in entries (used by self-test).
size_t EntryCount();

// Diagnostic: count of entries whose game_version matches the
// running build (i.e. entries that would resolve if the right id
// were queried). Used by the engine's self-test and dev-log
// startup summary.
size_t EntryCountForRunningVersion();

// Resolve a known address by NAME instead of numeric id. Names are
// the kebab/snake-case labels in the seed CSV (e.g. "lua_pcall",
// "cscriptsystem_init"). Returns the same VA as Resolve(id) for the
// matching row, or 0 with the same rules (unknown name, wrong
// game_version, unverified).
//
// Used by kcdx.hook's locator path so authors can write
//   kcdx.hook(kcdx.addr.lua_pcall, { ... })
// rather than remembering numeric ids. Names are advisory (no
// stability guarantee across kcdx versions); numeric ids remain
// the canonical reference for code that needs stability.
//
// `owningAuthor` + `owningPlugin` are the author + plugin namespace components
// of the calling plugin (the 2-dot prefix per naming-namespaces.md). Either or
// both may be "" for an anonymous / engine-internal resolve, or for a plugin
// whose manifest has not yet populated [plugin].author (the corpus's actual
// state during the in-progress namespace refactor — empty author is the
// legacy 1-dot tier that the resolver tolerates without dropping a self-tier
// match; step 4 of the refactor wires the real author through every binder).
//
// RESOLUTION (naming-namespaces.md):
//   - A 3-segment name "<author>.<plugin>.<bare>" is an EXPLICIT plugin-export
//     reference: callable from anywhere, NEVER warns; resolves directly to the
//     author target {author, plugin, bare}.
//   - A 2-segment name "<X>.<Y>" is the legacy 1-dot explicit form. When X ==
//     "kcdx" → the engine seed by the unprefixed engine name Y (the 1-dot
//     engine-root form). Any other 2-segment form is ambiguous under the 2-dot
//     model — for the transition it falls back to treating X as the plugin
//     name and Y as the bare name (legacy 1-dot author-target lookup), so
//     callers that have not yet adopted the 2-dot form keep resolving. Once
//     every author target has a populated author the 2-segment-non-kcdx form
//     becomes a teaching error.
//   - A BARE name (no '.') resolves self > engine > other: (1) the calling
//     plugin's own target keyed (owningAuthor, owningPlugin, name); (2) the
//     engine seed; (3) any other plugin's target with that bare name. First
//     hit wins. When the bare name occupies MORE THAN ONE tier it still
//     resolves by precedence but warns ONCE PER SESSION PER NAME (category
//     "NAMESPACE"), naming the winner + shadowed owners and teaching the
//     prefix fix.
//   - Author-target kinds Rva / AddressId resolve to a VA directly here.
//   - Author-target kinds Pattern / TargetSymbol cannot become a VA in this
//     leaf module (it must not depend on the patch engine / symbol table).
//     ResolveByName returns 0 for them; the caller then asks
//     FindResolvedAuthorTarget (below) for the winning author-target
//     descriptor and routes its pattern / symbol through the patch/symbol
//     pipeline itself (hook_chain::ResolveLocator does this). We never
//     fabricate a VA here (AP2).
//
// LAUNCH-TIME ONLY — runs during the registration/apply pass, never on a
// hook-fire / runtime path (the resolved VA is cached in the binding).
uintptr_t ResolveByName(const char* name,
                        const char* owningAuthor = "",
                        const char* owningPlugin = "");

// Resolve a NAME to the winning AUTHOR-TARGET descriptor, applying the SAME
// self > engine > other precedence (and sharing the SAME bare-collision
// once-per-session warn dedup) as ResolveByName — but returning the
// AuthorTarget* the name resolved to instead of a VA. Returns nullptr when no
// author target wins (the name is unknown, or the engine seed won the
// precedence — a seed row is NOT an author target).
//
// WHY THIS EXISTS — the leaf-module dependency rule (placement is invariant-
// determined, see address-library.md / hook-engine.md): an author target of
// kind Pattern / TargetSymbol cannot resolve to a VA inside this module,
// because doing so would make address_library depend on the patch engine /
// symbol table — inverting the dependency (the patch engine + symbol table
// depend on the name table, never the reverse) and pulling heavy machinery
// into the name table. So ResolveByName returns 0 for those kinds; the caller
// (hook_chain::ResolveLocator) calls THIS to learn that the name resolved to a
// Pattern / TargetSymbol author target, then routes that target's locatorStr
// (the pattern string / symbol name) + signature through the SAME patch::
// Resolve / symbol pipeline it already owns for a directly-set locator. This
// closes the disassembler-test guarantee (cornerstones.md §"author-declared
// targets are shareable"): an expert names a pattern site once, every
// non-expert hooks it BY NAME and it resolves end-to-end.
//
// Returned descriptor lifetime: a pointer into the resident g_authorTargets
// registry — valid for the rest of the process (the registry is append-only
// and never relocated after launch-time discovery). Read the kind + locatorStr
// + signature off it; do NOT cache the pointer past the apply pass.
//
// LAUNCH-TIME ONLY — same invariant as ResolveByName: reached only from the
// apply pass, never from a hook-fire / per-frame path.
const AuthorTarget* FindResolvedAuthorTarget(const char* name,
                                             const char* owningAuthor = "",
                                             const char* owningPlugin = "");

// Iterate every entry that matches the running KCD2 build AND has
// status "verified" — i.e. every row that would resolve via either
// Resolve(id) or ResolveByName(name). Calls `cb` with the entry's
// id, name, description, and resolved VA for each match. Used to
// eagerly populate kcdx.addr.* at startup.
//
// Stops iterating when `cb` returns false.
using ForEachResolvableCallback = bool (*)(uint64_t id, const char* name,
                                           const char* description,
                                           uintptr_t va, void* userdata);
void ForEachResolvable(ForEachResolvableCallback cb, void* userdata);

// Fetch the description string for a given id. Returns the entry's
// description column (the row's "notes" from data/address-library/seed.csv)
// or nullptr if id is unknown to this kcdx build. Description is
// returned regardless of game_version match or status — even
// unverified rows have descriptive notes worth surfacing for
// diagnostics ("here's what we know, but we can't promise the RVA").
//
// String lifetime: process (compiled into .rdata).
const char* Describe(uint64_t id);

// Same as Describe() but by name. Returns nullptr if name is unknown.
const char* DescribeByName(const char* name);

// Fetch the machine-readable function SIGNATURE for a given Address
// Library NAME, in the kcdx.hook signature DSL (see
// src/hook_signature.h) — e.g. "i32 (ptr L, i32 nargs, i32 nresults,
// i32 errfunc)" for lua_pcall. This is the STRUCTURED form of the
// verified ABI prose carried in the row's notes/description column; it
// lets `kcdx.hook{ target = "<name>" }` supply the ABI so the author
// never hand-writes a signature for a named target (the disassembler
// test — .claude/rules/cornerstones.md / AP12).
//
// Returns:
//   - the entry's signature string when the row exists AND carries a
//     verified, structured signature;
//   - "" (empty, non-null) when the row exists but has NO verified
//     signature yet (the prose carried no ABI to structure — we never
//     invent one, per AP2). The caller treats "" as "name resolved but
//     no ABI known: ask the author for an explicit signature=".
//   - "" when the name is unknown (callers resolve the address via
//     ResolveByName separately and report the unknown-name error there).
//
// Lifetime: process (compiled into .rdata). Returned independently of
// game_version / status, mirroring Describe() — the binder gates the
// address on ResolveByName (which enforces version + verified); the
// signature is descriptive metadata for the same row.
//
// `owningAuthor` + `owningPlugin` are the 2-dot namespace components of the
// resolving plugin per naming-namespaces.md, or "" for anonymous / engine-
// internal. The signature resolves by the SAME order as ResolveByName (self >
// engine > other for a bare name; explicit form resolves directly and never
// warns), so the returned ABI comes from the SAME row the address did. The
// bare-collision warn shares ResolveByName's once-per-session-per-name dedup
// — a name that already warned there does not double-warn here.
const char* ResolveSignatureByName(const char* name,
                                   const char* owningAuthor = "",
                                   const char* owningPlugin = "");

// ===========================================================================
// Author-declared targets — the runtime registry (STORAGE + VALIDATION ONLY).
// ===========================================================================
//
// The compiled-in seed (kEntries[]) is the engine's own name table. Authors
// can ALSO declare their own targets (the disassembler-test guarantees in
// cornerstones.md: an author identifies an un-named site via the expert hatch
// ONCE, names it, and shares it by name). Those author-declared targets live
// in a SEPARATE runtime registry (not the constexpr seed) because they're
// discovered at launch from plugin manifests, not baked into the binary.
//
// THIS HEADER SECTION IS THE STORAGE + VALIDATION LAYER ONLY. Precedence
// resolution (self > engine > other per naming-namespaces.md) is a LATER step
// and is NOT wired into ResolveByName here.

// How an author-declared target locates its address. Mirrors the locator
// kinds the seed Entry carries, but as a runtime tag (the seed packs them
// into separate columns; an author target carries exactly one).
enum class AuthorLocatorKind {
    Pattern,       // a byte/AOB signature string (expert hatch)
    Rva,           // a raw RVA literal (expert hatch)
    AddressId,     // a numeric Address Library id payload
    TargetSymbol,  // another already-known target name (seed or author)
};

// One author-declared target, as registered at launch. Runtime data — a
// std::vector<AuthorTarget> rather than the constexpr seed Entry, because the
// set is populated from plugin manifests during discovery, not compiled in.
//
// The owning author + plugin + bare target name together form the shared
// name `<author>.<pluginName>.<bareName>` per naming-namespaces.md; the
// engine derives the prefix, the author types only the bare name. During
// the in-progress refactor `author` may be empty (the legacy 1-dot tier);
// once every plugin's [plugin].author is populated the field becomes
// non-empty for every row.
struct AuthorTarget {
    std::string       author;       // leading namespace component (may be "")
    std::string       pluginName;   // plugin namespace component (validated)
    std::string       bareName;     // the author's bare <name> (no prefix)
    AuthorLocatorKind kind;         // which payload field below is meaningful
    std::string       locatorStr;   // payload for Pattern / TargetSymbol; "" otherwise
    uint64_t          locatorNum;   // payload for Rva / AddressId; 0 otherwise
    std::string       signature;    // the structured ABI in the hook DSL ("" = none)
};

// Validate a `[plugin].name` per naming-namespaces.md: charset [a-z0-9_],
// length 2..32, and NOT the reserved engine root. Returns true when the name
// is a legal namespace prefix; on failure returns false and fills `outError`
// with a teaching message naming the rule.
//
// Rejected (hard manifest rejection — a bad prefix corrupts every shared name
// the plugin exports; fail loud at the door):
//   - empty / under 2 / over 32 chars
//   - any char outside [a-z0-9_] (uppercase, '.', '-', etc.)
//   - the reserved value "kcdx" (the engine root), or any name beginning
//     "kcdx." (engine-namespace squatting)
bool ValidatePluginName(const char* name, std::string& outError);

// Validate a `[plugin].author` per naming-namespaces.md: charset [a-z0-9_],
// length 2..128, and NOT the reserved engine root. Same shape as
// ValidatePluginName — author is the leading component of the 2-dot
// `<author>.<plugin>.<bare>` shared-namespace prefix and must obey the same
// rules a namespace component does. Returns true when the author is a legal
// namespace prefix; on failure returns false and fills `outError` with a
// teaching message naming the rule.
//
// Rejected (hard manifest rejection — a bad author prefix corrupts every
// shared name the plugin exports, identical to a bad plugin name; fail loud
// at the door):
//   - empty / under 2 / over 128 chars
//   - any char outside [a-z0-9_] (uppercase, '.', '-', etc.)
//   - the reserved value "kcdx" (the engine root), or any name beginning
//     "kcdx." (engine-namespace squatting)
//
// Shares the 128-char cap with [plugin].name: both are namespace prefixes
// engine-author families (kcdx_builtin_*, etc.) may grow into; the runtime
// cost is nil (one strlen at launch-time discovery).
bool ValidateAuthorName(const char* name, std::string& outError);

// Register one author-declared target into the runtime registry. VALIDATES
// (the author via ValidateAuthorName when non-empty, the plugin name via
// ValidatePluginName, and the bare name's charset — same [a-z0-9_] rule,
// since it becomes the final segment of `<author>.<plugin>.<name>`), THEN
// appends. Returns true on success; on a validation failure returns false
// and fills `outError` with a teaching message and does NOT append.
//
// `author` MAY be "" during the in-progress namespace refactor (legacy 1-dot
// row — the plugin has not declared [plugin].author yet); when non-empty it
// must obey the same charset / length / reserved-root rules a namespace
// component does. `locatorStr` carries the payload for Pattern / TargetSymbol
// locators (pass "" for the numeric kinds); `locatorNum` carries it for Rva /
// AddressId (pass 0 for the string kinds). `signature` is the structured ABI
// in the kcdx.hook DSL, or "" when the author has none yet (we never invent
// one — AP2).
//
// Launch-time only. See the registry definition comment in the .cpp for the
// resident / never-read-at-runtime invariant.
bool RegisterAuthorTarget(const char*       author,
                          const char*       pluginName,
                          const char*       bareName,
                          AuthorLocatorKind kind,
                          const char*       locatorStr,
                          uint64_t          locatorNum,
                          const char*       signature,
                          std::string&      outError);

// Diagnostic: number of author-declared targets currently in the registry.
// (Self-test / dev-log startup summary; not a hot path.)
size_t AuthorTargetCount();

// ===========================================================================
// Aliases — per-plugin local handles (naming-namespaces.md §Aliasing).
// ===========================================================================
//
// `kcdx.alias(short, "plugin.name")` declares a LOCAL handle scoped to the
// calling plugin: within that plugin, the bare name `short` resolves to the
// full `target` name. An alias is pure local convenience layered ON TOP of the
// self > engine > other model — it CANNOT shadow an engine name or another
// plugin's bare name, because substitution happens only when the calling
// plugin owns an alias by that exact short name. It ADDS a handle, never
// displaces resolution.
//
// Storage: a per-plugin map (owningPlugin -> short -> fullName), populated at
// launch and read only during the apply pass (same launch-time-only invariant
// as g_authorTargets — never a hook-fire / per-frame path).

// Register an alias `short` -> `target` owned by (`owningAuthor`,
// `owningPlugin`). VALIDATES the owning plugin name (charset/length/reserved-
// root via ValidatePluginName), the owning author (when non-empty, same
// validation via ValidateAuthorName), the `short` handle (the [a-z0-9_] 2-32
// component rule — it is referenced like a bare name), and that `target` is
// non-empty. On success records the alias and returns true; on a validation
// failure returns false and fills `outError` with a teaching message and
// records nothing.
//
// `owningPlugin` "" (anonymous caller) is rejected: an alias is meaningless
// without a plugin to scope it to. `owningAuthor` "" is accepted during the
// in-progress namespace refactor — the legacy 1-dot scope (plugin-only key).
//
// Launch-time only.
bool RegisterAlias(const char*  owningAuthor,
                   const char*  owningPlugin,
                   const char*  shortName,
                   const char*  target,
                   std::string& outError);

// Resolve an alias: if (`owningAuthor`, `owningPlugin`) declared an alias
// whose short name is exactly `name`, returns the aliased full target name;
// otherwise returns "". Called at the TOP of name resolution (before the
// self > engine > other walk) so a matching alias substitutes its full
// target, which then resolves normally. A non-empty result is always
// re-resolved as a name.
//
// Launch-time only.
std::string ResolveAlias(const char* owningAuthor,
                         const char* owningPlugin,
                         const char* name);

// Diagnostic: number of registered aliases. (Self-test / dev-log summary.)
size_t AliasCount();

// Emit the shared once-per-session-per-name bare-collision warning. Exposed so
// the symbol table (symbols.cpp) reuses the SAME warn-once dedup as the
// address-name resolver — a bare name that collides warns ONCE across both
// surfaces. `winnerOwner` is the owner that won by precedence; `shadowed` lists
// the displaced owner(s). Launch-time only.
void WarnBareCollisionShared(const char*        bareName,
                             const char*        winnerTier,
                             const std::string& winnerOwner,
                             const std::string& shadowed);

}  // namespace kcdx::address_library
