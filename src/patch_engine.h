#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace kcdx::patch {

struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<bool> mask;  // true = literal, false = wildcard
};

// Parse "48 ?? 89 5C 24 ..." into Pattern. Throws std::runtime_error on bad input.
Pattern ParsePattern(const std::string& s);

// Parse "44 8A F0" into raw bytes. No wildcards allowed.
std::vector<uint8_t> ParseBytes(const std::string& s);

// Anchor variant types ----------------------------------------------------
struct AnchorString          { std::string literal; };
struct AnchorFunctionByExport{ std::string name; };
struct AnchorSymbol          { std::string name; };
using Anchor = std::variant<std::monostate, AnchorString, AnchorFunctionByExport, AnchorSymbol>;

struct PatchEntry {
    std::string sourceFile;   // for error messages — path of the toml that contributed this patch
    std::string name;
    std::string description;
    int priority = 100;
    std::string module = "WHGame.dll";

    Pattern pattern;
    int offset = 0;
    std::vector<uint8_t> original;
    std::vector<uint8_t> replacement;
    bool idempotent = true;

    std::optional<Pattern> context;
    Anchor anchor;
    uint32_t maxAnchorDistance = 4096;
};

// Engine state — set by config.cpp at startup, read by hooks.cpp when applying.
extern std::vector<PatchEntry> g_patches;
extern bool g_dryRun;

// A contiguous byte range [begin, end) on the loaded module.
struct ByteRange {
    uintptr_t begin = 0;
    uintptr_t end   = 0;   // exclusive
    bool empty() const { return end <= begin; }
    bool overlaps(const ByteRange& o) const {
        return begin < o.end && o.begin < end;
    }
};

// Result of resolving a patch's locators against the pristine module — produced
// by the pre-flight pass and consumed by both conflict detection and the apply
// step. If `ok == false` the locators couldn't be resolved (pattern not found,
// context disagreement, anchor failure); reason holds a one-line explanation.
//
// Read ranges are kept separate by source because they have different conflict
// semantics. A write that overlaps another plugin's `pattern` or `context`
// is incidental and SAFE (the other plugin still patches whatever it intended;
// its locators were already resolved). A write that overlaps another plugin's
// `original` field is GENUINE — that plugin's verify check will fail.
struct ResolvedPatch {
    bool ok = false;
    std::string reason;              // populated when !ok
    uintptr_t patchAddr = 0;         // pattern_match + offset
    ByteRange writeRange;            // bytes ApplyPatch will overwrite
    ByteRange patternRange;          // bytes the pattern scanned
    ByteRange originalRange;         // bytes the verify check will compare (== writeRange)
    std::optional<ByteRange> contextRange;  // bytes the context pattern scanned (if any)
};

// Category of inter-plugin byte overlap detected at pre-flight. Used to drive
// log severity and abort behavior.
enum class ConflictKind {
    WriteOnOriginal,   // genuine — reader's verify will fail
    WriteOnWriteFull,  // documented "mod B clobbers mod A" — both apply, B's bytes win
    WriteOnWritePartial,  // dangerous — bytes get a Frankenstein mix that may be invalid code
};

// One detected pre-flight conflict. Recorded by PreFlightAll() and referenced
// later: write-on-original conflicts surface as enriched error messages on
// the reader's abort; write-on-write conflicts log at apply time describing
// what got clobbered.
struct Conflict {
    ConflictKind kind;
    std::string writerName;       // plugin that writes (earlier priority)
    std::string readerName;       // plugin whose locator / write is affected
    ByteRange overlap;            // the overlapping byte range
};

extern std::vector<Conflict> g_conflicts;

// Resolve a patch's locators against the pristine module WITHOUT writing.
// Pure read; safe to call before any patches have been applied. Used by the
// pre-flight pass and by Lua-runtime patches (which don't participate in
// pre-flight). For TOML patches, ApplyPatch uses the cached pre-flight
// ResolvedPatch instead of calling this again.
ResolvedPatch Resolve(const PatchEntry& p);

// Run Resolve() on every patch in g_patches against the pristine module,
// compute pairwise byte overlaps with genuine-conflict semantics, populate
// g_conflicts, and stash the resolved data for ApplyAll() to consume.
// Safe to call once at startup, before ApplyAll().
void PreFlightAll();

// Apply one TOML patch using its pre-flight resolution. Idempotent. Returns
// true on a successful write OR a verified-idempotent skip. Returns false on
// any abort condition. If pre-flight predicted a write-on-original conflict
// affecting this patch, the abort message names the upstream culprit. If
// pre-flight predicted a write-on-write conflict where this patch is the
// writer landing on previously-written bytes, that's logged at apply time.
bool ApplyResolvedPatch(const PatchEntry& p, const ResolvedPatch& r);

// Apply a single patch by re-resolving (no pre-flight context). Used by the
// Lua KCDX.ScanAndWrite runtime path. Does not benefit from pre-flight's
// "incidental overlap" tolerance.
bool ApplyPatch(const PatchEntry& p);

// Apply all patches in g_patches. Internally calls PreFlightAll() first.
void ApplyAll();

// Find every byte-offset within [data, data+size) where the pattern matches.
std::vector<size_t> FindAllInBuffer(const uint8_t* data, size_t size, const Pattern& pat);

}  // namespace kcdx::patch
