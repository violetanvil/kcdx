#pragma once

// declared_targets — in-memory registry for kcdx.declare author-supplied
// named targets (the author-declared track of the named-target table).
//
// THIS HEADER IS THE STORE + VERSION-KEY MATCHER + RESOLVE ENTRY POINT ONLY.
// No Lua surface, no address_library integration, no Lua binder lives here.
// The Lua binder lua_bind_declare (a separate module) calls Register; the
// address_library AuthorTarget integration (a separate module) calls
// LookupForCaller. This module is the data layer both consume.
//
// The unified named-target table has two population sources:
//
//   - Curated refdb cache (engine-shipped; lives in refdb).
//   - Author-declared store (plugin-supplied via kcdx.declare; lives HERE).
//
// One DeclaredEntry per (declaring_author, declaring_plugin, name) — the
// 3-part shared-name triple per naming-namespaces.md. Each entry carries
// a list of per-version entries (pattern + optional signature + optional
// kind tag, OR a value literal). An empty per-version list means the
// table-omitted "attempt on all versions" form.
//
// Read path: LookupForCaller resolves a bare name against the SELF tier
// only (the calling plugin's own declarations). The other tier walk and
// the precedence machinery (self > engine > other) live in
// address_library; the address_library integration routes a self/other
// winner through this module for the pattern-resolve / value-read details.
//
// Pattern entries resolve once per (entry, runtime_version) via
// scan_engine::ResolveScan and the outcome is memoized — subsequent
// lookups are free. Value entries return the literal directly.
//
// Version-mismatch / scan-failure outcomes emit structured KV logs with
// category tag "DECLARED_TARGET"; mismatch is deduped once per
// (declaring_author, declaring_plugin, name, runtime_version) tuple so a
// tight resolve loop in a plugin does not flood the log.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kcdx::declared_targets {

// One per-version entry the author supplied via kcdx.declare's versions_kv
// table. `isPattern` discriminates the two payload shapes:
//
//   - isPattern == true  : patternStr (+ optional signatureStr, + optional
//                          kindTag) carries the locator. The engine parses
//                          patternStr lazily via patch::ParsePattern at
//                          resolve time (NOT at Register time — a
//                          malformed pattern surfaces with the runtime
//                          scan-failure log, alongside the running game
//                          version, not as a register-time rejection).
//   - isPattern == false : the entry is a literal value (integer or string
//                          per valueIsString). valueInt / valueStr carry
//                          the payload; the other is left default.
//
// kindTag is honored AS AUTHORED — Register does not auto-derive it. The
// default for a pattern entry is "function" (which is what triggers the
// pattern-without-signature rejection); set kindTag = "data_slot" /
// "value" / etc. to opt out of hook-mode usage and bypass that rejection.
struct VersionEntry {
    // The version key the author supplied: "1.5.1164953" exact, or a
    // wildcard like "1.5.*" / "1.*.*" (any suffix component may be '*').
    // Matched at lookup time against the running game version string.
    std::string  versionKey;

    bool         isPattern = false;
    std::string  patternStr;
    std::string  signatureStr;
    std::string  kindTag;

    int64_t      valueInt = 0;
    std::string  valueStr;
    // Disambiguator for the two value slots above. Ignored when
    // isPattern == true. (Lua-table-valued declarations are not yet
    // supported — the integer + pattern shapes are the current scope.)
    bool         valueIsString = false;
};

// One registered declared entry — keyed by
// (declaringAuthor, declaringPlugin, name). A second Register on the
// same key REPLACES the first (a plugin re-running plugin.lua across a
// reload must be able to re-declare cleanly).
struct DeclaredEntry {
    std::string  declaringAuthor;  // [plugin].author of the declaring plugin
    std::string  declaringPlugin;  // [plugin].name   of the declaring plugin
    std::string  name;             // the bare name the author supplied

    // The module arg the author supplied. REQUIRED (no default — a
    // defaulted module silently misroutes when secondary modules become
    // a concern).
    std::string  module;

    // The per-version list. Empty == "attempt on all versions" (the
    // table-omitted form). Otherwise the matcher picks the exact-match
    // key if any, else the longest wildcard.
    std::vector<VersionEntry> versions;
};

// Outcome of a LookupForCaller call.
//
//   - NoEntry         : no DeclaredEntry is registered under
//                       (callerAuthor, callerPlugin, name).
//   - VersionMismatch : entry exists but no version key matches the
//                       running version AND the entry is not the
//                       table-omitted attempt-all form. The once-per-
//                       session warn line is emitted from inside
//                       LookupForCaller for this outcome.
//   - Pattern         : version match; the picked entry is a pattern.
//                       resolvedRva is the memoized VA from a prior
//                       lookup, or the result of scan_engine::ResolveScan
//                       on this call's first arrival. 0 (with a logged
//                       reason) when the scan failed (0 matches OR
//                       multi-match without an author-supplied
//                       disambiguator — kcdx.declare's per-version entry
//                       shape carries no disambiguation knobs, so any
//                       scan that returns != 1 match fails here).
//                       signatureStr / kindTag are forwarded from the
//                       picked VersionEntry so the caller doesn't have
//                       to dig back into the registry.
//   - Value           : version match; the picked entry is a value
//                       literal. valueInt / valueStr / valueIsString
//                       carry the payload, same shape as the picked
//                       VersionEntry.
struct ResolvedDeclared {
    enum class Kind {
        NoEntry,
        VersionMismatch,
        Pattern,
        Value,
    };
    Kind         kind = Kind::NoEntry;

    uint64_t     resolvedRva = 0;        // for Kind::Pattern
    std::string  signatureStr;           // for Kind::Pattern
    std::string  kindTag;                // for Kind::Pattern

    int64_t      valueInt = 0;           // for Kind::Value (when !valueIsString)
    std::string  valueStr;               // for Kind::Value (when valueIsString)
    bool         valueIsString = false;

    // Back-pointer to the registry entry, for log-attribution in the
    // caller. Nullptr for Kind::NoEntry. The DeclaredEntry node ADDRESS is
    // stable for the process lifetime — deque-node-stable storage means
    // new triples append new nodes that never move prior elements, AND
    // existing-triple re-Register overwrites in place at the same node
    // address. The node CONTENTS, however, are overwritten by an
    // existing-triple re-Register from the owning plugin: any
    // VersionEntry::valueStr cached via this pointer is invalidated by a
    // same-triple re-Declare (cross-triple Declares from any plugin do
    // NOT invalidate it). The same-triple invalidation will be removed
    // when valueStr storage moves to a process-lifetime arena.
    const DeclaredEntry* entry = nullptr;
};

// THE WRITE PATH — the Lua binder calls this once per kcdx.declare
// site. Returns true on accept; false (with a structured
// KV log line written here) on validation reject — the binder need only
// surface the boolean to Lua's (nil, err) return shape.
//
// Validation rules:
//   - e.declaringAuthor + e.declaringPlugin must be non-empty
//     (anonymous declarations are rejected — a declared target is
//     identified by its full <author>.<plugin>.<bare> triple, and an
//     unattributed declaration has nowhere to live in the precedence
//     walk).
//   - e.module must be non-empty (no default).
//   - e.name must be non-empty AND pass the declared-name charset rule:
//     [a-z0-9_], 2..128 chars (same shape as ValidatePluginName, applied
//     inline here as the bare name's own validator — declared_targets
//     does not extend address_library in this module).
//   - Pattern-without-signature rejection: for every VersionEntry where
//     isPattern AND signatureStr is empty AND (kindTag is empty OR
//     kindTag == "function"), REJECT THE WHOLE DECLARATION with the
//     teaching error
//     "kcdx.declare('<module>', '<name>'): version '<vkey>' declares a
//     pattern with no sibling signature. A callback hook on this name
//     needs an ABI signature (the engine can't infer it). Add
//     signature = '...' to this version entry, or set kind = '<non-
//     function>' if this entry is not meant for hook use." Partial
//     acceptance is forbidden (all-or-nothing).
//   - Version keys must be either exact ("1.5.1164953" — non-empty,
//     non-wildcard, no embedded asterisk except as a trailing component)
//     or wildcard ("1.5.*" / "1.*.*" — any suffix component may be a
//     bare '*'). Malformed keys are rejected with a teaching error.
//
// Idempotent per (declaringAuthor, declaringPlugin, name): a second
// Register replaces the first (a plugin re-running plugin.lua across a
// reload re-declares cleanly). The memoization tied to the prior entry
// is dropped on overwrite so the new declaration resolves fresh.
//
// Launch-time only — the registry is built during plugin load.
bool Register(const DeclaredEntry& e);

// THE READ PATH — the address_library AuthorTarget integration calls
// this on resolve.
//
// callerAuthor + callerPlugin are the calling plugin's namespace
// components (the 2-dot prefix in the shared-namespace model). The
// binder reads them from OwningPluginForCurrentCall at the resolve
// site, same as the existing flat-table form.
//
// runtimeVersion is the running game version string (the canonical
// form, e.g. "1.5.1164953"; sourced from
// kcdx::plugins::g_runtimeGameVersionString — DO NOT add a new global,
// READ that one).
//
// Looks up by (callerAuthor, callerPlugin, name) — SELF TIER ONLY here
// (the self > engine > other walk lives in address_library; the
// integration adds the other tier as a separate query alongside this one).
//
// On Kind::VersionMismatch, emits ONE warn line per
// (declaringAuthor, declaringPlugin, name, runtimeVersion) tuple — a
// tight resolve loop does not spam the log.
//
// On Kind::Pattern, runs scan_engine::ResolveScan on the first call per
// (entry, runtime_version) and memoizes the outcome (success VA or
// failure marker — both are remembered). Subsequent lookups return the
// cached outcome without re-scanning. On a scan that returns != 1
// match, returns Kind::Pattern with resolvedRva == 0 AND emits a
// structured KV log line naming the cause; the caller propagates the
// failure as a teaching error to the install site.
//
// Read-only; idempotent; safe to call many times per launch. Caller is
// expected to be at or past init::InitPhase::RefdbOpened (so
// g_runtimeGameVersionString is populated and scan_engine is reachable).
// Enforced with KCDX_REQUIRE_PHASE inside the implementation.
ResolvedDeclared LookupForCaller(const std::string& callerAuthor,
                                 const std::string& callerPlugin,
                                 const std::string& name,
                                 const std::string& runtimeVersion);

// Pick the VersionEntry that matches `runtimeVersion` per the canonical
// exact-then-longest-wildcard rule: an exact key wins outright; otherwise the
// longest-prefix wildcard wins. Returns nullptr if no version matches AND the
// entry is not the table-omitted "attempt on all versions" form, OR if the
// entry has no versions at all.
//
// Returned pointer aliases into `e.versions`; deque-node-stable storage of
// g_entries means `e` itself has a process-stable ADDRESS across subsequent
// Register calls (new triples append new deque nodes, existing-triple
// re-Register overwrites in place at the same node — preserving the node's
// address). Callers reading string payloads via `valueStr.c_str()` get a
// pointer that survives any Register on a DIFFERENT triple from any plugin;
// a re-Register of the SAME triple from the owning plugin invalidates the
// prior `valueStr` storage at the same node (the new entry's versions
// vector is copy-assigned over the prior one, destroying the prior inner
// std::string). The same-triple invalidation will be removed when
// valueStr storage moves to a process-lifetime arena.
//
// Read-only; no logging side-effects; safe to call many times per launch.
const VersionEntry* FindPickedVersionEntry(const DeclaredEntry& e,
                                           const std::string& runtimeVersion);

// Reset all registry + memoization + warn-dedup state. For tests and
// self-test isolation. Never called during normal operation.
void Reset();

// Number of registered DeclaredEntry rows across all plugins.
// Diagnostic / dev-log summary — not a hot path.
size_t Size();

}  // namespace kcdx::declared_targets
