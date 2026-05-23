#pragma once

#include <cstdint>
#include <string>

// Address Library — id-to-RVA lookup compiled into kcdx.asi.
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
// `owningPlugin` is the name of the plugin whose call is resolving this
// name (the namespace prefix per naming-namespaces.md), or "" for an
// anonymous / engine-internal resolve.
//
// RESOLUTION (naming-namespaces.md):
//   - A name with a '.' is an EXPLICIT "<prefix>.<rest>" reference, callable
//     from anywhere and NEVER warns: prefix "kcdx" → the engine seed by the
//     unprefixed name `rest`; any other prefix → that plugin's author target
//     {pluginName==prefix, bareName==rest}.
//   - A BARE name (no '.') resolves self > engine > other: (1) the calling
//     plugin's own target {owningPlugin, name}, (2) the engine seed, (3) any
//     other plugin's target with that bare name. First hit wins. When the bare
//     name occupies MORE THAN ONE tier it still resolves by precedence but
//     warns ONCE PER SESSION PER NAME (category "NAMESPACE"), naming the winner
//     + shadowed owners and teaching the prefix fix.
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
uintptr_t ResolveByName(const char* name, const char* owningPlugin = "");

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
// `owningPlugin` is the resolving plugin's name (namespace prefix per
// naming-namespaces.md), or "" for anonymous / engine-internal. The signature
// resolves by the SAME order as ResolveByName (self > engine > other for a
// bare name; explicit "<prefix>.<rest>" resolves directly and never warns), so
// the returned ABI comes from the SAME row the address did. The bare-collision
// warn shares ResolveByName's once-per-session-per-name dedup — a name that
// already warned there does not double-warn here.
const char* ResolveSignatureByName(const char* name,
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
// The owning plugin name + the bare target name together form the shared
// name `<pluginName>.<bareName>` per naming-namespaces.md; the engine derives
// the prefix, the author types only the bare name.
struct AuthorTarget {
    std::string       pluginName;   // owner; the namespace prefix (validated)
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

// Register one author-declared target into the runtime registry. VALIDATES
// (the owning plugin name via ValidatePluginName, and the bare name's charset
// — same [a-z0-9_] rule, since it becomes the second half of the shared
// `<plugin>.<name>`), THEN appends. Returns true on success; on a validation
// failure returns false and fills `outError` with a teaching message and does
// NOT append.
//
// `locatorStr` carries the payload for Pattern / TargetSymbol locators (pass
// "" for the numeric kinds); `locatorNum` carries it for Rva / AddressId
// (pass 0 for the string kinds). `signature` is the structured ABI in the
// kcdx.hook DSL, or "" when the author has none yet (we never invent one —
// AP2).
//
// Launch-time only. See the registry definition comment in the .cpp for the
// resident / never-read-at-runtime invariant.
bool RegisterAuthorTarget(const char*       pluginName,
                          const char*       bareName,
                          AuthorLocatorKind kind,
                          const char*       locatorStr,
                          uint64_t          locatorNum,
                          const char*       signature,
                          std::string&      outError);

// Diagnostic: number of author-declared targets currently in the registry.
// (Self-test / dev-log startup summary; not a hot path.)
size_t AuthorTargetCount();

}  // namespace kcdx::address_library
