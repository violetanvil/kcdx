#pragma once

// === Asset overlay — production hook on the game's pak resolver ======
//
// Phase 8.5: kcdx absorbs pak mods. The engine hooks CCryPak::FOpen (the
// engine-wide open-by-path resolver) so a virtual-path open can be redirected
// to a loose overlay file before the pak-resident asset is read.
//
// This module lands the production hook SITE through the conflict engine
// (hook_chain::AddCEngine — engine-stamped, the chain owns the MinHook detour),
// NOT raw MinHook: the diagnostic FOPEN probe used MinHook directly because it
// was throwaway; a production hook is engine-owned and must go through the
// chain so two installs on one site can't silently clobber each other
// (hook-engine.md). The runtime unknowns this hook rests on were resolved by
// the now-removed FOPEN probe: CCryPak::FOpen fires for asset READs, and a
// pName rewrite in a body detour OVERRIDES a pak-resident asset end-to-end
// (_research/probe-archive/fopen-override.md).
//
// The redirect DECISION (overlay-map lookup + pName rewrite) is a later step;
// this step ships a PASS-THROUGH body (call original unchanged) so the hook
// site is in place and boot-safe.
//
// Target resolved by NAME: "CCryPak_FOpen" (kcdx_id 131) — the common named-
// target path; the engine carries the address AND the verified ABI. No RVA, no
// new seed row.

namespace kcdx::asset_overlay {

// Install the production CCryPak::FOpen overlay hook on the conflict engine's
// chain (hook_chain::AddCEngine). Resolves the target by canonical name; must
// run AFTER RefdbOpened (the name resolution reads the cache built in
// refdb::Open()). Idempotent. Returns true on success.
bool Install();

}  // namespace kcdx::asset_overlay
