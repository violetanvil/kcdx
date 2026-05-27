#pragma once

// === FOPEN PROBE (Phase 8.5 — pak-resolver override semantics) =======
//
// The gating diagnostic for the asset-overlay feature (Phase 8.5b-e). It
// resolves two RUNTIME unknowns the Phase-8.5a RE flagged as static-confirmed
// but live-unconfirmed (_research/phase8.5-pak-resolver/FINDINGS.md §CONFIDENCE
// MAP):
//
//   #1  CCryPak::FOpen (the engine-wide open-by-path resolver) actually fires
//       for asset READS at runtime — i.e. slot 36 / RVA 0x004614A0 is on the
//       live load path for the asset class the overlay wants to redirect, not
//       just for the writes WriteCachePak makes.
//
//   #2  A path-rewrite inside the FOpen hook actually OVERRIDES a pak-resident
//       asset — i.e. redirecting pName to a loose substitute resolves to the
//       substitute, rather than FOpen's internal pak precedence winning. This
//       is the load-bearing question: the overlay-hook strategy (8.5c) only
//       works if the hook can override a pak-resident asset.
//
// The probe runs in two PHASES across two launches (results-driven: observe
// ground truth first, then a single-variable mutation):
//
//   U.1 (observe-only, the DEFAULT build)  — log every read-mode FOpen pName
//       during boot→menu, always calling the original unmutated. Resolves #1
//       and YIELDS the actual list of pak-resident virtual paths the game opens
//       early, so U.2's redirect target is a confirmed-firing path, not a
//       guess.
//
//   U.2 (mutate, gated behind kFOpenProbeMutate) — for ONE confirmed pak path
//       from U.1, rewrite pName to a loose sentinel copy and log/observe which
//       file the game loaded. Resolves #2.
//
// Mirrors loc_dump_probe / bugsplat_ctor_probe install discipline (dev-mode-
// gated, idempotent latch, atomic orig-pointer, Win64-fastcall typing), but
// resolves its target through the ADDRESS LIBRARY (ids 1206 CCryPak_FOpen +
// 1207 gEnv_pCryPak) rather than a labeled RVA constant — the seed rows exist
// (landed 8.5a), so the AP1-clean path is available here and is used.
//
// Dev-mode-only. Throwaway diagnostic: the permanent regression plugin ships
// with the 8.5c overlay feature, not the probe. A manifest-only suite plugin
// (cap-44) registers the row names for PENDING tracking; the engine reports
// unknown #1's auto-pass on first read-mode fire.

namespace kcdx::probes::fopen_override_probe {

// Arm the probe: install the MinHook detour on CCryPak::FOpen's function body
// (Address Library id 1206), resolved against the running WHGame.dll build.
// Installed from the worker-thread path (after hooks::Install → WHGame mapped +
// MinHook initialized), the same point loc_dump_probe installs. Idempotent.
// Returns true on success; a no-op (returns false) when dev mode is off.
bool Install();

}  // namespace kcdx::probes::fopen_override_probe
