// CAP-40 — kcdxTrampolineInterface v2 (C++ mirror of kcdx.code) end-to-end.
//
// The verification plugin that proves the v2
// peers of the raw AllocateFrom*Pool floor work for a real C++ DLL author.
// The ABI was wired: the header decl (kcdxCodeOptions +
// Allocate/Export appended to kcdxTrampolineInterface, _Version=2u), the
// src/trampoline.cpp engine impl (Thunk_Allocate alloc+memcpy+NOP-pad+
// export-register; Thunk_Export standalone symbols::Register), and K.code on
// Kcdx.h already pointing at kcdxTrampolineInterface fetched at version 2u.
// This plugin is the FIRST non-Lua-binder consumer of Allocate/Export and the
// C++ PEER of the Lua kcdx.code test coverage (the same capability is
// tested across both languages).
//
// === Test design — fully self-hosting, deterministic, boot-time ==========
//
// Unlike cap-39 (bytes), kcdx.code allocation is PLUGIN-OWNED memory — the
// region comes from the trampoline pools, not a game site. So there is NO
// cap-01-style conflict entanglement and NO dual-Lua boundary issue: the
// plugin allocates its own executable memory and reads it straight back. Every
// observable is verifiable in-process with no game function involved.
//
// The export rows resolve via K.api->ResolveSymbolAs(K.self, "<bareName>").
// WHY ResolveSymbolAs and not the bare ResolveSymbol: Thunk_Allocate /
// Thunk_Export register the export through kcdx::symbols::Register(bareName,
// addr, author, plugin), which STORES the symbol under the
// <author>.<plugin>.<bareName> namespace key (symbols.h: the engine derives
// the prefix; the author writes only the bare name). A bare ResolveSymbol(
// "cap40_region") carries NO caller identity, so it resolves on the
// other-only path and MISSES our own export (Interfaces.h ResolveSymbolAs doc:
// "a bare ResolveSymbol with no owner misses your own export"). ResolveSymbolAs
// threads K.self as the OWNER, so the self-tier of self > other resolves our
// own <author>.<plugin>.<bareName> export from the bare name — the exact call
// the Lua side uses to consume a self-export. Getting this wrong (using the
// anonymous resolver) would make the export rows falsely FAIL.
//
// === The three rows ======================================================
//
//   * CAP-40-cpp-code-allocate   The all-in-one Allocate proof.
//       K.code->Allocate(&opts) with opts.bytes = "mov eax,42; ret"
//       (B8 2A 00 00 00 C3), opts.bytesSize = 6, opts.size = 10 (exercises the
//       NOP-pad tail), opts.pool = kcdxCodePool_Branch. Assert: region != null
//       AND the first 6 bytes at region == the bytes we wrote (read back our
//       own executable memory) AND the padded tail bytes [6,10) == 0x90 (the
//       NOP-pad) AND — the strongest proof the region is genuinely executable —
//       casting region to int(*)() and CALLING it returns 42. FALSIFIABLE:
//       null region / wrong head bytes / non-0x90 pad / call != 42 → FAIL.
//
//   * CAP-40-cpp-code-export      The Allocate-with-export publish path.
//       K.code->Allocate with opts.exportName = "cap40_region" (a BARE name).
//       Assert region != null AND ResolveSymbolAs(K.self, "cap40_region")
//       returns the SAME address as the allocated region. Proves the export=
//       publish path: publish via Allocate, consume via
//       ResolveSymbol. FALSIFIABLE: export doesn't resolve / resolves to a
//       different address → FAIL.
//
//   * CAP-40-cpp-code-export-standalone   The standalone Export (no alloc).
//       K.code->Export(K.self, "cap40_standalone", &g_standalone_target) where
//       the address is a stable plugin-owned static. Assert Export returns true
//       AND ResolveSymbolAs(K.self, "cap40_standalone") returns that same
//       address. Proves the standalone publish for an address the plugin
//       already holds (no allocation). FALSIFIABLE: Export false / resolves
//       wrong → FAIL.
//
// Test mode: boot-only. All three rows self-verify at kcdxPlugin_PostGameLoad
// (the after_game C++ export the engine fires AFTER ApplyZone(AfterGame) and
// BEFORE kcdxMessage_InputLoaded — by then the export symbol table is fully
// populated; the cap-29 / cap-36 / cap-39 pattern). NO console gestures, NO
// save/load. An InputLoaded backstop reports a loud FAIL on all three rows if
// PostGameLoad never fired, so a missing after-phase export never leaves a
// silent PENDING.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "kcdx/Interfaces.h"
#include "kcdx/Kcdx.h"

namespace {

// Manifest identity — must match [plugin].author + [plugin].name in
// kcdx.toml. K.Init derives self from the bare name.
const char* kAuthor = "ts";
const char* kName   = "cap_40_cpp_code";

// Row IDs (must match [plugin].test_names exactly).
const char* kRowAllocate   = "CAP-40-cpp-code-allocate";
const char* kRowExport     = "CAP-40-cpp-code-export";
const char* kRowStandalone = "CAP-40-cpp-code-export-standalone";

// "mov eax, 42; ret" — a valid, self-contained int() function: no relocations,
// no external calls, trivial calling convention. Safe to cast-and-call from a
// BRANCH-pool region (PAGE_EXECUTE_READWRITE), which is what makes the "call it
// and assert == 42" executable proof legitimate.
const unsigned char kMovEax42Ret[6] = { 0xB8, 0x2A, 0x00, 0x00, 0x00, 0xC3 };
const size_t        kCodeSize        = 10;  // 6 bytes of code + 4 bytes NOP-pad

// A stable plugin-owned address the standalone Export row publishes. A static
// buffer (its address is fixed for the process lifetime).
volatile unsigned char g_standalone_target[16] = {};

// The empowered wrapper — fetches Trampoline (K.code) + Messaging in one Init.
Kcdx K;

// Allocate/Export results captured at Load, asserted at PostGameLoad (the
// cross-phase symbol-table proof).
void*     g_alloc_region    = nullptr;   // row 1: Allocate, no export
void*     g_export_region   = nullptr;   // row 2: Allocate with exportName
uintptr_t g_standalone_addr = 0;         // row 3: published address
bool      g_standalone_ret  = false;     // row 3: Export return value

// One-shot guard: the InputLoaded backstop fires only if PostGameLoad never
// reported (the cap-36 / cap-39 design).
bool g_post_ran = false;

void Report(const char* row, bool pass, const char* reason) {
    if (pass) K.log.Info ("CAP40", "PASS %s: %s", row, reason);
    else      K.log.Error("CAP40", "FAIL %s: %s", row, reason);
    K.api->ReportTestResult(K.self, row, pass ? 1 : 0, reason);
}

// === InputLoaded backstop — loud FAIL if PostGameLoad never fired =========

void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    if (g_post_ran) return;  // PostGameLoad already reported the rows.

    const char* reason =
        "kcdxPlugin_PostGameLoad did not fire before kcdxMessage_InputLoaded "
        "— the after-phase C++ export was not dispatched; all rows reported "
        "FAIL via the InputLoaded backstop";
    K.log.Error("CAP40", "FAIL backstop: %s", reason);
    K.api->ReportTestResult(K.self, kRowAllocate,   0, reason);
    K.api->ReportTestResult(K.self, kRowExport,     0, reason);
    K.api->ReportTestResult(K.self, kRowStandalone, 0, reason);
}

}  // namespace

// === kcdxPlugin_Load ======================================================
//
// kcdx.code is NOT deferred — Allocate/Export run immediately at the call (the
// region is live the moment Allocate returns, and the symbol is registered
// immediately). So we do all the work AND can assert it right here at Load.
// But to mirror the cap-39 lifecycle (and to confirm the export symbol table
// survives into the after_game phase), we ALLOCATE + EXPORT at Load and ASSERT
// at PostGameLoad — the symbol-table resolution is then a cross-phase proof.

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!K.Init(api, kAuthor, kName)) {
        // K.Init logs why (it requires Hook; code/Trampoline is best-effort
        // below). Report all rows FAIL so none sits silent-PENDING.
        if (api) {
            kcdxPluginHandle self = api->GetPluginHandle(kName);
            const char* r = "Kcdx::Init returned false at Plugin_Load "
                            "(engine version mismatch?)";
            api->ReportTestResult(self, kRowAllocate,   0, r);
            api->ReportTestResult(self, kRowExport,     0, r);
            api->ReportTestResult(self, kRowStandalone, 0, r);
        }
        return true;
    }
    K.log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)",
               api->kcdxVersion);

    if (!K.code) {
        K.log.Error("INIT",
            "QueryInterface(Trampoline, v%u) returned null — all rows FAIL",
            kcdxTrampolineInterface_Version);
        Report(kRowAllocate, false,
            "K.code is null: QueryInterface(kcdxInterface_Trampoline, v2) "
            "returned null at Plugin_Load (engine older than v2?)");
        Report(kRowExport,     false, "K.code is null at Plugin_Load");
        Report(kRowStandalone, false, "K.code is null at Plugin_Load");
        return true;
    }

    // Messaging is best-effort — only the InputLoaded backstop needs it.
    if (K.messaging) {
        K.messaging->RegisterListener(K.self, /*sender=*/nullptr, OnMessage);
    } else {
        K.log.Warn("INIT",
            "QueryInterface(Messaging) returned null — InputLoaded backstop "
            "disabled (if PostGameLoad never fires the rows sit "
            "silent-PENDING rather than loud-FAIL)");
    }

    // --- Row 1: Allocate (no export) — the alloc+fill+pad region. ---------
    {
        kcdxCodeOptions opts = {};
        opts.owningPlugin = K.self;
        opts.name         = "cap40_movret_region";
        opts.bytes        = kMovEax42Ret;
        opts.bytesSize    = sizeof(kMovEax42Ret);   // 6
        opts.size         = kCodeSize;              // 10 → 4-byte NOP-pad tail
        opts.pool         = kcdxCodePool_Branch;
        g_alloc_region = K.code->Allocate(&opts);
        K.log.Info("CODE", "Allocate(cap40_movret_region) -> 0x%p",
                   g_alloc_region);
    }

    // --- Row 2: Allocate WITH export — publishes the region's address. ----
    {
        kcdxCodeOptions opts = {};
        opts.owningPlugin = K.self;
        opts.name         = "cap40_export_region";
        opts.bytes        = kMovEax42Ret;
        opts.bytesSize    = sizeof(kMovEax42Ret);
        opts.size         = sizeof(kMovEax42Ret);   // exact (no pad needed)
        opts.pool         = kcdxCodePool_Branch;
        opts.exportName   = "cap40_region";         // BARE — engine stamps prefix
        g_export_region = K.code->Allocate(&opts);
        K.log.Info("CODE", "Allocate(cap40_export_region, export=cap40_region) "
                   "-> 0x%p", g_export_region);
    }

    // --- Row 3: standalone Export of a plugin-owned static. ---------------
    {
        g_standalone_addr =
            reinterpret_cast<uintptr_t>(const_cast<unsigned char*>(
                g_standalone_target));
        g_standalone_ret =
            K.code->Export(K.self, "cap40_standalone", g_standalone_addr);
        K.log.Info("CODE", "Export(cap40_standalone -> 0x%p) returned %d",
                   reinterpret_cast<void*>(g_standalone_addr),
                   g_standalone_ret ? 1 : 0);
    }
    return true;
}

// === kcdxPlugin_PostGameLoad ==============================================
//
// Fires in the after_game phase (after ApplyZone(AfterGame), before
// InputLoaded). Assert all three rows here — the symbol-table resolution is a
// cross-phase proof that the Load-time exports survive into after_game.

extern "C" __declspec(dllexport)
bool kcdxPlugin_PostGameLoad(const kcdxInterface* api) {
    (void)api;  // K.api was cached in Load; same pointer.
    K.log.Info("CAP40",
               "kcdxPlugin_PostGameLoad — running the allocate + export "
               "assertions");
    g_post_ran = true;

    if (!K.code) {
        Report(kRowAllocate, false,
            "K.code null in PostGameLoad (should not happen — Load checked)");
        Report(kRowExport,     false, "K.code null in PostGameLoad");
        Report(kRowStandalone, false, "K.code null in PostGameLoad");
        return true;
    }

    // --- Row 1: allocate — read-back + NOP-pad + call-it-and-assert-42 ----
    {
        bool region_ok = (g_alloc_region != nullptr);
        bool head_ok   = false;
        bool pad_ok    = false;
        bool call_ok   = false;
        int  called    = -1;

        if (region_ok) {
            const auto* p = reinterpret_cast<const unsigned char*>(g_alloc_region);
            head_ok = std::memcmp(p, kMovEax42Ret, sizeof(kMovEax42Ret)) == 0;
            // Padded tail bytes [bytesSize, size) must all be 0x90 (NOP).
            pad_ok = true;
            for (size_t i = sizeof(kMovEax42Ret); i < kCodeSize; ++i)
                if (p[i] != 0x90) { pad_ok = false; break; }
            // Executable proof: the region IS a valid int() = `return 42`.
            // Safe — verified-correct self-contained machine code in a
            // PAGE_EXECUTE_READWRITE branch-pool region.
            if (head_ok) {
                using IntFn = int (*)();
                IntFn fn = reinterpret_cast<IntFn>(g_alloc_region);
                called  = fn();
                call_ok = (called == 42);
            }
        }

        char reason[400];
        const bool pass = region_ok && head_ok && pad_ok && call_ok;
        snprintf(reason, sizeof(reason),
            "%s — region=%s, head bytes match=%d (mov eax,42; ret), NOP-pad "
            "[%zu,%zu)=0x90 match=%d, executed region returned %d (expected "
            "42)=%d; allocated via K.code->Allocate(pool=branch, bytesSize=%zu, "
            "size=%zu) — the C++ peer of Lua kcdx.code{...}",
            pass ? "Allocate produced a filled, NOP-padded, executable region"
                 : "Allocate did NOT produce the expected region",
            region_ok ? "non-null" : "NULL",
            head_ok ? 1 : 0,
            sizeof(kMovEax42Ret), kCodeSize, pad_ok ? 1 : 0,
            called, call_ok ? 1 : 0,
            sizeof(kMovEax42Ret), kCodeSize);
        Report(kRowAllocate, pass, reason);
    }

    // --- Row 2: export via Allocate — ResolveSymbolAs round-trips. --------
    {
        bool region_ok = (g_export_region != nullptr);
        uintptr_t resolved =
            K.api->ResolveSymbolAs(K.self, "cap40_region");
        const uintptr_t expected =
            reinterpret_cast<uintptr_t>(g_export_region);

        char reason[400];
        const bool pass = region_ok && resolved != 0 && resolved == expected;
        snprintf(reason, sizeof(reason),
            "%s — region=%s (0x%p), ResolveSymbolAs(self, \"cap40_region\") "
            "resolved to 0x%p (expected 0x%p)=%d; published via "
            "K.code->Allocate(exportName=\"cap40_region\") (BARE name; engine "
            "stamps <author>.<plugin> prefix), consumed via the self-tier "
            "resolver — the export= publish path",
            pass ? "export resolves to the allocated region"
                 : "export did NOT resolve to the allocated region",
            region_ok ? "non-null" : "NULL",
            g_export_region,
            reinterpret_cast<void*>(resolved),
            reinterpret_cast<void*>(expected),
            (resolved == expected) ? 1 : 0);
        Report(kRowExport, pass, reason);
    }

    // --- Row 3: standalone Export — ResolveSymbolAs round-trips. ----------
    {
        uintptr_t resolved =
            K.api->ResolveSymbolAs(K.self, "cap40_standalone");

        char reason[400];
        const bool pass = g_standalone_ret && resolved != 0 &&
                          resolved == g_standalone_addr;
        snprintf(reason, sizeof(reason),
            "%s — Export returned %d (expected 1), ResolveSymbolAs(self, "
            "\"cap40_standalone\") resolved to 0x%p (expected 0x%p, a "
            "plugin-owned static)=%d; the standalone publish (no allocation) "
            "for an address the plugin already holds",
            pass ? "standalone Export publishes + resolves"
                 : "standalone Export did NOT round-trip",
            g_standalone_ret ? 1 : 0,
            reinterpret_cast<void*>(resolved),
            reinterpret_cast<void*>(g_standalone_addr),
            (resolved == g_standalone_addr) ? 1 : 0);
        Report(kRowStandalone, pass, reason);
    }

    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
