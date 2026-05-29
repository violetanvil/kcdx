// CAP-62 — kcdxDeclareInterface (C++ peer of kcdx.declare / kcdx.declared)
// end-to-end + the load-bearing stringValue lifetime falsifiability rows.
//
// Four rows, all self-reported from kcdxPlugin_Load (the declared-targets
// store is reachable at this init phase — the K.declare->Declare / Get
// path is the same code path the Lua binder reaches synchronously, no
// deferred-apply round-trip needed for a declared-target accessor):
//
//   1. CAP-62-declare-get-roundtrip
//      Declare a per-version INTEGER value entry; Get it back from the
//      SELF tier; assert (found && !isString && intValue matches).
//
//   2. CAP-62-stringvalue-content-survives-second-declare  (CONTRACT)
//      Declare a string-value canary; cache its stringValue pointer; issue
//      ONE distinct subsequent Declare (the minimum that exercises "a
//      subsequent Declare from any plugin"); assert the cached pointer
//      still reads back as the canary payload. The contract surface — what
//      the author actually cares about: the string they Got is still the
//      string they Declared, after some other Declare happened in between.
//
//   3. CAP-62-stringvalue-address-survives-second-declare  (MECHANISM)
//      Declare a string-value canary; cache its stringValue pointer; issue
//      ONE distinct subsequent Declare; re-Get the same canary name and
//      assert the returned stringValue POINTER is identity-equal (==) to
//      the cached pointer. The load-bearing falsifiability row: the
//      "process-lifetime pointer" contract reduces to "the pointer ADDRESS
//      survives." On node-stable storage, both pointers are into the same
//      slot inside the same node — address-equal by construction. On a
//      hypothetical container that moved elements on push_back, the
//      address-equality goes red even if SSO + move happened to preserve
//      the content at a different address.
//
//   4. CAP-62-pattern-entry-get-returns-miss
//      Declare a PATTERN entry; Get it; assert found == false. PATTERN
//      entries are consumed by the hook / bytes verbs by name, not by the
//      value accessor — the accessor's miss contract for pattern entries
//      is part of the API.
//
// Rows 2 and 3 share ONE setup (two distinct Declares + one cached
// pointer between them) — the minimum that exercises the "subsequent
// Declare from any plugin" surface of the contract. No filler loop, no
// store wipe, no priority gymnastics, no SSO precondition.
//
// All test_suite_only — production users never see this plugin.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "kcdx/Interfaces.h"
#include "kcdx/Kcdx.h"

namespace {

// Manifest identity — MUST match [plugin].author + [plugin].name in
// kcdx.toml. K.Init derives self from the bare name (api->GetPluginHandle).
const char* kAuthor = "ts";
const char* kName   = "cap_62_cpp_declare_interface";

// Row IDs (must match [plugin].test_names in kcdx.toml).
const char* kRowRoundtrip    = "CAP-62-declare-get-roundtrip";
const char* kRowContent      = "CAP-62-stringvalue-content-survives-second-declare";
const char* kRowAddress      = "CAP-62-stringvalue-address-survives-second-declare";
const char* kRowPatternMiss  = "CAP-62-pattern-entry-get-returns-miss";

// The Kcdx wrapper — fetches K.declare + K.log + K.api in one Init.
Kcdx K;

// Lifetime canary + second-declare names + payloads. The two Declares are
// independent triples (distinct names, distinct payloads) — the minimum
// that exercises "a subsequent Declare from any plugin."
const char* kCanaryName       = "cap62_lifetime_canary";
const char* kCanaryPayload    = "canary";
const char* kSecondName       = "cap62_lifetime_second";
const char* kSecondPayload    = "second";

void Report(const char* row, bool pass, const char* reason) {
    if (pass) K.log.Info ("CAP62", "PASS %s: %s", row, reason);
    else      K.log.Error("CAP62", "FAIL %s: %s", row, reason);
    K.api->ReportTestResult(K.self, row, pass ? 1 : 0, reason);
}

// === Row 1 — Declare + Get round-trip on an integer value entry =========

void RunRoundtripRow() {
    static constexpr int64_t kExpected = 0x0A55BABE;
    const kcdxDeclareEntry entries[] = {
        { /*versionKey=*/   "*",        // matches every running version
          /*patternStr=*/   nullptr,
          /*signatureStr=*/ nullptr,
          /*kindTag=*/      nullptr,
          /*valueInt=*/     kExpected,
          /*valueStr=*/     nullptr,
          /*valueIsString=*/false },
    };
    const bool decl_ok = K.declare->Declare(
        "WHGame.dll", "cap62_roundtrip_value", entries,
        sizeof(entries) / sizeof(entries[0]), K.self);
    if (!decl_ok) {
        Report(kRowRoundtrip, false,
            "K.declare->Declare returned false at the integer-value "
            "round-trip declaration — see DECLARED_TARGET / "
            "DECLARED_TARGET_BIND in the engine log for the teaching reason");
        return;
    }

    const kcdxDeclaredValue v =
        K.declare->Get("cap62_roundtrip_value", K.self);
    char reason[400];
    const bool pass =
        v.found && !v.isString && v.intValue == kExpected;
    snprintf(reason, sizeof(reason),
        "%s — found=%d, isString=%d, intValue=0x%llx (expected 0x%llx). "
        "SELF-tier Get of a bare 1-segment name returned %s",
        pass ? "Declare + Get integer round-trip succeeded"
             : "round-trip MISMATCH",
        v.found ? 1 : 0,
        v.isString ? 1 : 0,
        (unsigned long long)v.intValue,
        (unsigned long long)kExpected,
        v.found ? "the declared payload" : "a miss");
    Report(kRowRoundtrip, pass, reason);
}

// === Rows 2 + 3 — stringValue lifetime across a subsequent Declare ======
//
// One shared setup: Declare the canary, Get it (cache the pointer),
// Declare a second distinct triple. Then row 2 tests the contract
// (cached pointer still reads back as the canary payload) and row 3
// tests the mechanism (re-Get of the canary returns the same pointer
// ADDRESS). Both rows are independent post-conditions on the same
// minimum two-Declare interleaving.

void RunLifetimeRows() {
    // Step 1 — Declare the canary string-value entry.
    const kcdxDeclareEntry canary_entries[] = {
        { /*versionKey=*/   "*",
          /*patternStr=*/   nullptr,
          /*signatureStr=*/ nullptr,
          /*kindTag=*/      nullptr,
          /*valueInt=*/     0,
          /*valueStr=*/     kCanaryPayload,
          /*valueIsString=*/true },
    };
    if (!K.declare->Declare("WHGame.dll", kCanaryName,
                            canary_entries, 1, K.self)) {
        Report(kRowContent, false,
            "K.declare->Declare on the canary string-value entry returned "
            "false — see DECLARED_TARGET / DECLARED_TARGET_BIND in the "
            "engine log; lifetime rows cannot run without the canary");
        Report(kRowAddress, false,
            "K.declare->Declare on the canary string-value entry returned "
            "false (see CAP-62-stringvalue-content-survives-second-declare "
            "for the teaching reason)");
        return;
    }

    // Step 2 — Get the canary and cache the returned stringValue pointer.
    const kcdxDeclaredValue first_val =
        K.declare->Get(kCanaryName, K.self);
    if (!first_val.found || !first_val.isString ||
        first_val.stringValue == nullptr) {
        Report(kRowContent, false,
            "Get on the canary returned a miss / wrong shape — "
            "found=true & isString=true & stringValue non-null was "
            "expected; lifetime rows cannot proceed without a pointer to "
            "cache");
        Report(kRowAddress, false,
            "Get on the canary returned a miss / wrong shape (see "
            "CAP-62-stringvalue-content-survives-second-declare for the "
            "teaching reason)");
        return;
    }
    const char* const cached_first_ptr = first_val.stringValue;

    // Step 3 — Declare a SECOND, distinct triple. This is the "subsequent
    // Declare from any plugin" the contract promises to survive.
    const kcdxDeclareEntry second_entries[] = {
        { /*versionKey=*/   "*",
          /*patternStr=*/   nullptr,
          /*signatureStr=*/ nullptr,
          /*kindTag=*/      nullptr,
          /*valueInt=*/     0,
          /*valueStr=*/     kSecondPayload,
          /*valueIsString=*/true },
    };
    if (!K.declare->Declare("WHGame.dll", kSecondName,
                            second_entries, 1, K.self)) {
        Report(kRowContent, false,
            "K.declare->Declare on the SECOND string-value entry returned "
            "false — see DECLARED_TARGET / DECLARED_TARGET_BIND in the "
            "engine log; lifetime rows need two distinct Declares to "
            "exercise the contract");
        Report(kRowAddress, false,
            "K.declare->Declare on the SECOND string-value entry returned "
            "false (see CAP-62-stringvalue-content-survives-second-declare "
            "for the teaching reason)");
        return;
    }

    // Row 2 — CONTRACT: cached stringValue content equals the declared
    // canary payload, after the second Declare landed.
    {
        char reason[500];
        const bool pass =
            std::strcmp(cached_first_ptr, kCanaryPayload) == 0;
        snprintf(reason, sizeof(reason),
            "%s — cached stringValue reads back as \"%s\" (expected \"%s\") "
            "after a second distinct Declare landed. FALSIFIABLE: FAILS "
            "if a cached stringValue's content is not the value the author "
            "Declared, after a subsequent Declare from any plugin lands",
            pass ? "stringValue content survives a subsequent Declare"
                 : "stringValue content VIOLATED after subsequent Declare",
            cached_first_ptr,
            kCanaryPayload);
        Report(kRowContent, pass, reason);
    }

    // Row 3 — MECHANISM: re-Get the same canary name; returned
    // stringValue pointer is identity-equal to the cached pointer.
    {
        const kcdxDeclaredValue re_got =
            K.declare->Get(kCanaryName, K.self);
        char reason[500];
        const bool got_ok = re_got.found && re_got.isString &&
                            re_got.stringValue != nullptr;
        const bool pass   = got_ok && re_got.stringValue == cached_first_ptr;
        snprintf(reason, sizeof(reason),
            "%s — cached_ptr=%p re_got_ptr=%p (found=%d, isString=%d). "
            "FALSIFIABLE: FAILS if a cached stringValue pointer is not "
            "address-identical to the same name re-Got after a subsequent "
            "Declare lands — the mechanism check that holds regardless of "
            "whether content happens to survive via SSO + move on a "
            "particular STL implementation",
            pass ? "stringValue pointer ADDRESS survives a subsequent Declare"
                 : "stringValue pointer ADDRESS changed across subsequent Declare",
            cached_first_ptr,
            got_ok ? re_got.stringValue : nullptr,
            re_got.found ? 1 : 0,
            re_got.isString ? 1 : 0);
        Report(kRowAddress, pass, reason);
    }
}

// === Row 4 — Get on a PATTERN entry returns a miss ======================
//
// The accessor contract: PATTERN entries are consumed by the hook / bytes
// verbs by name, not by Get. The Get path's miss-on-pattern keeps the
// value-read surface clean (no leakage of an uninitialised intValue /
// stringValue for a name that has no value payload).

void RunPatternMissRow() {
    const kcdxDeclareEntry entries[] = {
        { /*versionKey=*/   "*",
          /*patternStr=*/   "DE AD BE EF DE AD BE EF",
          /*signatureStr=*/ "void ()",
          /*kindTag=*/      nullptr,    // default "function"
          /*valueInt=*/     0,
          /*valueStr=*/     nullptr,
          /*valueIsString=*/false },
    };
    if (!K.declare->Declare("WHGame.dll", "cap62_pattern_entry",
                            entries, 1, K.self)) {
        Report(kRowPatternMiss, false,
            "K.declare->Declare on the pattern entry returned false — "
            "see DECLARED_TARGET / DECLARED_TARGET_BIND in the engine "
            "log for the teaching reason");
        return;
    }

    const kcdxDeclaredValue v = K.declare->Get("cap62_pattern_entry", K.self);
    char reason[400];
    const bool pass = !v.found;
    snprintf(reason, sizeof(reason),
        "%s — found=%d (expected 0/false: the value accessor returns a "
        "miss for pattern entries; pattern declarations resolve through "
        "the hook / bytes verbs by name, NOT through this accessor). "
        "isString=%d, intValue=0x%llx, stringValue=%s",
        pass ? "Get on a pattern entry returned the expected miss"
             : "Get on a pattern entry MISCLASSIFIED as a value hit",
        v.found ? 1 : 0,
        v.isString ? 1 : 0,
        (unsigned long long)v.intValue,
        v.stringValue ? v.stringValue : "<null>");
    Report(kRowPatternMiss, pass, reason);
}

}  // namespace

// === kcdxPlugin_Load ====================================================

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!K.Init(api, kAuthor, kName)) {
        // K.Init logs why (it requires Hook; Declare is best-effort below).
        // Report every row FAIL so none sit silent-PENDING.
        if (api) {
            kcdxPluginHandle self = api->GetPluginHandle(kName);
            api->ReportTestResult(self, kRowRoundtrip,   0,
                "Kcdx::Init returned false at Plugin_Load (engine version mismatch?)");
            api->ReportTestResult(self, kRowContent,     0,
                "Kcdx::Init returned false at Plugin_Load");
            api->ReportTestResult(self, kRowAddress,     0,
                "Kcdx::Init returned false at Plugin_Load");
            api->ReportTestResult(self, kRowPatternMiss, 0,
                "Kcdx::Init returned false at Plugin_Load");
        }
        return true;
    }
    K.log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)",
               api->kcdxVersion);

    if (!K.declare) {
        K.log.Error("INIT",
            "QueryInterface(Declare, v%u) returned null — every row FAILs",
            kcdxDeclareInterface_Version);
        Report(kRowRoundtrip,   false,
            "K.declare is null: QueryInterface(kcdxInterface_Declare) "
            "returned null at Plugin_Load (engine version mismatch?)");
        Report(kRowContent,     false, "K.declare is null at Plugin_Load");
        Report(kRowAddress,     false, "K.declare is null at Plugin_Load");
        Report(kRowPatternMiss, false, "K.declare is null at Plugin_Load");
        return true;
    }

    RunRoundtripRow();
    RunLifetimeRows();
    RunPatternMissRow();
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
