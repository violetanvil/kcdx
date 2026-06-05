// CAP-76 — kcdxAssetInterface (C++ peer of kcdx.assets.*) end-to-end parity.
//
// The C++ mirror of the Lua cap-75 surface, exercised through the Kcdx.h
// wrapper's K.assets accessor. Seven rows, all self-reported from
// kcdxPlugin_Load (the runtime asset stores are reachable at this init phase —
// the K.assets->... path is the same engine-side resolution + the same runtime
// stores the Lua binder reaches synchronously, no deferred-apply round-trip):
//
//   1. CAP-76-own
//      GetByPath(self, "icons/marker.txt") (THIS plugin's own asset, no owner
//      prefix) returns a non-null loadable path ending with the requested
//      relative path. FALSIFIABLE: nullptr / empty / a path not ending with
//      the relative path -> FAIL.
//
//   2. CAP-76-missing
//      GetByPath(self, "icons/does_not_exist.dds") (NOT under assets/) returns
//      nullptr — the loud failure (the teaching error is in the dev log).
//      FALSIFIABLE: a non-null return for a missing asset -> FAIL.
//
//   3. CAP-76-declare
//      Declare(self, "shirt", "icons/marker.txt") returns the file's loadable
//      path AND GetByName(self, "shirt") resolves the SAME path back — a
//      published-name store round-trip. FALSIFIABLE: Declare nullptr for a
//      real file / GetByName nullptr after declaring / a path not the declared
//      file -> FAIL.
//
//   4. CAP-76-getbyname-missing
//      GetByName(self, "never_declared_name") returns nullptr (teaching error
//      in the dev log). FALSIFIABLE: a non-null return for an undeclared name
//      -> FAIL.
//
//   5. CAP-76-register
//      Register(self, vpath, "icons/marker.txt") returns the loadable path
//      (overlay written); Register(self, vpath2, missing file) returns nullptr.
//      FALSIFIABLE: nullptr for a real file OR a non-null for a missing file
//      -> FAIL.
//
//   6. CAP-76-replace
//      Replace(self, vanilla-vpath, "icons/marker.txt") returns the loadable
//      path (overlay keyed by target); Replace(self, target2, missing file)
//      returns nullptr. FALSIFIABLE: same shape as register.
//
//   7. CAP-76-replace-crossmod-unresolvable
//      Replace(self, "redmoon.outfit.belt", "icons/marker.txt") — a PACKED
//      cross-mod target whose owner (redmoon.outfit) is NOT loaded -> nullptr
//      (teaching error naming the packed target in the dev log). `with` is a
//      REAL file so the only reason for nullptr is the unresolvable packed
//      target. FALSIFIABLE: a non-null return -> FAIL.
//
// Each row is a PARITY assertion: the C++ const char* return equals the result
// the Lua peer (cap-75) produces. The in-game SERVE of a registered/replaced
// vpath is the SAME Phase-11-deferred gap as the Lua side (KI-0005) — NOT a row
// here.
//
// All test_suite_only — production users never see this plugin.

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "kcdx/Interfaces.h"
#include "kcdx/Kcdx.h"

namespace {

// Manifest identity — MUST match [plugin].author + [plugin].name in kcdx.toml.
const char* kAuthor = "ts";
const char* kName   = "cap_76_cpp_asset_interface";

// Row IDs (must match [plugin].test_names in kcdx.toml).
const char* kRowOwn            = "CAP-76-own";
const char* kRowMissing        = "CAP-76-missing";
const char* kRowDeclare        = "CAP-76-declare";
const char* kRowGetByNameMiss  = "CAP-76-getbyname-missing";
const char* kRowRegister       = "CAP-76-register";
const char* kRowReplace        = "CAP-76-replace";
const char* kRowCrossModUnres  = "CAP-76-replace-crossmod-unresolvable";

// A real file under THIS plugin's assets/, and a path NOT present.
const char* kOwnAsset     = "icons/marker.txt";
const char* kMissingAsset = "icons/does_not_exist.dds";

Kcdx K;

void Report(const char* row, bool pass, const char* reason) {
    if (pass) K.log.Info ("CAP76", "PASS %s: %s", row, reason);
    else      K.log.Error("CAP76", "FAIL %s: %s", row, reason);
    K.api->ReportTestResult(K.self, row, pass ? 1 : 0, reason);
}

// Does `path` end with `rel` (forward-slash) OR its backslash form? A loadable
// disk path uses native separators; compare both tails so the rows are
// OS-separator-agnostic — the C++ mirror of the Lua resolves_to helper.
bool ResolvesTo(const char* path, const char* rel) {
    if (!path || !rel) return false;
    const size_t pl = std::strlen(path);
    const size_t rl = std::strlen(rel);
    if (rl == 0 || pl < rl) return false;
    const char* tail = path + (pl - rl);
    // Compare tail to rel, treating '/' and '\\' as equal at each position.
    for (size_t i = 0; i < rl; ++i) {
        char a = tail[i];
        char b = rel[i];
        bool aSep = (a == '/' || a == '\\');
        bool bSep = (b == '/' || b == '\\');
        if (aSep && bSep) continue;
        if (a != b) return false;
    }
    return true;
}

// === Row 1 — GetByPath resolves the caller's own asset ===================
void RunOwnRow() {
    const char* path = K.assets->GetByPath(K.self, kOwnAsset);
    char reason[500];
    if (!path || !path[0]) {
        snprintf(reason, sizeof(reason),
            "K.assets->GetByPath(self, \"%s\") returned %s — a real asset under "
            "THIS plugin's assets/ must resolve to a non-null loadable path, "
            "never nullptr (the teaching error would be in ASSET_GET)",
            kOwnAsset, path ? "an empty string" : "nullptr");
        Report(kRowOwn, false, reason);
        return;
    }
    if (!ResolvesTo(path, kOwnAsset)) {
        snprintf(reason, sizeof(reason),
            "K.assets->GetByPath(self, \"%s\") returned \"%s\" — non-null, but "
            "it does NOT end with the requested relative path (the resolver "
            "joined the wrong file or root)",
            kOwnAsset, path);
        Report(kRowOwn, false, reason);
        return;
    }
    snprintf(reason, sizeof(reason),
        "K.assets->GetByPath(self, \"%s\") resolved THIS plugin's own asset to "
        "the loadable path \"%s\" (no owner prefix — the engine resolved the "
        "calling plugin from self), ending with the requested relative path — "
        "the C++ peer of CAP-75-own",
        kOwnAsset, path);
    Report(kRowOwn, true, reason);
}

// === Row 2 — GetByPath on a missing asset returns nullptr ================
void RunMissingRow() {
    const char* path = K.assets->GetByPath(K.self, kMissingAsset);
    char reason[500];
    const bool pass = (path == nullptr);
    snprintf(reason, sizeof(reason),
        "%s — K.assets->GetByPath(self, \"%s\") (NOT under assets/) returned "
        "%s. Option A: a missing asset is the LOUD failure (nullptr; the "
        "teaching error naming the path is in the dev log, category ASSET_GET) "
        "— never a non-null path for a typo. FALSIFIABLE: a non-null return "
        "would mean a missing asset \"resolved\". The C++ peer of CAP-75-missing",
        pass ? "GetByPath on a missing asset returned nullptr as expected"
             : "GetByPath on a missing asset MIS-RESOLVED to a non-null path",
        kMissingAsset, path ? path : "nullptr");
    Report(kRowMissing, pass, reason);
}

// === Row 3 — Declare + GetByName round-trip ==============================
void RunDeclareRow() {
    const char* kPubName = "shirt";
    const char* declared = K.assets->Declare(K.self, kPubName, kOwnAsset);
    const char* resolved = K.assets->GetByName(K.self, kPubName);
    char reason[600];

    if (!declared || !declared[0]) {
        snprintf(reason, sizeof(reason),
            "K.assets->Declare(self, \"%s\", \"%s\") returned %s — declaring a "
            "name for a REAL asset must publish it and return its loadable path, "
            "never nullptr (the teaching error would be in ASSET_GET/ASSET_RUNTIME)",
            kPubName, kOwnAsset, declared ? "an empty string" : "nullptr");
        Report(kRowDeclare, false, reason);
        return;
    }
    if (!resolved || !resolved[0]) {
        snprintf(reason, sizeof(reason),
            "K.assets->GetByName(self, \"%s\") returned %s AFTER Declare "
            "published it — the published-name store did not hold the write "
            "(GetByName must resolve a name Declare just set)",
            kPubName, resolved ? "an empty string" : "nullptr");
        Report(kRowDeclare, false, reason);
        return;
    }
    if (!ResolvesTo(resolved, kOwnAsset)) {
        snprintf(reason, sizeof(reason),
            "K.assets->GetByName(self, \"%s\") resolved to \"%s\" — a path, but "
            "NOT the file Declare named (%s). The published name points at the "
            "wrong file",
            kPubName, resolved, kOwnAsset);
        Report(kRowDeclare, false, reason);
        return;
    }
    snprintf(reason, sizeof(reason),
        "K.assets->Declare(self, \"%s\", \"%s\") published the name (returned "
        "\"%s\") and GetByName(self, \"%s\") resolved it back to the same file's "
        "loadable path (\"%s\") — a store round-trip, own form (no owner prefix). "
        "The C++ peer of CAP-75-declare",
        kPubName, kOwnAsset, declared, kPubName, resolved);
    Report(kRowDeclare, true, reason);
}

// === Row 4 — GetByName on an undeclared name returns nullptr =============
void RunGetByNameMissRow() {
    const char* kNever = "never_declared_name";
    const char* path = K.assets->GetByName(K.self, kNever);
    char reason[500];
    const bool pass = (path == nullptr);
    snprintf(reason, sizeof(reason),
        "%s — K.assets->GetByName(self, \"%s\") returned %s. A name never "
        "declared is the LOUD failure (nullptr; the teaching error naming it is "
        "in the dev log, ASSET_GET) — never a non-null path. FALSIFIABLE: a "
        "non-null return would mean an undeclared name \"resolved\". The C++ "
        "peer of CAP-75-getbyname-missing",
        pass ? "GetByName on an undeclared name returned nullptr as expected"
             : "GetByName on an undeclared name MIS-RESOLVED to a non-null path",
        kNever, path ? path : "nullptr");
    Report(kRowGetByNameMiss, pass, reason);
}

// === Row 5 — Register (real file -> path; missing file -> nullptr) =======
void RunRegisterRow() {
    const char* kGoodVpath = "Data/cap76_runtime_gen.txt";
    const char* kBadVpath  = "Data/cap76_runtime_bad.txt";
    const char* good = K.assets->Register(K.self, kGoodVpath, kOwnAsset);
    const char* bad  = K.assets->Register(K.self, kBadVpath, kMissingAsset);
    char reason[600];

    if (!good || !good[0]) {
        snprintf(reason, sizeof(reason),
            "K.assets->Register(self, \"%s\", \"%s\") returned %s — registering "
            "a REAL file must write the overlay and return its loadable path, "
            "never nullptr",
            kGoodVpath, kOwnAsset, good ? "an empty string" : "nullptr");
        Report(kRowRegister, false, reason);
        return;
    }
    if (bad != nullptr) {
        snprintf(reason, sizeof(reason),
            "K.assets->Register(self, \"%s\", \"%s\") returned a non-null value "
            "(\"%s\") for a file NOT in assets/ — a missing source file is the "
            "LOUD failure (nullptr + ASSET_GET teach), never a broken overlay",
            kBadVpath, kMissingAsset, bad);
        Report(kRowRegister, false, reason);
        return;
    }
    snprintf(reason, sizeof(reason),
        "K.assets->Register(self, \"%s\", \"%s\") wrote the runtime overlay and "
        "returned the loadable path (\"%s\"); Register with a file NOT in "
        "assets/ returned nullptr (the loud failure; the teaching error naming "
        "the path is in ASSET_GET). The runtime-overlay store holds the write — "
        "the in-game SERVE is the Phase-11-deferred gap (KI-0005). The C++ peer "
        "of CAP-75-register",
        kGoodVpath, kOwnAsset, good);
    Report(kRowRegister, true, reason);
}

// === Row 6 — Replace (vanilla target real file -> path; missing -> null) =
void RunReplaceRow() {
    const char* kTarget    = "Data/cap76_replace_target.dds";
    const char* kBadTarget = "Data/cap76_replace_bad.dds";
    const char* good = K.assets->Replace(K.self, kTarget, kOwnAsset);
    const char* bad  = K.assets->Replace(K.self, kBadTarget, kMissingAsset);
    char reason[600];

    if (!good || !good[0]) {
        snprintf(reason, sizeof(reason),
            "K.assets->Replace(self, \"%s\", \"%s\") returned %s — replacing a "
            "target with a REAL file must write the runtime overlay keyed by the "
            "target and return its loadable path, never nullptr",
            kTarget, kOwnAsset, good ? "an empty string" : "nullptr");
        Report(kRowReplace, false, reason);
        return;
    }
    if (bad != nullptr) {
        snprintf(reason, sizeof(reason),
            "K.assets->Replace(self, \"%s\", \"%s\") returned a non-null value "
            "(\"%s\") for a file NOT in assets/ — a missing replacement file is "
            "the LOUD failure (nullptr + ASSET_GET teach)",
            kBadTarget, kMissingAsset, bad);
        Report(kRowReplace, false, reason);
        return;
    }
    snprintf(reason, sizeof(reason),
        "K.assets->Replace(self, \"%s\", \"%s\") wrote the runtime overlay keyed "
        "by the target and returned the loadable path (\"%s\"); Replace with a "
        "file NOT in assets/ returned nullptr (the loud failure). Take-effect is "
        "thereafter — the in-game SERVE is the Phase-11-deferred gap (KI-0005). "
        "The C++ peer of CAP-75-replace",
        kTarget, kOwnAsset, good);
    Report(kRowReplace, true, reason);
}

// === Row 7 — Replace with an unresolvable packed cross-mod target ========
void RunCrossModUnresolvableRow() {
    // A packed <author>.<plugin>.<bare> whose owner (redmoon.outfit) is NOT
    // loaded in this suite, so it resolves to no published asset. `with` is a
    // REAL file so the only reason for nullptr is the unresolvable packed target
    // — isolating the cross-mod-resolution-miss path from a missing-`with` path.
    const char* kPacked = "redmoon.outfit.belt";
    const char* ret = K.assets->Replace(K.self, kPacked, kOwnAsset);
    char reason[600];
    const bool pass = (ret == nullptr);
    snprintf(reason, sizeof(reason),
        "%s — K.assets->Replace(self, \"%s\", \"%s\") returned %s for a PACKED "
        "cross-mod target that resolves to NO published asset (redmoon.outfit "
        "is not loaded). An unresolvable cross-mod name is the LOUD failure "
        "(nullptr; the teaching error naming the packed target is in the dev "
        "log, ASSET_GET) — never a path keyed at a non-existent serve-vpath. "
        "FALSIFIABLE: a non-null return would mean the unresolvable name "
        "\"resolved\". The C++ peer of CAP-75-replace-crossmod-unresolvable-teach",
        pass ? "Replace on an unresolvable packed cross-mod target returned "
               "nullptr as expected"
             : "Replace on an unresolvable packed cross-mod target MIS-RESOLVED "
               "to a non-null path",
        kPacked, kOwnAsset, ret ? ret : "nullptr");
    Report(kRowCrossModUnres, pass, reason);
}

}  // namespace

// === kcdxPlugin_Load ====================================================

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!K.Init(api, kAuthor, kName)) {
        // K.Init logs why (it requires Hook; Assets is best-effort below).
        // Report every row FAIL so none sit silent-PENDING.
        if (api) {
            kcdxPluginHandle self = api->GetPluginHandle(kName);
            const char* rows[] = {
                kRowOwn, kRowMissing, kRowDeclare, kRowGetByNameMiss,
                kRowRegister, kRowReplace, kRowCrossModUnres,
            };
            for (const char* row : rows) {
                api->ReportTestResult(self, row, 0,
                    "Kcdx::Init returned false at Plugin_Load (engine version "
                    "mismatch?)");
            }
        }
        return true;
    }
    K.log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)",
               api->kcdxVersion);

    if (!K.assets) {
        K.log.Error("INIT",
            "QueryInterface(Assets, v%u) returned null — every row FAILs",
            kcdxAssetInterface_Version);
        const char* rows[] = {
            kRowOwn, kRowMissing, kRowDeclare, kRowGetByNameMiss,
            kRowRegister, kRowReplace, kRowCrossModUnres,
        };
        for (const char* row : rows) {
            Report(row, false,
                "K.assets is null: QueryInterface(kcdxInterface_Assets) "
                "returned null at Plugin_Load (engine version mismatch?)");
        }
        return true;
    }

    RunOwnRow();
    RunMissingRow();
    RunDeclareRow();
    RunGetByNameMissRow();
    RunRegisterRow();
    RunReplaceRow();
    RunCrossModUnresolvableRow();
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
