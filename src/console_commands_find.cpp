// kcdx_find — engine-owned `~`-console command for in-game function discovery.
//
// An author hunting a game function types what they know into the in-game `~`
// console and sees the matching functions immediately, instead of writing a
// throwaway plugin:
//
//   kcdx_find WHGame.dll --string "test_marker"
//   kcdx_find WHGame.dll --callee SomeFn --name_contains Inventory
//
// The in-game peer of the Lua kcdx.find verb — same step-0 dev-DB search path
// (refdb::FindFunctions), same dev-mode + dev-DB gate, same teaching message on
// the gated-off path. A DEV TOOL: the author discovers a site here, then writes
// kcdx.statement.* / kcdx.locator.* code against it.
//
// This is a CONSOLE COMMAND (a `~`-console verb), NOT a kcdx.* Lua surface — it
// does not live in the kcdx.* namespace and has no Lua/C++ parity obligation; it
// IS the cross-surface in-game tool (mirrors kcdx_scan). The Lua kcdx.find{...}
// verb is the separate, parallel runtime surface.
//
// Output goes to the visible console OVERLAY via console::PrintLine (the author
// is typing in the console; results must appear there). On the dev-gate-off path
// it prints the same teaching message kcdx.find logs — never a silent no-op.

#include "console_commands_find.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "console.h"          // console::GetInterface, console::PrintLine
#include "find_dev_teaching.h"  // find_dev_teaching::kUnavailableConsole (shared)
#include "kcdx/Interfaces.h"  // kcdxConsoleCmdArgs, kcdxInvalidPluginHandle
#include "refdb.h"            // refdb::FindCriteria / FindResult / FindFunctions

namespace kcdx::console_commands_find {

namespace {

// Cap the per-record overlay lines so an over-broad criterion can't flood the
// console. A normal discovery query has a handful of hits; this is a guard, not
// the common path. When it trips, the output says how many were elided.
constexpr size_t kMaxPrintedRecords = 20;

const char* kUsage =
    "kcdx_find <module> --<criterion> \"<value>\" [...]  "
    "(criteria: --string --cvar --callers_of --callee --name_contains "
    "--callee_in_subsystem; e.g. kcdx_find WHGame.dll --string \"test_marker\")";

// The dev-tool-unavailable teaching message is the shared
// find_dev_teaching::kUnavailableConsole (one source of truth for the console
// rendering, also used by kcdx_dev_inspect — no drift). Printed on the gated-off
// path below.

// Map a --flag name to its FindCriteria slot. Returns false (no slot set) for an
// unrecognized flag — the caller treats that as a fail-loud usage error, never a
// silent drop (AP14).
bool ApplyFlag(refdb::FindCriteria& c, const char* flag, const char* value) {
    if (std::strcmp(flag, "--string") == 0) {
        c.has_string = true; c.string = value; return true;
    }
    if (std::strcmp(flag, "--cvar") == 0) {
        c.has_cvar = true; c.cvar = value; return true;
    }
    if (std::strcmp(flag, "--callers_of") == 0) {
        c.has_callers_of = true; c.callers_of = value; return true;
    }
    if (std::strcmp(flag, "--callee") == 0) {
        c.has_callee = true; c.callee = value; return true;
    }
    if (std::strcmp(flag, "--name_contains") == 0) {
        c.has_name_contains = true; c.name_contains = value; return true;
    }
    if (std::strcmp(flag, "--callee_in_subsystem") == 0) {
        c.has_callee_in_subsystem = true; c.callee_in_subsystem = value;
        return true;
    }
    return false;  // unrecognized flag
}

// kcdx_find <module> --<criterion> "<value>" [...]
//
// Argv contract (kcdxConsoleInterface, Interfaces.h): GetArgCount returns 1 + N;
// arg 0 is the command name, arg 1 is <module>, args 2.. are alternating
// --flag / value pairs. A missing module, no criterion, an unknown flag, or a
// flag with no value is fail-loud: print the usage line and return — never a
// silent no-op.
void Callback(const kcdxConsoleCmdArgs* args) {
    const kcdxConsoleInterface* iface = console::GetInterface();

    int argc = iface->GetArgCount(args);

    // Need at least: name + module + one --flag + its value  (argc >= 4).
    if (argc < 4) {
        console::PrintLine(kUsage);
        return;
    }

    const char* moduleArg = iface->GetArg(args, 1);
    if (!moduleArg || !*moduleArg) {
        console::PrintLine(kUsage);
        return;
    }
    // NOTE: the module name is accepted for surface consistency with the design's
    // `kcdx_find <module> ...` shape; the dev-DB search resolves by the criteria,
    // not by module (the corpus is the full game). Stated so an empty result is
    // never mistaken for a module-scoping miss.

    refdb::FindCriteria criteria;
    int setCount = 0;

    // Walk the remaining args as --flag value pairs (start at arg 2).
    for (int i = 2; i < argc; i += 2) {
        const char* flag = iface->GetArg(args, i);
        const char* value = (i + 1 < argc) ? iface->GetArg(args, i + 1) : nullptr;

        if (!flag || !*flag || !value || !*value) {
            // A flag with no value, or a dangling final token — fail loud.
            console::PrintLine(kUsage);
            return;
        }
        if (!ApplyFlag(criteria, flag, value)) {
            std::string msg = "[find] unknown criterion '";
            msg += flag;
            msg += "'. ";
            msg += kUsage;
            console::PrintLine(msg.c_str());
            return;
        }
        ++setCount;
    }

    if (setCount == 0) {
        // No --flag pair parsed — fail loud (the at-least-one-of-N bar).
        console::PrintLine(kUsage);
        return;
    }

    // Run the dev-DB search (step 0 lazy-opens the dev DB, gated on dev mode +
    // file presence; on gate failure it returns unavailable=true).
    refdb::FindResult result = refdb::FindFunctions(criteria);

    // Dev gate failed (dev mode off OR dev DB absent): print the same teaching
    // message kcdx.find logs — never a silent no-op.
    if (result.unavailable) {
        console::PrintLine(find_dev_teaching::kUnavailableConsole);
        return;
    }

    if (result.records.empty()) {
        console::PrintLine("[find] 0 matches");
        return;
    }

    {
        char header[96];
        if (result.truncated) {
            std::snprintf(header, sizeof(header),
                          "[find] %lld matches (showing first %zu):",
                          static_cast<long long>(result.total_matches),
                          result.records.size());
        } else {
            std::snprintf(header, sizeof(header), "[find] %zu matches:",
                          result.records.size());
        }
        console::PrintLine(header);
    }

    const size_t printed =
        result.records.size() < kMaxPrintedRecords ? result.records.size()
                                                    : kMaxPrintedRecords;
    for (size_t i = 0; i < printed; ++i) {
        const refdb::FindRecord& r = result.records[i];
        char line[256];
        // One line per function: name, module+rva, decompile-quality label,
        // statement count — the at-a-glance discovery row the author scans.
        std::snprintf(line, sizeof(line), "  %s  %s+0x%llX  [%s]  (%zu stmts)",
                      r.function.c_str(), r.module.c_str(),
                      static_cast<unsigned long long>(r.rva),
                      r.decompile_quality_label.empty()
                          ? "?"
                          : r.decompile_quality_label.c_str(),
                      r.statements.size());
        console::PrintLine(line);
    }
    if (result.records.size() > printed) {
        char more[64];
        std::snprintf(more, sizeof(more), "  ... and %zu more shown rows elided",
                      result.records.size() - printed);
        console::PrintLine(more);
    }
}

}  // namespace

void Register() {
    // Engine-owned command (kcdxInvalidPluginHandle owner — the slot table
    // accepts it; dispatch resolves it to no plugin name, correct for an engine
    // command). Registration is unconditional: any user can type kcdx_find. The
    // dev-mode/dev-DB gate is enforced at SEARCH time (refdb::FindFunctions
    // returns unavailable=true off-gate), not at registration — a non-dev user
    // typing it gets the teaching message, never a missing command.
    console::GetInterface()->RegisterCommand(
        kcdxInvalidPluginHandle, "kcdx_find",
        "kcdx_find <module> --<criterion> \"<value>\" -- discover a game "
        "function from the dev reference DB (dev mode only); prints matches to "
        "the console.",
        &Callback);
}

}  // namespace kcdx::console_commands_find
