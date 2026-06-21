#pragma once

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
constexpr uint64_t kProbeI_ExtraFrames = 600;  // ~2.5s at 240fps — covers render/UI init

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
