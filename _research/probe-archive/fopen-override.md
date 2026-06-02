# Probe archive — FOPEN override probe (Phase 8.5 pak-resolver)

Captured-and-removed: `src/probes/fopen_override_probe.{cpp,h}` (whole files `git rm`'d,
same model as `post_bracket_probe.*.txt` / `loc_dump_probe.*.txt`). The probe was
observe-only (`kFOpenProbeMutate = false`) at removal; its questions are RESOLVED.
The production asset-overlay hook (`src/asset_overlay.{h,cpp}`, installed through the
conflict engine) supersedes it.

## Archive header

- **Verdict:** unknown #1 (`CCryPak::FOpen` fires for asset READS) → PASS (`cap-44-fopen-read-fires`, live 2026-05-26). Unknown #2 / U.4 (a hook redirect to a loose substitute OVERRIDES a pak-resident asset end-to-end on a handle-consumed class) → CONFIRMED (`KCDX_U4_OVERRIDE_ACTIVE` reached `kcd.log`).
- **Mechanism:** `CCryPak::FOpen` (kcdx_id 131, WHGame+0x004614A0) is the engine-wide open-by-path resolver, on the live asset-read path; a `pName` rewrite inside a body detour on it, with the OS-search flag `0x10006` OR'd in, makes a `Data/`-relative loose substitute resolve, and a handle-consumed class (`.lua`) reads the substitute THROUGH the returned handle — so a body detour can override a pak-resident asset.
- **Backlink:** Phase 8.5 (`docs/outstanding-work/restructure/00-original-plan.md` §"Phase 8.5"); RE facts preserved in `_research/phase8.5-pak-resolver/FINDINGS.md`. The probe-archive entry captures the PROBE WIRING + outcome maps (the reconstruction recipe), not a re-derivation of the RE facts.
- **Revival hint:** to re-instrument FOpen for a NEW question, reconstruct the body detour below. NOTE the production hook is now engine-owned (`asset_overlay`, via `hook_chain::AddCEngine`); a fresh DIAGNOSTIC must not stack a second LIVE probe on the same site (`results-driven.md` §"Probe leaves no residue"; `guard-probe-stack.py`). Resolve the target by NAME `"CCryPak_FOpen"` (kcdx_id 131) + `"gEnv_pCryPak"` (kcdx_id 132); both rows exist. ABI = the verified seed signature for id 131.

## Reusable wiring — the FOpen ABI typedef + install discipline

The probe used MinHook DIRECTLY because it was a throwaway diagnostic. A PRODUCTION
hook on this site goes through the conflict engine (`hook_chain::AddCEngine`), never raw
MinHook (`hook-engine.md`, AP4) — see `src/asset_overlay.cpp`. The raw-MinHook discipline
below is preserved only as the diagnostic-reconstruction recipe.

```cpp
// === Function-pointer typing (Win64 fastcall, verified ABI id 131) ==
//   ptr (ptr this, cstr pName, cstr szMode, u32 nFlags)
//   RCX=this(ICryPak*), RDX=pName, R8=szMode, R9D=nFlags.
using FOpen_t = void* (__fastcall*)(void*       self,
                                    const char* pName,
                                    const char* szMode,
                                    uint32_t    nFlags);

// Canonical refdb names (seed rows landed during the Phase 8.5a RE work):
//   CCryPak_FOpen   — body at WHGame+0x004614A0 (kcdx_id 131)
//   gEnv_pCryPak    — slot at WHGame+0x0492B850 (kcdx_id 132)
// vtable slot 36 (offset +0x120) is used ONLY for a one-shot consistency check
// (does *pCryPak's vtable[36] equal the Library's FOpen body?); the detour
// itself targets the BODY (process-wide), not the slot — that matches what an
// overlay hook does and needs no live-instance capture.
constexpr size_t kFOpenVtableSlot = 36;  // 0x120 / 8

// Diagnostic install discipline (raw MinHook — DO NOT copy for production):
//   1. dev-mode gate (kcdx::dev::IsEnabled) + idempotent g_installed latch.
//   2. Resolve the body via refdb::ResolveAddrByName("CCryPak_FOpen") — runs
//      AFTER RefdbOpened (the cache is built in refdb::Open()).
//   3. MH_Initialize (idempotent; ALREADY_INITIALIZED is the no-op case).
//   4. One-shot reach_check: *pCryPak (gEnv+0x50) → vtable[36] == body?
//   5. MH_CreateHook(body, &HookedFOpen, &orig) + MH_EnableHook.
//   6. RegisterModification(body, Category::Probe, "fopen_override").
//   7. ONE LOG_INFO line on install; NEVER a per-call log (FOpen is hot —
//      680 call sites, fires thousands of times). The body deduped by distinct
//      (mode-class, path) under a mutex, logged once at LOG_DEBUG.
```

## Outcome maps (pre-committed, theory-independent)

### U.1 — does FOpen fire for asset READS? (observe-only)

> Probe asks: *does `CCryPak::FOpen` (id 131 / slot 36) fire with a read mode during boot→menu?*
> - Outcome A (a read-mode open is observed; `cap-44-fopen-read-fires` PASS on first read-mode fire) → means *slot 36 is on the live asset-read path, not just the WriteCachePak write path* → next action *U.1 yields the distinct pak-resident path list; pick a confirmed-firing redirect target for U.2.*
> - Outcome B (no read-mode open ever observed) → means *FOpen is NOT the read resolver; the overlay needs a different seam* → next action *re-RE the read path; do not build the overlay on FOpen.*

Read mode = `szMode[0] == 'r'`. Self-report fires ONCE from the first read-mode fire
(one-shot guarded, hook-fire-self-report convention — never poll a count at a pre-fire
lifecycle point). **RESOLVED: Outcome A (live 2026-05-26; 64 read opens captured; reach_check match=1).**

### U.4 — does a hook redirect OVERRIDE a pak-resident asset end-to-end? (mutate, gated)

Trigger: the boot-loaded `scripts/cheat/cheat_util.lua` (handle-consumed — read
through the FOpen handle via the shared CCryFile helper). On the FIRST read open of
the trigger, redirect `pName` → a VALID loose Lua substitute at a `Data/`-prefixed
path, OR-in `0x10006` (the OS-search flag U.2c proved makes a `Data/` loose path
resolve). The substitute is the byte-exact real file + one appended
`Cheat:logDebug("KCDX_U4_OVERRIDE_ACTIVE")` (uses the script's OWN logger, which
demonstrably reaches `kcd.log`). One-shot; null-fallback to the original `pName`
so a null can't break boot.

> Probe asks: *does the engine LOAD+EXECUTE our substitute when the hook redirects the open to it?* (read-back = `kcd.log`)
> - Outcome A (`"KCDX_U4_OVERRIDE_ACTIVE"` present) → means *override ACCEPTED end-to-end; handle-consumed FOpen override works; asset-file overlay mechanism CONFIRMED* → next action *build the production overlay hook on this site.*
> - Outcome B (only `"cheat_util.lua loaded"` / `"CombatTest Startup"`, no KCDX line) → means *NOT accepted (a finding; the partition predicted accept)* → next action *re-examine; the overlay needs a different mechanism (return-our-own-handle / pak registration).*
> - Outcome C (`u4_result resolved=0`) → means *the substitute path didn't resolve — re-check path/flag, NOT a verdict (fallback served the real file, boot safe).*

Earlier sub-probes that fed U.4: **U.2a** (a loose sibling at the SAME vpath lost
to the pak — `[3920]` not `[7]` — so CryEngine does NOT natively do loose-over-pak;
the overlay NEEDS the hook); **U.2c** (FOpen does NOT open an arbitrary loose path —
only specific loose roots resolve; `0x10006` + a `Data/`-relative path is the
resolvable shape; the detour called `orig()` ITSELF on a battery of candidate paths,
observe-only, and FClose'd each non-null handle via vtable slot 55 / +0x1B8 to avoid
leaking engine handles). **RESOLVED: Outcome A (`KCDX_U4_OVERRIDE_ACTIVE` reached `kcd.log`).**

## Test-row note

`cap-44-fopen-read-fires` (U.1 auto-pass) and `cap-44-fopen-override` (U.2 manual
read) were the PROBE's self-report rows (`test-plugins/cap-44-fopen-override/kcdx.toml`,
a manifest-only stub). They will no longer fire once the probe is removed — the behavior
they tested is subsumed by the production overlay hook + the phase's `cap-XX-asset-replace`
plugin (step 5). Whether to strike the cap-44 stub or keep it as a graduation marker is a
test-coverage decision surfaced to the manager in this step's deliverable, not decided here.
