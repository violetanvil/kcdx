#include "ctor_probe.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "MinHook.h"

#include "../address_library.h"
#include "../log.h"

// Read-only ctor probe — see ctor_probe.h for the framing (falsifiable
// question, outcome map A/B/C, transient lifetime). This .cpp owns only the
// MinHook plumbing + the snapshot dump.

namespace kcdx::mod_absorb::ctor_probe {

namespace {

constexpr const char* kCat = "MOD_ABSORB_PROBE";

// wh::C_ModManager ctor — Address Library id 3101. Resolved at install time
// (never a hardcoded RVA); the row carries the per-build address plus the
// verified ABI: __fastcall returning ptr, with 3 args
// (ptr outResult /*rcx*/, ptr sys /*rdx*/, ptr modsDir /*r8*/).
constexpr uint64_t kCtorId = 3101;

// Object size per the verified seed prose (id 3101): the ctor populates a
// 0x68-byte C_ModManager. The probe snapshots every 8-byte slot across that
// range so the dump is exhaustive — any field the ctor writes shows up,
// whether or not the prose predicted it.
constexpr size_t kObjectSize = 0x68;

using CtorFn_t = void* (__fastcall*)(void* outResult, void* sys, void* modsDir);

std::atomic<CtorFn_t> g_orig{nullptr};
std::atomic<bool>     g_installed{false};
std::atomic<bool>     g_installSucceeded{false};
std::atomic<bool>     g_captured{false};

void* __fastcall HookedCtor(void* outResult, void* sys, void* modsDir) {
    // One-shot capture: a second fire (defensive — the seed prose confirms
    // single-call from CSystem::Init, but we guard anyway) returns straight to
    // the original without re-dumping.
    bool expected = false;
    const bool firstFire = g_captured.compare_exchange_strong(
        expected, true, std::memory_order_acq_rel);

    CtorFn_t orig = g_orig.load(std::memory_order_acquire);
    if (!orig) {
        // Should not happen — Install() refuses to enable the detour without
        // a non-null trampoline. Log loud and return null; the engine will
        // observe a null mod-manager and fail loud downstream rather than
        // corrupting memory.
        LOG_ERROR_KV(kCat, "orig_ctor_null_at_dispatch",
            kcdx::log::KV("detail", "the MinHook trampoline is null at "
                                    "dispatch; cannot forward to the "
                                    "original ctor this fire"));
        return nullptr;
    }

    if (!firstFire) {
        // Defensive re-entry — forward and return without snapshotting.
        return orig(outResult, sys, modsDir);
    }

    // === ENTRY: log input args (helps confirm the ABI matches the seed
    // prose). The `outResult` buffer is uninitialized at entry — it is the
    // caller's stack alloc for the about-to-be-constructed object. We log it
    // raw as "pre-ctor state" for completeness; downstream readers should
    // treat its bytes as undefined.
    LOG_INFO_KV(kCat, "ctor_entry",
        kcdx::log::KV("out_result", outResult),
        kcdx::log::KV("sys",        sys),
        kcdx::log::KV("mods_dir",   modsDir));

    LOG_INFO_KV(kCat, "input_arg",
        kcdx::log::KV("arg",      "sys"),
        kcdx::log::KV("ptr",      sys),
        kcdx::log::KV("kind",     "input_arg"));
    LOG_INFO_KV(kCat, "input_arg",
        kcdx::log::KV("arg",      "mods_dir"),
        kcdx::log::KV("ptr",      modsDir),
        kcdx::log::KV("kind",     "input_arg"));

    // === Call the original ctor unchanged. Observe-only — we do NOT mutate
    // outResult, sys, or modsDir. The engine sees the same behavior it would
    // see without the probe installed.
    void* ret = orig(outResult, sys, modsDir);

    // === EXIT: snapshot the full 0x68 bytes the ctor populated. Log every
    // non-zero 8-byte slot. The whole range is dumped (not just the prose-
    // predicted slots) so an unexpected write at +0x40..+0x68 surfaces in
    // the log instead of being filtered out by our prior assumption.
    if (!outResult) {
        LOG_ERROR_KV(kCat, "out_result_null_after_ctor",
            kcdx::log::KV("detail", "the ctor returned with outResult null — "
                                    "cannot snapshot the C_ModManager state"));
        return ret;
    }

    const uint8_t* base = static_cast<const uint8_t*>(outResult);

    // Build a list of every offset that holds a non-zero value, for the
    // summary line at the end. Cap is generous (every 8-byte slot fits).
    char summaryBuf[256];
    size_t summaryLen = 0;
    summaryBuf[0] = '\0';

    auto appendSummary = [&](size_t off) {
        if (summaryLen >= sizeof(summaryBuf) - 1) return;
        int n = snprintf(summaryBuf + summaryLen,
                         sizeof(summaryBuf) - summaryLen,
                         summaryLen == 0 ? "0x%02zX" : ",0x%02zX", off);
        if (n > 0) summaryLen += static_cast<size_t>(n);
    };

    // Iterate the object as 8-byte slots. kObjectSize is 0x68 = 13 slots.
    for (size_t off = 0; off + 8 <= kObjectSize; off += 8) {
        uint64_t value = 0;
        std::memcpy(&value, base + off, sizeof(value));
        if (value == 0) continue;

        appendSummary(off);

        // Classify the slot for greppable per-line context. The kind values
        // come from the verified seed prose — they are descriptive labels for
        // the human reader, not interpretations of the bytes. The seed's
        // predicted range is +0x00..+0x58 (vptr / sys / modsDir / sub-vptr /
        // zero-init lists); a write at +0x58 or +0x60 logs with kind="raw" —
        // the genuine surprise-write signal Outcome B watches for.
        const char* kind;
        switch (off) {
            case 0x00: kind = "vtable";    break;  // wh::C_ModManager vftable
            case 0x08: kind = "ptr_sys";   break;  // input sys
            case 0x10: kind = "ptr_mods";  break;  // input modsDir
            case 0x18: kind = "vtable";    break;  // sub-object vftable
            case 0x20:
            case 0x28:
            case 0x30:
            case 0x38:
            case 0x40:
            case 0x48:
            case 0x50: kind = "list_zero_init"; break;  // seed +0x18..+0x58 zero-init range
            default:   kind = "raw";            break;  // surprise write — +0x58/+0x60 or out-of-range
        }

        // hex offset string ("0xNN") — KV's HEX form formats with the 0x
        // prefix but treats the value as the *content*, not the offset, so we
        // pass the offset as a quoted STR for trivial grepping by exact text.
        char offStr[8];
        snprintf(offStr, sizeof(offStr), "0x%02zX", off);

        LOG_INFO_KV(kCat, "exit_slot",
            kcdx::log::KV::BareStr("offset", offStr),
            kcdx::log::KV("value_hex", static_cast<uintptr_t>(value)),
            kcdx::log::KV::BareStr("kind", kind));
    }

    // Summary line — single greppable answer to "which offsets did the ctor
    // write?" If summaryBuf is empty the ctor produced an all-zero object
    // (Outcome impossible by the prose — the vptr alone is non-zero — so an
    // empty summary is itself a finding).
    LOG_INFO_KV(kCat, "exit_summary",
        kcdx::log::KV("nonzero_offsets",
                      summaryBuf[0] ? summaryBuf : "(none)"),
        kcdx::log::KV("object_size_hex",
                      static_cast<uintptr_t>(kObjectSize)),
        kcdx::log::KV("return_value", ret));

    return ret;
}

}  // namespace

bool Install() {
    // Idempotent — a repeated Install() returns the cached outcome of the
    // first attempt this session.
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        return g_installSucceeded.load(std::memory_order_acquire);
    }

    HMODULE whgame = GetModuleHandleW(L"WHGame.dll");
    if (!whgame) {
        LOG_ERROR_KV(kCat, "install_failed",
            kcdx::log::KV("reason", "WHGame.dll not mapped at Install time — "
                                    "the ctor probe cannot be installed this "
                                    "boot"));
        return false;
    }

    // Resolve the ctor address by Address Library ID (never a hardcoded RVA;
    // the row carries the per-build address gated on a game-version match).
    const uintptr_t target = address_library::Resolve(kCtorId);
    if (target == 0) {
        LOG_ERROR_KV(kCat, "install_failed",
            kcdx::log::KV("reason", "ModManager_ctor (Address Library id "
                                    "3101) did not resolve — version mismatch "
                                    "or unverified row; the ctor probe is "
                                    "inactive this boot"),
            kcdx::log::KV("id", static_cast<uint64_t>(kCtorId)));
        return false;
    }

    // MinHook init is idempotent — the worker-thread caller / earlier
    // mod_absorb installs may have initialized it already.
    MH_STATUS si = MH_Initialize();
    if (si != MH_OK && si != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_ERROR_KV(kCat, "install_failed",
            kcdx::log::KV("reason", "MH_Initialize failed; the ctor probe is "
                                    "inactive this boot"),
            kcdx::log::KV("mh_status", static_cast<long long>(si)));
        return false;
    }

    void* targetPtr = reinterpret_cast<void*>(target);
    void* origPtr   = nullptr;
    MH_STATUS s = MH_CreateHook(targetPtr,
                                reinterpret_cast<void*>(&HookedCtor),
                                &origPtr);
    if (s != MH_OK) {
        LOG_ERROR_KV(kCat, "install_failed",
            kcdx::log::KV("reason", "MH_CreateHook on ModManager_ctor failed; "
                                    "the ctor probe is inactive this boot"),
            kcdx::log::KV("target", reinterpret_cast<uintptr_t>(targetPtr)),
            kcdx::log::KV("mh_status", static_cast<long long>(s)));
        return false;
    }
    g_orig.store(reinterpret_cast<CtorFn_t>(origPtr),
                 std::memory_order_release);

    s = MH_EnableHook(targetPtr);
    if (s != MH_OK) {
        LOG_ERROR_KV(kCat, "install_failed",
            kcdx::log::KV("reason", "MH_EnableHook on ModManager_ctor failed; "
                                    "the ctor probe is inactive this boot"),
            kcdx::log::KV("target", reinterpret_cast<uintptr_t>(targetPtr)),
            kcdx::log::KV("mh_status", static_cast<long long>(s)));
        return false;
    }

    LOG_INFO_KV(kCat, "install_ok",
        kcdx::log::KV("target", reinterpret_cast<uintptr_t>(targetPtr)),
        kcdx::log::KV("id", static_cast<uint64_t>(kCtorId)),
        kcdx::log::KV("detail", "ModManager_ctor read-only state probe armed "
                                "— a one-shot snapshot will dump on first "
                                "fire under category MOD_ABSORB_PROBE"));
    g_installSucceeded.store(true, std::memory_order_release);
    return true;
}

}  // namespace kcdx::mod_absorb::ctor_probe
