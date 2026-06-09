#pragma once
// foreign_hook_detect — the prologue classifier (foreign-hook DETECTION).
//
// One responsibility: before kcdx installs a function-entry hook, read the
// target's prologue and classify it — Clean (real game instructions),
// KcdxTrampoline (a jump into a range kcdx itself allocated — already in a kcdx
// chain), or Foreign (an E9/FF25 jump pointing OUTSIDE every kcdx-owned
// trampoline range — another mod hooked it first). This classifier is DETECTION
// only — its one job is the verdict. CHAINING onto a Foreign verdict (Step 8,
// §6.2/§6.3) lives in the install path that consumes the verdict (hook_chain
// DetectForeignBeforeInstall → the normal SafetyhookBackend install, which
// relocates the foreign jump into kcdx's trampoline), NOT here. Design §6.1.
//
// The discriminator is kcdx's OWN trampoline records, NOT a query into
// safetyhook: safetyhook's Allocator does NOT expose its ranges — m_memory is
// private, there is no public contains()/owns() (SOURCE:
// vendor/safetyhook/include/safetyhook/allocator.hpp — Allocator has only
// allocate/allocate_near public; the Memory blocks are private; read this
// session). kcdx tracks every trampoline it allocates, so "is this address
// kcdx-owned" is answered from kcdx's own registry (kcdx_trampoline_registry),
// fed at install by the three producers: SafetyhookBackend's InlineHook
// trampoline, the branch-pool reservations, and the safetyhook_midhook MidHook
// trampolines.

#include <cstdint>

namespace kcdx::foreign_hook_detect {

// The classifier verdict (design §6.1).
enum class Prologue {
    // Real game instructions, no prologue jump → install normally.
    Clean,
    // An E9/FF25 jump whose target falls inside a kcdx-owned trampoline range →
    // the target is already in a kcdx chain; the existing CanCoexist / append
    // path handles it, unchanged.
    KcdxTrampoline,
    // An E9/FF25 jump pointing OUTSIDE every kcdx-owned trampoline range →
    // another mod hooked this target first. Step 8 chains onto it.
    Foreign,
    // A prologue that begins with a jump-OPCODE byte (E9 / FF) the decoder could
    // not fully decode into a valid 5-byte E9 / 14-byte FF25 form (a truncated
    // read, an unrecognized FF /4 modrm). Conservative: NOT treated as foreign,
    // NEVER chained — surfaced + logged so an unrecognized shape is never
    // silently mis-handled (AP14). The caller installs normally (treat-as-clean)
    // but the LOG records the unknown shape.
    Unknown,
};

// Read the prologue at `targetVa` and classify it. `targetVa` is a VA kcdx
// already owns the address of (it is about to hook there) — reading its first
// bytes is reading a known prologue, NOT inventing an ABI/offset (AP2/AP3 do
// not apply). `hookName` is used only in the log line for an Unknown/Foreign
// prologue. Pure read; installs nothing, follows no jump.
Prologue Classify(uintptr_t targetVa, const char* hookName);

// Decode helpers — exposed for the engine selftest to exercise the jump-form
// decode against synthetic prologues without needing a live target.
struct JumpDecode {
    bool      isJump = false;   // the prologue is a recognized E9 / FF25 jump
    uintptr_t target = 0;       // the absolute VA the jump lands at (valid iff isJump)
};

// Decode a 5-byte E9 rel32 (opcode 0xE9; target = va + 5 + (int32)disp) OR a
// 14-byte FF25 rip-relative absolute jump (FF 25 00 00 00 00 then an 8-byte
// absolute target at [rip+0]; target = *(uint64*)(va + 6)). `bytes` points at
// at least 14 readable bytes (the caller guarantees the read window).
// SOURCE for the FF25 encoding: Intel SDM Vol.2 — `FF /4` is JMP r/m64; the
// `25` modrm (mod=00, reg=100b=/4, rm=101b) selects RIP-relative [rip+disp32]
// addressing in 64-bit mode, so the 6-byte `FF 25 00 00 00 00` is followed by
// the 8-byte absolute pointer it jumps THROUGH. This is exactly the shape
// safetyhook writes as its far-target fallback (vendor/safetyhook ff_hook,
// read this session).
JumpDecode DecodeJump(const uint8_t* bytes);

}  // namespace kcdx::foreign_hook_detect
