// CAP-69 — C++ peer of CAP-68 (kcdx.console.print via the C++ surface).
//
// Exercises BOTH C++ floors of the console-print capability (full-parity test
// mandate — the same capability from the C++ surface, raw floor AND empowered
// wrapper, mirroring the 3-floor model the other cpp caps follow):
//   * CAP-69-cpp-raw     — the raw floor: K.console->Print(text) on the v2
//                          kcdxConsoleInterface method.
//   * CAP-69-cpp-wrapper — the empowered floor: kcdx::console::print(K, text)
//                          in Kcdx.h (null-guards K.console + ->Print, then
//                          forwards).
//
// The console surface is armed by kcdxMessage_InputLoaded, so both prints fire
// from the InputLoaded listener (the same readiness gate cap-64 uses for its
// backstop). Each row is `console`-mode: the auto-pass asserts the call
// RETURNED TRUE (the surface accepted the line); the printed marker line
// appearing on the `~` console overlay is the MANUAL observable the user
// eyeballs (overlay-paint is human-perceptual — no machine-readable signal).
//
// An InputLoaded-not-seen guard is implicit: if InputLoaded never fires the
// rows never report (they stay PENDING) — but InputLoaded is a guaranteed
// lifecycle message, so a PENDING row signals the listener never registered.

#include <windows.h>

#include <atomic>
#include <cstdint>

#include "kcdx/Interfaces.h"
#include "kcdx/Kcdx.h"

namespace {

constexpr const char* kName       = "cap_69_cpp_console_print";
constexpr const char* kRowRaw     = "CAP-69-cpp-raw";
constexpr const char* kRowWrapper = "CAP-69-cpp-wrapper";

Kcdx              g_K;
std::atomic<bool> g_reported{false};

void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;

    // One-shot: InputLoaded can fire more than once across a session; report
    // both rows on the first fire only.
    bool expected = false;
    if (!g_reported.compare_exchange_strong(expected, true,
                                            std::memory_order_acq_rel)) {
        return;
    }

    if (g_K.api == nullptr || g_K.self == kcdxInvalidPluginHandle) return;

    // ---- CAP-69-cpp-raw — the raw floor: K.console->Print(text). ----------
    // The v2 interface method must be resolved (K.console + ->Print non-null),
    // then a print of the marker line must return true (the surface accepted
    // it). FALSIFIABLE: a null interface/slot, or a print returning false
    // (surface not ready / print path unresolved on this build) → FAIL.
    if (g_K.console == nullptr || g_K.console->Print == nullptr) {
        g_K.api->ReportTestResult(g_K.self, kRowRaw, 0,
            "K.console or K.console->Print is null at InputLoaded — the v2 "
            "kcdxConsoleInterface Print method did not resolve. Rebuild the "
            "plugin against an engine whose kcdxConsoleInterface is version 2 "
            "(the version carrying Print).");
    } else {
        bool ok = g_K.console->Print("CAP69_CPP_RAW_OVERLAY_OK");
        if (ok) {
            g_K.api->ReportTestResult(g_K.self, kRowRaw, 1,
                "K.console->Print(\"CAP69_CPP_RAW_OVERLAY_OK\") returned true "
                "at InputLoaded — the raw v2 interface method accepted the "
                "line (auto-pass). MANUAL: open the ~ console and confirm the "
                "line CAP69_CPP_RAW_OVERLAY_OK is painted on the overlay (the "
                "perceptual observable, no machine signal).");
        } else {
            g_K.api->ReportTestResult(g_K.self, kRowRaw, 0,
                "K.console->Print returned false for the raw-floor overlay "
                "marker at InputLoaded — the surface refused the print, so the "
                "line will not paint. FAILS if the console surface is not "
                "ready or the print path is unavailable on this build.");
        }
    }

    // ---- CAP-69-cpp-wrapper — the empowered floor: kcdx::console::print. --
    // The Kcdx.h wrapper null-guards then forwards to K.console->Print.
    // FALSIFIABLE: a false return (missing interface/slot → the wrapper's
    // null-guard returns false, or the surface refused the print) → FAIL.
    {
        bool ok = kcdx::console::print(g_K, "CAP69_CPP_WRAPPER_OVERLAY_OK");
        if (ok) {
            g_K.api->ReportTestResult(g_K.self, kRowWrapper, 1,
                "kcdx::console::print(K, \"CAP69_CPP_WRAPPER_OVERLAY_OK\") "
                "returned true at InputLoaded — the empowered wrapper "
                "null-guarded and forwarded to K.console->Print, which "
                "accepted the line (auto-pass). MANUAL: open the ~ console and "
                "confirm the line CAP69_CPP_WRAPPER_OVERLAY_OK is painted on "
                "the overlay (the perceptual observable, no machine signal).");
        } else {
            g_K.api->ReportTestResult(g_K.self, kRowWrapper, 0,
                "kcdx::console::print(K, ...) returned false for the "
                "wrapper-floor overlay marker at InputLoaded — either the "
                "wrapper's null-guard tripped (K.console / ->Print null on an "
                "older engine) or the surface refused the print. FAILS if the "
                "wrapper does not reach a ready Print on this build.");
        }
    }
}

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    // Init fetches the shipped sub-interfaces (incl. K.console) + the plugin
    // handle + the logger. Init returns false only when a REQUIRED interface
    // (Hook) is missing; Console is best-effort, so a null K.console is handled
    // at report time, not here.
    if (!g_K.Init(api, "ts", kName)) {
        // Hook missing — Init logged why. Report both rows FAIL so neither
        // sits silently PENDING.
        api->ReportTestResult(api->GetPluginHandle(kName), kRowRaw, 0,
            "Kcdx::Init returned false at Plugin_Load (a required interface is "
            "missing — see the engine log). The console-print rows cannot run.");
        api->ReportTestResult(api->GetPluginHandle(kName), kRowWrapper, 0,
            "Kcdx::Init returned false at Plugin_Load (a required interface is "
            "missing — see the engine log). The console-print rows cannot run.");
        return true;
    }
    g_K.log.Info("INIT", "kcdxPlugin_Load called");

    if (g_K.messaging) {
        g_K.messaging->RegisterListener(g_K.self, nullptr, OnMessage);
    } else {
        // No messaging interface = no InputLoaded readiness gate. Report FAIL
        // loud rather than sit PENDING forever.
        api->ReportTestResult(g_K.self, kRowRaw, 0,
            "QueryInterface(Messaging) returned null at Plugin_Load — cannot "
            "gate the print on the InputLoaded console-ready message.");
        api->ReportTestResult(g_K.self, kRowWrapper, 0,
            "QueryInterface(Messaging) returned null at Plugin_Load — cannot "
            "gate the print on the InputLoaded console-ready message.");
        return true;
    }

    g_K.log.Info("INIT",
        "registered InputLoaded listener; CAP-69-cpp-raw + CAP-69-cpp-wrapper "
        "report from the first InputLoaded fire (console surface armed)");
    return true;
}
