// CAP-117 — the loose-open mode sanitizer maps an engine mode the strict UCRT
// rejects to a valid one, preserving read/write/binary intent (KI-0026 fix).
//
// KI-0026's third onion layer (after the index/alias fix + the slot-46 FGetSize
// fix): the engine opens settings.xml with mode "rbx". The `x` flag is C11
// exclusive-create, valid ONLY on a write base; on a read base MSVC's strict
// static UCRT `__acrt_stdio_parse_mode` FAST-FAILS (_invalid_parameter →
// _invoke_watson, c0000409 — process killed, no SEH, "no bugsplat"). The engine's
// own lenient CRT tolerated it; kcdx's strict CRT does not. kcdx's
// OpenLooseAndMint forwarded the mode verbatim → crash.
//
// The fix: SanitizeLooseMode (src/fs_takeover/loose_mode.cpp) normalizes the
// engine's mode to a UCRT-valid one — dropping `x` on a read base (inert to a
// read), keeping it on a write base, keeping every other flag. This plugin
// compiles that engine TU into its own DLL (the cap-112/115/116 shape — pure +
// self-contained, no log stub needed) and asserts the mapping table directly.
//
// Assertions (each a fixed input → expected output + expected `changed` flag):
//   (a) "rbx" → "rb", changed=true   — THE KI-0026 offender. The `x` on a read
//       base is dropped. FALSIFIES the bug: if "rbx" passed through unchanged the
//       strict CRT fast-fails (the crash). This is the load-bearing row.
//   (b) "rb"  → "rb", changed=false  — a valid read mode is untouched (the common
//       path; no spurious normalization).
//   (c) "wbx" → "wbx", changed=false — `x` on a WRITE base is valid (exclusive-
//       create), kept. Proves the fix is scoped to the invalid combo, not a blanket
//       `x`-strip that would break a legitimate exclusive-create write.
//   (d) "rb+" → "rb+", changed=false (and "r+bx" → "r+bx") — a `+` makes the
//       stream writable, so `x` is valid; not dropped. Proves the writeBase test
//       accounts for `+`, not just the w/a base char.
//   (e) ""/null → "rb", changed=false — the default; never an empty mode to the CRT.
//
// (a)+(b)+(c)+(d)+(e) are the load-bearing FALSIFIABLE claim. A regression that
// stopped dropping `x` on read → (a) FAIL (and the live boot fast-fails again).
// One that dropped `x` on a write base too → (c) FAIL (breaks exclusive-create).
// One that ignored `+` → (d) FAIL. This is a DIRECT assertion on the mapping the
// loose-open path runs; the live boot (settings.xml opens, game proceeds) is the
// end-to-end proof, this row guards the pure function.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "kcdx/Interfaces.h"

#include "loose_mode.h"

namespace {

const char* kName = "cap_117_loose_mode_sanitize";
const char* kRow  = "cap-117-loose-mode-sanitize";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
kcdxLogger           g_log;

namespace fst = kcdx::fs_takeover;

void Report(bool pass, const char* reason) {
    if (pass) g_log.Info ("CAP117", "PASS %s: %s", kRow, reason);
    else      g_log.Error("CAP117", "FAIL %s: %s", kRow, reason);
    g_api->ReportTestResult(g_self, kRow, pass ? 1 : 0, reason);
}

// One mapping check: input mode → expected output + expected changed flag.
bool Check(const char* in, const char* wantOut, bool wantChanged,
           char* reason, size_t rn) {
    bool changed = false;
    const std::string got = fst::SanitizeLooseMode(in, changed);
    if (got != wantOut || changed != wantChanged) {
        std::snprintf(reason, rn,
            "SanitizeLooseMode(\"%s\") = \"%s\" changed=%s, expected \"%s\" "
            "changed=%s", in ? in : "(null)", got.c_str(),
            changed ? "true" : "false", wantOut, wantChanged ? "true" : "false");
        return false;
    }
    return true;
}

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    g_log  = kcdxLogger(api, g_self);
    g_log.Info("INIT", "kcdxPlugin_Load called (engine v0x%08X)", api->kcdxVersion);

    char reason[400];

    // (a) the KI-0026 offender — "rbx" → "rb", changed.
    if (!Check("rbx", "rb", true, reason, sizeof(reason))) { Report(false, reason); return true; }
    // (b) valid read mode untouched.
    if (!Check("rb", "rb", false, reason, sizeof(reason))) { Report(false, reason); return true; }
    // (c) `x` on a write base is valid — kept.
    if (!Check("wbx", "wbx", false, reason, sizeof(reason))) { Report(false, reason); return true; }
    // (d) `+` makes the stream writable — `x` valid, kept (both forms).
    if (!Check("rb+", "rb+", false, reason, sizeof(reason))) { Report(false, reason); return true; }
    if (!Check("r+bx", "r+bx", false, reason, sizeof(reason))) { Report(false, reason); return true; }
    // (e) null/empty → "rb" default.
    if (!Check(nullptr, "rb", false, reason, sizeof(reason))) { Report(false, reason); return true; }
    if (!Check("", "rb", false, reason, sizeof(reason))) { Report(false, reason); return true; }

    std::snprintf(reason, sizeof(reason),
        "loose-open mode sanitizer PASS — \"rbx\"→\"rb\" (the KI-0026 offender: "
        "`x` dropped on a read base, the strict-UCRT fast-fail removed), valid "
        "modes (\"rb\", \"rb+\") untouched, `x` kept on a write base (\"wbx\") and "
        "on a `+` read-write base (\"r+bx\"), null/empty→\"rb\". The engine's mode "
        "is translated to kcdx's strict CRT, never crashed on");
    Report(true, reason);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
