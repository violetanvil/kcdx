// kcdx_scan — engine-owned `~`-console command for in-game AOB iteration.
//
// An author iterating an AOB pattern types it into the in-game `~` console and
// sees the match count + addresses immediately, instead of the
// edit-build-launch-grep cycle:
//
//   kcdx_scan WHGame.dll "48 8B 88 ?? ?? ?? ?? 48"
//
// The hand-written `<pattern>` is the expert address-discovery form — this
// command IS the tool an expert uses to FIND a site they will then name via
// the authoring surface. It is not a per-hook hex burden; the everyday hook
// path resolves a name to address AND verified signature.
//
// This is a CONSOLE COMMAND (a `~`-console verb), NOT a kcdx.* Lua surface. It
// does not live in the kcdx.* namespace and has no Lua/C++ parity obligation —
// it IS the cross-surface in-game tool. The Lua kcdx.scan{...} diagnostic is a
// separate, unchanged surface.
//
// Output goes to the visible console OVERLAY via console::PrintLine (the author
// is typing in the console; results must appear there). scan_engine::RunScan
// also emits the concise `[scan '<name>']` lines to the dev log — this command
// does NOT duplicate those.

#include "console_commands_scan.h"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <string>

#include "console.h"        // console::GetInterface, console::PrintLine
#include "kcdx/Interfaces.h"  // kcdxConsoleCmdArgs, kcdxInvalidPluginHandle
#include "patch_engine.h"   // patch::ParsePattern
#include "scan_engine.h"    // scan_engine::ScanEntry / ScanResult / RunScan

namespace kcdx::console_commands_scan {

namespace {

// Cap the per-match overlay lines so a degenerate over-broad pattern can't
// flood the console. A normal iterate-to-unique scan has 0-3 matches, so this
// is a guard, never the common path; when it trips, the output says so.
constexpr size_t kMaxPrintedMatches = 16;

const char* kUsage =
    "kcdx_scan <module> \"<AOB pattern>\"  "
    "(e.g. kcdx_scan WHGame.dll \"48 8B 88 ?? ?? ?? ?? 48\")";

// kcdx_scan <module> "<pattern>"
//
// Argv contract (kcdxConsoleInterface, Interfaces.h): GetArgCount returns
// 1 + N; arg 0 is the command name ("kcdx_scan"), arg 1 is <module>, arg 2 is
// <pattern>. A missing module or pattern (argc < 3) is fail-loud: print the
// usage line to the overlay and return — never a silent no-op.
void Callback(const kcdxConsoleCmdArgs* args) {
    const kcdxConsoleInterface* iface = console::GetInterface();

    int argc = iface->GetArgCount(args);
    if (argc < 3) {
        console::PrintLine(kUsage);
        return;
    }

    const char* moduleArg  = iface->GetArg(args, 1);
    const char* patternArg = iface->GetArg(args, 2);
    if (!moduleArg || !*moduleArg || !patternArg || !*patternArg) {
        // GetArg returned null/empty where a value was expected — treat the
        // same as a missing argument (fail loud, never a silent no-op).
        console::PrintLine(kUsage);
        return;
    }

    scan_engine::ScanEntry entry;
    entry.name       = "kcdx_scan";  // fixed diagnostic name (the console
                                     // command has no author-supplied name).
    entry.module     = moduleArg;
    entry.sourceFile = "<console>";
    entry.offset     = 0;
    // context + anchor stay empty: the simple iterate-an-AOB path. No
    // context/anchor argv form in the console command.

    // Parse the pattern. ParsePattern throws on a bad string — a malformed
    // pattern prints a teaching error to the overlay and returns; it never
    // crashes the game.
    try {
        entry.pattern = kcdx::patch::ParsePattern(patternArg);
    } catch (const std::exception& e) {
        std::string msg = "[scan] parse error in pattern: ";
        msg += e.what();
        msg += " — pattern is an AOB byte string with optional ?? wildcards "
               "(e.g. \"48 8B 88 ?? ?? ?? ?? 48\").";
        console::PrintLine(msg.c_str());
        return;
    }

    // Resolve + emit the concise dev-log lines (RunScan owns that logging; do
    // NOT re-log here).
    scan_engine::ScanResult result = scan_engine::RunScan(entry);

    // Print the outcome to the overlay.
    if (!result.moduleLoaded) {
        std::string msg = "[scan] module '";
        msg += entry.module;
        msg += "' not loaded";
        console::PrintLine(msg.c_str());
        return;
    }

    if (result.patternMatches == 0) {
        console::PrintLine("[scan] 0 matches");
        return;
    }

    {
        char header[64];
        std::snprintf(header, sizeof(header), "[scan] %zu matches:",
                      result.patternMatches);
        console::PrintLine(header);
    }

    const size_t printed =
        result.matches.size() < kMaxPrintedMatches ? result.matches.size()
                                                    : kMaxPrintedMatches;
    for (size_t i = 0; i < printed; ++i) {
        const scan_engine::ScanMatch& m = result.matches[i];
        char line[128];
        std::snprintf(line, sizeof(line), "  %s+0x%llX", m.module.c_str(),
                      static_cast<unsigned long long>(m.relOffset));
        console::PrintLine(line);
    }
    if (result.matches.size() > printed) {
        char more[64];
        std::snprintf(more, sizeof(more), "  ... and %zu more",
                      result.matches.size() - printed);
        console::PrintLine(more);
    }
}

}  // namespace

void Register() {
    // Engine-owned command (kcdxInvalidPluginHandle owner — the slot table
    // accepts it; dispatch resolves it to no plugin name, which is correct for
    // an engine command). Registration is unconditional: any user can type
    // kcdx_scan; it is NOT dev-mode-gated.
    console::GetInterface()->RegisterCommand(
        kcdxInvalidPluginHandle, "kcdx_scan",
        "kcdx_scan <module> \"<AOB pattern>\" — iterate an AOB pattern "
        "in-game; prints matches to the console.",
        &Callback);
}

}  // namespace kcdx::console_commands_scan
