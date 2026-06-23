#include "enum_diff_probe.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <unordered_set>

#include "boot_trace.h"     // BootWindowActive — the boot-window gate (reused)
#include "find_slots.h"     // kFindDataAttrOffset / kFindDataNameOffset / kFindDataDirBit
#include "../asset_overlay.h"  // NormalizeVPath (compare names the index-fold way)
#include "../log.h"

// PROBE Y implementation — see enum_diff_probe.h for the full design + outcome
// map + §-safety argument. This file is fully isolated so its removal on retire
// is a single-file delete plus the capture/diff call sites.

namespace kcdx::fs_takeover {

namespace {

constexpr const char* kCat = "ENUM_DIFF";

// The engine's universal path cap — the find-data buffer must be a MAX_PATH-class
// region (the original FindFirst writes the inline name at +0x24). 2048 matches
// the other slot files' kMaxPath; the find-data header is 0x24 bytes, the name
// follows, so a 2048 buffer holds any real base name.
constexpr size_t kBufBytes = 2048;

// The captured ORIGINAL enumeration-slot ABIs (BODY-VERIFIED, find_slots.h /
// enum_slots.cpp). On x64 there is ONE calling convention; `T fn(void* self,…)`
// IS the member-call shape (self==this in RCX).
using FindFirstOrigFn_t = intptr_t (*)(void* self, const char* pattern,
                                       void* findData, int flags);
using FindNextOrigFn_t  = intptr_t (*)(void* self, intptr_t handle, void* findData);
using FindCloseOrigFn_t = int      (*)(void* self, intptr_t handle);
// ForEachFile original: (self, cbCtx, pattern, userData); it invokes the
// object's slot-15 per-file callback as (self, cbCtx, fullPath, userData). So
// `cbCtx` carries the per-file callback fn and `userData` its context — the
// replay passes its OWN collector callback + a vector as userData.
using ForEachOrigFn_t   = uint8_t  (*)(void* self, void* cbCtx,
                                       const char* pattern, void* userData);
using PerFileCallbackFn_t =
    void (*)(void* self, void* cbCtx, const char* fullPath, void* userData);

std::atomic<FindFirstOrigFn_t> g_origFindFirst{nullptr};
std::atomic<FindNextOrigFn_t>  g_origFindNext{nullptr};
std::atomic<FindCloseOrigFn_t> g_origFindClose{nullptr};
std::atomic<ForEachOrigFn_t>   g_origForEach{nullptr};

// Read the base name (inline NUL-terminated C-string at +0x24) and the dir flag
// (bit 0x10 at +0x00) out of a find-data buffer the original FindFirst/FindNext
// filled. Mirrors the consumer ABI find_slots.h documents.
std::string ReadFindName(const uint8_t* buf, bool* isDir) {
    if (isDir) *isDir = (buf[kFindDataAttrOffset] & kFindDataDirBit) != 0;
    const char* name = reinterpret_cast<const char*>(buf + kFindDataNameOffset);
    // Bound the read to the buffer (defensive — a real base name is NUL-
    // terminated well within kBufBytes).
    size_t n = 0;
    const size_t cap = kBufBytes - kFindDataNameOffset;
    while (n < cap && name[n] != '\0') ++n;
    return std::string(name, n);
}

// The probe collector callback the replayed ORIGINAL ForEachFile invokes per
// entry. `userData` is a std::vector<std::string>* we collect base names into.
// `fullPath` is the engine's per-entry full path ("<dir>/<name>"); extract the
// base name past the last separator so it compares against FindFirst's base
// names + kcdx's emitted names on the same footing.
void ProbeForEachCollect(void* /*self*/, void* /*cbCtx*/, const char* fullPath,
                         void* userData) {
    if (!fullPath || !userData) return;
    auto* out = static_cast<std::vector<std::string>*>(userData);
    const char* base = fullPath;
    for (const char* p = fullPath; *p; ++p)
        if (*p == '/' || *p == '\\') base = p + 1;
    out->emplace_back(base);
}

// Compute + log the set-difference between kcdx's emitted names and vanilla's,
// each normalized (the index-fold compare, so a case-only difference is not a
// false divergence). Logs ONE ENUM_DIFF line iff the sets differ, naming the
// pattern, both counts, and a capped sample of names ONLY-in-kcdx and
// ONLY-in-vanilla. Silent on an exact set match.
constexpr size_t kSampleCap = 16;

void DiffAndLog(const char* whichSlot, const char* pattern,
                const std::vector<std::string>& kcdxNames,
                const std::vector<std::string>& vanillaNames) {
    std::unordered_set<std::string> kcdxSet, vanillaSet;
    kcdxSet.reserve(kcdxNames.size() * 2);
    vanillaSet.reserve(vanillaNames.size() * 2);
    for (const auto& n : kcdxNames)    kcdxSet.insert(asset_overlay::NormalizeVPath(n));
    for (const auto& n : vanillaNames) vanillaSet.insert(asset_overlay::NormalizeVPath(n));

    std::string onlyKcdx, onlyVanilla;
    size_t onlyKcdxN = 0, onlyVanillaN = 0;
    for (const auto& n : kcdxSet) {
        if (!vanillaSet.count(n)) {
            ++onlyKcdxN;
            if (onlyKcdxN <= kSampleCap) { if (!onlyKcdx.empty()) onlyKcdx += ", "; onlyKcdx += n; }
        }
    }
    for (const auto& n : vanillaSet) {
        if (!kcdxSet.count(n)) {
            ++onlyVanillaN;
            if (onlyVanillaN <= kSampleCap) { if (!onlyVanilla.empty()) onlyVanilla += ", "; onlyVanilla += n; }
        }
    }

    if (onlyKcdxN == 0 && onlyVanillaN == 0) return;  // exact set match — silent.

    if (onlyKcdxN > kSampleCap) onlyKcdx += " …(+" + std::to_string(onlyKcdxN - kSampleCap) + " more)";
    if (onlyVanillaN > kSampleCap) onlyVanilla += " …(+" + std::to_string(onlyVanillaN - kSampleCap) + " more)";

    LOG_WARN_KV(kCat, "enum_diverge",
        kcdx::log::KV::BareStr("slot", whichSlot),
        kcdx::log::KV("pattern", pattern ? pattern : "<null>"),
        kcdx::log::KV("kcdx_count", static_cast<long long>(kcdxNames.size())),
        kcdx::log::KV("vanilla_count", static_cast<long long>(vanillaNames.size())),
        kcdx::log::KV("only_in_kcdx", onlyKcdx.empty() ? "<none>" : onlyKcdx.c_str()),
        kcdx::log::KV("only_in_vanilla", onlyVanilla.empty() ? "<none>" : onlyVanilla.c_str()),
        kcdx::log::KV::BareStr("detail",
            "kcdx's SYNTHESIZED enumeration set DIFFERS from what the engine "
            "original would return for this directory walk — the engine builds "
            "its content/render list from a different directory view than vanilla, "
            "so geometry under the divergent names can be dropped upstream of any "
            "FOpen. This is the KI-0028 enumeration signal: kcdx did not FAIL a "
            "serve, it ENUMERATED differently. only_in_vanilla = entries the "
            "engine would have seen but kcdx omitted (the drop suspects); "
            "only_in_kcdx = entries kcdx added the engine would not have seen."));
}

}  // namespace

void SetEnumOriginalsForDiff(const void* const* originalVtable) {
    if (!originalVtable) {
        LOG_ERROR_KV(kCat, "set_enum_originals_null_vtable",
            kcdx::log::KV::BareStr("detail",
                "SetEnumOriginalsForDiff got a null original vtable — PROBE Y's "
                "enumeration differential is INERT this boot (no original to "
                "replay against)."));
        return;
    }
    g_origForEach.store(
        reinterpret_cast<ForEachOrigFn_t>(originalVtable[14]),
        std::memory_order_release);
    g_origFindFirst.store(
        reinterpret_cast<FindFirstOrigFn_t>(originalVtable[63]),
        std::memory_order_release);
    g_origFindNext.store(
        reinterpret_cast<FindNextOrigFn_t>(originalVtable[64]),
        std::memory_order_release);
    g_origFindClose.store(
        reinterpret_cast<FindCloseOrigFn_t>(originalVtable[65]),
        std::memory_order_release);
    LOG_INFO_KV(kCat, "enum_originals_captured",
        kcdx::log::KV::BareStr("detail",
            "PROBE Y captured the engine ORIGINAL enumeration slots (14 "
            "ForEachFile, 63 FindFirst, 64 FindNext, 65 FindClose) from the live "
            "object's original vtable — the boot-window differential replays each "
            "kcdx enumeration through these and logs ENUM_DIFF on a set mismatch."));
}

void ReplayAndDiffFind(void* self, const char* pattern,
                       const std::vector<std::string>& kcdxNames,
                       const std::vector<uint8_t>& /*kcdxIsDir*/) {
    if (!BootWindowActive()) return;  // predicted-skip after boot
    if (!self || !pattern) return;
    FindFirstOrigFn_t ff = g_origFindFirst.load(std::memory_order_acquire);
    FindNextOrigFn_t  fn = g_origFindNext.load(std::memory_order_acquire);
    FindCloseOrigFn_t fc = g_origFindClose.load(std::memory_order_acquire);
    if (!ff || !fn || !fc) return;  // no captured original → nothing to compare.

    // Replay the SAME pattern through the engine original iterator. The buffer
    // is local + zeroed; the original fills it with each entry's find-data
    // (name @ +0x24, dir bit @ +0x00). Drain via the original FindNext, close
    // via the original FindClose — the iterator lives + dies in the original's
    // own runtime; no kcdx handle crosses in.
    // CONTRACT (body-verified, _research/ki0028-findfirst-replay-contract-recon/
    // FINDINGS.md): the engine original FindFirst returns a CCryPakFindData OBJECT
    // POINTER on success / -1 on no-match; FindNext returns >=0 to continue, -1 to
    // stop; the find-data header bytes 0x01..0x23 carry FindNext's ITERATION STATE.
    // THE RUNAWAY BUG (PROBE Y.2): the drain memset the buffer before EVERY
    // FindNext, wiping that state → the engine restarted from entry 0 each call →
    // infinite loop. THE FIX: zero the buffer ONCE before FindFirst, NEVER inside
    // the drain loop — exactly as the engine consumer does (it reuses local_158
    // un-zeroed across FindNext calls).
    alignas(16) uint8_t buf[kBufBytes];
    std::memset(buf, 0, sizeof(buf));  // ONCE — the engine consumer never re-zeros.
    std::vector<std::string> vanillaNames;
    const intptr_t h = ff(self, pattern, buf, /*flags=*/0);
    if (h >= 0) {
        bool dummyDir = false;
        vanillaNames.push_back(ReadFindName(buf, &dummyDir));
        // FindNext returns >=0 to continue, -1 when exhausted (body-verified). The
        // buffer is NOT re-zeroed — its header carries the engine's iteration state.
        while (true) {
            const intptr_t more = fn(self, h, buf);
            if (more < 0) break;
            vanillaNames.push_back(ReadFindName(buf, &dummyDir));
            if (vanillaNames.size() > 200000) break;  // defensive backstop only.
        }
        fc(self, h);
    }
    // (h < 0 → vanilla found no match for this pattern; an empty vanilla set vs
    //  a non-empty kcdx set IS a divergence DiffAndLog will surface.)

    DiffAndLog("FindFirst", pattern, kcdxNames, vanillaNames);
}

void ReplayAndDiffForEach(void* self, const char* pattern,
                          const std::vector<std::string>& kcdxNames) {
    if (!BootWindowActive()) return;  // predicted-skip after boot
    if (!self || !pattern) return;
    ForEachOrigFn_t fe = g_origForEach.load(std::memory_order_acquire);
    if (!fe) return;  // no captured original → nothing to compare.

    // Replay the SAME pattern through the engine original ForEachFile, passing
    // OUR probe collector as the per-file callback (cbCtx) + a vector as its
    // userData. The original walks its own disk+pak set and fires the collector
    // per entry; we extract the base name. No kcdx state threads in.
    std::vector<std::string> vanillaNames;
    PerFileCallbackFn_t collector = &ProbeForEachCollect;
    fe(self, reinterpret_cast<void*>(collector), pattern,
       static_cast<void*>(&vanillaNames));

    DiffAndLog("ForEachFile", pattern, kcdxNames, vanillaNames);
}

}  // namespace kcdx::fs_takeover
