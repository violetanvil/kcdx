#include "conflict_engine.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "hook_engine.h"
#include "load_order.h"
#include "log.h"

namespace kcdx::conflict_engine {

// Engine state.
std::vector<patch::ResolvedPatch> g_resolvedPatches;
std::vector<ResolvedHook>         g_resolvedHooks;
std::vector<WriteFootprint>       g_writes;
std::vector<ReadFootprint>        g_reads;
std::vector<Conflict>             g_conflicts;
std::vector<EntryRef>             g_applyOrder;

namespace {

// MinHook's rel32-jmp footprint at a hook target. Pre-flight assumes 5
// bytes; if MinHook picks the 14-byte abs64 form at actual install time
// (which can only happen when no branch-pool address is within ±2GB —
// kcdx's branch pool is sized to make this rare), pre-flight conflict
// detection slightly under-counts. The 14-byte abs64 case can be added
// later by widening the footprint here.
constexpr uintptr_t kHookWriteFootprintBytes = 5;

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

// Resolve hooks: factored from old hook_engine::ApplyAll. Adapts the hook's
// locator data into a synthetic PatchEntry (the locator pipeline doesn't
// care that there's no original/replacement), runs patch::Resolve, captures
// patchAddr as the hook target.
void ResolveHooks() {
    g_resolvedHooks.clear();
    g_resolvedHooks.resize(hook_engine::g_hooks.size());
    for (size_t i = 0; i < hook_engine::g_hooks.size(); ++i) {
        const hook_engine::HookEntry& h = hook_engine::g_hooks[i];
        ResolvedHook& rh = g_resolvedHooks[i];

        // Phase 5f: hooks may declare either 'bytes' (raw machine code)
        // OR 'lua_callback' (TOML schema, kcdx routes through JIT
        // trampoline + scripting). Both produce a valid hook; only
        // reject when both are empty (caught at parse time in config.cpp,
        // but defensive double-check here).
        if (h.bytes.empty() && h.lua_callback.empty()) {
            rh.reason = "neither 'bytes' nor 'lua_callback' set";
            continue;
        }

        // Adapt to a PatchEntry for patch::Resolve.
        patch::PatchEntry locator;
        locator.sourceFile = h.sourceFile;
        locator.name = h.name;
        locator.module = h.module;
        locator.pattern = h.pattern;
        locator.context = h.context;
        locator.anchor = h.anchor;
        locator.maxAnchorDistance = h.maxAnchorDistance;
        locator.offset = h.offset;
        locator.original.clear();
        locator.replacement.clear();

        patch::ResolvedPatch r = patch::Resolve(locator);
        if (!r.ok) {
            rh.reason = r.reason;
            continue;
        }
        rh.ok = true;
        rh.targetAddr = r.patchAddr;
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

    // Hooks contribute one write footprint (the 5-byte rel32 jmp).
    for (size_t i = 0; i < hook_engine::g_hooks.size(); ++i) {
        const auto& h = hook_engine::g_hooks[i];
        const auto& r = g_resolvedHooks[i];
        if (!r.ok) continue;
        WriteFootprint w;
        w.name = h.name;
        w.kind = WriteKind::HookPrologue;
        w.priority = h.priority;
        w.begin = r.targetAddr;
        w.end   = r.targetAddr + kHookWriteFootprintBytes;
        w.hookIndex = static_cast<int>(i);
        g_writes.push_back(std::move(w));
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

// Build the unified apply order. Walks every resolved patch and hook,
// emits an EntryRef for each, then sorts by (priority, name) so the
// orchestration in hooks.cpp can dispatch in load order. Used by the
// unified apply loop.
//
// Entries from plugins the user disabled via load_order.toml
// (enabled = false) are filtered out here so the orchestrator dispatch
// loop, conflict reporting, and apply-summary counts all naturally see
// zero work for them. We log a single line per skipped entry so the
// modder can verify their disable took effect.
void BuildApplyOrder() {
    g_applyOrder.clear();
    g_applyOrder.reserve(patch::g_patches.size() + hook_engine::g_hooks.size());

    size_t skippedPatches = 0;
    size_t skippedHooks   = 0;

    for (size_t i = 0; i < patch::g_patches.size(); ++i) {
        const auto& p = patch::g_patches[i];
        if (!load_order::IsPluginEnabled(p.pluginName)) {
            log::InfoF("[%s] skipping patch '%s' (plugin disabled via load_order.toml)",
                       p.pluginName.c_str(), p.name.c_str());
            ++skippedPatches;
            continue;
        }
        g_applyOrder.push_back({ EntryKind::Patch, i });
    }
    for (size_t i = 0; i < hook_engine::g_hooks.size(); ++i) {
        const auto& h = hook_engine::g_hooks[i];
        if (!load_order::IsPluginEnabled(h.pluginName)) {
            log::InfoF("[%s] skipping hook '%s' (plugin disabled via load_order.toml)",
                       h.pluginName.c_str(), h.name.c_str());
            ++skippedHooks;
            continue;
        }
        g_applyOrder.push_back({ EntryKind::Hook, i });
    }
    if (skippedPatches + skippedHooks > 0) {
        log::InfoF("load_order: skipped %zu patch(es) + %zu hook(s) from disabled plugin(s)",
                   skippedPatches, skippedHooks);
    }

    // Sort by (priority, name) across all entry types. Lower priority
    // applies first. Stable so ties preserve the relative order from
    // config::LoadAllConfigs (which itself sorted by priority then name).
    std::stable_sort(g_applyOrder.begin(), g_applyOrder.end(),
        [](const EntryRef& a, const EntryRef& b) {
            int aPri, bPri;
            const std::string* aName;
            const std::string* bName;
            if (a.kind == EntryKind::Patch) {
                aPri = patch::g_patches[a.index].priority;
                aName = &patch::g_patches[a.index].name;
            } else {
                aPri = hook_engine::g_hooks[a.index].priority;
                aName = &hook_engine::g_hooks[a.index].name;
            }
            if (b.kind == EntryKind::Patch) {
                bPri = patch::g_patches[b.index].priority;
                bName = &patch::g_patches[b.index].name;
            } else {
                bPri = hook_engine::g_hooks[b.index].priority;
                bName = &hook_engine::g_hooks[b.index].name;
            }
            if (aPri != bPri) return aPri < bPri;
            return *aName < *bName;
        });
}

}  // namespace

void RunPreFlight() {
    log::InfoF("Conflict engine: pre-flight starting (%zu patch(es), %zu hook(s))",
               patch::g_patches.size(), hook_engine::g_hooks.size());

    ResolvePatches();
    ResolveHooks();
    BuildApplyOrder();
    CollectFootprints();
    DetectConflicts();

    if (g_conflicts.empty()) {
        log::Info("Conflict engine: pre-flight clean, no conflicts detected.");
    } else {
        // Tallies for the summary line.
        size_t woo = 0, woof = 0, woop = 0, hoh = 0, hoEp = 0, pOeh = 0;
        for (const auto& c : g_conflicts) {
            switch (c.category) {
            case Category::WriteOnOriginal:           ++woo;  break;
            case Category::WriteOnWriteFull:          ++woof; break;
            case Category::WriteOnWritePartial:       ++woop; break;
            case Category::HookOnHook:                ++hoh;  break;
            case Category::HookOverlapsEarlierPatch:  ++hoEp; break;
            case Category::PatchOverlapsEarlierHook:  ++pOeh; break;
            }
        }
        log::InfoF("Conflict engine: %zu conflict(s) recorded "
                   "(WriteOnOriginal=%zu, WriteOnWriteFull=%zu, WriteOnWritePartial=%zu, "
                   "HookOnHook=%zu, HookOverlapsEarlierPatch=%zu, PatchOverlapsEarlierHook=%zu)",
                   g_conflicts.size(), woo, woof, woop, hoh, hoEp, pOeh);
    }
}

const Conflict* FindWriteOnOriginalAffecting(const std::string& patchName) {
    for (const auto& c : g_conflicts) {
        if (c.category == Category::WriteOnOriginal && c.later.name == patchName) {
            return &c;
        }
    }
    return nullptr;
}

const Conflict* FindHookOnHookAffecting(const std::string& hookName) {
    for (const auto& c : g_conflicts) {
        if (c.category == Category::HookOnHook && c.later.name == hookName) {
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
