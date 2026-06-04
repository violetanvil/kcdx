// CAP-72 — C++ peer of CAP-71 (kcdx.cvar.* CVar read via the C++ surface).
//
// Exercises the CVar-read capability from the C++ surface — the v3
// kcdxConsoleInterface accessors that mirror the Lua kcdx.cvar.* surface
// (full-parity test mandate, the same capability from C++):
//   * CAP-72-callable — K.console->GetCVarInt("sys_pakPriority", &v) returns
//                       true and writes v (a plausible small mode 0..3).
//   * CAP-72-float    — K.console->GetCVarFloat("sys_pakPriority", &f) returns
//                       true and writes f (value-agnostic — read succeeded).
//   * CAP-72-bool     — K.console->GetCVarBool("sys_pakPriority", &b) returns
//                       true and writes b (= int != 0; value-agnostic).
//   * CAP-72-miss     — GetCVarInt("kcdx_nonexistent_cvar_xyz", &v) returns
//                       false and leaves v UNTOUCHED — the no-garbage-write
//                       contract (a miss is distinguishable from a real 0).
//
// The CVar surface is armed by kcdxMessage_InputLoaded, so every read fires
// from the InputLoaded listener (the same readiness gate cap-69 uses for its
// print rows). Every row is BOOT-ONLY (machine-checkable from the bool-return +
// out-param): there is NO perceptual overlay to eyeball (unlike cap-69's print
// rows). launch-to-menu confirms every row.
//
// GetCVarInt/Bool/Float are the v3 kcdxConsoleInterface methods (Kcdx::Init
// requests kcdxConsoleInterface_Version, which is 3). A plugin compiled against
// a header newer than the engine, or loaded by a v2 engine, gets a null
// K.console or a null accessor field — handled at report time as a FAIL
// "rebuild against v3", never a null deref.
//
// An InputLoaded-not-seen guard is implicit: if InputLoaded never fires the
// rows never report (they stay PENDING) — but InputLoaded is a guaranteed
// lifecycle message, so a PENDING row signals the listener never registered.

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

#include "kcdx/Interfaces.h"
#include "kcdx/Kcdx.h"

namespace {

constexpr const char* kName        = "cap_72_cpp_cvar_read";
constexpr const char* kRowCallable = "CAP-72-callable";
constexpr const char* kRowFloat    = "CAP-72-float";
constexpr const char* kRowBool     = "CAP-72-bool";
constexpr const char* kRowMiss     = "CAP-72-miss";

// Known-good read target: a CONFIRMED boot-present int CVar (verified live in
// the asset-system recon). Its value is a small pakPriority mode (kcdx's
// launcher sets 0 via user.cfg; default 2 otherwise) — assert the READ
// SUCCEEDED + the value is a plausible small mode (0..3), NOT a hardcoded int.
constexpr const char* kCVarInt   = "sys_pakPriority";
constexpr const char* kCVarBogus = "kcdx_nonexistent_cvar_xyz";  // deliberately absent

Kcdx              g_K;
std::atomic<bool> g_reported{false};

// FAIL all four rows with one reason — used when the whole CVar accessor surface
// is unavailable (v3 not resolved), so no row sits silently PENDING.
void FailAll(const char* reason) {
    g_K.api->ReportTestResult(g_K.self, kRowCallable, 0, reason);
    g_K.api->ReportTestResult(g_K.self, kRowFloat,    0, reason);
    g_K.api->ReportTestResult(g_K.self, kRowBool,     0, reason);
    g_K.api->ReportTestResult(g_K.self, kRowMiss,     0, reason);
}

void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;

    // One-shot: InputLoaded can fire more than once across a session; report
    // every row on the first fire only.
    bool expected = false;
    if (!g_reported.compare_exchange_strong(expected, true,
                                            std::memory_order_acq_rel)) {
        return;
    }

    if (g_K.api == nullptr || g_K.self == kcdxInvalidPluginHandle) return;

    // ---- v3 resolve guard ------------------------------------------------
    // GetCVarInt/Bool/Float are the v3 kcdxConsoleInterface accessors. If
    // K.console is null (QueryInterface(Console, v3) missed on an older engine)
    // or any accessor field is null, the surface did not resolve — FAIL every
    // row loud with the rebuild hint, never deref a null slot.
    if (g_K.console == nullptr || g_K.console->GetCVarInt == nullptr ||
        g_K.console->GetCVarFloat == nullptr || g_K.console->GetCVarBool == nullptr) {
        FailAll(
            "K.console or a GetCVar* accessor is null at InputLoaded — the v3 "
            "kcdxConsoleInterface CVar-read methods did not resolve. Rebuild "
            "the plugin against an engine whose kcdxConsoleInterface is "
            "version 3 (the version carrying GetCVarInt/Bool/Float).");
        return;
    }

    // ---- CAP-72-callable — GetCVarInt reads a CONFIRMED real int CVar. ----
    // true return + v written to a plausible small pakPriority mode (0..3) for
    // a CVar that DEMONSTRABLY exists. FALSIFIABLE: a false return (read failed
    // for a CVar that exists) or a value out of the plausible mode range → FAIL.
    {
        int v = -12345;  // sentinel: detect "wrote nothing but returned true"
        bool got = g_K.console->GetCVarInt(kCVarInt, &v);
        if (got && v >= 0 && v <= 3) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "K.console->GetCVarInt(\"%s\", &v) returned true and wrote v=%d "
                "at InputLoaded — a plausible small pakPriority mode (0..3) for "
                "a CVar that demonstrably exists. Cross-surface parity: the Lua "
                "CAP-71-callable reads the SAME name via kcdx.cvar.get_int — "
                "both call the same engine cvar:: core, so the two rows MUST "
                "report the same observed value (compare them).",
                kCVarInt, v);
            g_K.api->ReportTestResult(g_K.self, kRowCallable, 1, buf);
        } else {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "GetCVarInt(\"%s\", &v) read mismatch: returned %s, v=%d "
                "(want true + a value in 0..3 — the read of a CVar that exists "
                "must succeed and write a plausible small mode).",
                kCVarInt, got ? "true" : "false", v);
            g_K.api->ReportTestResult(g_K.self, kRowCallable, 0, buf);
        }
    }

    // ---- CAP-72-float — GetCVarFloat reads sys_pakPriority as a float. ----
    // Reads the SAME confirmed-present CVar via GetCVarFloat. Value-agnostic:
    // the float reading of a nominally-int CVar may be the int-as-float or 0.0
    // — assert the READ SUCCEEDED (returned true), NOT a specific value.
    // WHY this CVar, not an e_* float CVar: only sys_pakPriority is recon-
    // CONFIRMED boot-present (results-driven — read a confirmed CVar, never a
    // guessed name). GetCVarFloat on it exercises the GetFVal accessor path on
    // a CVar that demonstrably exists.
    {
        float f = -99999.0f;
        bool got = g_K.console->GetCVarFloat(kCVarInt, &f);
        if (got) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "K.console->GetCVarFloat(\"%s\", &f) returned true at "
                "InputLoaded — the GetFVal accessor path read a CVar that "
                "demonstrably exists (value-agnostic: the float reading of a "
                "nominally-int CVar may be int-as-float or 0.0; the row asserts "
                "the read SUCCEEDED, not a specific value).",
                kCVarInt);
            g_K.api->ReportTestResult(g_K.self, kRowFloat, 1, buf);
        } else {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "GetCVarFloat(\"%s\", &f) returned false at InputLoaded — the "
                "float read of a CVar that exists must succeed → FAIL.",
                kCVarInt);
            g_K.api->ReportTestResult(g_K.self, kRowFloat, 0, buf);
        }
    }

    // ---- CAP-72-bool — GetCVarBool reads sys_pakPriority as a bool. -------
    // GetCVarBool is (GetCVarInt != 0). Reads the SAME confirmed CVar; the read
    // must SUCCEED (return true). Value-agnostic: the bool is whatever
    // (int != 0) yields for the live pakPriority value.
    {
        bool b = false;
        bool got = g_K.console->GetCVarBool(kCVarInt, &b);
        if (got) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "K.console->GetCVarBool(\"%s\", &b) returned true (b=%s) at "
                "InputLoaded — GetCVarBool (= int != 0) read a CVar that "
                "demonstrably exists (value-agnostic: the bool reflects the "
                "live mode value).",
                kCVarInt, b ? "true" : "false");
            g_K.api->ReportTestResult(g_K.self, kRowBool, 1, buf);
        } else {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "GetCVarBool(\"%s\", &b) returned false at InputLoaded — the "
                "bool read of a CVar that exists must succeed → FAIL.",
                kCVarInt);
            g_K.api->ReportTestResult(g_K.self, kRowBool, 0, buf);
        }
    }

    // ---- CAP-72-miss — a bogus CVar name returns false, leaves out untouched.
    // The no-garbage-write / observable-miss contract: a CVar that does NOT
    // exist returns false AND leaves *out untouched (a miss is distinguishable
    // from a real value of 0). FALSIFIABLE: a true return for a bogus name (a
    // fabricated value), OR the out-param mutated despite the false return
    // (a garbage write) → FAIL.
    {
        const int kSentinel = 0x6BADF00D;
        int v = kSentinel;
        bool got = g_K.console->GetCVarInt(kCVarBogus, &v);
        if (!got && v == kSentinel) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "GetCVarInt(\"%s\", &v) returned false and left v UNTOUCHED "
                "(still the 0x%08X sentinel) at InputLoaded — a non-existent "
                "CVar reads as false with NO write (the no-garbage-write "
                "contract; a miss is distinguishable from a real 0), NEVER a "
                "fabricated value.",
                kCVarBogus, (unsigned)kSentinel);
            g_K.api->ReportTestResult(g_K.self, kRowMiss, 1, buf);
        } else {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "miss contract mismatch for bogus \"%s\": returned %s, v=0x%08X "
                "(want false + v UNTOUCHED at the 0x%08X sentinel — a "
                "non-existent CVar must return false and write nothing, never a "
                "fabricated value / a garbage write).",
                kCVarBogus, got ? "true" : "false", (unsigned)v,
                (unsigned)kSentinel);
            g_K.api->ReportTestResult(g_K.self, kRowMiss, 0, buf);
        }
    }
}

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    // Init fetches the shipped sub-interfaces (incl. K.console at v3) + the
    // plugin handle + the logger. Init returns false only when a REQUIRED
    // interface (Hook) is missing; Console is best-effort, so a null/older
    // K.console is handled at report time (the v3 resolve guard), not here.
    if (!g_K.Init(api, "ts", kName)) {
        // Hook missing — Init logged why. Report all rows FAIL so none sits
        // silently PENDING.
        kcdxPluginHandle self = api->GetPluginHandle(kName);
        const char* reason =
            "Kcdx::Init returned false at Plugin_Load (a required interface is "
            "missing — see the engine log). The CVar-read rows cannot run.";
        api->ReportTestResult(self, kRowCallable, 0, reason);
        api->ReportTestResult(self, kRowFloat,    0, reason);
        api->ReportTestResult(self, kRowBool,     0, reason);
        api->ReportTestResult(self, kRowMiss,     0, reason);
        return true;
    }
    g_K.log.Info("INIT", "kcdxPlugin_Load called");

    if (g_K.messaging) {
        g_K.messaging->RegisterListener(g_K.self, nullptr, OnMessage);
    } else {
        // No messaging interface = no InputLoaded readiness gate. Report FAIL
        // loud rather than sit PENDING forever.
        FailAll(
            "QueryInterface(Messaging) returned null at Plugin_Load — cannot "
            "gate the CVar reads on the InputLoaded console-ready message.");
        return true;
    }

    g_K.log.Info("INIT",
        "registered InputLoaded listener; CAP-72-callable/-float/-bool/-miss "
        "report from the first InputLoaded fire (CVar surface armed)");
    return true;
}
