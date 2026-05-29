// declared_targets — implementation.
//
// In-memory store keyed by (declaringAuthor, declaringPlugin, name) with
// per-version entries; version-key matcher (exact > longest wildcard >
// table-omitted attempt-all); pattern-resolve memoization keyed by
// (entryIdx, runtime_version); once-per-session warn dedup for the
// VersionMismatch outcome.
//
// THIS MODULE IS THE STORE + MATCHER + RESOLVE ENTRY POINT. Lua binding
// and address_library AuthorTarget integration are separate modules —
// they consume Register / LookupForCaller / Size but do not live here.
//
// Storage shape:
//
//   - g_entries : std::deque<DeclaredEntry>. Node-stable storage —
//     element addresses survive subsequent Register calls. A Register on
//     an existing (declaringAuthor, declaringPlugin, name) triple still
//     overwrites the slot in place at the same node; new triples
//     allocate a new node without moving prior elements. Indexed access
//     stays O(1). Linear lookup is fine — TC scale is hundreds of
//     entries, never millions; cache locality across one deque chunk +
//     no hash overhead beats a flat_map at that size, and we never sort
//     by key. The back-pointers the resolver returns into entry slots
//     are valid for the rest of the session by construction (no
//     reallocation event exists).
//
//   - g_memo : std::vector<MemoEntry>, keyed by (entryIdx, runtimeVer).
//     entryIdx indexes into g_entries; deque's stable indexed access
//     keeps that key valid across subsequent appends. MemoEntry itself
//     carries no cross-element pointers, so its std::vector is fine.
//     One row per (entry, running version) the resolver has seen. On a
//     Pattern hit, we run scan_engine::ResolveScan once and record the
//     outcome here (success VA OR failure marker); subsequent lookups
//     read the cached outcome without re-scanning. The failure cache is
//     intentional — a scan that returned 0 matches against this version
//     will return 0 matches every time; re-running it would spam the log
//     with the same diagnostic and waste cycles.
//
//   - g_warned : std::vector<WarnKey>, the once-per-session VersionMismatch
//     dedup set. Same shape as the memo, keyed by the
//     (declaringAuthor, declaringPlugin, name, runtimeVersion) tuple.
//
// THREADING: declared_targets is launch-time storage. Register is called
// during plugin discovery (worker thread, before EngineHooksInstalled);
// LookupForCaller runs at the address_library apply-pass site, also
// worker-thread, after RefdbOpened. Both are single-threaded by
// construction at the boot point they occupy; no internal lock is taken.
//
// Logging: structured KV under category "DECLARED_TARGET". KV uses must
// be fully qualified (::kcdx::log::KV(...)) — there is no `using KV` in
// this TU and bare KV does not resolve.

#include "declared_targets.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include "init_phase.h"     // KCDX_REQUIRE_PHASE
#include "log.h"            // LOG_*_KV, ::kcdx::log::KV
#include "patch_engine.h"   // patch::ParsePattern
#include "scan_engine.h"    // ScanEntry, ScanResult, ResolveScan

namespace kcdx::declared_targets {

namespace {

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------

// The registry. Node-stable storage (std::deque<DeclaredEntry>):
// element addresses survive subsequent Register calls. A Register on an
// existing (declaringAuthor, declaringPlugin, name) triple OVERWRITES
// the slot in place at the same node; new triples allocate a new node
// without moving prior elements. Back-pointers handed out by
// LookupForCaller therefore have a stable ADDRESS for the process
// lifetime. The node's CONTENTS, however, are overwritten by an
// existing-triple Register: the `g_entries[existing] = e;` copy-assign
// in Register destroys the prior versions vector (including every inner
// std::string valueStr and the chars they pointed at). Net contract for
// a cached VersionEntry::valueStr.c_str() the caller holds:
//
//   - Cross-triple Register from any plugin: cached pointer SURVIVES.
//     The deque node-stability guarantees prior nodes never move.
//   - Same-triple re-Register from the owning plugin: cached pointer is
//     INVALIDATED — the inner std::string is destroyed by copy-assign.
//
// The same-triple invalidation will be removed when valueStr storage
// moves to a process-lifetime arena; at that point the broad
// "process-lifetime; survives any subsequent Register" contract becomes
// true unconditionally. Memoization tied to the prior entry is dropped
// on overwrite (see DropMemoForEntry).
std::deque<DeclaredEntry> g_entries;

// Memoized per-(entryIdx, runtimeVersion) Pattern resolves. attempted
// distinguishes "never resolved" (no row) from "resolved but failed"
// (row present, resolvedRva == 0, attempted == true).
struct MemoEntry {
    size_t      entryIdx = 0;
    std::string runtimeVersion;
    uint64_t    resolvedRva = 0;
    bool        attempted = false;
};
std::vector<MemoEntry> g_memo;

// Once-per-session dedup for the VersionMismatch warn.
struct WarnKey {
    std::string declaringAuthor;
    std::string declaringPlugin;
    std::string name;
    std::string runtimeVersion;
};
std::vector<WarnKey> g_warned;

// ---------------------------------------------------------------------------
// Helpers — lookups, dedup, drops
// ---------------------------------------------------------------------------

// Linear search for an existing entry by (author, plugin, name). Returns
// the index or g_entries.size() on miss.
size_t FindEntryIndex(const std::string& author,
                      const std::string& plugin,
                      const std::string& name) {
    for (size_t i = 0; i < g_entries.size(); ++i) {
        const DeclaredEntry& e = g_entries[i];
        if (e.declaringAuthor == author &&
            e.declaringPlugin == plugin &&
            e.name == name) {
            return i;
        }
    }
    return g_entries.size();
}

// Find a memoized resolve row for (entryIdx, runtimeVersion). Returns the
// index in g_memo or g_memo.size() on miss.
size_t FindMemoIndex(size_t entryIdx, const std::string& runtimeVersion) {
    for (size_t i = 0; i < g_memo.size(); ++i) {
        const MemoEntry& m = g_memo[i];
        if (m.entryIdx == entryIdx && m.runtimeVersion == runtimeVersion) {
            return i;
        }
    }
    return g_memo.size();
}

// Drop every memoized resolve for an entry — called on Register
// overwrite so a re-declared entry resolves fresh against the running
// version.
void DropMemoForEntry(size_t entryIdx) {
    g_memo.erase(
        std::remove_if(g_memo.begin(), g_memo.end(),
                       [entryIdx](const MemoEntry& m) {
                           return m.entryIdx == entryIdx;
                       }),
        g_memo.end());
}

// Returns true and adds the tuple to g_warned iff this is the first
// time we've seen it this session; returns false on a repeat.
bool TryClaimWarn(const std::string& declaringAuthor,
                  const std::string& declaringPlugin,
                  const std::string& name,
                  const std::string& runtimeVersion) {
    for (const WarnKey& w : g_warned) {
        if (w.declaringAuthor == declaringAuthor &&
            w.declaringPlugin == declaringPlugin &&
            w.name == name &&
            w.runtimeVersion == runtimeVersion) {
            return false;
        }
    }
    g_warned.push_back(WarnKey{declaringAuthor, declaringPlugin,
                               name, runtimeVersion});
    return true;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

// The declared-name validator. Same shape as
// address_library::ValidatePluginName (charset [a-z0-9_], 2..128 chars)
// applied INLINE here — declared_targets does not extend
// address_library; that integration is a separate module. A bare
// declared name becomes the final segment of the <author>.<plugin>.<bare>
// triple and must obey the same namespace-component rules.
bool ValidateDeclaredName(const std::string& name, std::string& outError) {
    if (name.empty()) {
        outError = "declared name is empty";
        return false;
    }
    if (name.size() < 2) {
        outError = "declared name '" + name + "' is too short (min 2 chars)";
        return false;
    }
    if (name.size() > 128) {
        outError = "declared name is too long (max 128 chars)";
        return false;
    }
    for (char c : name) {
        const bool isLower = (c >= 'a' && c <= 'z');
        const bool isDigit = (c >= '0' && c <= '9');
        const bool isUnder = (c == '_');
        if (!(isLower || isDigit || isUnder)) {
            outError = "declared name '" + name +
                       "' contains an invalid character "
                       "(charset is [a-z0-9_])";
            return false;
        }
    }
    return true;
}

// A version key is either:
//   - exact            : one or more chars from [a-zA-Z0-9._], NO '*'.
//   - wildcard suffix  : the key splits on '.' into components; some
//                        TRAILING component(s) are exactly "*", and the
//                        preceding ones are exact. E.g. "1.5.*" yes;
//                        "1.*.*" yes; "*" alone yes (matches anything);
//                        "1.*.1164953" NO (a wildcard cannot appear in
//                        the middle); "1.5*" NO (an asterisk inside a
//                        component without standing alone as one).
//
// The matcher relies on this shape (exact suffix prefix is the part
// up to but not including the first all-'*' component).
bool ValidateVersionKey(const std::string& key, std::string& outError) {
    if (key.empty()) {
        outError = "version key is empty";
        return false;
    }

    // Split on '.' into components.
    std::vector<std::string> comps;
    comps.emplace_back();
    for (char c : key) {
        if (c == '.') {
            comps.emplace_back();
        } else {
            comps.back().push_back(c);
        }
    }
    // Empty component (leading/trailing/consecutive dot) is malformed.
    for (const std::string& c : comps) {
        if (c.empty()) {
            outError = "version key '" + key +
                       "' contains an empty component "
                       "(leading/trailing dot or consecutive dots)";
            return false;
        }
    }

    // First locate the first all-'*' component (if any).
    size_t firstStar = comps.size();
    for (size_t i = 0; i < comps.size(); ++i) {
        if (comps[i] == "*") {
            firstStar = i;
            break;
        }
    }

    // Components BEFORE the first '*' must be exact (no embedded '*').
    for (size_t i = 0; i < firstStar; ++i) {
        for (char c : comps[i]) {
            if (c == '*') {
                outError = "version key '" + key +
                           "' has an asterisk inside a component "
                           "(only bare-'*' components are wildcards)";
                return false;
            }
        }
    }

    // Components AT OR AFTER the first '*' must each be bare '*'.
    for (size_t i = firstStar; i < comps.size(); ++i) {
        if (comps[i] != "*") {
            outError = "version key '" + key +
                       "' has a non-wildcard component after a "
                       "wildcard (wildcards may only TRAIL)";
            return false;
        }
    }

    // Charset on the exact components: [a-zA-Z0-9._] within each
    // component (already enforced by the all-'*' rule for wildcard
    // components; here we check the exact ones).
    for (size_t i = 0; i < firstStar; ++i) {
        for (char c : comps[i]) {
            const bool isLower = (c >= 'a' && c <= 'z');
            const bool isUpper = (c >= 'A' && c <= 'Z');
            const bool isDigit = (c >= '0' && c <= '9');
            const bool isPunc  = (c == '_');
            if (!(isLower || isUpper || isDigit || isPunc)) {
                outError = "version key '" + key +
                           "' contains an invalid character "
                           "(charset is [a-zA-Z0-9._] plus trailing '*')";
                return false;
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Version-key matching — helpers (the public picker is FindPickedVersionEntry
// below, outside this anonymous namespace; the helpers stay file-local).
// ---------------------------------------------------------------------------
//
// Per spec:
//   - Empty versions vector → "attempt on all versions" (the table-omitted
//     form). The store models attempt-all as `versions` being empty; the
//     caller short-circuits to the attempt-all path before reaching the
//     picker, so the picker is never asked to pick from an empty vector
//     (the public accessor returns nullptr if it is, matching the no-match
//     contract).
//
//   - Exact match wins outright (a key equal to the runtime version
//     string).
//
//   - Else: pick the LONGEST wildcard prefix that matches. "1.5.*"
//     matches "1.5.1164953" with prefix length 4 ("1.5."); "1.*.*"
//     matches with prefix length 2 ("1."); the longer wins.
//     Implementation: simple string-prefix match on the up-to-first-'*'
//     portion of each version key.
//
//   - No matches → return nullptr (the "VersionMismatch" signal). The
//     caller treats this as Kind::VersionMismatch and emits the badge log
//     once-per-session-per-tuple.

// Length of the exact prefix portion of a version key (the substring
// before the first '*'). For an exact key this is the whole key length;
// for a wildcard key this is the leading non-'*' portion.
size_t ExactPrefixLen(const std::string& key) {
    for (size_t i = 0; i < key.size(); ++i) {
        if (key[i] == '*') return i;
    }
    return key.size();
}

bool IsWildcardKey(const std::string& key) {
    for (char c : key) {
        if (c == '*') return true;
    }
    return false;
}

bool ExactMatches(const std::string& key, const std::string& version) {
    return key == version;
}

bool WildcardMatches(const std::string& key, const std::string& version) {
    // Wildcard match: the exact-prefix portion of the key must equal the
    // first N chars of the version. This is correct only for the
    // "trailing all-'*' components" shape ValidateVersionKey enforces.
    const size_t prefixLen = ExactPrefixLen(key);
    if (version.size() < prefixLen) return false;
    for (size_t i = 0; i < prefixLen; ++i) {
        if (key[i] != version[i]) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Register-time pattern-without-signature enforcement
// ---------------------------------------------------------------------------

// True iff this VersionEntry needs a signature but lacks one: a pattern
// entry that would later be hooked needs a sibling signature; without
// one the engine can't bind the ABI. The opt-out is an explicit
// non-"function" kindTag (e.g. "data_slot", "value") which says the
// entry isn't meant for hook-mode use.
bool NeedsSignatureButHasNone(const VersionEntry& v) {
    if (!v.isPattern) return false;
    if (!v.signatureStr.empty()) return false;
    if (!v.kindTag.empty() && v.kindTag != "function") return false;
    return true;
}

// ---------------------------------------------------------------------------
// Scan resolve (for Kind::Pattern lookups)
// ---------------------------------------------------------------------------

// Resolve the picked pattern entry to a VA via scan_engine::ResolveScan.
// Returns the VA on a unique 1-match success; returns 0 (with the
// appropriate KV log line emitted from here) on a parse error, a 0-match
// outcome, or a multi-match outcome (kcdx.declare's per-version entry
// carries no anchor / disambiguation knob, so anything other than a
// single match fails).
uint64_t ResolvePatternToVA(const DeclaredEntry& e,
                            const VersionEntry& v) {
    scan_engine::ScanEntry entry;
    entry.sourceFile = "<kcdx.declare>";
    entry.pluginName = e.declaringPlugin;
    entry.name       = e.name;
    entry.module     = e.module;

    // Parse the pattern lazily — a malformed pattern surfaces here, not
    // at Register, so the author's running-version log line carries the
    // real cause (this is also what makes round-tripping through a
    // re-Register on reload safe — Register doesn't reject for runtime
    // shape errors).
    try {
        entry.pattern = kcdx::patch::ParsePattern(v.patternStr);
    } catch (const std::exception& ex) {
        LOG_ERROR_KV("DECLARED_TARGET", "scan_failed",
            ::kcdx::log::KV("plugin", e.declaringPlugin),
            ::kcdx::log::KV("author", e.declaringAuthor),
            ::kcdx::log::KV("name",   e.name),
            ::kcdx::log::KV("module", e.module),
            ::kcdx::log::KV("reason", "pattern_parse_error"),
            ::kcdx::log::KV("detail", ex.what()));
        return 0;
    }

    scan_engine::ScanResult result = scan_engine::ResolveScan(entry);

    if (!result.moduleLoaded) {
        LOG_ERROR_KV("DECLARED_TARGET", "scan_failed",
            ::kcdx::log::KV("plugin", e.declaringPlugin),
            ::kcdx::log::KV("author", e.declaringAuthor),
            ::kcdx::log::KV("name",   e.name),
            ::kcdx::log::KV("module", e.module),
            ::kcdx::log::KV("pattern_matches", static_cast<long long>(0)),
            ::kcdx::log::KV("reason", "module_not_loaded"));
        return 0;
    }

    if (result.patternMatches != 1) {
        const char* reason = result.patternMatches == 0
                             ? "no_match"
                             : "multi_match";
        LOG_ERROR_KV("DECLARED_TARGET", "scan_failed",
            ::kcdx::log::KV("plugin", e.declaringPlugin),
            ::kcdx::log::KV("author", e.declaringAuthor),
            ::kcdx::log::KV("name",   e.name),
            ::kcdx::log::KV("module", e.module),
            ::kcdx::log::KV("pattern_matches",
                static_cast<long long>(result.patternMatches)),
            ::kcdx::log::KV("reason", reason));
        return 0;
    }

    return static_cast<uint64_t>(result.matches[0].applyAddr);
}

}  // namespace

// ===========================================================================
// Public API
// ===========================================================================

const VersionEntry* FindPickedVersionEntry(const DeclaredEntry& e,
                                           const std::string& runtimeVersion) {
    // Pass 1: any exact match wins outright.
    for (const VersionEntry& v : e.versions) {
        if (!IsWildcardKey(v.versionKey) &&
            ExactMatches(v.versionKey, runtimeVersion)) {
            return &v;
        }
    }
    // Pass 2: longest wildcard prefix wins.
    const VersionEntry* best = nullptr;
    size_t bestLen = 0;
    for (const VersionEntry& v : e.versions) {
        if (!IsWildcardKey(v.versionKey)) continue;
        if (!WildcardMatches(v.versionKey, runtimeVersion)) continue;
        const size_t prefixLen = ExactPrefixLen(v.versionKey);
        if (best == nullptr || prefixLen > bestLen) {
            best = &v;
            bestLen = prefixLen;
        }
    }
    return best;
}

bool Register(const DeclaredEntry& e) {
    // Identity validation
    if (e.declaringAuthor.empty()) {
        LOG_ERROR_KV("DECLARED_TARGET", "register_rejected",
            ::kcdx::log::KV("author", e.declaringAuthor),
            ::kcdx::log::KV("plugin", e.declaringPlugin),
            ::kcdx::log::KV("name",   e.name),
            ::kcdx::log::KV("reason", "empty_author"),
            ::kcdx::log::KV("detail",
                "kcdx.declare: the declaring plugin has no [plugin].author — "
                "a declared target needs the full <author>.<plugin>.<bare> "
                "triple to live in the precedence walk."));
        return false;
    }
    if (e.declaringPlugin.empty()) {
        LOG_ERROR_KV("DECLARED_TARGET", "register_rejected",
            ::kcdx::log::KV("author", e.declaringAuthor),
            ::kcdx::log::KV("plugin", e.declaringPlugin),
            ::kcdx::log::KV("name",   e.name),
            ::kcdx::log::KV("reason", "empty_plugin"),
            ::kcdx::log::KV("detail",
                "kcdx.declare: the declaring plugin has no [plugin].name — "
                "anonymous declarations are rejected."));
        return false;
    }
    if (e.module.empty()) {
        LOG_ERROR_KV("DECLARED_TARGET", "register_rejected",
            ::kcdx::log::KV("author", e.declaringAuthor),
            ::kcdx::log::KV("plugin", e.declaringPlugin),
            ::kcdx::log::KV("name",   e.name),
            ::kcdx::log::KV("reason", "empty_module"),
            ::kcdx::log::KV("detail",
                "kcdx.declare(module, name, ...): module is required "
                "(no default — a defaulted module silently misroutes when "
                "secondaries get involved)."));
        return false;
    }

    // Name charset
    {
        std::string nameErr;
        if (!ValidateDeclaredName(e.name, nameErr)) {
            LOG_ERROR_KV("DECLARED_TARGET", "register_rejected",
                ::kcdx::log::KV("author", e.declaringAuthor),
                ::kcdx::log::KV("plugin", e.declaringPlugin),
                ::kcdx::log::KV("name",   e.name),
                ::kcdx::log::KV("reason", "invalid_name"),
                ::kcdx::log::KV("detail", nameErr));
            return false;
        }
    }

    // Version-key shape + pattern-without-signature enforcement
    // (per-VersionEntry; all-or-nothing).
    for (const VersionEntry& v : e.versions) {
        std::string keyErr;
        if (!ValidateVersionKey(v.versionKey, keyErr)) {
            LOG_ERROR_KV("DECLARED_TARGET", "register_rejected",
                ::kcdx::log::KV("author", e.declaringAuthor),
                ::kcdx::log::KV("plugin", e.declaringPlugin),
                ::kcdx::log::KV("name",   e.name),
                ::kcdx::log::KV("reason", "invalid_version_key"),
                ::kcdx::log::KV("detail", keyErr));
            return false;
        }

        if (NeedsSignatureButHasNone(v)) {
            // Build the teaching error spec'd in declared_targets.h.
            std::string msg = "kcdx.declare('";
            msg += e.module;
            msg += "', '";
            msg += e.name;
            msg += "'): version '";
            msg += v.versionKey;
            msg += "' declares a pattern with no sibling signature. "
                   "A callback hook on this name needs an ABI signature "
                   "(the engine can't infer it). Add signature = '...' "
                   "to this version entry, or set kind = '<non-function>' "
                   "if this entry is not meant for hook use.";
            LOG_ERROR_KV("DECLARED_TARGET", "register_rejected",
                ::kcdx::log::KV("author",  e.declaringAuthor),
                ::kcdx::log::KV("plugin",  e.declaringPlugin),
                ::kcdx::log::KV("name",    e.name),
                ::kcdx::log::KV("version", v.versionKey),
                ::kcdx::log::KV("reason",  "pattern_without_signature"),
                ::kcdx::log::KV("detail",  msg));
            return false;
        }
    }

    // Append or overwrite in place. Overwrite drops the entry's memo so
    // the re-declared shape resolves fresh.
    const size_t existing = FindEntryIndex(e.declaringAuthor,
                                           e.declaringPlugin, e.name);
    if (existing < g_entries.size()) {
        g_entries[existing] = e;
        DropMemoForEntry(existing);
    } else {
        g_entries.push_back(e);
    }

    LOG_INFO_KV("DECLARED_TARGET", "registered",
        ::kcdx::log::KV("author",         e.declaringAuthor),
        ::kcdx::log::KV("plugin",         e.declaringPlugin),
        ::kcdx::log::KV("name",           e.name),
        ::kcdx::log::KV("module",         e.module),
        ::kcdx::log::KV("versions_count",
            static_cast<long long>(e.versions.size())));
    return true;
}

ResolvedDeclared LookupForCaller(const std::string& callerAuthor,
                                 const std::string& callerPlugin,
                                 const std::string& name,
                                 const std::string& runtimeVersion) {
    // Phase gate — the lookup needs g_runtimeGameVersionString populated
    // and scan_engine reachable (refdb's the canonical "everything is up"
    // milestone for resolution).
    KCDX_REQUIRE_PHASE(::kcdx::init::InitPhase::RefdbOpened);

    ResolvedDeclared out;

    // Self-tier lookup — the calling plugin's own (author, plugin, name).
    const size_t entryIdx = FindEntryIndex(callerAuthor, callerPlugin, name);
    if (entryIdx >= g_entries.size()) {
        out.kind = ResolvedDeclared::Kind::NoEntry;
        return out;
    }

    const DeclaredEntry& e = g_entries[entryIdx];
    out.entry = &e;

    // Attempt-all form: the author omitted the version table, so any
    // running version matches. The entry's `versions` vector is empty
    // by construction in this case — the spec leaves the payload shape
    // for the attempt-all form to the binder/integration modules (a
    // single synthesized VersionEntry the binder pushes with
    // versionKey == "*", OR a non-versioned payload at the entry
    // level). This module models empty-versions as a VersionMismatch
    // fall-through: the integration module + binder will decide whether
    // the attempt-all sugar synthesizes a "*" key (clean, uses the same
    // matcher path) or carries entry-level payload (would require an
    // entry-level pattern/value field — open design question). Today,
    // an empty versions vector takes the fall-through; the only way to
    // reach a Pattern/Value result is via a versioned entry.
    if (e.versions.empty()) {
        // No version rows at all — fall through to the VersionMismatch
        // path so this surfaces loudly until the attempt-all storage
        // shape is settled. (The Lua binder may also synthesize a "*"
        // entry at Register time; that would match here and reach the
        // wildcard path naturally without changing this module.)
        if (TryClaimWarn(e.declaringAuthor, e.declaringPlugin,
                         e.name, runtimeVersion)) {
            LOG_WARN_KV("DECLARED_TARGET", "version_mismatch",
                ::kcdx::log::KV("plugin",   e.declaringPlugin),
                ::kcdx::log::KV("author",   e.declaringAuthor),
                ::kcdx::log::KV("name",     e.name),
                ::kcdx::log::KV("running",  runtimeVersion),
                ::kcdx::log::KV("declared", "(none)"));
        }
        out.kind = ResolvedDeclared::Kind::VersionMismatch;
        return out;
    }

    // Versioned form: pick the best entry per the matcher.
    const VersionEntry* pickedPtr = FindPickedVersionEntry(e, runtimeVersion);
    if (pickedPtr == nullptr) {
        // Build a comma-joined list of declared keys for the warn line.
        std::string declared;
        for (size_t i = 0; i < e.versions.size(); ++i) {
            if (i > 0) declared += ",";
            declared += e.versions[i].versionKey;
        }
        if (TryClaimWarn(e.declaringAuthor, e.declaringPlugin,
                         e.name, runtimeVersion)) {
            LOG_WARN_KV("DECLARED_TARGET", "version_mismatch",
                ::kcdx::log::KV("plugin",   e.declaringPlugin),
                ::kcdx::log::KV("author",   e.declaringAuthor),
                ::kcdx::log::KV("name",     e.name),
                ::kcdx::log::KV("running",  runtimeVersion),
                ::kcdx::log::KV("declared", declared));
        }
        out.kind = ResolvedDeclared::Kind::VersionMismatch;
        return out;
    }

    const VersionEntry& picked = *pickedPtr;

    if (picked.isPattern) {
        // Memoize: one resolve per (entryIdx, runtimeVersion). The memo
        // remembers BOTH a success VA and a failure (attempted == true,
        // resolvedRva == 0). Failure cache is intentional — a 0-match
        // scan against this version will be 0-match every time;
        // re-scanning would re-spam the log.
        uint64_t resolvedVA = 0;
        const size_t memoIdx = FindMemoIndex(entryIdx, runtimeVersion);
        if (memoIdx < g_memo.size() && g_memo[memoIdx].attempted) {
            resolvedVA = g_memo[memoIdx].resolvedRva;
        } else {
            resolvedVA = ResolvePatternToVA(e, picked);
            MemoEntry m;
            m.entryIdx       = entryIdx;
            m.runtimeVersion = runtimeVersion;
            m.resolvedRva    = resolvedVA;
            m.attempted      = true;
            g_memo.push_back(m);
        }

        out.kind         = ResolvedDeclared::Kind::Pattern;
        out.resolvedRva  = resolvedVA;
        out.signatureStr = picked.signatureStr;
        out.kindTag      = picked.kindTag;
        return out;
    }

    // Value entry.
    out.kind          = ResolvedDeclared::Kind::Value;
    out.valueIsString = picked.valueIsString;
    out.valueInt      = picked.valueInt;
    out.valueStr      = picked.valueStr;
    return out;
}

void Reset() {
    g_entries.clear();
    g_memo.clear();
    g_warned.clear();
}

size_t Size() {
    return g_entries.size();
}

}  // namespace kcdx::declared_targets
