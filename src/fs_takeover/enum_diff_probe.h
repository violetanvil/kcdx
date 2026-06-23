#pragma once

// === DIAGNOSTIC (PROBE Y) — enumeration vanilla-DIFFERENTIAL =================
//
// KI-0028. Enumeration is the ONE filesystem-takeover surface kcdx replaces
// WHOLESALE with no captured original and no differential. The open / read /
// metadata families each keep a captured engine original and thunk it on an
// index MISS — so PROBE W could call the original alongside kcdx's answer and
// log on divergence. FindFirst/FindNext/FindClose (slots 63/64/65) and
// ForEachFile (slot 14) instead SYNTHESIZE kcdx's own unified entry set (disk
// walk UNION index pak-vpaths) with NOTHING to compare against. PROBE W was
// structurally blind to it.
//
// This is exactly where "draws abandoned UPSTREAM of command-list recording"
// (PROBE X — draw_indexed=0, ia_set_ib=0 swap-ON) would ORIGINATE for a
// filesystem takeover: the engine discovers renderable content by WALKING
// DIRECTORIES (level/material/mesh/render-list population). If kcdx's
// synthesized enumeration returns a DIFFERENT entry set than the engine's
// original would — a missing name, an extra name, a wrong dir-vs-file flag,
// a different membership — the engine builds its content list from a different
// directory view and DROPS geometry before a single FOpen is ever issued. No
// failed serve, no metadata VANILLA_DIFF, yet swap-caused. Enumeration has
// ALREADY been the divergence source twice this investigation (KI-0027's
// 528-entry over-match; PROBE Q's synthetic-subdir recursion gap).
//
// THE PROBE (theory-independent): capture the engine ORIGINAL FindFirst/
// FindNext/FindClose + ForEachFile at swap time. On each boot-window kcdx
// enumeration, REPLAY THE SAME PATTERN through the captured original into a
// throwaway buffer, collect vanilla's entry set, and DIFF it against kcdx's set
// — by count, by the actual base names (set-difference both ways), and by the
// dir-vs-file flag. Log under "ENUM_DIFF" ONLY on divergence (silent on match,
// the PROBE W discipline). The vpath/pattern names WHICH walk; the name deltas
// name WHAT differs.
//
// OUTCOME MAP (pre-committed, every outcome equally real):
//   A — ENUM_DIFF fires on a boot/render walk  → the FS takeover IS still the
//       cause; kcdx's enumeration steers content discovery down a different
//       tree → geometry dropped upstream of the draw. FS NOT exonerated.
//   B — zero ENUM_DIFF across the whole window, draws still draw_indexed=0  →
//       kcdx enumeration is byte-identical to vanilla → the FS takeover (serve
//       AND enumerate) is exonerated BY MEASUREMENT (not inference). Pivot the
//       next probe entirely off the filesystem to the render/PSO/scene layer.
//   C — ENUM_DIFF fires only on walks provably unrelated to geometry/render →
//       record the benign diffs; verify no level/material/mesh/render dir walk
//       is in the divergent set before concluding.
//
// §-SAFETY OF THE REPLAY: the captured original runs the ENGINE'S OWN directory
// walk on the engine's OWN iterator state, touching only the intact object
// members the vtable-pointer-only swap preserves (pak vector, search paths,
// alias table). The replay mints NO kcdx handle and threads NO kcdx state into
// the engine: it calls the original FindFirst, drains it with the original
// FindNext, and CLOSES it with the original FindClose — the iterator is created
// and destroyed entirely within the original's own runtime, then discarded. The
// same read-only-replay class as PROBE W's metadata differential, extended from
// a scalar to a set. kcdx's returned enumeration is UNCHANGED — the original's
// set is collected, compared, and dropped; the ENUM_DIFF log is the only delta.
//
// COST: the replay is a COLD boot-window-only path (BootWindowActive gate), and
// each replay is a full directory re-walk — acceptable for the bounded boot/
// render window this investigation needs, NEVER after (predicted-skip, same
// gate as every other boot trace). NO-RESIDUE: this entire file + its capture
// call in vtable_swap.cpp + the two diff calls in find_slots.cpp / enum_slots.cpp
// are removed together when PROBE Y retires (working-artifacts.md).

#include <string>
#include <vector>

namespace kcdx::fs_takeover {

// Capture the engine ORIGINAL enumeration-slot bodies (14 ForEachFile, 63
// FindFirst, 64 FindNext, 65 FindClose) from the live object's original vtable —
// the SAME array SetMetadataOriginals reads. Called once at swap time, beside
// SetMetadataOriginals, BEFORE the object's vtable pointer is overwritten.
void SetEnumOriginalsForDiff(const void* const* originalVtable);

// kcdx just built `kcdxEntries` (base names) for `pattern` via its synthesized
// FindFirst/unified-set core. Replay the SAME `pattern` through the captured
// ORIGINAL FindFirst/FindNext/FindClose on `self`, collect vanilla's base-name
// set, and log an ENUM_DIFF iff the two sets differ (count, membership, or
// dir-flag). `kcdxIsDir` is parallel to `kcdxEntries`. Read-only; boot-window
// gated. A null captured original → silent no-op (nothing to compare).
void ReplayAndDiffFind(void* self, const char* pattern,
                       const std::vector<std::string>& kcdxNames,
                       const std::vector<uint8_t>& kcdxIsDir);

// kcdx just fired its ForEachFile callbacks for `pattern`, surfacing
// `kcdxNames` (the unified-set base/vpath names it emitted, in emission order).
// Replay the SAME `pattern` through the captured ORIGINAL ForEachFile on `self`
// (collecting vanilla's emitted names via a probe callback), and log an
// ENUM_DIFF iff the sets differ. Read-only; boot-window gated.
void ReplayAndDiffForEach(void* self, const char* pattern,
                          const std::vector<std::string>& kcdxNames);

}  // namespace kcdx::fs_takeover
