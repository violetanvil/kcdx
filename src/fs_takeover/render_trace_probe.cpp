// === DIAGNOSTIC (PROBE Z10) — KI-0028 ordered differential render-submission trace ===
// See render_trace_probe.h for WHY + the ordered site list + the outcome->meaning map.
// NO-RESIDUE on retire (file + seating arm + CMakeLists), capture to _research/probe-archive.

#include "render_trace_probe.h"

#include <windows.h>
#include <intrin.h>  // _ReturnAddress

#include <atomic>
#include <cstdint>

#include "MinHook.h"
#include "../log.h"
#include "../pe_helpers.h"  // kcdx::pe::OpenModule / ModuleView

namespace kcdx::fs_takeover {

namespace {

using KV = ::kcdx::log::KV;
constexpr const char* kCat = "RENDER_TRACE";

// SOURCE: static Ghidra (_research/ki0028-differential-trace-recon/FINDINGS.md).
// Scratch RVAs for THIS boot's ordered observation — diagnostic ground-truth reads,
// NOT resolved-by-name address targets (same scratch-RVA shape as PROBE Z9/Y/S); not
// an AP1 hardcoded resolution. Image base 0x180000000; these are module-relative.
constexpr uint32_t kRvaStageSequencer = 0x86b574;  // FUN_18086b574(this, stage_id)
constexpr uint32_t kRvaCompilePass    = 0x429384;  // FUN_180429384(ctx) per-frame CCRO compile
constexpr uint32_t kRvaCcroCompile    = 0x429794;  // FUN_180429794(...8 args...) -> char
constexpr uint32_t kRvaSetIndexBuffer = 0x5025b4;  // FUN_1805025b4(cmdIf, ibDesc)  ★
constexpr uint32_t kRvaRenderFlush    = 0x777f6c;  // FUN_180777f6c(...) render-thread flush

// The WHGame base captured at arm — the _ReturnAddress caller-attribution subtracts it
// so the logged caller RVA is module-relative + directly comparable across arms/runs.
uintptr_t g_whBase = 0;

// A per-site cap so a per-frame site's 14k repeats do not bury the first-frame
// divergence (the Z9 per-frame-trap lesson). We keep the FIRST kMaxPerSite fires of
// each site; the global sequence number preserves the true interleaved order.
constexpr uint64_t kMaxPerSite = 6;

std::atomic<bool>     g_armed{false};
std::atomic<uint64_t> g_seq{0};  // global monotonic sequence across all sites

// Per-site fire counters (relaxed — diagnostic tallies, no happens-before needed;
// the log line itself is the ordered record, the counter only gates the latch).
std::atomic<uint64_t> g_fires[5] = {};  // [stage, compile, ccroCompile, setIB, flush]

enum SiteId { kSiteStage = 0, kSiteCompile, kSiteCcroCompile, kSiteSetIB, kSiteFlush };
constexpr const char* kSiteName[5] = {
    "stage_sequencer", "compile_pass", "ccro_compile", "set_index_buffer", "render_flush"};

// Emit one ordered record if this site is still under its per-site cap. Returns the
// sequence number assigned (or 0 if capped — the caller still forwards regardless).
uint64_t RecordFire(int site, uint64_t detailA, uint64_t detailB, const char* note) {
    const uint64_t n = g_fires[site].fetch_add(1, std::memory_order_relaxed);
    if (n >= kMaxPerSite) return 0;  // latched — do not log past the cap
    const uint64_t seq = g_seq.fetch_add(1, std::memory_order_relaxed);
    LOG_WARN_KV(kCat, "seq",
        KV("n",        seq),
        KV::BareStr("site", kSiteName[site]),
        KV("fire",     n),          // 0..kMaxPerSite-1: which of this site's first fires
        KV("a",        detailA),    // site-specific: stage id / caller-RVA / return val
        KV("b",        detailB),    // site-specific secondary
        KV::BareStr("note", note));
    return seq;
}

// --- Site 1: render-stage sequencer FUN_18086b574(this, stage_id) ------------------
// 2-arg __fastcall (rcx=this, edx=stage_id). Capture the stage id — stage 4 runs the
// compile pass; whether swap-ON ever reaches stage 4 is the first divergence candidate.
using StageFn_t = void(__fastcall*)(void* thisObj, int stageId);
StageFn_t g_origStage = nullptr;
void __fastcall HookedStage(void* thisObj, int stageId) {
    RecordFire(kSiteStage, static_cast<uint64_t>(static_cast<uint32_t>(stageId)),
               reinterpret_cast<uint64_t>(thisObj),
               "render-stage transition; a=stage_id (4=compile stage)");
    g_origStage(thisObj, stageId);
}

// --- Site 2: per-frame CCRO compile pass FUN_180429384(ctx) ------------------------
// 1-arg __fastcall. Records the compile pass ran (produces the compiled objects the
// draw loop submits). Absent swap-ON => the compile pass itself is not reached.
using CompileFn_t = void(__fastcall*)(void* ctx);
CompileFn_t g_origCompile = nullptr;
void __fastcall HookedCompile(void* ctx) {
    RecordFire(kSiteCompile, reinterpret_cast<uint64_t>(ctx), 0,
               "per-frame CCRO compile pass entered");
    g_origCompile(ctx);
}

// --- Site 3: CCRO::Compile FUN_180429794(...8 args...) -> char ---------------------
// The 8-arg ABI is READ from the caller decomp (FINDINGS _dr_ccro_compile):
//   (ptr, u16, ptr, u64, u16, ptr, ptr, u8) -> char. Declaring the FULL signature is
// REQUIRED for a safe trampoline (a truncated forward would corrupt the stack args).
// We capture the RETURN (0 = "Compile failed, PSO creation failed" -> no compiled obj).
using CcroCompileFn_t = char(__fastcall*)(void* a, uint16_t b, void* c, uint64_t d,
                                          uint16_t e, void* f, void* g, uint8_t h);
CcroCompileFn_t g_origCcroCompile = nullptr;
char __fastcall HookedCcroCompile(void* a, uint16_t b, void* c, uint64_t d,
                                  uint16_t e, void* f, void* g, uint8_t h) {
    const char ret = g_origCcroCompile(a, b, c, d, e, f, g, h);
    RecordFire(kSiteCcroCompile, static_cast<uint64_t>(static_cast<uint8_t>(ret)),
               reinterpret_cast<uint64_t>(a),
               "CCRO::Compile returned; a=retval (0=compile/PSO FAIL, no compiled obj)");
    return ret;
}

// --- Site 4: ★ engine SetIndexBuffer FUN_1805025b4(cmdIf, ibDesc) ------------------
// 2-arg __fastcall (rcx=command-interface this, rdx=ib descriptor). The DECISIVE leaf:
// ends in the indirect D3D12 IASetIndexBuffer. _ReturnAddress() names WHICH of its 6
// callers fired (module-relative RVA) — the diff shows which caller stops firing swap-ON.
using SetIBFn_t = void(__fastcall*)(void* cmdIf, void* ibDesc);
SetIBFn_t g_origSetIB = nullptr;
void __fastcall HookedSetIB(void* cmdIf, void* ibDesc) {
    const uintptr_t retAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());
    const uint64_t callerRva = (g_whBase && retAddr >= g_whBase)
        ? static_cast<uint64_t>(retAddr - g_whBase) : retAddr;
    RecordFire(kSiteSetIB, callerRva, reinterpret_cast<uint64_t>(cmdIf),
               "engine SetIndexBuffer (IASetIndexBuffer leaf); a=caller RVA (which of 6)");
    g_origSetIB(cmdIf, ibDesc);
}

// --- Site 5: render-thread command flush FUN_180777f6c(...) ------------------------
// Top of the submit path (9 callers). 1-arg forward is ABI-safe for observation (we do
// not read args 2+; the register args in place are preserved by the trampoline call).
using FlushFn_t = void(__fastcall*)(void* a);
FlushFn_t g_origFlush = nullptr;
void __fastcall HookedFlush(void* a) {
    RecordFire(kSiteFlush, reinterpret_cast<uint64_t>(a), 0,
               "render-thread command flush entered");
    g_origFlush(a);
}

// Install one after-hook at base+rva. Logs the outcome; a failure is loud (not silent).
bool ArmSite(uintptr_t base, uint32_t rva, void* detour, void** origOut,
             const char* label) {
    void* target = reinterpret_cast<void*>(base + rva);
    MH_STATUS s = MH_CreateHook(target, detour, origOut);
    if (s != MH_OK && s != MH_ERROR_ALREADY_CREATED) {
        LOG_ERROR_KV(kCat, "hook_create_failed",
            KV::BareStr("site", label), KV("rva", (uint64_t)rva),
            KV("mh_status", (uint64_t)s));
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        LOG_ERROR_KV(kCat, "hook_enable_failed",
            KV::BareStr("site", label), KV("rva", (uint64_t)rva));
        return false;
    }
    return true;
}

}  // namespace

void RenderTraceProbeStart() {
    bool expected = false;
    if (!g_armed.compare_exchange_strong(expected, true,
                                         std::memory_order_acq_rel)) {
        return;  // already armed
    }
    MH_STATUS mi = MH_Initialize();
    if (mi != MH_OK && mi != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_ERROR_KV(kCat, "mh_init_failed", KV("mh_status", (uint64_t)mi));
        g_armed.store(false, std::memory_order_release);
        return;
    }

    kcdx::pe::ModuleView whgame{};
    if (!kcdx::pe::OpenModule(L"WHGame.dll", whgame) || !whgame.base) {
        LOG_ERROR_KV(kCat, "whgame_not_loaded", KV::BareStr("detail",
            "WHGame.dll not resolvable — cannot arm the render trace."));
        g_armed.store(false, std::memory_order_release);
        return;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(whgame.base);
    g_whBase = base;

    int armed = 0;
    armed += ArmSite(base, kRvaStageSequencer, reinterpret_cast<void*>(&HookedStage),
                     reinterpret_cast<void**>(&g_origStage), "stage_sequencer") ? 1 : 0;
    armed += ArmSite(base, kRvaCompilePass, reinterpret_cast<void*>(&HookedCompile),
                     reinterpret_cast<void**>(&g_origCompile), "compile_pass") ? 1 : 0;
    armed += ArmSite(base, kRvaCcroCompile, reinterpret_cast<void*>(&HookedCcroCompile),
                     reinterpret_cast<void**>(&g_origCcroCompile), "ccro_compile") ? 1 : 0;
    armed += ArmSite(base, kRvaSetIndexBuffer, reinterpret_cast<void*>(&HookedSetIB),
                     reinterpret_cast<void**>(&g_origSetIB), "set_index_buffer") ? 1 : 0;
    armed += ArmSite(base, kRvaRenderFlush, reinterpret_cast<void*>(&HookedFlush),
                     reinterpret_cast<void**>(&g_origFlush), "render_flush") ? 1 : 0;

    LOG_INFO_KV(kCat, "render_trace_armed",
        KV("sites_armed", (uint64_t)armed),
        KV("wh_base", base),
        KV::BareStr("detail",
            "PROBE Z10: ordered render-submission trace armed at 5 sites (stage "
            "sequencer / compile pass / CCRO::Compile / engine SetIndexBuffer / render "
            "flush). Each emits sequenced RENDER_TRACE seq records (first 6 fires/site). "
            "Run swap-OFF (menu) + swap-ON (black); diff the two seq sequences for the "
            "FIRST site that fires on the menu arm but is absent/differs on the black "
            "arm — that is the divergence (DESIGN.md step 3)."));
}

}  // namespace kcdx::fs_takeover
