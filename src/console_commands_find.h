#pragma once

namespace kcdx::console_commands_find {

// Register the engine-owned `kcdx_find` console command with the console
// surface. Call once, right AFTER console::Init() arms the surface on the first
// update tick — registration either lands immediately or accept-defers through
// the same queue plugin commands use (mirrors console_commands_scan::Register).
//
// kcdx_find <module> --<criterion> "<value>" [...]  discovers a game function
// from the dev reference DB and prints the matches to the `~` console overlay.
// It is the in-game peer of the kcdx.find Lua verb — same dev-DB search path,
// same dev-mode/dev-DB gate, same teaching message printed on the gated-off
// path. A DEV TOOL: the author discovers a site here, then writes
// kcdx.statement.* / kcdx.locator.* code against it.
void Register();

}  // namespace kcdx::console_commands_find
