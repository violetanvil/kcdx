// CAP-118 — standalone dependency stub for compiling the engine find_slots.cpp
// into the test-plugin DLL.
//
// find_slots.cpp is engine source. To compile it standalone into this test DLL
// (the cap-114 shape — a self-contained artifact built against include/ + the one
// engine source under test, NOT linked against the engine), the external symbols
// it references must be supplied here so they are not unresolved at link time.
// find_slots.cpp references:
//   - kcdx::asset_overlay::NormalizeVPath — the key fold (REAL impl below, so the
//     test's index inserts + the prefix + the de-dup all agree on the SAME fold
//     the engine uses; the pure core under test calls it);
//   - the kcdx::fs_takeover handle-pool find ops (MintFind/FindPeek/FindAdvance/
//     Close) — reached ONLY by the slot impls (kcdx_FindFirst/FindNext/FindClose),
//     which the test does NOT call; trivial link-completeness stubs;
//   - kcdx::fs_takeover::kcdx_AdjustFileName (slot-1 resolution) + GetBuiltIndex
//     — reached ONLY by kcdx_FindFirst; trivial stubs;
//   - kcdx::init::Current() — reached ONLY through boot_trace.h's BootWindowActive
//     (in the slot impls' TraceEnum); a stub returning the post-boot phase makes
//     BootWindowActive false, so even a stray call is a no-op;
//   - the kcdx::log symbols its LOG_*_KV diagnostics expand to (no-op).
//
// The test exercises BuildUnifiedFindEntries + FillFindData (the pure core, which
// needs only NormalizeVPath); the rest are link-completeness stubs for the slot
// impls the test does not drive. find_slots.cpp stays BYTE-IDENTICAL between the
// engine build and this test build (no #ifdef carve-out in production source).

#include <cstdarg>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "../../src/asset_overlay.h"
#include "../../src/fs_takeover/asset_index.h"
#include "../../src/fs_takeover/file_handle.h"
#include "../../src/fs_takeover/open_slots.h"
#include "../../src/init_phase.h"
#include "../../src/log.h"

// === asset_overlay::NormalizeVPath — the REAL fold (lowercase + '\' -> '/'). ==
// Must match src/asset_overlay.cpp exactly (the test keys its index inserts +
// the prefix with it, and the core's de-dup normalizes base names with it).
namespace kcdx::asset_overlay {

std::string NormalizeVPath(const std::string& vpath) {
    std::string out;
    out.reserve(vpath.size());
    for (char c : vpath) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        out.push_back(c);
    }
    return out;
}

}  // namespace kcdx::asset_overlay

// === fs_takeover slot-impl deps — link-completeness stubs (test never calls
//     the slot impls). =========================================================
namespace kcdx::fs_takeover {

// Handle-pool find ops — the slot impls mint/iterate; the test drives only the
// pure core, so these are never reached. Trivial well-defined stubs.
KcdxHandle MintFind(std::vector<std::string>&& /*names*/,
                    std::vector<uint8_t>&& /*isDir*/) { return 0; }
bool FindPeek(KcdxHandle /*h*/, std::string* /*outName*/, bool* /*outIsDir*/) {
    return false;
}
bool FindAdvance(KcdxHandle /*h*/) { return false; }
int Close(KcdxHandle /*h*/) { return 0; }

// slot-1 resolution + the process-lifetime built index — reached only by
// kcdx_FindFirst. Stubs (an empty index, an identity resolve).
void* kcdx_AdjustFileName(void* /*self*/, const char* /*pName*/, void* outBuf,
                          uint32_t /*nFlags*/) { return outBuf; }
const AssetIndex& GetBuiltIndex() {
    static const AssetIndex kEmpty;
    return kEmpty;
}

// FoldEngineAliasToIndexKey — reached by IndexDirPrefix (the slot impls' prefix
// fold). The test's index keys carry no engine alias (%engine%/data/gameshaders),
// so an identity stub is correct for the pure core under test.
void FoldEngineAliasToIndexKey(std::string& /*key*/) {}

// BootWatchTickCount — reached only through boot_trace.h's BootWindowActive (the
// PROBE I extended-window check). A 0 tick count keeps the extended window
// inactive; paired with init::Current()==AfterGameApply, BootWindowActive() is
// false, so the slot impls' trace calls (incl. the PROBE W differential) are
// no-ops in the test build.
uint64_t BootWatchTickCount() { return 0; }

}  // namespace kcdx::fs_takeover

// === init::Current — post-boot phase so BootWindowActive() is false. =========
namespace kcdx::init {
InitPhase Current() { return InitPhase::AfterGameApply; }
}  // namespace kcdx::init

// === kcdx::log — no-op emitters (mirrors cap-114's stub). ====================
namespace kcdx::log {

bool IsCategoryEnabled(const char* /*category*/) { return false; }

void EmitEngine(Level, const char*, const char*) {}

void EmitEngineKV(Level, const char*, const char*,
                  std::initializer_list<KV>) {}

namespace detail {
void FormatTo(char* buf, size_t bufsize, const char* fmt, ...) {
    if (!buf || bufsize == 0) return;
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, bufsize, fmt, ap);
    va_end(ap);
}
}  // namespace detail

KV::KV(const char* key, const std::string& val)
    : k(key), kind(STR), sv(val.c_str()), svn(val.size()) {}
KV::KV(const char* key, long long val)
    : k(key), kind(INT), i(val) {}
KV::KV(const char* key, unsigned long long val)
    : k(key), kind(UINT), u(val) {}
KV KV::BareStr(const char* key, const char* val) {
    KV kv(key, val);
    kv.kind = BARE_STR;
    return kv;
}
KV::KV(const char* key, const char* val)
    : k(key), kind(STR), sv(val), svn(val ? std::char_traits<char>::length(val) : 0) {}

}  // namespace kcdx::log
