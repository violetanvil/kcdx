// safetyhook_midhook — the mid-function detour adapter over safetyhook::MidHook.
// See safetyhook_midhook.h for WHY this is a dedicated unit (not an
// IDetourBackend / InstallRuntime route).
//
// SOURCE (all read this session — a safetyhook API claim cites
// the vendored header, never recall):
//   - MidHookFn is `void(*)(Context&)`, no userdata
//        vendor/safetyhook/include/safetyhook/mid_hook.hpp:22
//   - MidHook::create(target, fn) installs + enable()s (StartDisabled defers)
//        vendor/safetyhook/include/safetyhook/mid_hook.hpp:75 + src/mid_hook.cpp:71
//   - Context64 fields (rax..r15, rbp, rsp[read-only], rip, xmm0..xmm15)
//        vendor/safetyhook/include/safetyhook/context.hpp:30-33
//   - ctx.rip points at the safetyhook trampoline of the replaced instruction
//        vendor/safetyhook/include/safetyhook/context.hpp:27
//   - rsp is READ-ONLY (writes are no-ops); trampoline_rsp is the writable SP
//        vendor/safetyhook/include/safetyhook/context.hpp:28-29
//   - InlineHook::original_bytes() is the relocated region (whole instructions
//     spanning >= sizeof(JmpE9)) — the skip/resume boundary
//        vendor/safetyhook/include/safetyhook/inline_hook.hpp:209 +
//        src/inline_hook.cpp e9_hook() (decode-until->=5 loop)
// The capture-form -> Context64 map this file wires against (verified against the
// safetyhook headers + the binary): F1 GPR -> ctx.<reg>; F2 sub-width
// masked [H2]; F3 XMM -> ctx.xmm<N>.<lane>; F4-F9 memory -> addr from ctx field
// values, deref [H3]; F9 [rsp+N] -> ctx.rsp raw, read-only [H1]. The JIT's
// rsp_offset frame-delta DROPS — Context64 hands back the original rsp.

#include "safetyhook_midhook.h"

#include <array>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>

#include <safetyhook/mid_hook.hpp>
#include <safetyhook/context.hpp>

#include "hook_chain.h"  // MidDispatch / ConsumeMidSkip (the dispatch/marshaling
                         // layer stays in hook_chain, UNCHANGED)
#include "log.h"
#include "rom_borrowed/asmjit_helper.h"  // get_gp_from_name / get_xmm_from_name /
                                         // get_addr_from_name / parse_number_from_string

namespace kcdx::safetyhook_midhook {

namespace {

// ===========================================================================
// The slot table — one row per claimed trampoline. Bound at Install, read at
// fire. targetVa is the chain-lookup key; resumeAddr is targetVa +
// original_bytes().size() (safetyhook's relocated-region size) for the False/skip
// path — the first clean byte past everything the E9/FF patch swallowed, NOT the
// captured-instruction length (which lands inside the patch for a sub-5-byte capture).
// ===========================================================================
struct MidSlot {
    bool      inUse = false;
    uintptr_t targetVa = 0;
    uintptr_t resumeAddr = 0;
    // Capture grammar (the same vectors make_jit_midfunc consumed), read at
    // fire to wire each capture between Context64 and the 16-byte payload.
    std::vector<std::string> captureExprs;
    std::vector<std::string> captureTypes;
    // The live MidHook, held for the session (kcdx never unhooks — SKSE model).
    // Holding it keeps the trampoline + patch alive.
    safetyhook::MidHook hook;
};

std::array<MidSlot, kMidTrampolinePoolSize> g_slots;
std::mutex                                  g_slotsMu;

// ===========================================================================
// Capture wire — Context64 <-> the 16-byte-stride payload MidDispatch expects.
//
// The existing MidDispatch + the C-mid dispatch thunk (dynamic_call_jit) read
// captures from a 16-byte-stride payload (PushCaptureHandle slot = payload +
// 16*i). The JIT used to write that payload from registers; here the C adapter
// builds it from Context64 (read side) and writes mutated values back to
// Context64 (write side). The payload buffer + stride are UNCHANGED — only the
// source/sink moves from the JIT stack frame to Context64.
// ===========================================================================
constexpr size_t kMidStride = 16;  // matches make_jit_midfunc / dynamic_call_jit

using Ctx = safetyhook::Context64;

// Map a 64-bit GPR name (F1) to its writable Context64 field. rsp is
// READ-ONLY (H1) — a write through this returns nullptr so the caller skips it.
// Returns nullptr for a name that is not a 64-bit GPR (F2 sub-width names are
// handled by base-name lookup + width masking in the read/write helpers).
uintptr_t* gpr_field(Ctx& c, const std::string& reg, bool forWrite) {
    if (reg == "rax") return &c.rax;
    if (reg == "rbx") return &c.rbx;
    if (reg == "rcx") return &c.rcx;
    if (reg == "rdx") return &c.rdx;
    if (reg == "rsi") return &c.rsi;
    if (reg == "rdi") return &c.rdi;
    if (reg == "rbp") return &c.rbp;
    if (reg == "r8")  return &c.r8;
    if (reg == "r9")  return &c.r9;
    if (reg == "r10") return &c.r10;
    if (reg == "r11") return &c.r11;
    if (reg == "r12") return &c.r12;
    if (reg == "r13") return &c.r13;
    if (reg == "r14") return &c.r14;
    if (reg == "r15") return &c.r15;
    if (reg == "rsp") return forWrite ? nullptr : &c.rsp;  // H1: rsp read-only
    return nullptr;
}

// F2 — a sub-width register name -> (its 64-bit field, the width in bytes).
// 32-bit (eax/r8d) -> width 4 (H2: a 32-bit write zero-extends the upper 32);
// 16-bit (ax/r8w)  -> width 2 (preserve upper bits);
// 8-bit  (al/r8b)  -> width 1 (preserve upper bits). `al`/`ah` etc. all map to
// the low byte (ah's high-byte position is not preserved on writeback — the JIT
// stored sub-width natively; an 8-bit capture round-trips the low byte, which is
// the supported form). Returns {field, width} or {nullptr, 0} if not sub-width.
struct SubWidth { uintptr_t* field; int width; };
SubWidth subwidth_field(Ctx& c, const std::string& reg, bool forWrite) {
    static const std::unordered_map<std::string, std::pair<const char*, int>> tbl = {
        // 32-bit -> 64-bit base, width 4
        {"eax",{"rax",4}},{"ebx",{"rbx",4}},{"ecx",{"rcx",4}},{"edx",{"rdx",4}},
        {"esi",{"rsi",4}},{"edi",{"rdi",4}},{"ebp",{"rbp",4}},{"esp",{"rsp",4}},
        {"r8d",{"r8",4}},{"r9d",{"r9",4}},{"r10d",{"r10",4}},{"r11d",{"r11",4}},
        {"r12d",{"r12",4}},{"r13d",{"r13",4}},{"r14d",{"r14",4}},{"r15d",{"r15",4}},
        // 16-bit -> width 2
        {"ax",{"rax",2}},{"bx",{"rbx",2}},{"cx",{"rcx",2}},{"dx",{"rdx",2}},
        {"si",{"rsi",2}},{"di",{"rdi",2}},{"bp",{"rbp",2}},{"sp",{"rsp",2}},
        {"r8w",{"r8",2}},{"r9w",{"r9",2}},{"r10w",{"r10",2}},{"r11w",{"r11",2}},
        {"r12w",{"r12",2}},{"r13w",{"r13",2}},{"r14w",{"r14",2}},{"r15w",{"r15",2}},
        // 8-bit -> width 1 (low byte)
        {"al",{"rax",1}},{"ah",{"rax",1}},{"bl",{"rbx",1}},{"bh",{"rbx",1}},
        {"cl",{"rcx",1}},{"ch",{"rcx",1}},{"dl",{"rdx",1}},{"dh",{"rdx",1}},
        {"sil",{"rsi",1}},{"dil",{"rdi",1}},{"bpl",{"rbp",1}},{"spl",{"rsp",1}},
        {"r8b",{"r8",1}},{"r9b",{"r9",1}},{"r10b",{"r10",1}},{"r11b",{"r11",1}},
        {"r12b",{"r12",1}},{"r13b",{"r13",1}},{"r14b",{"r14",1}},{"r15b",{"r15",1}},
    };
    const auto it = tbl.find(reg);
    if (it == tbl.end()) return {nullptr, 0};
    uintptr_t* base = gpr_field(c, it->second.first, forWrite);
    return {base, it->second.second};
}

// F3 — an XMM name -> its Context64 Xmm union. Returns nullptr if not an XMM
// name. The lane (.f32[0]/.f64[0]/.u64[0]) is selected by the capture type at
// the read/write site.
safetyhook::Xmm* xmm_field(Ctx& c, const std::string& reg) {
    if (reg.size() < 4 || reg.compare(0, 3, "xmm") != 0) return nullptr;
    int n = -1;
    if (auto v = kcdx::rom::get_xmm_from_name(reg); v.has_value()) {
        n = static_cast<int>(v->id());
    }
    if (n < 0 || n > 15) return nullptr;
    safetyhook::Xmm* x = &c.xmm0;  // the 16 are contiguous (context.hpp:31)
    return x + n;
}

// F4-F9 — compute the effective address of a memory capture from Context64
// register-VALUE fields (NOT the JIT's frame-delta math — Context64 hands back
// the original rsp, so rsp_offset DROPS, H1/finding). Returns 0 + false if a
// base/index name doesn't resolve. The parsed forms: [base], [base+/-disp],
// [base+index], [base+index*scale], [absolute], [rsp+N].
bool mem_capture_addr(Ctx& c, const std::string& expr, uintptr_t& outAddr) {
    // Reuse the same grammar acceptor make_jit_midfunc used (get_addr_from_name)
    // for parse VALIDATION, but compute the address from Context64 field VALUES
    // (get_addr_from_name yields an asmjit::Mem describing base/index/disp; we
    // re-walk the expression here to fold in the live register values). Parse a
    // minimal [base (+|-) disp (+ index (* scale))] / [absolute].
    std::string s = expr;
    // strip the brackets
    if (s.size() < 2 || s.front() != '[' || s.back() != ']') return false;
    s = s.substr(1, s.size() - 2);

    auto regValue = [&](const std::string& name, uintptr_t& v) -> bool {
        // 64-bit GPR value (read side — rsp is fine to READ, H1).
        if (uintptr_t* f = gpr_field(c, name, /*forWrite=*/false)) { v = *f; return true; }
        return false;
    };

    // Split on the FIRST top-level + or - for the displacement / index, then
    // handle an optional *scale on the index.
    std::string baseTok, rest;
    size_t opPos = std::string::npos;
    char op = '+';
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+' || s[i] == '-') { opPos = i; op = s[i]; break; }
    }
    if (opPos == std::string::npos) { baseTok = s; }
    else { baseTok = s.substr(0, opPos); rest = s.substr(opPos + 1); }
    auto trim = [](std::string& t) {
        while (!t.empty() && t.front() == ' ') t.erase(t.begin());
        while (!t.empty() && t.back() == ' ') t.pop_back();
    };
    trim(baseTok);

    // [absolute] — base token is a number, no register.
    if (auto num = kcdx::rom::parse_number_from_string(baseTok); num.has_value()
        && !kcdx::rom::get_gp_from_name(baseTok).has_value()) {
        outAddr = static_cast<uintptr_t>(*num);
        return true;
    }

    uintptr_t addr = 0;
    if (!regValue(baseTok, addr)) return false;  // base must be a GPR

    if (!rest.empty()) {
        trim(rest);
        // rest is either a pure number (disp) or index[*scale].
        // index*scale handling.
        std::string idxTok = rest;
        uint64_t scale = 1;
        if (size_t star = rest.find('*'); star != std::string::npos) {
            idxTok = rest.substr(0, star);
            std::string scaleTok = rest.substr(star + 1);
            trim(idxTok); trim(scaleTok);
            if (auto sc = kcdx::rom::parse_number_from_string(scaleTok); sc.has_value())
                scale = *sc;
            else return false;
        } else {
            trim(idxTok);
        }
        if (auto disp = kcdx::rom::parse_number_from_string(idxTok); disp.has_value()
            && !kcdx::rom::get_gp_from_name(idxTok).has_value()) {
            // a numeric displacement
            if (op == '+') addr += static_cast<uintptr_t>(*disp);
            else           addr -= static_cast<uintptr_t>(*disp);
        } else {
            // an index register (only meaningful with '+')
            uintptr_t idxVal = 0;
            if (!regValue(idxTok, idxVal)) return false;
            addr += idxVal * static_cast<uintptr_t>(scale);
        }
    }
    outAddr = addr;
    return true;
}

// Byte width to copy for a memory deref / writeback, by capture type. A memory
// deref MUST copy the typed width only (not always 8) — copying 8 from a narrow
// capture near the end of a valid page could fault. PushCaptureValue then reads
// the typed width from the slot.
size_t type_width(const std::string& type) {
    if (type == "i8" || type == "u8" || type == "bool") return 1;
    if (type == "i16" || type == "u16") return 2;
    if (type == "i32" || type == "u32" || type == "f32" || type == "float") return 4;
    return 8;  // i64 / u64 / ptr / f64 / double / default
}

// Read one capture out of Context64 into its 16-byte payload slot.
void read_capture_into_payload(Ctx& c, const std::string& expr,
                               const std::string& type, void* slot) {
    // Memory form (F4-F9): compute addr, deref the typed width into the slot.
    if (!expr.empty() && expr.front() == '[') {
        uintptr_t addr = 0;
        if (!mem_capture_addr(c, expr, addr)) {
            // Loud failure: a memory capture whose base/index didn't resolve.
            // Zero the slot so the callback reads a defined value; the install
            // already validated the grammar via get_addr_from_name.
            std::memset(slot, 0, kMidStride);
            log::WarnF("safetyhook_midhook: memory capture '%s' base/index "
                       "unresolved at fire — slot zeroed", expr.c_str());
            return;
        }
        std::memset(slot, 0, kMidStride);
        std::memcpy(slot, reinterpret_cast<const void*>(addr), type_width(type));
        return;
    }
    // XMM form (F3): copy the selected lane into the slot.
    if (safetyhook::Xmm* x = xmm_field(c, expr)) {
        std::memset(slot, 0, kMidStride);
        std::memcpy(slot, x, 16);  // copy the full 16-byte lane region
        return;
    }
    // Sub-width GPR (F2): read the low N bytes of the field.
    if (SubWidth sw = subwidth_field(c, expr, /*forWrite=*/false); sw.field) {
        std::memset(slot, 0, kMidStride);
        std::memcpy(slot, sw.field, static_cast<size_t>(sw.width));
        return;
    }
    // 64-bit GPR (F1).
    if (uintptr_t* f = gpr_field(c, expr, /*forWrite=*/false)) {
        std::memset(slot, 0, kMidStride);
        std::memcpy(slot, f, 8);
        return;
    }
    // Unknown form — should not reach here (install validated the grammar).
    std::memset(slot, 0, kMidStride);
    log::WarnF("safetyhook_midhook: capture '%s' unrecognized at fire — slot "
               "zeroed", expr.c_str());
}

// Write one (possibly mutated) capture from its 16-byte payload slot back to
// Context64. Mirrors read_capture_into_payload's form dispatch.
void write_capture_from_payload(Ctx& c, const std::string& expr,
                                const std::string& type, const void* slot) {
    // Memory form (F4-F9): deref the addr, store the typed width. [rsp+N] writes
    // through the dereferenced address (H1: a memory write is fine; only a write
    // to ctx.rsp itself is the no-op).
    if (!expr.empty() && expr.front() == '[') {
        uintptr_t addr = 0;
        if (!mem_capture_addr(c, expr, addr)) return;  // unresolved -> skip (logged on read)
        std::memcpy(reinterpret_cast<void*>(addr), slot, type_width(type));
        return;
    }
    // XMM form (F3): store the 16-byte lane region back.
    if (safetyhook::Xmm* x = xmm_field(c, expr)) {
        std::memcpy(x, slot, 16);
        return;
    }
    // Sub-width GPR (F2): mask-write the low N bytes (H2). A 32-bit write
    // zero-extends the upper 32 (x86 semantics); 16/8-bit preserve the upper
    // bits — replicate by writing exactly N low bytes for 16/8 and zeroing the
    // upper 32 for the 32-bit case.
    if (SubWidth sw = subwidth_field(c, expr, /*forWrite=*/true); sw.field) {
        if (sw.width == 4) {
            uint32_t v = 0;
            std::memcpy(&v, slot, 4);
            *sw.field = static_cast<uintptr_t>(v);  // zero-extend upper 32 (H2)
        } else {
            std::memcpy(sw.field, slot, static_cast<size_t>(sw.width));  // preserve upper (H2)
        }
        return;
    }
    // 64-bit GPR (F1). rsp is read-only (H1) -> gpr_field(forWrite) returns null.
    if (uintptr_t* f = gpr_field(c, expr, /*forWrite=*/true)) {
        std::memcpy(f, slot, 8);
        return;
    }
    // rsp write or unknown -> no-op (rsp read-only is parity-preserving; the JIT
    // never wrote rsp back either).
}

// ===========================================================================
// The fire path — one per captured-instruction hit. Reads captures from
// Context64 into the payload, runs the existing MidDispatch (UNCHANGED), writes
// mutated captures back, then resolves the call-original mode via ctx.rip.
// ===========================================================================
void MidDispatchFromContext(safetyhook::Context64& ctx, size_t slotIndex) {
    if (slotIndex >= kMidTrampolinePoolSize) return;  // defensive

    // Read the slot by const-reference — NO lock, NO copy on the hot path. The
    // slot is bound ONCE in Install (under g_slotsMu, completing BEFORE enable()
    // makes the patch live) and is NEVER mutated afterward (kcdx never unhooks —
    // SKSE session-lifetime model), so a fire reads a fully-bound, immutable
    // slot. The lock in Install only serializes installs against each other, not
    // against fires. Copying the capture vectors per fire would allocate on the
    // hot path — exactly what the retired JIT avoided
    // by baking the layout; reading by reference keeps the fire allocation-free.
    const MidSlot& s = g_slots[slotIndex];
    if (!s.inUse) return;
    const uintptr_t targetVa   = s.targetVa;
    const uintptr_t resumeAddr = s.resumeAddr;
    const std::vector<std::string>& exprs = s.captureExprs;
    const std::vector<std::string>& types = s.captureTypes;

    const size_t n = exprs.size();
    // The 16-byte-stride capture payload MidDispatch expects (PushCaptureHandle
    // slot = payload + 16*i). A fixed cap keeps it on the stack (no hot-path
    // alloc); mid captures are few. Cap to the pool's capture budget.
    constexpr size_t kMaxCaptures = 32;
    alignas(16) uint8_t payload[kMaxCaptures * kMidStride] = {};
    const size_t count = (n < kMaxCaptures) ? n : kMaxCaptures;

    // READ: Context64 -> payload (16-byte stride).
    for (size_t i = 0; i < count; ++i) {
        read_capture_into_payload(ctx, exprs[i], types[i],
                                  payload + kMidStride * i);
    }

    // DISPATCH: the existing MidDispatch (off-thread filter, engine carve-out,
    // re-entrancy, pin-arena, Lua/C marshaling) — UNCHANGED. It clears
    // and may set the skip flag; we read the result via ConsumeMidSkip below.
    kcdx::hook_chain::MidDispatch(
        reinterpret_cast<const kcdx::rom::runtime_func_t::parameters_t*>(payload),
        count, targetVa);

    // WRITE BACK: payload -> Context64 (any :set() mutation lands in the real
    // register/memory when the original resumes).
    for (size_t i = 0; i < count; ++i) {
        write_capture_from_payload(ctx, exprs[i], types[i],
                                   payload + kMidStride * i);
    }

    // CALL-ORIGINAL via ctx.rip (spike-proven against the binary). The chain decides
    // run-vs-skip exactly as today (return "skip"/true sets the flag); Auto IS
    // this conditional set. True/run = leave ctx.rip alone (safetyhook's
    // trampoline re-runs the captured instruction). False/skip = ctx.rip =
    // resumeAddr (the first clean byte past safetyhook's relocated region —
    // computed in Install from the hook's own original_bytes() size, NOT the
    // patch width, NOT a single-instruction length; see Install).
    if (kcdx::hook_chain::ConsumeMidSkip()) {
        ctx.rip = resumeAddr;
    }
}

// ===========================================================================
// The fixed C-trampoline pool — N compile-time functions, each baking its own
// index. ZERO runtime codegen: each template instantiation is an ordinary
// compiled C function; the array is built at compile time from an index_sequence
// (the whole point of retiring the per-target JIT). MidHookFn is a bare
// void(*)(Context&), so the index — not a userdata or ctx.rip — recovers the
// slot. SOURCE: mid_hook.hpp:22 (no userdata) + context.hpp:27 (rip = tramp).
// ===========================================================================
template <size_t K>
void mid_trampoline(safetyhook::Context64& c) {
    MidDispatchFromContext(c, K);
}

template <size_t... Is>
constexpr std::array<safetyhook::MidHookFn, sizeof...(Is)>
make_trampoline_table(std::index_sequence<Is...>) {
    // Each &mid_trampoline<Is> is a distinct compile-time function pointer.
    return { &mid_trampoline<Is>... };
}

// The compile-time trampoline array (one entry per pool slot).
constexpr auto g_trampolines =
    make_trampoline_table(std::make_index_sequence<kMidTrampolinePoolSize>{});

// Claim a free slot index, or kMidTrampolinePoolSize if exhausted. Caller holds
// g_slotsMu.
size_t claim_slot_locked() {
    for (size_t i = 0; i < kMidTrampolinePoolSize; ++i) {
        if (!g_slots[i].inUse) return i;
    }
    return kMidTrampolinePoolSize;  // exhausted
}

}  // namespace

InstallResult Install(uintptr_t                       targetVa,
                      const std::vector<std::string>& captureExprs,
                      const std::vector<std::string>& captureTypes,
                      const std::string&              hookName) {
    InstallResult res;

    // Validate every capture EXPR resolves to a known form at INSTALL time (so a
    // malformed capture fails loud here, not at fire). A register form must be a
    // GPR / sub-width GPR / XMM name; a memory form must parse (a Context64
    // snapshot is needed for the live address, but the grammar — base reg etc. —
    // is validated against the acceptor here). Replaces the validation
    // make_jit_midfunc did via get_addr_from_name (fail loud, never drop).
    for (size_t i = 0; i < captureExprs.size(); ++i) {
        const std::string& e = captureExprs[i];
        if (e.empty()) {
            res.reason = "mid capture expr is empty";
            return res;
        }
        if (e.front() == '[') {
            // get_addr_from_name validates the SIB grammar (base/index/disp);
            // it returns nullopt on a bad base/index name.
            if (!kcdx::rom::get_addr_from_name(e).has_value()) {
                res.reason = "mid capture memory expr '" + e +
                             "' is not a valid [base(+/-disp)(+index(*scale))] form";
                return res;
            }
        } else {
            // A bare register name: 64-bit GPR, sub-width GPR, or XMM.
            const bool gp  = kcdx::rom::get_gp_from_name(e).has_value();
            const bool xmm = kcdx::rom::get_xmm_from_name(e).has_value();
            if (!gp && !xmm) {
                res.reason = "mid capture '" + e +
                             "' is not a recognized register or [memory] expr";
                return res;
            }
        }
    }

    std::lock_guard<std::mutex> lock(g_slotsMu);

    size_t k = claim_slot_locked();
    if (k >= kMidTrampolinePoolSize) {
        // FAIL LOUD — never a silent drop; the failure is logged with context. A
        // fixed pool of N is exhausted only with an unrealistic number of distinct mid targets;
        // surface it so the cap can be raised deliberately.
        res.reason =
            "mid-hook trampoline pool exhausted (all " +
            std::to_string(kMidTrampolinePoolSize) +
            " slots claimed) — too many distinct mid-hook targets; raise "
            "kMidTrampolinePoolSize in safetyhook_midhook.h";
        log::ErrorF("safetyhook_midhook: pool exhausted installing mid '%s' at "
                    "0x%p (cap=%zu)", hookName.c_str(), (void*)targetVa,
                    kMidTrampolinePoolSize);
        return res;
    }

    MidSlot& s = g_slots[k];
    // Bind the captures + targetVa BEFORE create() — and resumeAddr AFTER create
    // but BEFORE enable() (below) — so a fire can never read an unset slot. The
    // ordering relies on StartDisabled: create(StartDisabled) builds the stub +
    // trampoline only (NO patch), enable() writes the patch later, so the patch
    // is never live until after resumeAddr is set.
    s.targetVa     = targetVa;
    s.captureExprs = captureExprs;
    s.captureTypes = captureTypes;
    s.inUse        = true;

    // Create the MidHook DISABLED so the relocated-region size is known and the
    // slot's resumeAddr is set BEFORE the patch goes live (no fire can read a
    // resumeAddr==0). create(StartDisabled) builds the stub + trampoline only;
    // the separate enable() writes the patch under trap_threads — a VEH +
    // VirtualProtect mechanism, NOT a thread suspend — fine for a mid chain
    // install. Never under the loader lock; the loader-lock paths are
    // function-entry MinHook only, the conservative choice. SOURCE: mid_hook.hpp:75
    // + mid_hook.cpp:115-156 (create=setup, no patch under StartDisabled);
    // os.windows.cpp:268-318 (enable → trap_threads VEH+VirtualProtect body).
    // + src/mid_hook.cpp:71-91, this session.
    auto created = safetyhook::MidHook::create(
        reinterpret_cast<void*>(targetVa), g_trampolines[k],
        safetyhook::MidHook::StartDisabled);
    if (!created) {
        s = MidSlot{};  // roll back the slot claim (clears inUse + bindings)
        const auto& err = created.error();
        const char* kind = (err.type == safetyhook::MidHook::Error::BAD_ALLOCATION)
                               ? "BAD_ALLOCATION" : "BAD_INLINE_HOOK";
        res.reason = std::string("safetyhook::MidHook::create failed (") + kind +
                     ") at the capture site";
        log::ErrorF("safetyhook_midhook: MidHook::create failed (%s) for mid "
                    "'%s' at 0x%p", kind, hookName.c_str(), (void*)targetVa);
        return res;
    }
    s.hook = std::move(*created);

    // resumeAddr (the False/skip target) = targetVa + the size of safetyhook's
    // RELOCATED region — the WHOLE-instruction span safetyhook overwrote, read
    // from safetyhook itself (original_bytes()), NEVER recomputed. This is the
    // single highest-risk asm subtlety (the cap-04 scar): the relocated region
    // is the captured instruction PLUS any following instructions safetyhook had
    // to swallow to reach a 5-byte E9 / 14-byte FF patch, rounded UP to an
    // instruction boundary. Resuming at targetVa + this size lands on the first
    // CLEAN byte past the patch — resuming at "VA + just the captured
    // instruction length" would land INSIDE the E9/FF jump bytes when the
    // captured instruction is shorter than the patch (the cap-04b crash). It is
    // patch-WIDTH-correct by construction (E9 vs FF) because safetyhook computed
    // it. (Test stubs pad the swallowed tail with nops, so skip is exact.)
    // SOURCE: vendor/safetyhook/src/inline_hook.cpp e9_hook() — decodes whole
    // instructions until m_original_bytes spans >= sizeof(JmpE9) — this session.
    // (g_slotsMu is already held by this Install call, so this write to the slot
    // happens-before the enable() below makes the patch live.)
    s.resumeAddr = targetVa +
                   static_cast<uintptr_t>(s.hook.original_bytes().size());

    // Now enable() — the patch goes live with the slot fully bound (targetVa +
    // captures + resumeAddr all set). A fire after this reads a complete slot.
    if (auto en = s.hook.enable(); !en) {
        s = MidSlot{};  // roll back
        res.reason =
            "safetyhook::MidHook::enable failed at the capture site (patch "
            "could not be written)";
        log::ErrorF("safetyhook_midhook: MidHook::enable failed for mid '%s' at "
                    "0x%p", hookName.c_str(), (void*)targetVa);
        return res;
    }

    // FOREIGN-HOOK REGISTRY — the mid trampoline range is NOT registered
    // here because safetyhook::MidHook does NOT expose its stub/trampoline range
    // publicly: its inner InlineHook (m_hook) and its mid stub Allocation
    // (m_stub) are PRIVATE with no public accessor — only original_bytes() (the
    // relocated TARGET bytes, not the trampoline) is reachable. SOURCE:
    // vendor/safetyhook/include/safetyhook/mid_hook.hpp:156-160 (m_hook / m_stub
    // private; no trampoline()/stub() accessor) — read this session. Registering
    // a mid trampoline would require a vendored-header accessor; that is a
    // surfaced decision, NOT guessed here (the function-entry InlineHook DOES
    // expose trampoline(), so SafetyhookBackend registers its range). The
    // practical gap is narrow: a mid hook's prologue jump lands at a mid-function
    // VA (not a function entry), and the chain keeps one mid per VA — a later
    // function-entry hook does not target a mid VA, so a mid trampoline is rarely
    // a classifier jump target in v1. Surfaced as a finding for the foreign-hook
    // chaining work to weigh against the foreign-mid-hook case.
    res.ok = true;
    log::InfoF("safetyhook_midhook: installed mid '%s' at 0x%p (slot %zu, "
               "%zu captures, resume +%zu)",
               hookName.c_str(), (void*)targetVa, k, captureExprs.size(),
               s.hook.original_bytes().size());
    return res;
}

}  // namespace kcdx::safetyhook_midhook
