#include "conflict_engine.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "log.h"

// HISTORICAL (apply-consolidation cut): the HOOK half of this engine
// (g_resolvedHooks + ResolveHooks, the hook write-footprint loop, the
// kHookWriteFootprintBytes constant), the unified apply order
// (g_applyOrder + BuildApplyOrder + EntryRef), the pre-flight driver
// RunPreFlight, and the FindHookOnHookAffecting lookup were removed. They
// fed a dead apply loop in hooks.cpp that walked g_patches/g_hooks (TOML
// vectors with no populator since Phase 5). The PATCH half below survives
// as a dead-but-present builder kept for the compile dependency in
// patch::ApplyResolvedPatch (the Find* readers); see the header banner.

namespace kcdx::conflict_engine {

// Engine state.
std::vector<patch::ResolvedPatch> g_resolvedPatches;
std::vector<WriteFootprint>       g_writes;
std::vector<ReadFootprint>        g_reads;
std::vector<Conflict>             g_conflicts;

namespace {

inline uintptr_t IntersectBegin(uintptr_t a, uintptr_t b) {
    return a > b ? a : b;
}
inline uintptr_t IntersectEnd(uintptr_t a, uintptr_t b) {
    return a < b ? a : b;
}

// Overlap test: do [a1, a2) and [b1, b2) share any byte?
inline bool Overlaps(uintptr_t a1, uintptr_t a2, uintptr_t b1, uintptr_t b2) {
    return a1 < b2 && b1 < a2;
}

// Plain-English conflict log lines. Mirror the wording mempatch already
// uses for patch-vs-patch so users see consistent diagnostics across
// engine versions. Cross-engine cases (HookOnHook, etc.) get their own
// wording, but follow the same "Plugin '%s' did X to plugin '%s'" form.
std::string Explain(const Conflict& c) {
    char buf[720];
    switch (c.category) {
    case Category::WriteOnOriginal:
        snprintf(buf, sizeof(buf),
            "Plugin '%s' modified bytes that plugin '%s' needs to verify before "
            "patching (overlap at 0x%p..0x%p). The earlier mod stopped the later "
            "one from applying. Try removing or reordering one of them.",
            c.earlier.name.c_str(), c.later.name.c_str(),
            reinterpret_cast<void*>(c.overlapBegin),
            reinterpret_cast<void*>(c.overlapEnd));
        break;
    case Category::WriteOnWriteFull:
        snprintf(buf, sizeof(buf),
            "Plugin '%s' fully overwrote bytes already written by plugin '%s' "
            "(at 0x%p..0x%p). Both mods applied; '%s' wins because it ran "
            "later. If you wanted plugin '%s' to take effect at this address "
            "instead, give it a lower 'priority' number in its kcdx.toml.",
            c.later.name.c_str(), c.earlier.name.c_str(),
            reinterpret_cast<void*>(c.overlapBegin),
            reinterpret_cast<void*>(c.overlapEnd),
            c.later.name.c_str(),
            c.earlier.name.c_str());
        break;
    case Category::WriteOnWritePartial:
        snprintf(buf, sizeof(buf),
            "Plugin '%s' partially overwrote bytes already written by plugin "
            "'%s' (overlap at 0x%p..0x%p). Both mods applied, but the result "
            "is a MIX of their bytes -- this may produce invalid instructions "
            "and crash the game. If the game becomes unstable, remove one of "
            "the two conflicting mods.",
            c.later.name.c_str(), c.earlier.name.c_str(),
            reinterpret_cast<void*>(c.overlapBegin),
            reinterpret_cast<void*>(c.overlapEnd));
        break;
    case Category::HookOnHook:
        snprintf(buf, sizeof(buf),
            "Hook '%s' targets function entry 0x%p, but hook '%s' already "
            "claimed that target. kcdx v0.1 is first-wins; '%s' will install, "
            "'%s' will abort. Chained hooks are a v0.2 feature.",
            c.later.name.c_str(),
            reinterpret_cast<void*>(c.overlapBegin),
            c.earlier.name.c_str(),
            c.earlier.name.c_str(),
            c.later.name.c_str());
        break;
    case Category::HookOverlapsEarlierPatch:
        snprintf(buf, sizeof(buf),
            "Hook '%s' will install a 5-byte rel32 jmp at 0x%p..0x%p, overlapping "
            "bytes already modified by patch '%s'. MinHook will relocate the "
            "patched prologue into the trampoline, so the patch survives inside "
            "the hook's call-original path. Both apply, no action needed.",
            c.later.name.c_str(),
            reinterpret_cast<void*>(c.overlapBegin),
            reinterpret_cast<void*>(c.overlapEnd),
            c.earlier.name.c_str());
        break;
    case Category::PatchOverlapsEarlierHook:
        snprintf(buf, sizeof(buf),
            "Patch '%s' writes inside hook '%s''s 5-byte rel32 jmp range "
            "(overlap 0x%p..0x%p). The patch's verify-against-original will "
            "see the hook's E9 jmp bytes rather than the function's original "
            "prologue, so the patch will abort cleanly. Remove or reorder one "
            "of the two if you need both to take effect.",
            c.later.name.c_str(),
            c.earlier.name.c_str(),
            reinterpret_cast<void*>(c.overlapBegin),
            reinterpret_cast<void*>(c.overlapEnd));
        break;
    }
    return std::string(buf);
}

// Resolve patches: call patch::Resolve for each. Population happens after
// trampoline_engine::ApplyAll, so target_symbol-using patches resolve
// cleanly via the symbol table.
void ResolvePatches() {
    g_resolvedPatches.clear();
    g_resolvedPatches.resize(patch::g_patches.size());
    for (size_t i = 0; i < patch::g_patches.size(); ++i) {
        g_resolvedPatches[i] = patch::Resolve(patch::g_patches[i]);
    }
}

// Build write + read footprints from resolved data.
void CollectFootprints() {
    g_writes.clear();
    g_reads.clear();

    // Patches contribute one write footprint (the replacement) and one
    // read footprint (the original verify range, which equals the write
    // range — same bytes get verified then overwritten).
    for (size_t i = 0; i < patch::g_patches.size(); ++i) {
        const auto& p = patch::g_patches[i];
        const auto& r = g_resolvedPatches[i];
        if (!r.ok) continue;
        WriteFootprint w;
        w.name = p.name;
        w.kind = WriteKind::Patch;
        w.priority = p.priority;
        w.begin = r.writeRange.begin;
        w.end   = r.writeRange.end;
        w.patchIndex = static_cast<int>(i);
        g_writes.push_back(std::move(w));

        ReadFootprint rd;
        rd.name = p.name;
        rd.priority = p.priority;
        rd.begin = r.originalRange.begin;
        rd.end   = r.originalRange.end;
        rd.patchIndex = static_cast<int>(i);
        g_reads.push_back(std::move(rd));
    }
}

// Decide which of two writes applies "earlier" under the load-order policy.
// Today: by priority asc, then by name asc (matching existing tiebreaker).
// Future: load_order.txt overrides feed in here.
bool IsEarlier(const WriteFootprint& a, const WriteFootprint& b) {
    if (a.priority != b.priority) return a.priority < b.priority;
    return a.name < b.name;
}

// Pairwise overlap detection. Walks every (write, write) pair and every
// (write, read) pair to classify into Conflict records. Patches and hooks
// share the same write-footprint set, so this loop naturally handles
// every cross-category case.
void DetectConflicts() {
    g_conflicts.clear();

    // (write, read) pairs: did anyone write into anyone else's verify range?
    for (size_t i = 0; i < g_writes.size(); ++i) {
        const WriteFootprint& w = g_writes[i];
        for (size_t j = 0; j < g_reads.size(); ++j) {
            const ReadFootprint& rd = g_reads[j];
            if (w.name == rd.name) continue;  // own footprint
            if (!Overlaps(w.begin, w.end, rd.begin, rd.end)) continue;

            // Determine load order. The patch that VERIFIES is the "reader";
            // we only flag if the WRITER applies earlier than the reader,
            // because that's when the reader will see modified bytes.
            //
            // Build a synthetic WriteFootprint for the reader for the
            // IsEarlier comparison; the reader's name + priority is what
            // we need.
            WriteFootprint readerAsWrite;
            readerAsWrite.name = rd.name;
            readerAsWrite.priority = rd.priority;
            // Other fields don't matter for IsEarlier.
            if (!IsEarlier(w, readerAsWrite)) continue;

            Conflict c;
            c.category = Category::WriteOnOriginal;
            c.earlier = w;
            c.later = readerAsWrite;
            // Stash the read range as the later footprint's begin/end so
            // logs can describe the verify-range that was clobbered.
            c.later.begin = rd.begin;
            c.later.end   = rd.end;
            c.later.patchIndex = rd.patchIndex;
            c.overlapBegin = IntersectBegin(w.begin, rd.begin);
            c.overlapEnd   = IntersectEnd(w.end, rd.end);
            g_conflicts.push_back(std::move(c));
            log::Warn(Explain(g_conflicts.back()));
        }
    }

    // (write, write) pairs: every unordered pair, classify into the right
    // cross-engine category.
    for (size_t i = 0; i < g_writes.size(); ++i) {
        for (size_t j = i + 1; j < g_writes.size(); ++j) {
            const WriteFootprint& a = g_writes[i];
            const WriteFootprint& b = g_writes[j];
            if (!Overlaps(a.begin, a.end, b.begin, b.end)) continue;

            // Order them: `earlier` applies first under load order.
            const WriteFootprint& earlier = IsEarlier(a, b) ? a : b;
            const WriteFootprint& later   = IsEarlier(a, b) ? b : a;

            Conflict c;
            c.earlier = earlier;
            c.later = later;
            c.overlapBegin = IntersectBegin(a.begin, a.end);
            // Wait — that's wrong. We want intersection of the two ranges.
            c.overlapBegin = IntersectBegin(a.begin, b.begin);
            c.overlapEnd   = IntersectEnd(a.end, b.end);

            // Classify by entry-kind pair:
            const bool earlierHook = earlier.kind == WriteKind::HookPrologue;
            const bool laterHook   = later.kind   == WriteKind::HookPrologue;

            if (earlierHook && laterHook) {
                // Hook-on-hook: identical targets count as the same target
                // function entry. With our footprint = [target, target+5),
                // overlap is automatic when targets match.
                c.category = Category::HookOnHook;
            } else if (earlierHook && !laterHook) {
                // Earlier hook, later patch -> patch writes into hook's jmp range
                c.category = Category::PatchOverlapsEarlierHook;
            } else if (!earlierHook && laterHook) {
                // Earlier patch, later hook -> hook captures patched bytes
                c.category = Category::HookOverlapsEarlierPatch;
            } else {
                // Both patches. Classify by full vs partial overlap.
                bool full = (a.begin == b.begin) && (a.end == b.end);
                c.category = full ? Category::WriteOnWriteFull
                                  : Category::WriteOnWritePartial;
            }
            g_conflicts.push_back(c);

            // Log severity by category.
            switch (c.category) {
            case Category::WriteOnWriteFull:
            case Category::HookOverlapsEarlierPatch:
                log::Info(Explain(c));
                break;
            default:
                log::Warn(Explain(c));
                break;
            }
        }
    }
}

}  // namespace

const Conflict* FindWriteOnOriginalAffecting(const std::string& patchName) {
    for (const auto& c : g_conflicts) {
        if (c.category == Category::WriteOnOriginal && c.later.name == patchName) {
            return &c;
        }
    }
    return nullptr;
}

std::vector<const Conflict*> FindWriteOnWriteAffecting(const std::string& writerName) {
    std::vector<const Conflict*> out;
    for (const auto& c : g_conflicts) {
        if ((c.category == Category::WriteOnWriteFull ||
             c.category == Category::WriteOnWritePartial) &&
            c.later.name == writerName) {
            out.push_back(&c);
        }
    }
    return out;
}

}  // namespace kcdx::conflict_engine
