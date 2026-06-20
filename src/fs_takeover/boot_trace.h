#pragma once

#include <cstdint>  // uintptr_t (PROBE L object-member snapshot)
#include <cstring>  // std::memcpy (PROBE L unaligned member read)

#include "../init_phase.h"
#include "../log.h"

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

// True while boot / graphics-init is in flight (before the first update tick).
// One relaxed-atomic load + a compare; inlined at each slot call site. After
// AfterGameApply this is a predicted-skip branch (the phase enum is monotonic).
inline bool BootWindowActive() {
    return kcdx::init::Current() < kcdx::init::InitPhase::AfterGameApply;
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
inline void TraceRead(const char* slot, long long handle) {
    if (!BootWindowActive()) return;  // predicted-skip after boot
    LOG_DEBUG_KV("FS_BOOT_TRACE", "read",
        kcdx::log::KV::BareStr("slot", slot),
        kcdx::log::KV("handle", handle),
        kcdx::log::KV("tag", static_cast<long long>(handle & 1)));
}

// === DIAGNOSTIC (PROBE N) — wide whole-object image diff ======================
// KI-0026: the missing-write OFFSET is unknown (the pak-vector [+0x40] reading was
// falsified for loose files; the registration-call theory is unread/ambiguous).
// PROBE N observes the divergence DIRECTLY without pre-guessing any offset: snapshot
// the WHOLE object image before/after the engine original open and before/after the
// kcdx open, and REPORT which 8-byte offsets each open wrote. The orchestration (the
// two opens + close) lives in the slot-36 marker (it needs kcdx_FOpen + the captured
// originals); these two helpers are the pure snapshot + diff primitives.
//
// kProbeN_ObjSize: snapshot width. Err HIGH — over-reading read-only object memory
// is safe; a too-small window is the only way to MISS the divergence. 0x400 covers
// well past the highest member offset any FOpen-body write touches ([+0x50], [+0x268]
// is a VTABLE offset not an object member).
constexpr size_t kProbeN_ObjSize = 0x400;

// Snapshot `self[0 .. kProbeN_ObjSize)` into `dst` (a caller-provided buffer of at
// least kProbeN_ObjSize bytes). Read-only byte copy; no allocation. Caller gates on
// BootWindowActive() + non-null self.
inline void SnapObject(uint8_t* dst, const void* self) {
    std::memcpy(dst, self, kProbeN_ObjSize);
}

// Log every 8-byte offset where snapshot `a` differs from `b`, under FS_BOOT_TRACE
// action "objdiff". `label` names the transition (e.g. "engine" = A→B = what the
// engine open wrote; "kcdx" = A→D = what kcdx wrote). `vpath` is the file. One log
// line per differing word (offset + before + after). Boot-window-gated by the
// caller. A diff with ZERO differing words logs a single "objdiff label=… diffs=0"
// summary so a no-write outcome is explicit, not silent.
inline void LogObjDiff(const char* label, const char* vpath,
                       const uint8_t* a, const uint8_t* b) {
    long long diffs = 0;
    for (size_t off = 0; off + 8 <= kProbeN_ObjSize; off += 8) {
        uintptr_t wa = 0, wb = 0;
        std::memcpy(&wa, a + off, sizeof(wa));
        std::memcpy(&wb, b + off, sizeof(wb));
        if (wa != wb) {
            ++diffs;
            LOG_DEBUG_KV("FS_BOOT_TRACE", "objdiff",
                kcdx::log::KV::BareStr("label", label),
                kcdx::log::KV("vpath", vpath ? vpath : "<null>"),
                kcdx::log::KV("off", static_cast<long long>(off)),
                kcdx::log::KV("before", static_cast<long long>(wa)),
                kcdx::log::KV("after", static_cast<long long>(wb)));
        }
    }
    LOG_DEBUG_KV("FS_BOOT_TRACE", "objdiff_summary",
        kcdx::log::KV::BareStr("label", label),
        kcdx::log::KV("vpath", vpath ? vpath : "<null>"),
        kcdx::log::KV("diffs", diffs));
}

}  // namespace kcdx::fs_takeover
