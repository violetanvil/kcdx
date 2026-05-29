// CAP-63 — kcdx::bytes::Write empowered wrapper (C++) end-to-end.
//
// The empowered-floor C++ peer of cap-01's Lua kcdx.bytes test. cap-39
// already covers the RAW floor (K.bytes->Register(&opts) with all options
// hand-built); this row covers the WRAPPER floor (the one-liner the docs
// put as the everyday path):
//
//   kcdx::bytes::TryWrite(K, "outfit_swap_callsite_aob", "45 31 F6", &opts);
//
// The wrapper threads owningPlugin = K.self automatically, takes name +
// replacement positionally, and falls back to the supplied opts for
// advanced refinements (offset / original / idempotent).
//
// One row: CAP-63-wrapper-installs — at PostGameLoad the bytes at the
// resolved site equal `45 31 F6`. The site is the same verified-safe
// rewrite cap-01 + cap-39 already cover (same conflict_engine
// WriteOnWriteFull idempotent-skip coexistence). The wrapper produces the
// SAME observable as the flat-table form, end-to-end.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "kcdx/Interfaces.h"
#include "kcdx/Kcdx.h"

namespace {

const char* kAuthor = "ts";
const char* kName   = "cap_63_cpp_bytes_wrapper";

const char* kRowInstalls = "CAP-63-wrapper-installs";

// The same verified-safe rewrite cap-01 / cap-39 prove: id 1004 =
// outfit_swap_callsite_aob, the 'mov r14b, al' site at +13 from the AOB
// start that becomes 'xor r14d, r14d'.
const char* kTarget      = "outfit_swap_callsite_aob";
const char* kOriginal    = "44 8A F0";
const char* kReplacement = "45 31 F6";
const unsigned char kReplBytes[3] = { 0x45, 0x31, 0xF6 };

Kcdx           K;
kcdxBytesHandle g_handle = 0;
bool            g_post_ran = false;

void Report(const char* row, bool pass, const char* reason) {
    if (pass) K.log.Info ("CAP63", "PASS %s: %s", row, reason);
    else      K.log.Error("CAP63", "FAIL %s: %s", row, reason);
    K.api->ReportTestResult(K.self, row, pass ? 1 : 0, reason);
}

// Resolve the rewrite site's live VA so the PostGameLoad read-back has an
// address to read. Same wide-context approach as cap-39's
// ResolveSiteForReadback — scan for the POST-rewrite context (offset +20
// lands on the 3-byte rewrite site), so the scan still resolves after the
// apply has flipped the bytes.
uintptr_t ResolveSiteForReadback() {
    if (!K.memory) return 0;
    const char* postCtx =
        "48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 45 31 F6";
    uintptr_t ctx = K.memory->ScanPattern("WHGame.dll", postCtx);
    if (!ctx) return 0;
    return ctx + 20;
}

void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    if (g_post_ran) return;
    const char* reason =
        "kcdxPlugin_PostGameLoad did not fire before kcdxMessage_"
        "InputLoaded — the after-phase export was not dispatched; "
        "row reported FAIL via the InputLoaded backstop";
    K.log.Error("CAP63", "FAIL backstop: %s", reason);
    K.api->ReportTestResult(K.self, kRowInstalls, 0, reason);
}

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!K.Init(api, kAuthor, kName)) {
        if (api) {
            kcdxPluginHandle self = api->GetPluginHandle(kName);
            api->ReportTestResult(self, kRowInstalls, 0,
                "Kcdx::Init returned false at Plugin_Load (engine version mismatch?)");
        }
        return true;
    }
    K.log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)",
               api->kcdxVersion);

    if (!K.bytes) {
        K.log.Error("INIT",
            "QueryInterface(Bytes, v%u) returned null — row FAILs",
            kcdxBytesInterface_Version);
        Report(kRowInstalls, false,
            "K.bytes is null: QueryInterface(kcdxInterface_Bytes) returned "
            "null at Plugin_Load (engine version mismatch?)");
        return true;
    }

    if (K.messaging) {
        K.messaging->RegisterListener(K.self, /*sender=*/nullptr, OnMessage);
    } else {
        K.log.Warn("INIT",
            "QueryInterface(Messaging) returned null — InputLoaded backstop "
            "disabled (if PostGameLoad never fires the row sits silent-PENDING)");
    }

    // Build the advanced-refinement options: offset 13 (the rewrite is at
    // +13 within the named AOB), original-byte verify, idempotent so it
    // coexists with cap-01 + cap-39 same-write rewrites via the conflict
    // engine's WriteOnWriteFull idempotent-skip. The wrapper's positional
    // (target, replacement) are passed at the call; the wrapper threads
    // owningPlugin = K.self for us — the empowered floor's whole point.
    kcdxBytesOptions opts = {};
    opts.name         = "cap63_wrapper_outfit_swap_rewrite";
    opts.offset       = 13;
    opts.original     = kOriginal;
    opts.idempotent   = true;

    // The empowered call — one line for the install, no manual
    // opts.owningPlugin assignment, no manual opts.target / opts.replacement
    // (the wrapper writes them from the positional args).
    g_handle = kcdx::bytes::TryWrite(K, kTarget, kReplacement, &opts);
    if (g_handle == 0) {
        K.log.Error("INIT",
            "kcdx::bytes::TryWrite returned 0 (see BYTES_INTERFACE engine "
            "log for the teaching error)");
        // Let PostGameLoad surface the FAIL with the observed handle == 0.
    } else {
        K.log.Info("INIT",
            "kcdx::bytes::TryWrite returned a non-zero handle; final IsApplied "
            "verdict + read-back read in PostGameLoad after the apply pass");
    }
    return true;
}

extern "C" __declspec(dllexport)
bool kcdxPlugin_PostGameLoad(const kcdxInterface* api) {
    (void)api;
    K.log.Info("CAP63",
               "kcdxPlugin_PostGameLoad — apply pass done; running the "
               "wrapper-install + bytes-read-back assertion");
    g_post_ran = true;

    if (!K.bytes) {
        Report(kRowInstalls, false, "K.bytes null in PostGameLoad (should not happen — Load checked)");
        return true;
    }

    const bool applied = K.bytes->IsApplied(g_handle);

    uintptr_t site = ResolveSiteForReadback();
    unsigned char live[3] = { 0, 0, 0 };
    bool read_ok = false;
    bool bytes_match = false;
    if (site && K.memory) {
        read_ok = K.memory->ReadBytes(site, live, sizeof(live)) != 0;
        bytes_match = read_ok &&
            std::memcmp(live, kReplBytes, sizeof(kReplBytes)) == 0;
    }

    char reason[400];
    const bool pass = (g_handle != 0) && applied && bytes_match;
    snprintf(reason, sizeof(reason),
        "%s — handle=%s, IsApplied=%d; site %s, read=%d, live bytes "
        "%02X %02X %02X (expected 45 31 F6); registered via "
        "kcdx::bytes::TryWrite(K, \"%s\", \"45 31 F6\", &opts) — the "
        "empowered wrapper installs at the same site cap-01 / cap-39 cover, "
        "producing the same observable end-to-end",
        pass ? "wrapper install + apply took effect"
             : "wrapper install / apply did NOT take effect",
        g_handle != 0 ? "non-zero" : "ZERO",
        applied ? 1 : 0,
        site ? "resolved" : "UNRESOLVED (read-back skipped)",
        read_ok ? 1 : 0,
        live[0], live[1], live[2],
        kTarget);
    Report(kRowInstalls, pass, reason);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
