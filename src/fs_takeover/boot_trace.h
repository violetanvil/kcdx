#pragma once

#include <windows.h>
#include <cstdint>
#include <string>  // VpathForHandle resolution (read-family trace names its file)
#include <vector>  // TraceEnumNames entry-name sample

#include "../init_phase.h"
#include "../log.h"
#include "file_handle.h"  // VpathForHandle + KcdxHandle (name a read's file)

// kcdx::fs_takeover boot-window FS-slot trace.
//
// Every CCryPak slot kcdx serves logs one factual line under the FS_BOOT_TRACE
// tag while boot is in flight: which slot ran, the path it was given, which
// resolution branch it took (`how`), and the raw result value. The log states
// what the code did and where — no verdict, no comparison, no inference.
//
// The gate — near-zero steady-state cost: the trace fires ONLY while
// init::Current() < AfterGameApply (the last init phase, the first update tick).
// Once past it the enum only advances, so the check is one relaxed-atomic load +
// a predicted-skip branch forever after boot. Zero allocation on the traced
// path: the path/vpath is logged by the borrowed inbound const char*, the slot +
// how by string literals, the result as an integer.
//
// Greppable tag: "FS_BOOT_TRACE" — the whole boot-window slot stream is one grep.

namespace kcdx::fs_takeover {

// True while boot / graphics-init is in flight (init phase before the first
// update tick). One relaxed-atomic load + a compare; inlined at each call site.
inline bool BootWindowActive() {
    return kcdx::init::Current() < kcdx::init::InitPhase::AfterGameApply;
}

// Trace a metadata / existence slot call (a by-name query: IsFileExist,
// GetFileSize, GetFileAttributes, …). `result` is the slot's answer as an
// integer (exists 0/1, a size, a returned int). `how` names which branch
// answered ("index-pak" / "index-loose" / "index" / "original") — the code path
// that actually ran. Allocation-free.
inline void TraceMeta(const char* slot, const char* vpath, const char* how,
                      long long result) {
    if (!BootWindowActive()) return;
    LOG_DEBUG_KV("FS_BOOT_TRACE", "meta",
        kcdx::log::KV::BareStr("slot", slot),
        kcdx::log::KV::BareStr("how", how),
        kcdx::log::KV("vpath", vpath ? vpath : "<null>"),
        kcdx::log::KV("result", result));
}

// Trace an open-family slot call (FOpen / AdjustFileName). `vpath` is the inbound
// name; `disk` is the resolved disk path (or the pak path / "" when not
// applicable); `how` names the branch taken ("index-loose" / "index-pak" /
// "miss-original" / "resolve"); `result` is the open/resolve outcome (a non-zero
// kcdx handle id, 0 on a failed open). Allocation-free.
inline void TraceOpen(const char* slot, const char* vpath, const char* disk,
                      const char* how, long long result) {
    if (!BootWindowActive()) return;
    LOG_DEBUG_KV("FS_BOOT_TRACE", "open",
        kcdx::log::KV::BareStr("slot", slot),
        kcdx::log::KV::BareStr("how", how),
        kcdx::log::KV("vpath", vpath ? vpath : "<null>"),
        kcdx::log::KV("disk", disk ? disk : ""),
        kcdx::log::KV("result", result));
}

// Trace an enumeration slot call (ForEachFile). `pattern` is the inbound walk
// pattern; `matched` is the number of entries the walk fired the callback for.
// Allocation-free.
inline void TraceEnum(const char* slot, const char* pattern, long long matched) {
    if (!BootWindowActive()) return;
    LOG_DEBUG_KV("FS_BOOT_TRACE", "enum",
        kcdx::log::KV::BareStr("slot", slot),
        kcdx::log::KV("pattern", pattern ? pattern : "<null>"),
        kcdx::log::KV("matched", matched));
}

// Trace an enumeration AND the entry NAMES it returned (a count alone can't tell
// one result set from another). Logs the pattern, the total count, and a capped
// sample of the returned base names. Gated to the boot window; the string build
// happens only when the gate is open (a cold path). `names` is the entry set the
// walk seeded.
constexpr size_t kEnumSampleCap = 24;  // names logged before truncating
inline void TraceEnumNames(const char* slot, const char* pattern,
                           const std::vector<std::string>& names) {
    if (!BootWindowActive()) return;
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
// … — the handle-operating slots). The read family carries no path — only the
// opaque handle. `handle` is the raw value received; `tag` is its low bit (1 = a
// kcdx-minted handle `(id<<1)|1`; 0 = a value whose kcdx tag is clear). `want`/
// `got` are bytes-requested/bytes-returned for a read (pass -1 for a non-read
// handle op — FSeek/FTell/FClose/FEof, which move no payload); `ok` is the op's
// success. The vpath is resolved from the handle inside the boot-window gate (a
// cold path), so the read hot path after boot is a predicted skip. Allocation-
// free after boot.
inline void TraceRead(const char* slot, long long handle, long long want,
                      long long got, bool ok) {
    if (!BootWindowActive()) return;
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
