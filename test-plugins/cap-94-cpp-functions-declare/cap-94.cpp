// CAP-94 — C++ peer of the function-reference + dll-declare surface.
//
// Exercises kcdxFunctionsInterface (the by-value kcdxFunctionRef mints) +
// kcdxDllInterface::Declare (declare your own DLL's functions) from the C++
// surface, through the Kcdx.h wrapper's K.functions / K.dll accessors — the
// C++ parity mirror of the Lua kcdx.functions.* + kcdx.dll.declare surface
// (cap-88). Proves ONE STORE, both languages: a function declared via the C++
// Declare resolves through PluginByName exactly as the Lua kcdx.dll.declare
// path does.
//
// Three falsifiable rows:
//
//   1. CAP-94-declare-resolve
//      K.dll->Declare("<author>.<plugin>", { { "CanSwapInCombat",
//      "bool (ptr self)" } }, 1) returns true, then
//      K.functions->PluginByName("<author>.<plugin>", "CanSwapInCombat")
//      returns a ref with found==true, signature == the author-declared
//      "bool (ptr self)", and isGame==false. FALSIFIABLE: found is false, the
//      signature is lost/wrong, or isGame is true -> FAIL.
//
//   2. CAP-94-game-resolve
//      K.functions->GameByName("WHGame", "SaveGame") AND GameById(144)
//      (SaveGame's stable id) both return found==true + isGame==true, with
//      hasAddress==true + a non-zero address. DEGRADED PASS if the reference
//      DB is not loaded in this environment (reason db_not_loaded — a
//      pre-deploy state), mirroring the cap-88 degraded posture. FALSIFIABLE:
//      a known game function does NOT resolve (found==false reason
//      name_unknown — a real rename/renumber regression, NOT degraded),
//      isGame is false, or a non-deploy-state miss -> FAIL.
//
//   3. CAP-94-miss-reason
//      K.functions->PluginByName("nope.nope", "DoesNotExist") returns
//      found==false AND a non-empty reason token (a teaching token, never a
//      silent empty). FALSIFIABLE: a miss returns found==true or an empty
//      reason -> FAIL.
//
// SaveGame is referenced BY NAME (no hardcoded address) and by its stable id
// 144 (a curated entity in the contiguous 1-157 id scheme — a stable handle,
// not a per-version RVA). The rows resolve from the InputLoaded readiness gate
// (the reference DB is open by then, the same gate cap-72 uses for its CVar
// reads); the declare itself is DB-independent and runs at the same point.
//
// All test_suite_only — production users never see this plugin. snprintf for
// every report string (a bounded format, never wsprintfA).

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "kcdx/Interfaces.h"
#include "kcdx/Kcdx.h"

namespace {

// Manifest identity — MUST match [plugin].author + [plugin].name in kcdx.toml.
constexpr const char* kAuthor = "ts";
constexpr const char* kName   = "cap_94_cpp_functions_declare";

// The plugin namespace under which this plugin declares its own functions:
// <author>.<plugin> (the form the Lua kcdx.dll.declare / the C++ Declare both
// take). A function declared here resolves via PluginByName(kNs, ...).
constexpr const char* kNs = "ts.cap_94_cpp_functions_declare";

constexpr const char* kRowDeclareResolve = "CAP-94-declare-resolve";
constexpr const char* kRowGameResolve    = "CAP-94-game-resolve";
constexpr const char* kRowMissReason     = "CAP-94-miss-reason";

// The function this plugin declares (an arbitrary author-owned name + an ABI
// from "its source") and the exact signature it must round-trip.
constexpr const char* kDeclName = "CanSwapInCombat";
constexpr const char* kDeclSig  = "bool (ptr self)";

// A known curated game function (resolved by name AND by stable id). SaveGame's
// stable id in the contiguous 1-157 curated scheme (a version-stable handle).
constexpr const char* kGameStem = "WHGame";
constexpr const char* kGameName = "SaveGame";
constexpr unsigned long long kGameId = 144ull;  // SaveGame's stable id.

Kcdx              g_K;
std::atomic<bool> g_reported{false};

void Report(const char* row, bool pass, const char* reason) {
    if (pass) g_K.log.Info ("CAP94", "PASS %s: %s", row, reason);
    else      g_K.log.Error("CAP94", "FAIL %s: %s", row, reason);
    g_K.api->ReportTestResult(g_K.self, row, pass ? 1 : 0, reason);
}

// FAIL all three rows with one reason — used when the whole functions/dll
// surface is unavailable (a version mismatch), so no row sits silent-PENDING.
void FailAll(const char* reason) {
    Report(kRowDeclareResolve, false, reason);
    Report(kRowGameResolve,    false, reason);
    Report(kRowMissReason,     false, reason);
}

// === Row 1 — Declare then resolve the declared plugin function ===========
void RunDeclareResolveRow() {
    char reason[600];

    const kcdxDeclaredFn entries[] = { { kDeclName, kDeclSig } };
    const bool ok = g_K.dll->Declare(kNs, entries, 1);
    if (!ok) {
        snprintf(reason, sizeof(reason),
            "K.dll->Declare(\"%s\", { { \"%s\", \"%s\" } }, 1) returned false — "
            "a well-formed single-entry declaration must be accepted (the "
            "teaching reason for any reject is in the dev log, category "
            "DLL_DECLARE)",
            kNs, kDeclName, kDeclSig);
        Report(kRowDeclareResolve, false, reason);
        return;
    }

    const kcdxFunctionRef ref = g_K.functions->PluginByName(kNs, kDeclName);
    if (!ref.found) {
        snprintf(reason, sizeof(reason),
            "K.functions->PluginByName(\"%s\", \"%s\") after Declare reported "
            "found=false (reason \"%s\") — a function declared through the C++ "
            "Declare must resolve through PluginByName (ONE store, both "
            "surfaces); a not_declared here means the C++ Declare did not write "
            "the SAME store the resolution reads",
            kNs, kDeclName, ref.reason ? ref.reason : "(null)");
        Report(kRowDeclareResolve, false, reason);
        return;
    }
    if (ref.isGame) {
        snprintf(reason, sizeof(reason),
            "K.functions->PluginByName(\"%s\", \"%s\") resolved but reported "
            "isGame=true — a plugin-declared function is NOT a game function "
            "(the dotted <author>.<plugin> stem must classify as plugin, not "
            "game)",
            kNs, kDeclName);
        Report(kRowDeclareResolve, false, reason);
        return;
    }
    const char* sig = ref.signature ? ref.signature : "";
    if (std::strcmp(sig, kDeclSig) != 0) {
        snprintf(reason, sizeof(reason),
            "K.functions->PluginByName(\"%s\", \"%s\") resolved but its "
            "signature is \"%s\", NOT the author-declared \"%s\" — the "
            "reference LOST or mangled the declared ABI (the one irreducible "
            "thing a callback hook needs)",
            kNs, kDeclName, sig, kDeclSig);
        Report(kRowDeclareResolve, false, reason);
        return;
    }
    snprintf(reason, sizeof(reason),
        "K.dll->Declare(\"%s\", { \"%s\" = \"%s\" }) then "
        "K.functions->PluginByName(\"%s\", \"%s\"):  found=true, isGame=false, "
        "signature=\"%s\" — the C++ Declare wrote the SAME store the resolution "
        "reads (one store, both surfaces); the C++ peer of cap-88's "
        "declare-populates-namespace",
        kNs, kDeclName, kDeclSig, kNs, kDeclName, sig);
    Report(kRowDeclareResolve, true, reason);
}

// === Row 2 — resolve a curated game function by name AND by id ===========
void RunGameResolveRow() {
    char reason[700];

    const kcdxFunctionRef byName = g_K.functions->GameByName(kGameStem, kGameName);
    const kcdxFunctionRef byId   = g_K.functions->GameById(kGameId);

    // DEGRADED PASS: the reference DB is not loaded in this environment (a
    // pre-deploy state). reason db_not_loaded is the deploy-state miss — the
    // namespace + resolution wiring is proven, only the address check is
    // skipped. Any OTHER miss (name_unknown) is a real FAIL (a rename/renumber
    // regression), NOT degraded.
    const bool nameDbMiss =
        !byName.found && byName.reason && std::strcmp(byName.reason, "db_not_loaded") == 0;
    const bool idDbMiss =
        !byId.found && byId.reason && std::strcmp(byId.reason, "db_not_loaded") == 0;
    if (nameDbMiss || idDbMiss) {
        snprintf(reason, sizeof(reason),
            "DEGRADED PASS — the reference DB is not loaded (GameByName(\"%s\", "
            "\"%s\") / GameById(%llu) reason=db_not_loaded, a pre-deploy "
            "state). The K.functions mint + resolution wiring is proven; the "
            "address check is skipped (mirrors the cap-88 degraded posture; "
            "name_unknown would be a real FAIL, db_not_loaded is environmental)",
            kGameStem, kGameName, kGameId);
        Report(kRowGameResolve, true, reason);
        return;
    }

    // By-name must resolve to a game reference with an address.
    if (!byName.found) {
        snprintf(reason, sizeof(reason),
            "K.functions->GameByName(\"%s\", \"%s\") reported found=false "
            "(reason \"%s\") — a known curated game function must resolve by "
            "name; a name_unknown is a real rename/renumber regression, NOT a "
            "degraded deploy-state miss",
            kGameStem, kGameName, byName.reason ? byName.reason : "(null)");
        Report(kRowGameResolve, false, reason);
        return;
    }
    if (!byName.isGame) {
        snprintf(reason, sizeof(reason),
            "K.functions->GameByName(\"%s\", \"%s\") resolved but isGame=false "
            "— a dot-free game stem must classify as a game reference",
            kGameStem, kGameName);
        Report(kRowGameResolve, false, reason);
        return;
    }
    if (!byName.hasAddress || byName.address == nullptr) {
        snprintf(reason, sizeof(reason),
            "K.functions->GameByName(\"%s\", \"%s\") resolved (found, isGame) "
            "but hasAddress=%s / address=%p — a curated game function present "
            "in the loaded DB must resolve to a real non-zero address",
            kGameStem, kGameName, byName.hasAddress ? "true" : "false",
            byName.address);
        Report(kRowGameResolve, false, reason);
        return;
    }

    // By-id must resolve the SAME function (game, with an address).
    if (!byId.found || !byId.isGame || !byId.hasAddress || byId.address == nullptr) {
        snprintf(reason, sizeof(reason),
            "K.functions->GameById(%llu) (SaveGame's stable id) reported "
            "found=%s isGame=%s hasAddress=%s address=%p — the by-id accessor "
            "must resolve the SAME curated game function (found, isGame, a "
            "non-zero address) the by-name path resolved",
            kGameId, byId.found ? "true" : "false",
            byId.isGame ? "true" : "false", byId.hasAddress ? "true" : "false",
            byId.address);
        Report(kRowGameResolve, false, reason);
        return;
    }

    snprintf(reason, sizeof(reason),
        "K.functions->GameByName(\"%s\", \"%s\") -> found, isGame, address=%p; "
        "GameById(%llu) -> found, isGame, address=%p — a known game function "
        "resolves by NAME and by STABLE ID to a non-zero address (the C++ peer "
        "of cap-88's game-fn-resolves + by-id-resolves)",
        kGameStem, kGameName, byName.address, kGameId, byId.address);
    Report(kRowGameResolve, true, reason);
}

// === Row 3 — a deliberately-unknown plugin name misses with a reason =====
void RunMissReasonRow() {
    char reason[600];
    const kcdxFunctionRef ref = g_K.functions->PluginByName("nope.nope",
                                                            "DoesNotExist");
    if (ref.found) {
        snprintf(reason, sizeof(reason),
            "K.functions->PluginByName(\"nope.nope\", \"DoesNotExist\") "
            "reported found=true for a name NEVER declared — an unknown plugin "
            "function must miss, never resolve");
        Report(kRowMissReason, false, reason);
        return;
    }
    const char* tok = ref.reason ? ref.reason : "";
    if (tok[0] == '\0') {
        snprintf(reason, sizeof(reason),
            "K.functions->PluginByName(\"nope.nope\", \"DoesNotExist\") "
            "reported found=false but an EMPTY reason — a miss must carry a "
            "teaching token (not_declared), never a silent empty");
        Report(kRowMissReason, false, reason);
        return;
    }
    snprintf(reason, sizeof(reason),
        "K.functions->PluginByName(\"nope.nope\", \"DoesNotExist\") -> "
        "found=false, reason=\"%s\" — an unknown plugin function misses loud "
        "with a teaching token, never a silent empty (the C++ peer of the "
        "fail-loud reference contract)",
        tok);
    Report(kRowMissReason, true, reason);
}

void RunAllRows() {
    if (g_reported.exchange(true)) return;  // run once.
    RunDeclareResolveRow();
    RunGameResolveRow();
    RunMissReasonRow();
}

// InputLoaded readiness gate — the reference DB is open by here (the same gate
// cap-72's CVar reads use). The declare + the plugin/miss resolves are
// DB-independent but run from the same point so the whole row set fires once.
void OnMessage(kcdxMessage* msg) {
    if (msg && msg->messageType == kcdxMessage_InputLoaded) {
        RunAllRows();
    }
}

}  // namespace

// === kcdxPlugin_Load ====================================================

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!g_K.Init(api, kAuthor, kName)) {
        // Init logs why (it requires Hook; functions/dll are best-effort below).
        // Report every row FAIL so none sit silent-PENDING.
        if (api) {
            kcdxPluginHandle self = api->GetPluginHandle(kName);
            const char* rows[] = {
                kRowDeclareResolve, kRowGameResolve, kRowMissReason,
            };
            for (const char* row : rows) {
                api->ReportTestResult(self, row, 0,
                    "Kcdx::Init returned false at Plugin_Load (engine version "
                    "mismatch?)");
            }
        }
        return true;
    }
    g_K.log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)",
                 api->kcdxVersion);

    if (!g_K.functions || !g_K.dll) {
        FailAll("K.functions and/or K.dll is null: "
                "QueryInterface(kcdxInterface_Functions / _Dll) returned null "
                "at Plugin_Load (engine version mismatch — rebuild against the "
                "engine that ships these interfaces)");
        return true;
    }

    if (!g_K.messaging) {
        // No readiness gate available — run the rows now. The declare + plugin
        // resolves are DB-independent; the game-resolve row degrades cleanly if
        // the DB is not yet open (reason db_not_loaded).
        g_K.log.Warn("INIT",
            "messaging interface null — running rows at load (game-resolve "
            "degrades to db_not_loaded if the reference DB is not yet open)");
        RunAllRows();
        return true;
    }
    g_K.messaging->RegisterListener(g_K.self, nullptr, OnMessage);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
