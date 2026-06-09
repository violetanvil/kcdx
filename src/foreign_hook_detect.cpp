#include "foreign_hook_detect.h"

#include <cstdint>
#include <cstring>

#include "kcdx_trampoline_registry.h"
#include "log.h"

namespace kcdx::foreign_hook_detect {

namespace {

constexpr const char* kCategory = "FOREIGN_HOOK";

// The two x86-64 prologue-jump forms kcdx + safetyhook write.
constexpr uint8_t kE9   = 0xE9;  // E9 rel32 — 5-byte near jump
constexpr uint8_t kFF   = 0xFF;  // FF /4 group opcode — the absolute-jump family
constexpr uint8_t kModrm25 = 0x25;  // modrm for FF /4 [rip+disp32] (mod=00, reg=100, rm=101)

}  // namespace

JumpDecode DecodeJump(const uint8_t* bytes) {
    JumpDecode out;
    if (!bytes) return out;

    // CONVENTION: DecodeJump reads a byte window, NOT a VA. The E9 rel32 target
    // is VA-relative (target = va_of_E9 + 5 + disp), so `bytes` alone cannot
    // produce an absolute VA — DecodeJump returns the rel32 result as the DELTA
    // (5 + disp) and the caller (Classify) folds in the instruction VA. The FF25
    // target is VA-ABSOLUTE in the bytes, so it is returned directly (the caller
    // does NOT add the VA). IsAbsoluteFF25() below tells the caller which case
    // applies. This keeps DecodeJump self-contained + testable on a synthetic
    // buffer.

    // E9 rel32 — opcode 0xE9, then a signed int32 disp. Return the DELTA
    // (5 + disp); 5 = the E9 + 4-byte-disp instruction length. Caller adds the VA.
    if (bytes[0] == kE9) {
        int32_t disp = 0;
        std::memcpy(&disp, bytes + 1, sizeof(disp));  // little-endian int32
        out.isJump = true;
        // Delta from this instruction's VA (caller adds the VA). 5 = E9 + 4-byte
        // disp instruction length.
        out.target = static_cast<uintptr_t>(static_cast<intptr_t>(5) +
                                            static_cast<intptr_t>(disp));
        return out;
    }

    // FF 25 disp32 [target8] — 6-byte instruction (FF 25 + a 4-byte
    // displacement) followed by the 8-byte ABSOLUTE target the jump reads
    // THROUGH (rip-relative [rip+disp32] in 64-bit mode). target =
    // *(uint64*)(bytes + 6 + disp32). SOURCE: Intel SDM Vol.2 FF /4 + modrm 25
    // (rip-relative); the safetyhook far-target fallback writes exactly this
    // with disp32 == 0 (vendor/safetyhook ff_hook — read this session).
    //
    // disp32 != 0 — the pointer is NOT at [rip+0]: reading bytes+6 as the
    // absolute target would decode the WRONG VA (the bytes there are an
    // instruction, not the pointer). The reader buffer only guarantees the
    // 14-byte safetyhook form (disp32 == 0), and following a non-zero
    // displacement could read outside it. So a non-zero disp32 is NOT decoded as
    // a recognized jump — isJump stays false, and Classify surfaces it as
    // Unknown (conservative, never silently mis-decoded → never mis-chained —
    // fail loud, never silently drop). A foreign FF25 with a non-zero disp32 is still correctly NOT
    // mis-classified; its verdict becomes the surfaced Unknown rather than a
    // Foreign with a wrong jumps_to VA.
    if (bytes[0] == kFF && bytes[1] == kModrm25) {
        int32_t disp32 = 0;
        std::memcpy(&disp32, bytes + 2, sizeof(disp32));  // little-endian int32
        if (disp32 != 0) {
            return out;  // not the [rip+0] form this decoder reads — surfaced as Unknown
        }
        uint64_t abs = 0;
        std::memcpy(&abs, bytes + 6, sizeof(abs));  // little-endian uint64 absolute target
        out.isJump = true;
        out.target = static_cast<uintptr_t>(abs);  // already absolute — caller does NOT add the VA
        return out;
    }

    return out;  // not a recognized jump form
}

// Whether the FF25 absolute form was decoded (its target is already absolute;
// the E9 form's target is a VA-delta). Recompute the discriminator here so
// Classify knows whether to add the instruction VA.
namespace {
bool IsAbsoluteFF25(const uint8_t* bytes) {
    return bytes && bytes[0] == kFF && bytes[1] == kModrm25;
}
}  // namespace

Prologue Classify(uintptr_t targetVa, const char* hookName) {
    if (targetVa == 0) return Prologue::Clean;  // a 0 target never reaches here in practice
    const uint8_t* p = reinterpret_cast<const uint8_t*>(targetVa);
    const char* who = hookName ? hookName : "?";

    const uint8_t op = p[0];

    // Fast path: the prologue does NOT begin with a jump-family opcode byte →
    // real game instructions, install normally. This is the overwhelming common
    // case; it must NOT change any existing clean-prologue install.
    if (op != kE9 && op != kFF) {
        return Prologue::Clean;
    }

    // A jump-FAMILY opcode byte is present. Decode it into a recognized form.
    // An FF that is NOT FF /4 [rip] (e.g. FF /4 with a different modrm, or an
    // FF that begins some other instruction the game's real prologue happens to
    // start with) is NOT a jump we recognize → Unknown (conservative: do NOT
    // mis-chain it, surface it).
    JumpDecode d = DecodeJump(p);
    if (!d.isJump) {
        // op was E9 or FF but the full form didn't decode (an FF not /4-rip).
        // This is the conservative branch (fail loud, never silently drop):
        // surfaced + logged, NOT treated as foreign, NEVER chained. The caller
        // installs normally.
        LOG_WARN_KV(kCategory, "unknown_prologue",
            ::kcdx::log::KV("hook", who),
            ::kcdx::log::KV("target", reinterpret_cast<void*>(targetVa)),
            ::kcdx::log::KV("op0", static_cast<long long>(op)),
            ::kcdx::log::KV("op1", static_cast<long long>(p[1])));
        return Prologue::Unknown;
    }

    // Compute the absolute jump target. E9 returned a VA-delta (add the
    // instruction VA); FF25 returned an absolute target (use as-is).
    const uintptr_t jumpTarget = IsAbsoluteFF25(p)
        ? d.target
        : (targetVa + d.target);

    // The discriminator: the jump target falls inside a range
    // KCDX ITSELF allocated → it is a kcdx trampoline (the target is already in
    // a kcdx chain), so the existing CanCoexist / append path owns it. A target
    // elsewhere is FOREIGN — another mod's detour.
    if (kcdx::kcdx_trampoline_registry::Contains(jumpTarget)) {
        return Prologue::KcdxTrampoline;
    }

    // `jumps_to` is INFORMATIONAL only — the decoded foreign-detour VA for the
    // log. It is NOT load-bearing for chaining: kcdx does not follow this jump by
    // hand. The install that follows (DetectForeignBeforeInstall's Foreign branch
    // → the normal SafetyhookBackend install) chains by safetyhook RELOCATING the
    // foreign prologue jump into kcdx's trampoline IP-fixed, which captures
    // the foreign detour correctly regardless of what jumps_to decoded. (An FF25
    // with a non-zero disp32 never reaches here — DecodeJump returns the
    // surfaced-Unknown verdict for it, so jumps_to is always the [rip+0] /
    // E9-relative target.)
    LOG_INFO_KV(kCategory, "foreign_hook_detected",
        ::kcdx::log::KV("hook", who),
        ::kcdx::log::KV("target", reinterpret_cast<void*>(targetVa)),
        ::kcdx::log::KV("jumps_to", reinterpret_cast<void*>(jumpTarget)),
        ::kcdx::log::KV("form", IsAbsoluteFF25(p) ? "ff25" : "e9"),
        ::kcdx::log::KV("note", "chaining onto it via safetyhook relocation"));
    return Prologue::Foreign;
}

}  // namespace kcdx::foreign_hook_detect
