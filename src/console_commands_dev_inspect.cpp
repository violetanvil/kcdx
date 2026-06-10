// kcdx_dev_inspect — engine-owned `~`-console command for in-game function
// statement inspection.
//
// An author who has FOUND a game function (via kcdx_find, kcdx.find, or a name
// they already know) types it into the in-game `~` console and sees the
// function's full statement list — per-statement kind, pseudo-text, captured
// variables, and the kcdx.op.* ops that fit each statement — instead of writing
// a throwaway plugin:
//
//   kcdx_dev_inspect WHGame.dll IsInCombat
//
// The in-game peer of inspecting a kcdx.find record's `statements` array — same
// step-0 dev-DB layer (refdb::EnumerateStatements), same dev-mode + dev-DB gate,
// same teaching message on the gated-off path. A DEV TOOL: the author reads the
// statement table here, then writes kcdx.statement.* / kcdx.locator.* code
// against the ops it reports.
//
// This is a CONSOLE COMMAND (a `~`-console verb), NOT a kcdx.* Lua surface — it
// does not live in the kcdx.* namespace and has no Lua/C++ parity obligation; it
// IS the cross-surface in-game tool (mirrors kcdx_scan / kcdx_find).
//
// Output goes to the visible console OVERLAY via console::PrintLine (the author
// is typing in the console; results must appear there). On the dev-gate-off path
// it prints the same teaching message kcdx_find emits, and on an unknown name a
// teaching error with the Levenshtein-ranked suggestion EnumerateStatements
// already computed — never a silent no-op (AP14).

#include "console_commands_dev_inspect.h"

#include <cstdint>
#include <cstdio>
#include <string>

#include "console.h"          // console::GetInterface, console::PrintLine
#include "find_dev_teaching.h"  // find_dev_teaching::kUnavailableConsole (shared)
#include "kcdx/Interfaces.h"  // kcdxConsoleCmdArgs, kcdxInvalidPluginHandle
#include "refdb.h"            // refdb::EnumerateResult / EnumerateStatements

namespace kcdx::console_commands_dev_inspect {

namespace {

// Cap the printed statement rows so a large function can't flood the overlay. A
// curated function carries tens of statements; this is a guard, not the common
// path. When it trips, the output says how many were elided.
constexpr size_t kMaxPrintedStatements = 40;

const char* kUsage =
    "kcdx_dev_inspect <module> <function>  "
    "(e.g. kcdx_dev_inspect WHGame.dll IsInCombat)";

// The dev-tool-unavailable teaching message is the shared
// find_dev_teaching::kUnavailableConsole (one source of truth, also used by
// kcdx_find — the two cannot drift). Printed on the gated-off path below.

// Render one statement's captures as a compact "[name:type, ...]" cell, or "-"
// when the statement captures nothing (an empty capture vector is legitimate —
// AP14: absence is observable, not silent). Written into the caller's buffer; no
// allocation beyond the std::string the cell text builds in (cold console path,
// not a hot loop — memory.md does not bind here).
std::string FormatCaptures(const refdb::FindStatement& s) {
    if (s.captures.empty()) return "-";
    std::string cell;
    for (size_t i = 0; i < s.captures.size(); ++i) {
        if (i != 0) cell += ", ";
        const refdb::FindCapture& c = s.captures[i];
        cell += c.var_name.empty() ? "?" : c.var_name;
        if (!c.data_type.empty()) {
            cell += ":";
            cell += c.data_type;
        }
    }
    return cell;
}

// Render one statement's applicable ops as a space-joined list, or "-" when none
// fit (legitimate — some kinds carry no ops beyond noop). The author copies an
// op name verbatim into kcdx.statement.replace_with(...).
std::string FormatOps(const refdb::FindStatement& s) {
    if (s.applicable_ops.empty()) return "-";
    std::string cell;
    for (size_t i = 0; i < s.applicable_ops.size(); ++i) {
        if (i != 0) cell += " ";
        cell += s.applicable_ops[i];
    }
    return cell;
}

// kcdx_dev_inspect <module> <function>
//
// Argv contract (kcdxConsoleInterface, Interfaces.h): GetArgCount returns 1 + N;
// arg 0 is the command name, arg 1 is <module>, arg 2 is <function>. A missing
// module or function is fail-loud: print the usage line and return — never a
// silent no-op.
void Callback(const kcdxConsoleCmdArgs* args) {
    const kcdxConsoleInterface* iface = console::GetInterface();

    int argc = iface->GetArgCount(args);

    // Need: name + module + function  (argc >= 3).
    if (argc < 3) {
        console::PrintLine(kUsage);
        return;
    }

    const char* moduleArg = iface->GetArg(args, 1);
    const char* fnArg = iface->GetArg(args, 2);
    if (!moduleArg || !*moduleArg || !fnArg || !*fnArg) {
        // GetArg returned null/empty where a value was expected — fail loud.
        console::PrintLine(kUsage);
        return;
    }
    // NOTE: the module name is accepted for surface consistency with the design's
    // `kcdx_dev_inspect <module> <function>` shape; EnumerateStatements resolves
    // a function by name across the full dev corpus, not by module. Stated so a
    // not-found is never mistaken for a module-scoping miss (mirrors kcdx_find).

    // Run the dev-DB enumeration (step 0 lazy-opens the dev DB, gated on dev
    // mode + file presence; on gate failure it returns unavailable=true).
    refdb::EnumerateResult result = refdb::EnumerateStatements(fnArg);

    // Dev gate failed (dev mode off OR dev DB absent): print the same teaching
    // message kcdx_find prints — never a silent no-op (AP14).
    if (result.unavailable) {
        console::PrintLine(find_dev_teaching::kUnavailableConsole);
        return;
    }

    // Not found (dev DB present, function unknown): a teaching error with the
    // Levenshtein-ranked name-similarity suggestion EnumerateStatements already
    // computed (read result.suggestions, do NOT recompute) + the recommended
    // next step — the exact step-2 §Scope format. Fail loud (AP14).
    if (!result.found) {
        {
            char line[256];
            std::snprintf(line, sizeof(line),
                          "[dev_inspect] no function '%s' in %s.", fnArg,
                          moduleArg);
            console::PrintLine(line);
        }
        if (!result.suggestions.empty()) {
            char line[256];
            std::snprintf(line, sizeof(line), "  Did you mean: %s? Try:",
                          result.suggestions.front().c_str());
            console::PrintLine(line);
            std::snprintf(line, sizeof(line),
                          "      kcdx_dev_inspect %s %s", moduleArg,
                          result.suggestions.front().c_str());
            console::PrintLine(line);
        }
        console::PrintLine("  Or search by content:");
        {
            char line[256];
            std::snprintf(line, sizeof(line),
                          "      kcdx_find %s --name_contains %s", moduleArg,
                          fnArg);
            console::PrintLine(line);
        }
        return;
    }

    // Found — print the statement table. Header first (function + module + rva +
    // statement count), then one row per statement: idx, kind, captures, ops,
    // and the pseudo-text. Mirrors kcdx_find's one-line-per-record overlay
    // discipline (snprintf into a fixed buffer, a printed-cap with a loud
    // elision line).
    // Header from the record; the full statement DETAIL now lives on
    // EnumerateResult.statements (find carries no statement bodies — KI-0015;
    // dev_inspect's ONE-function path still gets them).
    const refdb::FindRecord& r = result.record;
    const std::vector<refdb::FindStatement>& stmts = result.statements;
    {
        char header[256];
        std::snprintf(header, sizeof(header),
                      "[dev_inspect] %s  %s+0x%llX  [%s]  (%zu stmts)",
                      r.function.c_str(), r.module.c_str(),
                      static_cast<unsigned long long>(r.rva),
                      r.decompile_quality_label.empty()
                          ? "?"
                          : r.decompile_quality_label.c_str(),
                      stmts.size());
        console::PrintLine(header);
    }

    if (stmts.empty()) {
        // A resolved function with no statements is a legitimate observable
        // (AP14: state it, never a blank table) — e.g. a non-curated kind.
        console::PrintLine("  (no statements)");
        return;
    }

    const size_t printed =
        stmts.size() < kMaxPrintedStatements ? stmts.size()
                                             : kMaxPrintedStatements;
    for (size_t i = 0; i < printed; ++i) {
        const refdb::FindStatement& s = stmts[i];
        std::string caps = FormatCaptures(s);
        std::string ops = FormatOps(s);
        char line[512];
        // One line per statement: idx, kind, captures, ops, then the pseudo-text
        // (the at-a-glance row the author scans to pick a statement + op).
        std::snprintf(line, sizeof(line),
                      "  [%lld] %-8s  caps:%s  ops:%s  %s",
                      static_cast<long long>(s.idx),
                      s.kind.empty() ? "?" : s.kind.c_str(), caps.c_str(),
                      ops.c_str(),
                      s.pseudo_text.empty() ? "" : s.pseudo_text.c_str());
        console::PrintLine(line);
    }
    if (stmts.size() > printed) {
        char more[64];
        std::snprintf(more, sizeof(more), "  ... and %zu more statements elided",
                      stmts.size() - printed);
        console::PrintLine(more);
    }
}

}  // namespace

void Register() {
    // Engine-owned command (kcdxInvalidPluginHandle owner — the slot table
    // accepts it; dispatch resolves it to no plugin name, correct for an engine
    // command). Registration is unconditional: any user can type
    // kcdx_dev_inspect. The dev-mode/dev-DB gate is enforced at ENUMERATE time
    // (refdb::EnumerateStatements returns unavailable=true off-gate), not at
    // registration — a non-dev user typing it gets the teaching message, never a
    // missing command (mirrors kcdx_find).
    console::GetInterface()->RegisterCommand(
        kcdxInvalidPluginHandle, "kcdx_dev_inspect",
        "kcdx_dev_inspect <module> <function> -- enumerate a game function's "
        "statements from the dev reference DB (dev mode only); prints the "
        "statement table to the console.",
        &Callback);
}

}  // namespace kcdx::console_commands_dev_inspect
