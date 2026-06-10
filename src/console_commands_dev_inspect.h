#pragma once

namespace kcdx::console_commands_dev_inspect {

// Register the engine-owned `kcdx_dev_inspect` console command with the console
// surface. Call once, right AFTER console::Init() arms the surface on the first
// update tick — registration either lands immediately or accept-defers through
// the same queue plugin commands use (mirrors console_commands_find::Register).
//
// kcdx_dev_inspect <module> <function>  enumerates one function's statements
// from the dev reference DB and prints them as a formatted table to the `~`
// console overlay. It consumes the same dev-DB search layer as kcdx_find
// (refdb::EnumerateStatements), shares the same dev-mode/dev-DB gate, and prints
// the same dev-tool-unavailable teaching message on the gated-off path. On an
// unknown function name it prints a teaching error with a Levenshtein-ranked
// name-similarity suggestion. A DEV TOOL: the author inspects a function's
// statements here, then writes kcdx.statement.* / kcdx.locator.* code against
// the ops the table reports.
void Register();

}  // namespace kcdx::console_commands_dev_inspect
