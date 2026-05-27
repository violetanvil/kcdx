#pragma once

// === MOD-LOADER PROBE (Phase 8.5 absorb — PROBE U.6) =================
//
// The two probe-first gates for the kcdx "absorb the KCD2 mod loader" feature
// (FINDINGS.md §"ABSORB DESIGN — APPROVED"). Both resolved in ONE launch by a
// log-only detour on the engine's mod-loader SELECT orchestrator
// (wh::C_ModManager, FUN_180da104c, RVA 0x00DA104C):
//
//   U.6.1 TIMING — does a detour installed from the worker-thread point (after
//     WaitForGameDll + hooks::Install) FIRE before CSystem::Init runs the native
//     mod-load? If it fires, worker-thread install is early enough for the
//     narrow takeover; if the native mod-load runs first (no detour fire), the
//     absorb needs before_game/LDR (Phase-11) install timing. DECIDES the
//     install-timing design.
//
//   U.6.2 I_MOD RECORD LAYOUT — the enabled-mod list lives at C_ModManager+0x30
//     as 0x70-byte records. kcdx must SYNTHESIZE these for kcdx-plugins/ entries
//     (the narrow takeover). The detour walks this+0x30 and dumps the first
//     record's 0x70 bytes so the layout can be reverse-engineered. Gates whether
//     the narrow takeover is buildable.
//
// OBSERVE-ONLY: the detour logs, dumps, and ALWAYS calls the original SELECT
// orchestrator unchanged — no mutation of the mod list, no behavior change.
// Mirrors loc_dump_probe / fopen_override_probe install discipline (dev-mode-
// gated, idempotent latch, MinHook detour, atomic orig-pointer). Dev-mode-only.

namespace kcdx::probes::mod_loader_probe {

// Arm the probe: install the log-only detour on the SELECT orchestrator
// (FUN_180da104c) against the running WHGame.dll. Idempotent; a no-op (returns
// false) when dev mode is off. Returns true on successful install.
bool Install();

}  // namespace kcdx::probes::mod_loader_probe
