#pragma once

namespace kcdx::console_commands_scan {

// Register the engine-owned `kcdx_scan` console command with the console
// surface. Call once, right AFTER console::Init() arms the surface on the
// first update tick — registration either lands immediately or accept-defers
// through the same queue plugin commands use.
//
// kcdx_scan <module> "<AOB pattern>" iterates an AOB pattern from the in-game
// `~` console and prints the match count + module-relative addresses to the
// console overlay (and the concise dev-log lines RunScan emits). It is the
// address-discovery tool an author uses to FIND a site, then names via the
// authoring surface — the expert AOB hatch, not a per-hook hex burden.
void Register();

}  // namespace kcdx::console_commands_scan
