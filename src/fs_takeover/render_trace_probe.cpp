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
// HOP 2 (HOP2-FINDINGS.md): the SetIndexBuffer CALLER — the per-item apply+submit fn.
// Walks its render-item list; SetIndexBuffer is gated on item+0x20 (the IB field). We
// sample that field per item, null vs non-null, to CONFIRM (not theorize) the null-IB
// hypothesis. Item-list at [ctx+0x298]..[ctx+0x2a0]; IB field at item+0x20 (plVar6[4]).
constexpr uint32_t kRvaApplyDraw      = 0x501cb0;  // FUN_180501cb0(ctx, cmdIf)
constexpr uintptr_t kApplyItemListBeg = 0x298;     // [ctx+0x298] = first item ptr
constexpr uintptr_t kApplyItemListEnd = 0x2a0;     // [ctx+0x2a0] = one-past-last item ptr
constexpr uintptr_t kItemIbFieldOff   = 0x20;      // item+0x20 = plVar6[4] = the IB pointer
// HOP 3 (_hop3_caller_{a,b} decomps): FUN_180501cb0's TWO callers — the render
// passes that DECIDE to submit. Both gate the call identically:
//   [ctx+0x298] != [ctx+0x2a0]                              (item list non-empty)
//   && [ctx+0x178] != 0 && *(char*)([ctx+0x178]+0x28) != 0  (technique ready)
// Caller B DIVERTS to FUN_1825400d4 (bypassing the apply+submit fn) when
// [ctx+0x2b0]'s +0x1a0 char flag is set; caller A early-returns on the same flag
// via FUN_1804ec9d4. Sampling the gate variables AT ENTRY decomposes the swap-ON
// bypass into WHICH condition fails (or shows the callers never fire at all).
constexpr uint32_t  kRvaPassCallerA  = 0x4ec3a0;  // FUN_1804ec3a0(ctx) -> void
constexpr uint32_t  kRvaPassCallerB  = 0x5014a0;  // FUN_1805014a0(ctx) -> u64
constexpr uintptr_t kCtxTechObjOff   = 0x178;     // [ctx+0x178] = technique/pass obj
constexpr uintptr_t kTechReadyOff    = 0x28;      //   its +0x28 ready flag (char)
constexpr uintptr_t kCtxDivertObjOff = 0x2b0;     // [ctx+0x2b0] = divert-carrier obj
constexpr uintptr_t kDivertFlagOff   = 0x1a0;     //   its +0x1a0 divert flag (char)

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

// HOP 2 IB-field sampler tallies (cumulative across the whole run — the null-rate IS
// the confirm signal; relaxed, diagnostic).
std::atomic<uint64_t> g_applyInvokes{0};   // FUN_180501cb0 invocations
std::atomic<uint64_t> g_itemsSeen{0};      // total render items walked
std::atomic<uint64_t> g_itemsIbNull{0};    // items with a NULL IB field (non-indexed)
std::atomic<uint64_t> g_itemsIbSet{0};     // items with a non-null IB field (indexed)

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

// --- HOP 2: FUN_180501cb0(ctx, cmdIf) — the per-item apply+submit fn ---------------
// After-hook: let it run, then read-only-walk its render-item list and tally the IB
// field (item+0x20) null vs non-null. The null-RATE swap-ON vs swap-OFF CONFIRMS or
// kills "the render items are built without index buffers swap-ON" (HOP2-FINDINGS map).
// SEH-guarded reads (engine memory; a torn/freed list must not fault the game).
uint64_t SafeReadU64(uintptr_t addr) {
    uint64_t v = 0;
    if (addr) __try {
        v = *reinterpret_cast<volatile uint64_t*>(addr);
    } __except (EXCEPTION_EXECUTE_HANDLER) { v = 0; }
    return v;
}

using ApplyDrawFn_t = void(__fastcall*)(void* ctx, void* cmdIf);
ApplyDrawFn_t g_origApplyDraw = nullptr;
void __fastcall HookedApplyDraw(void* ctx, void* cmdIf) {
    const uint64_t inv = g_applyInvokes.fetch_add(1, std::memory_order_relaxed);
    // Walk [ctx+0x298]..[ctx+0x2a0] — each entry is a render-item pointer; read item+0x20.
    const uintptr_t c = reinterpret_cast<uintptr_t>(ctx);
    uintptr_t cur = static_cast<uintptr_t>(SafeReadU64(c + kApplyItemListBeg));
    const uintptr_t end = static_cast<uintptr_t>(SafeReadU64(c + kApplyItemListEnd));
    uint64_t local_seen = 0, local_null = 0, local_set = 0;
    // Bound the walk defensively (a torn list must not spin) — the real list is small.
    for (int guard = 0; cur && cur != end && guard < 4096; ++guard, cur += sizeof(void*)) {
        const uintptr_t item = static_cast<uintptr_t>(SafeReadU64(cur));
        if (!item) break;
        const uint64_t ib = SafeReadU64(item + kItemIbFieldOff);
        ++local_seen;
        if (ib) ++local_set; else ++local_null;
    }
    g_itemsSeen.fetch_add(local_seen, std::memory_order_relaxed);
    g_itemsIbNull.fetch_add(local_null, std::memory_order_relaxed);
    g_itemsIbSet.fetch_add(local_set, std::memory_order_relaxed);
    // Log the first kMaxPerSite invocations' per-invocation breakdown (per-frame-safe).
    if (inv < kMaxPerSite) {
        LOG_WARN_KV(kCat, "apply_draw_items",
            KV("inv",       inv),
            KV("items",     local_seen),
            KV("ib_null",   local_null),   // items with NO index buffer -> non-indexed draw
            KV("ib_set",    local_set),    // items WITH an index buffer -> indexed draw
            KV::BareStr("note",
                "per-item IB field (item+0x20) tally for this apply+submit call. "
                "ib_null>0 + ib_set=0 => vertex-only items (the black-arm signature)."));
    }
    g_origApplyDraw(ctx, cmdIf);
}

// --- HOP 3: the two render-pass callers of FUN_180501cb0 -----------------------------
// Sample the submit-gate variables at entry (SEH-guarded reads); the cumulative
// tallies decompose WHICH condition blocks the apply+submit call swap-ON. Each
// counter is one gate condition — one run attributes the bypass (results-driven
// obligation 3: the map decomposes per condition).
struct PassGateTally {
    std::atomic<uint64_t> invokes{0};
    std::atomic<uint64_t> listEmpty{0};     // [0x298]==[0x2a0]: nothing enqueued
    std::atomic<uint64_t> techNull{0};      // [0x178]==0: no technique obj
    std::atomic<uint64_t> techNotReady{0};  // tech+0x28==0: technique not ready
    std::atomic<uint64_t> divertFlag{0};    // divert obj +0x1a0 != 0: alt route
    std::atomic<uint64_t> gatePass{0};      // all submit conditions hold at entry
};
PassGateTally g_passA, g_passB;

void SamplePassGate(const char* site, PassGateTally& t, void* ctx) {
    const uintptr_t c = reinterpret_cast<uintptr_t>(ctx);
    const uint64_t inv  = t.invokes.fetch_add(1, std::memory_order_relaxed);
    const uint64_t beg  = SafeReadU64(c + kApplyItemListBeg);
    const uint64_t end  = SafeReadU64(c + kApplyItemListEnd);
    const uint64_t tech = SafeReadU64(c + kCtxTechObjOff);
    const uint64_t rdy  = tech ? (SafeReadU64(tech + kTechReadyOff) & 0xff) : 0;
    const uint64_t dvo  = SafeReadU64(c + kCtxDivertObjOff);
    const uint64_t dvf  = dvo ? (SafeReadU64(dvo + kDivertFlagOff) & 0xff) : 0;
    const bool listEmpty = (beg == end);
    if (listEmpty)      t.listEmpty.fetch_add(1, std::memory_order_relaxed);
    if (!tech)          t.techNull.fetch_add(1, std::memory_order_relaxed);
    if (tech && !rdy)   t.techNotReady.fetch_add(1, std::memory_order_relaxed);
    if (dvf)            t.divertFlag.fetch_add(1, std::memory_order_relaxed);
    if (!listEmpty && tech && rdy) t.gatePass.fetch_add(1, std::memory_order_relaxed);
    if (inv < kMaxPerSite) {
        LOG_WARN_KV(kCat, "pass_gate",
            KV::BareStr("site", site),
            KV("inv",        inv),
            KV("items",      (end > beg) ? (end - beg) / sizeof(void*) : 0),
            KV("tech",       tech),
            KV("tech_ready", rdy),
            KV("divert",     dvf),
            KV::BareStr("note",
                "submit-gate state at pass entry. items=0 => nothing enqueued; "
                "tech=0 or tech_ready=0 => technique blocks; divert!=0 => the "
                "alt-route flag is set (caller B routes AWAY from apply+submit)."));
    }
}

using PassAFn_t = void(__fastcall*)(void* ctx);
PassAFn_t g_origPassA = nullptr;
void __fastcall HookedPassA(void* ctx) {
    SamplePassGate("pass_caller_a", g_passA, ctx);
    g_origPassA(ctx);
}

using PassBFn_t = uint64_t(__fastcall*)(void* ctx);
PassBFn_t g_origPassB = nullptr;
uint64_t __fastcall HookedPassB(void* ctx) {
    SamplePassGate("pass_caller_b", g_passB, ctx);
    return g_origPassB(ctx);
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
    // HOP 2 IB-field sampler.
    armed += ArmSite(base, kRvaApplyDraw, reinterpret_cast<void*>(&HookedApplyDraw),
                     reinterpret_cast<void**>(&g_origApplyDraw), "apply_draw") ? 1 : 0;
    // HOP 3: the two render-pass callers (submit-gate samplers).
    armed += ArmSite(base, kRvaPassCallerA, reinterpret_cast<void*>(&HookedPassA),
                     reinterpret_cast<void**>(&g_origPassA), "pass_caller_a") ? 1 : 0;
    armed += ArmSite(base, kRvaPassCallerB, reinterpret_cast<void*>(&HookedPassB),
                     reinterpret_cast<void**>(&g_origPassB), "pass_caller_b") ? 1 : 0;

    // A bounded watcher emits the cumulative IB-null tally so the confirm survives past
    // the per-invocation cap (the whole-run null-rate IS the signal).
    struct TallyWatcher {
        static void EmitPassGate(const char* site, PassGateTally& t) {
            LOG_INFO_KV(kCat, "pass_gate_tally",
                KV::BareStr("site", site),
                KV("invokes",        t.invokes.load()),
                KV("list_empty",     t.listEmpty.load()),
                KV("tech_null",      t.techNull.load()),
                KV("tech_not_ready", t.techNotReady.load()),
                KV("divert_flag",    t.divertFlag.load()),
                KV("gate_pass",      t.gatePass.load()),
                KV::BareStr("note",
                    "HOP-3 cumulative submit-gate tally for this pass caller. "
                    "invokes=0 => the pass itself never runs (walk one more up); "
                    "gate_pass=0 + a dominant blocker column => THAT condition is "
                    "the swap-ON bypass; gate_pass>0 yet apply_invokes=0 => "
                    "re-read (divert route or a mid-body branch)."));
        }
        static DWORD WINAPI Run(LPVOID) {
            for (int i = 0; i < 40; ++i) {  // ~2 min at 3s
                Sleep(3000);
                EmitPassGate("pass_caller_a", g_passA);
                EmitPassGate("pass_caller_b", g_passB);
                LOG_INFO_KV(kCat, "ib_tally",
                    KV("apply_invokes", g_applyInvokes.load()),
                    KV("items_seen",    g_itemsSeen.load()),
                    KV("ib_null",       g_itemsIbNull.load()),   // non-indexed items
                    KV("ib_set",        g_itemsIbSet.load()),    // indexed items
                    KV::BareStr("note",
                        "cumulative render-item IB-field tally. ib_set=0 + ib_null>0 => "
                        "every item is vertex-only (items built without index buffers) = "
                        "the confirmed black-arm mechanism; ib_set>0 => indexed items exist "
                        "(re-check the bind gate / list bound)."));
            }
            return 0;
        }
    };
    HANDLE tw = CreateThread(nullptr, 0, &TallyWatcher::Run, nullptr, 0, nullptr);
    if (tw) CloseHandle(tw);

    LOG_INFO_KV(kCat, "render_trace_armed",
        KV("sites_armed", (uint64_t)armed),
        KV("wh_base", base),
        KV::BareStr("detail",
            "PROBE Z10 (+HOP2+HOP3): render-submission trace armed at 8 sites (stage "
            "sequencer / compile pass / CCRO::Compile / engine SetIndexBuffer / render "
            "flush / apply_draw IB-sampler / pass_caller_a 0x4ec3a0 / pass_caller_b "
            "0x5014a0). pass_gate + pass_gate_tally = the HOP-3 submit-gate state at "
            "each pass entry (which condition bypasses the apply+submit fn swap-ON). "
            "Run swap-OFF (menu) + swap-ON (black); diff."));
}

}  // namespace kcdx::fs_takeover
