#pragma once

#include <windows.h>  // === DIAGNOSTIC (PROBE W) === GetModuleHandleW + IMAGE_* for BootTraceCallerRva
#include <atomic>   // === DIAGNOSTIC (PROBE W) === lock-free WHGame bounds + once-per-caller gate
#include <cstdint>  // uintptr_t (PROBE L object-member snapshot)
#include <cstring>  // std::memcpy (PROBE L unaligned member read)
#include <string>   // VpathForHandle resolution (FS-op trace contract)
#include <vector>   // TraceEnumNames entry-name sample (FS-op trace contract)

#include "../init_phase.h"
#include "../log.h"
#include "boot_watch.h"  // === DIAGNOSTIC (PROBE I) === BootWatchTickCount for the extended window
#include "file_handle.h"  // VpathForHandle + KcdxHandle (FS-op trace contract — name the read's file)

// kcdx::fs_takeover boot-window FS-slot trace — a PERMANENT kept diagnostic.
//
// WHY THIS EXISTS (KI-0026): the graphics-init 0xC8 CryFatalError (NGX/FSR2,
// C_Game::CreateInstance) is a BLACK BOX. The dev log shows kcdx's last actions,
// then the engine fatals ~240ms later with NOTHING about which file ops
// graphics-init drove through kcdx's CCryPak slots in that window — the
// one-shot g_loggedFirst* latches in open/metadata/enum slots log only the FIRST
// op of the session, which is insufficient: the crash window needs EVERY op
// (path + slot + result), not just the first. This trace records all of them
// while init is in flight, so the next crash launch shows the full crash-window
// slot stream under one greppable tag.
//
// NOT A SCRATCH PROBE — the working-artifacts no-residue rule does NOT apply.
// This is a kept observability tool: it stays in source as the permanent
// crash-window slot trace, gated to near-zero cost after boot.
//
// THE GATE — near-zero steady-state cost (the load-bearing constraint):
//   BootTrace fires ONLY while init::Current() < AfterGameApply. AfterGameApply
//   is the LAST init phase (ctx C, first update tick) — so the gate is TRUE for
//   all of boot / graphics-init and FALSE forever after. The cost on the traced
//   path forever after boot is EXACTLY ONE relaxed-atomic load of the monotonic
//   phase enum + a predicted-skip branch (the enum only advances, so once past
//   AfterGameApply it is ALWAYS past — the CPU predicts the skip). ZERO
//   allocation: the path/vpath is logged BY POINTER (the inbound const char*,
//   borrowed), the slot by a string literal, the result as an integer/bool — no
//   std::string construction, no string building on the traced path. Same
//   hot-path discipline as the existing first-only latches, applied to a
//   bounded window instead of a single fire. The gate is the monotonic
//   init-phase compare (init::Current() vs AfterGameApply).
//
// Greppable tag: "FS_BOOT_TRACE" — the entire crash-window slot stream is one
// grep of the dev log.

namespace kcdx::fs_takeover {

// === DIAGNOSTIC (PROBE I) — KI-0028 render/UI-init trace-window extension ===
// The ORIGINAL gate stopped at AfterGameApply (the first update tick) — but
// KI-0028's failure (main loop runs, no render, no input) lives in the render/UI
// init that runs AFTER the first tick, which the original window left DARK. This
// extends the window kProbeI_ExtraFrames frames PAST the first tick so every FS
// slot op the render/UI init drives (open/read/metadata/find, with vpath+slot+
// how+result) is traced. Frame-counted via the existing P-H heartbeat
// (BootWatchTickCount), so the window stops at a deterministic, reproducible
// point tied to engine progress, NOT a wall-clock timer (logging.md/polling.md).
// NO-RESIDUE: removed when KI-0028 closes; the permanent gate reverts to the
// boot-phase compare below.
// KI-0028 freeze window: the wedge (C_Game::CreateInstance never completing) holds
// for 90+ s AFTER the first tick — far past the original 600-frame window, so the
// freeze period was DARK in the trace. Extend to cover it. At ~35 ticks/s the
// black-screen run reaches ~3000 ticks/min, so 200000 frames ≈ the freeze + margin.
// This is a DIAGNOSTIC widening for the active investigation (NOT the permanent
// near-zero-cost gate — reverted to the boot-phase compare on KI-0028 close).
constexpr uint64_t kProbeI_ExtraFrames = 200000;  // cover the post-init freeze window

// True while boot / graphics-init is in flight, EXTENDED through the first
// kProbeI_ExtraFrames update ticks (render/UI init). Boot phase: always on.
// After the first tick: on while tick <= kProbeI_ExtraFrames, then a predicted-
// skip (the tick counter is monotonic). One relaxed-atomic load + a compare per
// arm; inlined at each slot call site.
inline bool BootWindowActive() {
    if (kcdx::init::Current() < kcdx::init::InitPhase::AfterGameApply) {
        return true;  // boot / graphics-init still in flight
    }
    // Past the first tick: keep tracing the render/UI-init window (PROBE I).
    return BootWatchTickCount() <= kProbeI_ExtraFrames;
}

// Trace a metadata / existence slot call (a by-name query: IsFileExist,
// GetFileSize, GetFileAttributes, …). `result` is the slot's answer rendered as
// an integer (exists 0/1, a size, a returned int) — the caller picks the
// meaningful field. `how` names HOW it answered ("index-pak" / "index-loose" /
// "index" / "original" / "miss"), so the crash window shows whether kcdx served
// the op from its index or thunked the engine original. Allocation-free: vpath
// logged by the borrowed inbound pointer, slot/how by literals, result as int.
inline void TraceMeta(const char* slot, const char* vpath, const char* how,
                      long long result) {
    if (!BootWindowActive()) return;  // predicted-skip after boot
    LOG_DEBUG_KV("FS_BOOT_TRACE", "meta",
        kcdx::log::KV::BareStr("slot", slot),
        kcdx::log::KV::BareStr("how", how),
        kcdx::log::KV("vpath", vpath ? vpath : "<null>"),
        kcdx::log::KV("result", result));
}

// === DIAGNOSTIC (PROBE W) — vanilla-DIFFERENTIAL self-validation ============
// KI-0028: kcdx never FAILS (every serve correct), yet the engine silently makes
// a different successful decision under the swap. The observability gap is that
// the trace flags "kcdx answered X", never "kcdx answered X but VANILLA would
// have answered Y". This helper closes it: an index-HIT metadata slot that
// answers WITHOUT asking the original ALSO calls the captured original (read-only
// existence/size — idempotent, §-safe, the same captured bodies the miss arm
// thunks) and logs ONLY on a divergence. Silent when kcdx matches vanilla; SHOUTS
// the slot + vpath + both answers when it doesn't — the first time the log can
// say "this is where kcdx diverges from vanilla", attributed by vpath.
//
// A divergence on a geometry/UI-loader path = the mechanism, named. ZERO
// divergences logged across boot = the "kcdx answers differently" class is
// FALSIFIED by direct measurement (pivot off the filesystem).
//
// Cost: logs only on mismatch; the original call is a cold read-only metadata
// query (engine pak-dir + disk), the same the miss arm already makes. The caller
// returns kcdx's answer UNCHANGED — the original's answer is compared + discarded,
// so kcdx's behavior + the screen are identical (the log is the only delta).
// NO-RESIDUE: remove with PROBE W on retirement.
// === DIAGNOSTIC (PROBE W) — once-per-distinct-caller gate. The vanilla-
// differential's COST is the engine-original call it makes ALONGSIDE kcdx's answer
// (a pak-dir + disk query) on every HIT — during the boot storm the FS slots fire
// thousands of times/sec on worker threads, so doing the doubled original-call
// per-call pegs the CPU. The DIAGNOSTIC VALUE, though, is per-CALLER: once we know
// "caller X gets a divergent answer for SOME path," the 100th identical divergence
// from X adds nothing. So the differential runs only the FIRST time each distinct
// caller_rva is seen (a tiny bounded lock-free set) — full attribution coverage,
// near-zero steady cost. A caller of 0 (non-WHGame) is never gated in (skipped).
// NO-RESIDUE: remove with PROBE W. ===
// `kind` distinguishes the differential family (existence vs enum vs size) so the
// SAME caller is reported once PER family — an IsFileExist3 first-seen does not
// gate out that caller's later FindFirst differential. Mixed into the key.
inline bool BootTraceCallerFirstSeen(uintptr_t caller, uint32_t kind = 0) {
    if (caller == 0) return false;  // unattributable frame — do not run the cost.
    const uintptr_t key = caller ^ (static_cast<uintptr_t>(kind) * 0x9E3779B97F4A7C15ull);
    constexpr int kCap = 256;       // far more than the distinct FS callers at boot
    static std::atomic<uintptr_t> seen[kCap]{};
    const size_t h = (key >> 4) % kCap;  // cheap hash; collisions just under-report
    // Linear-probe a few slots: claim the first empty one for `key`, or report
    // already-seen if found. Lock-free CAS; bounded probe so the hot path is O(1).
    for (int i = 0; i < 8; ++i) {
        const size_t idx = (h + i) % kCap;
        uintptr_t cur = seen[idx].load(std::memory_order_relaxed);
        if (cur == key) return false;             // already ran for this caller+kind
        if (cur == 0) {
            uintptr_t expected = 0;
            if (seen[idx].compare_exchange_strong(expected, key,
                                                  std::memory_order_relaxed))
                return true;                       // first time — run the differential
            if (seen[idx].load(std::memory_order_relaxed) == key) return false;
        }
    }
    return false;  // set full / probe exhausted — stop running (fail-safe to cheap)
}

inline void TraceVanillaDiff(const char* slot, const char* vpath,
                             long long kcdxAnswer, long long vanillaAnswer,
                             uintptr_t caller = 0) {
    if (!BootWindowActive()) return;  // predicted-skip after boot
    if (kcdxAnswer == vanillaAnswer) return;  // MATCH — silent (the common case)
    // PROBE W caller attribution: the engine return address, module-relative
    // (subtract WHGame's base) so a divergence is tied to the engine SUBSYSTEM
    // that asked — the geometry/UI loader vs a harmless shader-include check. 0
    // when not captured (the older call sites). The caller resolves to an RVA the
    // disassembly maps to a function.
    LOG_WARN_KV("VANILLA_DIFF", "diverge",
        kcdx::log::KV::BareStr("slot", slot),
        kcdx::log::KV("vpath", vpath ? vpath : "<null>"),
        kcdx::log::KV("kcdx", kcdxAnswer),
        kcdx::log::KV("vanilla", vanillaAnswer),
        kcdx::log::KV("caller_rva", static_cast<long long>(caller)),
        kcdx::log::KV::BareStr("detail",
            "kcdx's index-HIT answer DIFFERS from what the engine original would "
            "return for this name — a correct-but-different-from-vanilla serve "
            "that can steer an engine loader down a different branch (load/skip/"
            "pick-a-different-source). This is the KI-0028 observability signal: "
            "kcdx did not FAIL, it answered DIFFERENTLY. The vpath names what; "
            "caller_rva names which engine subsystem asked; the decision it steers "
            "is downstream."));
}

// === DIAGNOSTIC (PROBE W) — WHGame module bounds, resolved ONCE at seat into
// plain atomics (NOT a function-local static — MSVC's thread-safe-init guard on a
// local static is a hidden lock checked on EVERY call, and these helpers run on
// the HOT multi-threaded FS path during boot; the guard serialized worker threads
// and pegged the CPU machine-wide). BootTraceResolveWhBounds() is called once at
// the seat (single-threaded, before any FS slot fires); the hot path only does
// two relaxed atomic LOADS + a range compare — no lock, no PEB walk. ===
inline std::atomic<uintptr_t> g_btWhBase{0};
inline std::atomic<uintptr_t> g_btWhEnd{0};

inline void BootTraceResolveWhBounds() {
    HMODULE m = GetModuleHandleW(L"WHGame.dll");
    if (!m) return;
    const uintptr_t base = reinterpret_cast<uintptr_t>(m);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(m);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    g_btWhEnd.store(base + nt->OptionalHeader.SizeOfImage, std::memory_order_relaxed);
    g_btWhBase.store(base, std::memory_order_release);  // publish base LAST
}

// Caller return-address → WHGame-relative RVA. Returns 0 when the caller is NOT
// in WHGame (kcdx-internal/thunk frame) or the bounds were not yet resolved. Hot
// path: two relaxed loads + a compare, lock-free.
inline uintptr_t BootTraceCallerRva(uintptr_t callerAddr) {
    const uintptr_t s_whBase = g_btWhBase.load(std::memory_order_acquire);
    const uintptr_t s_whEnd = g_btWhEnd.load(std::memory_order_relaxed);
    if (s_whBase && callerAddr >= s_whBase && callerAddr < s_whEnd) {
        return callerAddr - s_whBase;  // an RVA the WHGame disassembly maps.
    }
    return 0;  // caller not in WHGame — not an engine subsystem to attribute.
}

// === DIAGNOSTIC (PROBE W enum half) — the ENUM vanilla-differential. The
// metadata-existence differential (above) answers "does kcdx give a different
// EXISTENCE answer"; this answers the design's STRONGEST suspect — "does kcdx
// hand the caller a different directory LISTING than vanilla." kcdx's ForEachFile/
// FindFirst enumerates the UNIFIED set (the engine's on-disk entries PLUS the
// index's pak-resident vpaths under the same prefix). Vanilla's enum returns ONLY
// the disk/engine entries. So `count_pak_added` (the index-only pak entries kcdx
// adds) IS the unified-set delta over vanilla. Logged ONLY when the delta is
// non-zero (kcdx returned MORE than vanilla would), with the count split + caller.
// A geometry/UI-loader caller getting extra pak-virtual entries it would not see
// in vanilla can make it load/skip/iterate differently → the draw_indexed=0 lead.
// Read-only: the walk already computed both halves; this only logs the split.
// NO-RESIDUE: remove with PROBE W on retirement.
inline void TraceVanillaEnumDiff(const char* slot, const char* pattern,
                                 long long countDisk, long long countPakAdded,
                                 uintptr_t caller = 0) {
    if (!BootWindowActive()) return;  // predicted-skip after boot
    if (countPakAdded == 0) return;   // kcdx returned the SAME set as vanilla — silent
    if (!BootTraceCallerFirstSeen(caller, /*kind=enum*/3)) return;  // once per caller (log economy)
    LOG_WARN_KV("VANILLA_DIFF", "enum_diverge",
        kcdx::log::KV::BareStr("slot", slot),
        kcdx::log::KV("pattern", pattern ? pattern : "<null>"),
        kcdx::log::KV("count_vanilla", countDisk),
        kcdx::log::KV("count_kcdx", countDisk + countPakAdded),
        kcdx::log::KV("pak_added", countPakAdded),
        kcdx::log::KV("caller_rva", static_cast<long long>(caller)),
        kcdx::log::KV::BareStr("detail",
            "kcdx's enumeration returned MORE entries than the engine original "
            "would for this pattern — the unified-set delta (pak-resident vpaths "
            "the engine's own disk/pak walk would not list at this point). A "
            "loader that enumerates to decide what to load/iterate may behave "
            "differently with the extra entries. caller_rva names the subsystem; "
            "a geometry/UI-loader caller here is the KI-0028 lead."));
}

// Trace an open-family slot call (FOpen / AdjustFileName). `vpath` is the
// inbound name; `disk` is the resolved disk path (or the pak path / "" when not
// applicable); `result` is the open outcome (a non-zero kcdx handle id, 0 on a
// failed open) or the resolve outcome. `how` names the path taken
// ("index-loose" / "index-pak" / "miss-original" / "resolve"). Allocation-free:
// every string is a borrowed inbound/literal pointer, result an int.
inline void TraceOpen(const char* slot, const char* vpath, const char* disk,
                      const char* how, long long result) {
    if (!BootWindowActive()) return;  // predicted-skip after boot
    LOG_DEBUG_KV("FS_BOOT_TRACE", "open",
        kcdx::log::KV::BareStr("slot", slot),
        kcdx::log::KV::BareStr("how", how),
        kcdx::log::KV("vpath", vpath ? vpath : "<null>"),
        kcdx::log::KV("disk", disk ? disk : ""),
        kcdx::log::KV("result", result));
}

// Trace an enumeration slot call (ForEachFile). `pattern` is the inbound walk
// pattern; `matched` is the number of entries the unified walk fired the
// callback for. Allocation-free.
inline void TraceEnum(const char* slot, const char* pattern, long long matched) {
    if (!BootWindowActive()) return;  // predicted-skip after boot
    LOG_DEBUG_KV("FS_BOOT_TRACE", "enum",
        kcdx::log::KV::BareStr("slot", slot),
        kcdx::log::KV("pattern", pattern ? pattern : "<null>"),
        kcdx::log::KV("matched", matched));
}

// Trace an enumeration AND the ENTRY NAMES it returned (the KI-0027-class gap: a
// count alone can't tell a right result set from a wrong one — KI-0027 was a
// 528-entry over-match that looked identical to a correct walk by count). Logs
// the pattern, the total count, and a CAPPED sample of the returned base names
// (first kEnumSampleCap, joined) so a wrong/over/under-match is visible in the
// log. Gated (boot window only); the string build happens only when the gate is
// open — a cold path. `names` is the unified entry set FindFirst seeded.
constexpr size_t kEnumSampleCap = 24;  // names logged before truncating
inline void TraceEnumNames(const char* slot, const char* pattern,
                           const std::vector<std::string>& names) {
    if (!BootWindowActive()) return;  // predicted-skip after boot
    std::string sample;
    const size_t n = names.size();
    const size_t shown = n < kEnumSampleCap ? n : kEnumSampleCap;
    for (size_t i = 0; i < shown; ++i) {
        if (i) sample += ", ";
        sample += names[i];
    }
    if (n > shown) sample += " …(+" + std::to_string(n - shown) + " more)";
    LOG_DEBUG_KV("FS_BOOT_TRACE", "enum",
        kcdx::log::KV::BareStr("slot", slot),
        kcdx::log::KV("pattern", pattern ? pattern : "<null>"),
        kcdx::log::KV("matched", static_cast<long long>(n)),
        kcdx::log::KV("entries", sample.empty() ? "<none>" : sample.c_str()));
}

// Trace a READ-family slot call (FReadRaw / FSeek / FClose / FGetCachedFileData /
// … — the handle-operating slots, KI-0026 PROBE K). The read family carries NO
// path — only the opaque handle the engine hands back. `handle` is that raw
// value EXACTLY as received (the integer the engine passed into the member
// call); `tag` is its low bit (1 = a valid kcdx-minted handle `(id<<1)|1`; 0 = a
// value whose kcdx tag is CLEAR — a foreign handle the engine produced, or a
// kcdx handle the engine mutated, which a kcdx read slot should never legitimately
// receive). The decisive observables for the handle-id-straddle theory:
//   - WHETHER any read slot fires at all on the minted boot-window handle (`3`)
//     before the fatal — graphics-init routing through kcdx vs operating the
//     handle off our slots entirely.
//   - WHETHER any fire arrives with `tag=0` — the engine operating a non-kcdx
//     value through a kcdx read slot.
// Allocation-free: slot by literal, handle/tag as integers. No std::string, no
// path (the read family has none). After AfterGameApply this is a predicted-skip
// branch, same gate as the open/meta/enum traces above.
// `want`/`got` are the bytes-requested/bytes-returned for a read op (pass -1 for
// a non-read handle op — FSeek/FTell/FClose/FEof, which move no payload); `ok` is
// the op's success. The vpath is resolved from the handle (file_handle::
// VpathForHandle) — the systemic fix that lets a read line name its FILE instead
// of only an opaque id. Resolution happens INSIDE the boot-window gate (a cold,
// already-rare path), so the read hot path after boot is untouched (a predicted
// skip — no lock, no string).
inline void TraceRead(const char* slot, long long handle, long long want,
                      long long got, bool ok) {
    if (!BootWindowActive()) return;  // predicted-skip after boot
    const std::string vpath =
        kcdx::fs_takeover::VpathForHandle(static_cast<KcdxHandle>(handle));
    // KI-0028: the menu background video (.bk2) re-reads in a tight loop
    // (131072-byte chunks, many per second) and is PROVEN innocent + correctly
    // served — it drowns the freeze-period signal. Skip it so the non-video FS
    // the wedge cares about is readable. (Diagnostic filter for the active
    // investigation; removed with the probe on KI-0028 close.)
    if (vpath.find(".bk2") != std::string::npos) return;
    LOG_DEBUG_KV("FS_BOOT_TRACE", "read",
        kcdx::log::KV::BareStr("slot", slot),
        kcdx::log::KV("vpath", vpath.empty() ? "<unresolved>" : vpath.c_str()),
        kcdx::log::KV("handle", handle),
        kcdx::log::KV("tag", static_cast<long long>(handle & 1)),
        kcdx::log::KV("want", want),
        kcdx::log::KV("got", got),
        kcdx::log::KV::BareStr("result", ok ? "ok" : "FAIL"));
}

}  // namespace kcdx::fs_takeover
