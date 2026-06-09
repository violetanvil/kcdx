#include "foreign_hook_detect_selftest.h"

#include <atomic>
#include <cstdint>
#include <cstdio>   // snprintf
#include <cstring>  // memcpy

#include "foreign_hook_detect.h"
#include "kcdx_trampoline_registry.h"
#include "log.h"
#include "test.h"

#include <limits>  // int32 disp range-guard in WriteE9

namespace kcdx::foreign_hook_detect_selftest {

namespace {

constexpr const char* kRow      = "comp-18-foreign-classifier";
constexpr const char* kCategory = "FOREIGN_HOOK";

using kcdx::foreign_hook_detect::Classify;
using kcdx::foreign_hook_detect::DecodeJump;
using kcdx::foreign_hook_detect::JumpDecode;
using kcdx::foreign_hook_detect::Prologue;

// A 16-byte prologue buffer the classifier reads as a target VA. 16 bytes
// covers the largest decoded form (14-byte FF25) with margin. Aligned so a
// computed E9 displacement is deterministic.
struct alignas(16) Prologue16 {
    uint8_t b[16] = {};
};

// Write an E9 rel32 into `buf` that jumps to absolute `dstVa`. target = bufVa +
// 5 + disp  =>  disp = dstVa - (bufVa + 5).
//
// A real E9 can only reach ±2GB by definition, so dstVa MUST be rel32-reachable
// from buf — that is a production invariant, not a test convenience. The
// range-guard below catches a TEST-SETUP bug (a synthetic target placed >±2GB
// from buf, e.g. a static/global range buffer vs a stack jump buffer): without
// it the int64→int32 cast TRUNCATES the displacement silently, the written E9
// misses its intended target, and the resulting mis-classification reads as an
// engine bug when it is really a bad test input. On an out-of-range disp it logs
// loudly and writes the full 64-bit delta truncated (so the downstream verdict
// goes RED with a traceable cause) rather than pretending success.
void WriteE9(Prologue16& buf, uintptr_t dstVa) {
    const uintptr_t bufVa = reinterpret_cast<uintptr_t>(&buf);
    const intptr_t disp64 =
        static_cast<intptr_t>(dstVa) - static_cast<intptr_t>(bufVa + 5);
    if (disp64 < std::numeric_limits<int32_t>::min() ||
        disp64 > std::numeric_limits<int32_t>::max()) {
        LOG_ERROR_KV(kCategory, "selftest_writee9_disp_out_of_rel32",
            ::kcdx::log::KV("buf", reinterpret_cast<void*>(bufVa)),
            ::kcdx::log::KV("dst", reinterpret_cast<void*>(dstVa)),
            ::kcdx::log::KV("note",
                "test-setup bug: E9 target not rel32-reachable; disp truncates"));
    }
    const int32_t disp = static_cast<int32_t>(disp64);
    buf.b[0] = 0xE9;
    std::memcpy(buf.b + 1, &disp, sizeof(disp));
}

// Write an FF25 [rip+0] absolute jump into `buf` whose 8-byte target is `dstVa`.
// FF 25 00 00 00 00  then the 8-byte absolute target at +6.
void WriteFF25(Prologue16& buf, uintptr_t dstVa) {
    buf.b[0] = 0xFF;
    buf.b[1] = 0x25;
    buf.b[2] = 0x00; buf.b[3] = 0x00; buf.b[4] = 0x00; buf.b[5] = 0x00;
    const uint64_t abs = static_cast<uint64_t>(dstVa);
    std::memcpy(buf.b + 6, &abs, sizeof(abs));
}

}  // namespace

void RunSelfTestOnce() {
    static std::atomic<bool> done{false};
    bool expected = false;
    if (!done.compare_exchange_strong(expected, true,
                                      std::memory_order_acq_rel)) {
        return;
    }

    char reason[768];

    // A synthetic kcdx-owned trampoline range: a STACK-LOCAL buffer registered as
    // a BranchPool-kind range. Stack-local (not static/global) so it sits next to
    // the jump buffer `kcdxJmp` below — within rel32 reach — and the computed E9
    // in WriteE9 produces an in-range int32 displacement (matching the production
    // invariant that an E9 hook always jumps within ±2GB; a real kcdx trampoline
    // is always rel32-reachable from the prologue it detours). A static/global
    // range can sit >±2GB from a stack jump buffer, truncating the int32 disp so
    // the written E9 misses the range — an impossible-in-production out-of-range
    // jump that does NOT exercise the engine's discriminator.
    //
    // Lifetime: the registry holds (base,size) BY VALUE, and the classify +
    // decode below run synchronously between Register and this buffer going out
    // of scope — the already-completed classification is unaffected by any later
    // stack reuse (same RAII-locals model as comp-19). A computed E9 target
    // landing INSIDE [&kcdxRange, +sizeof) must classify KcdxTrampoline.
    uint8_t kcdxRange[256] = {};
    const uintptr_t kcdxBase = reinterpret_cast<uintptr_t>(kcdxRange);
    kcdx::kcdx_trampoline_registry::Register(
        kcdxBase, sizeof(kcdxRange),
        kcdx::kcdx_trampoline_registry::Kind::BranchPool);

    // An address far outside any kcdx range (a foreign mod's detour stand-in).
    // Picked well away from this module + the registered range. Used as a jump
    // target the registry does NOT contain.
    const uintptr_t foreignDst = 0x0000'7FFE'0000'1000ull;

    // ---- Row 1: clean game bytes -> Clean -----------------------------------
    // A real non-jump prologue (mov + push pattern). Begins with neither E9 nor
    // FF, so it is the common install path. FALSIFIABLE: red if classified a jump.
    Prologue16 clean;
    const uint8_t cleanBytes[] = {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83};
    std::memcpy(clean.b, cleanBytes, sizeof(cleanBytes));
    const Prologue cleanV = Classify(reinterpret_cast<uintptr_t>(&clean), "selftest-clean");
    const bool cleanOk = (cleanV == Prologue::Clean);

    // ---- Row 2: E9 into a REGISTERED kcdx range -> KcdxTrampoline ------------
    // FALSIFIABLE: red if Foreign (the discriminator failed to recognize kcdx's
    // own registered range).
    Prologue16 kcdxJmp;
    WriteE9(kcdxJmp, kcdxBase + 32);  // target lands 32 bytes into the registered range
    const Prologue kcdxV = Classify(reinterpret_cast<uintptr_t>(&kcdxJmp), "selftest-kcdx");
    const bool kcdxOk = (kcdxV == Prologue::KcdxTrampoline);
    // Decode check: the E9 target VA is computed exactly.
    const JumpDecode kcdxDec = DecodeJump(kcdxJmp.b);
    const uintptr_t kcdxDecTarget =
        reinterpret_cast<uintptr_t>(&kcdxJmp) + kcdxDec.target;  // E9 returns a VA-delta
    const bool kcdxDecOk = kcdxDec.isJump && (kcdxDecTarget == kcdxBase + 32);

    // ---- Row 3: foreign E9 into an UNREGISTERED range -> Foreign ------------
    // The CORE detection claim. FALSIFIABLE: red if Clean or KcdxTrampoline.
    Prologue16 fgnE9;
    WriteE9(fgnE9, foreignDst);
    const Prologue fgnE9V = Classify(reinterpret_cast<uintptr_t>(&fgnE9), "selftest-foreign-e9");
    const bool fgnE9Ok = (fgnE9V == Prologue::Foreign);

    // ---- Row 4: foreign FF25 into an unregistered range -> Foreign ----------
    // FALSIFIABLE: red if not Foreign. Also checks the FF25 absolute decode.
    Prologue16 fgnFF;
    WriteFF25(fgnFF, foreignDst);
    const Prologue fgnFFV = Classify(reinterpret_cast<uintptr_t>(&fgnFF), "selftest-foreign-ff25");
    const bool fgnFFOk = (fgnFFV == Prologue::Foreign);
    const JumpDecode ffDec = DecodeJump(fgnFF.b);
    const bool ffDecOk = ffDec.isJump && (ffDec.target == foreignDst);  // FF25 target is absolute

    // ---- Row 5: an unrecognized jump-family shape -> Unknown ----------------
    // An FF opcode that is NOT FF /4 [rip] (modrm != 0x25). Conservative: must be
    // Unknown, never Foreign/KcdxTrampoline (AP14 — never silently chained).
    // FALSIFIABLE: red if classified Foreign or KcdxTrampoline.
    Prologue16 unk;
    unk.b[0] = 0xFF; unk.b[1] = 0xD0;  // FF D0 = call rax (an FF that is not /4-rip jmp)
    const Prologue unkV = Classify(reinterpret_cast<uintptr_t>(&unk), "selftest-unknown");
    const bool unkOk = (unkV == Prologue::Unknown);

    const bool pass = cleanOk && kcdxOk && kcdxDecOk && fgnE9Ok &&
                      fgnFFOk && ffDecOk && unkOk;

    if (pass) {
        std::snprintf(reason, sizeof(reason),
            "PASS — foreign-hook classifier: clean prologue=Clean, E9 into a "
            "registered kcdx range=KcdxTrampoline, foreign E9 + foreign FF25 into "
            "an unregistered range=Foreign (the detection claim), unrecognized "
            "FF-shape=Unknown (never silently chained). E9/FF25 byte decode "
            "computes the exact target VA. Registry has %zu ranges.",
            kcdx::kcdx_trampoline_registry::Count());
        LOG_INFO_KV(kCategory, "selftest_pass",
            ::kcdx::log::KV("clean", cleanOk),
            ::kcdx::log::KV("kcdx", kcdxOk),
            ::kcdx::log::KV("foreign_e9", fgnE9Ok),
            ::kcdx::log::KV("foreign_ff25", fgnFFOk),
            ::kcdx::log::KV("unknown", unkOk));
        kcdx::test::ReportResult(kRow, true, reason);
    } else {
        std::snprintf(reason, sizeof(reason),
            "FAIL: foreign-hook classifier mis-verdict — clean=%d(want Clean) "
            "kcdx=%d(want KcdxTrampoline) kcdx_decode=%d foreign_e9=%d(want "
            "Foreign — THIS being 0 means a foreign hook is NOT detected) "
            "foreign_ff25=%d ff25_decode=%d unknown=%d(want Unknown — 0 means an "
            "unrecognized prologue was mis-chained, AP14). Verdicts: clean=%d "
            "kcdx=%d fgnE9=%d fgnFF=%d unk=%d (0=Clean 1=KcdxTramp 2=Foreign "
            "3=Unknown).",
            (int)cleanOk, (int)kcdxOk, (int)kcdxDecOk, (int)fgnE9Ok,
            (int)fgnFFOk, (int)ffDecOk, (int)unkOk,
            (int)cleanV, (int)kcdxV, (int)fgnE9V, (int)fgnFFV, (int)unkV);
        LOG_ERROR_KV(kCategory, "selftest_fail",
            ::kcdx::log::KV("clean", cleanOk),
            ::kcdx::log::KV("kcdx", kcdxOk),
            ::kcdx::log::KV("foreign_e9", fgnE9Ok),
            ::kcdx::log::KV("foreign_ff25", fgnFFOk),
            ::kcdx::log::KV("unknown", unkOk));
        kcdx::test::ReportResult(kRow, false, reason);
    }
    kcdx::test::EmitSummaryIfChanged("comp-18 foreign-hook-classifier");
}

}  // namespace kcdx::foreign_hook_detect_selftest
