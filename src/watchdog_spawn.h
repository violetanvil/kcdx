#pragma once

// watchdog_spawn — launch the kcdx-watchdog.exe sidecar.
//
// The watchdog is a tiny external process that blocks on the game's
// process handle and, when the game dies with a non-zero exit code,
// bundles up the engine + plugin + crash artifacts into a zip under
// kcdx-engine/logs/crash/.
//
// This module handles the spawn from inside kcdx.asi's DllMain
// worker thread, after paths::Init and log::Init have completed.
// On launch failure (security software, missing exe, etc.), kcdx
// continues running normally — only the crash-bundle UX is missed,
// and the operator can re-run kcdx-watchdog.exe manually against a
// post-mortem session.

namespace kcdx::watchdog {

// Spawn kcdx-watchdog.exe with this process's PID + engine paths.
// Returns true if CreateProcessW succeeded. Logs to kcdx.log via the
// LOGGING category.
bool Spawn();

}  // namespace kcdx::watchdog
