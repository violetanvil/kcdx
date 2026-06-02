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

#if 0  // === ARCHIVED PROBE SCAN_ARGV (2026-06-01): argv arrives INTACT (argc==3, arg2==full pattern, parsed==16 bytes, module=='WHGame.dll'); the 0-match was a test-FIXTURE defect (cap-70 scanned a cap-39-rewritten site post-apply), NOT a kcdx_scan bug.
       // Root cause: the cap-70 fixture scanned cap-32's outfit-swap AOB whose
       //   tail bytes cap-39 rewrites at the apply pass, so by input_loaded the
       //   pattern was gone -> kcdx_scan correctly reported 0. Repointed the
       //   fixture to luaL_openlibs' .text-unique, un-rewritten, un-hooked entry
       //   AOB; kcdx_scan itself was never wrong.
       // Confirmed: argc==3, arg2 == the full "48 81 ... F0" quoted token (quotes
       //   stripped, kept whole); parsed pattern_bytes==16; module_len==10.
       // Revive by flipping #if 0 -> #if 1 to re-observe the argv a future scan
       //   command receives.
    // PROBE SCAN_ARGV — how does the console tokenizer hand a quoted AOB to the
    //   command? A user-typed `kcdx_scan WHGame.dll "48 81 ..."` resolved to 0
    //   matches against a site verified to resolve to 1; observe the RAW argc +
    //   every GetArg(i) before any use, so the next action is driven by ground
    //   truth (which arg the pattern landed in, whether quotes were stripped,
    //   whether it was split across args), not a tokenization theory.
    //   Outcome A: argc==3, arg2 == the full "48 81 ... F0" (quotes stripped,
    //              kept whole) -> the pattern arrives intact; 0-match cause is
    //              elsewhere (ParsePattern? the scan itself?). Re-observe there.
    //   Outcome B: argc>3, the pattern split across arg2..argN (each byte/run a
    //              separate arg) -> the tokenizer split on spaces inside quotes;
    //              fix = rejoin arg2..end (or read the raw command line).
    //   Outcome C: arg2 carries leading/trailing quote chars, or a leading
    //              space -> trim/strip before ParsePattern.
    {
        LOG_DEBUG_KV("SCAN_ARGV", "raw",
                     log::KV("argc", static_cast<long long>(argc)));
        for (int i = 0; i < argc; ++i) {
            const char* a = iface->GetArg(args, i);
            LOG_DEBUG_KV("SCAN_ARGV", "arg",
                         log::KV("i", static_cast<long long>(i)),
                         log::KV("val", a ? a : "<null>"));
        }
    }
#endif

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
        msg += " -- pattern is an AOB byte string with optional ?? wildcards "
               "(e.g. \"48 8B 88 ?? ?? ?? ?? 48\").";
        console::PrintLine(msg.c_str());
        return;
    }

#if 0  // === ARCHIVED PROBE SCAN_ARGV (2026-06-01): parsed pattern is byte-clean (pattern_bytes==16, module_len==10) — the 0-match was a test-FIXTURE defect (cap-70 scanned a cap-39-rewritten site post-apply), NOT a kcdx_scan bug.
       // Root cause: same as the argv-dump block above — the cap-70 fixture's
       //   pattern was a site cap-39 rewrites at the apply pass, gone by
       //   input_loaded; the inputs were always clean, the site was the problem.
       //   Fixture repointed to luaL_openlibs' un-rewritten .text-unique AOB.
       // Confirmed: pattern.bytes.size()==16 AND entry.module.size()==10 — both
       //   inputs byte-clean (no hidden CR / NBSP / trailing space).
       // Revive by flipping #if 0 -> #if 1 to re-observe the parsed pattern +
       //   module a future scan command yields.
    // SCAN_ARGV settled Outcome A: arg2 arrives intact. Next checkable: does
    // ParsePattern produce the SAME byte vector cap-32's working path does for
    // the identical 16-byte string? Log the parsed pattern's size + the raw
    // module string with explicit length, so a hidden trailing char (CR, NBSP,
    // space) on either string — invisible in a plain log print — shows as a
    // size != 16 or a module length != 10 ("WHGame.dll").
    //   Outcome A: pattern.bytes.size()==16 AND module len==10 -> both inputs
    //              are byte-clean; the 0-match cause is NOT the inputs.
    //   Outcome B: size != 16 -> the pattern string carried a hidden char that
    //              changed the parse; trim it before ParsePattern.
    //   Outcome C: module len != 10 -> the module arg carried a hidden char so
    //              OpenModule opened nothing / the wrong module; trim it.
    LOG_DEBUG_KV("SCAN_ARGV", "parsed",
                 log::KV("pattern_bytes",
                         static_cast<long long>(entry.pattern.bytes.size())),
                 log::KV("module_len",
                         static_cast<long long>(entry.module.size())),
                 log::KV("module", entry.module.c_str()));
#endif

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
        "kcdx_scan <module> \"<AOB pattern>\" -- iterate an AOB pattern "
        "in-game; prints matches to the console.",
        &Callback);
}

}  // namespace kcdx::console_commands_scan
