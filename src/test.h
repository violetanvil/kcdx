// test — kcdx regression test suite aggregator.
//
// Test plugins (anything under <plugins>/ with [kcdx] test_suite_only = true)
// call ReportResult(name, pass, reason) to record their pass/fail. Plugins
// can call ReportResult any number of times; the last call wins.
//
// The aggregator emits a roll-up line to kcdx.log when EmitSummary is
// called — currently invoked from messaging::FireEngineMessage so that
// each lifecycle event (kPostLoad, kPostPostLoad, kInputLoaded, ...) gets
// its own summary as-of-that-message.
//
// All-quiet when dev mode is off: ReportResult is a no-op, EmitSummary
// emits nothing. Test plugins still self-gate via dev::IsEnabled() but
// this layer guards against accidental noise.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace kcdx::test {

// Record a test result. name = matrix row ID (e.g. "CAP-01"), pass =
// outcome, reason = freeform one-sentence explanation. Last call for a
// given name wins.
//
// Thread-safe. Cheap when dev mode is off (early-returns).
void ReportResult(std::string_view name, bool pass, std::string_view reason);

// Emit a "Test suite: X/Y passing as of <messageLabel>" line to kcdx.log,
// followed by FAIL lines for each failing test. No-op when dev mode is
// off or no tests have reported.
//
// Called from messaging::FireEngineMessage after the message dispatch
// completes. messageLabel is a human-readable name like "kPostLoad".
void EmitSummary(const char* messageLabel);

// Same as EmitSummary, but only emits if the report state has changed
// since the last emit (different test count OR different
// pass/fail counts). Use after task::DrainQueue and other places where
// async work might have reported between lifecycle messages — avoids
// spamming the same summary every tick while still catching late
// reports.
void EmitSummaryIfChanged(const char* messageLabel);

// Map a kcdxMessage_* uint to its display name. Used by the FireEngineMessage
// caller so test::EmitSummary gets a sensible label. Returns nullptr for
// unknown messages.
const char* MessageLabel(uint32_t messageType);

// Register a test name a plugin promises to report. The aggregator uses
// this to track PENDING (registered but no report yet) vs reported.
// Called from config.cpp during ParsePluginManifest for any plugin
// with [kcdx] test_suite_only = true + [plugin] test_names = [...].
//
// pluginName is the plugin's stable ID (for the PENDING log line so
// the user knows which plugin owns the silent test).
void RegisterExpectedTestName(std::string_view testName,
                              std::string_view pluginName);

// Increment the count of test_suite_only plugins skipped because dev
// mode is off. Reported once at the end of LoadAllConfigs as
// "Test suite: N plugin(s) gated off (dev mode disabled)" so even
// production users notice that test plugins exist.
void IncrementGatedOffCount();

// Emit the production-quiet count line if any test plugins were gated
// off this session. Called once from LoadAllConfigs after all TOMLs
// have been parsed. No-op when count == 0.
void EmitGatedOffSummary();

}  // namespace kcdx::test
