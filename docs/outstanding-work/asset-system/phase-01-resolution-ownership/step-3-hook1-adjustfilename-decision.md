# Phase 1 step 3 — HOOK 1: REPLACE `CCryPak::AdjustFileName` (the resolution decision)

**Status: NOT STARTED.** Ledger: [`README.md`](README.md) → step 3.

## What

HOOK 1 of the two-hook seam (design §7). kcdx replaces `CCryPak::AdjustFileName`
(slot 1, id 152) via `hook_chain::AddCEngine` (Around/Replace) to own the
resolution DECISION — which file a vpath resolves to, for every asset class and
BOTH byte-lanes, above the `sys_pakPriority` gate. On a declared-overlay HIT, kcdx
decides its overlay wins (consulting the load-order overlay map, built `2588b33`);
on a MISS, it calls through to the engine leaves (pak-membership id 153,
disk-existence id 154, root-prefix id 155) so stock content — including a stock
Nexus/Workshop pak (US-7) — resolves byte-identically to today. This is the hook
that reaches **replace-vanilla** (pak-resident assets, via the resolver redirect
into the mount/stream lane). It also **removes the dead FOpen probe + the
`InstallSeamAProbe()` SEAM-A diagnostic** from `src/asset_overlay.{h,cpp}` — the
FOpen hook is not the mechanism (design §7); the live source returns to the
production resolver hook with no probe residue (`working-artifacts.md`).

## The HIT-write contract (VERIFIED + gated, `c28f53d`)

The probe phase deferred the HIT-path `outBuf` write because the caller-side
buffer capacity was unconfirmed. Now verified (`_research/adjustfilename-outbuf-recon/`,
gated PROCEED): every `AdjustFileName` caller passes a **2048-byte buffer as
`outBuf`** (read in 3 caller bodies — FOpen / FOpenRaw / GetFileSize-by-name;
2048 = CryEngine `ICryPak::g_nMaxPath`). `AdjustFileName` also RETURNS a `char*`
(FOpen consumes the return). So on a HIT kcdx's replacement:

- **Writes the overlay's concrete path into `outBuf`, bounded to 2048** — a
  bounded `snprintf(outBuf, 2048, …)` with loud truncation, NEVER an unbounded
  copy. Bound to 2048 AS the engine's universal path cap (`g_nMaxPath`), an
  invariant robust to unread callers — not "because these 3 callers allocate it"
  (the KI-0004 stack-overflow discipline: a bounded write, never trust caller
  capacity). A real filesystem path is always well under 2048.
- **Returns a `char*` to that resolved path** (point at `outBuf` after the write,
  matching the engine's return==outBuf convention so return-consuming callers
  like FOpen get the overlay path too).
- **MISS** → call through to the original (stock resolution byte-identical).

## Scope

- `src/asset_overlay.{h,cpp}`: install the `AdjustFileName` resolver hook —
  resolve the target by NAME (`CCryPak_AdjustFileName`, id 152) via refdb (never a
  literal RVA — AP1, `no-hardcoded-addresses.md`); install through
  `hook_chain::AddCEngine` (NOT raw MinHook — AP4, `hook-engine.md`); the body:
  normalize the requested vpath (`NormalizeVPath`, built) → overlay-map lookup →
  HIT writes the overlay path into `outBuf` bounded to 2048 + returns the ptr
  (the verified write contract above) / MISS calls through to the original (which
  itself falls to the leaves 153/154/155).
- Install in the ready-bracket window (before `SetEvent(g_kcdxReadyEvent)`) per
  step 1's confirmed ordering. (HOOK 2, step 4, installs alongside it.) If step 1
  surfaced the falsifying ordering outcome, this step is BLOCKED on the user's
  resolution of that fork.
- Remove `InstallSeamAProbe()` + its `src/dllmain.cpp` call + the dead FOpen probe
  hook; capture any still-useful wiring to `_research/probe-archive/` first.
- The MISS-path overlay lookup stays allocation-light — it runs on every FS query
  ahead of the engine's existing search loop (`memory.md`; plan-spec §"seam"
  per-FS-query cost).
- Resolve game facts by name/id only; **no new seed row** (ids 152–155 exist, AP18).

## Test bar

A behavior step proven by the DECISION being observable (the resolver hook fires
and decides, even before HOOK 2 serves the bytes): a declared overlay for a vanilla
path is CHOSEN and logged as the winning resolution (the overlay-HIT log line —
winning plugin + vpath); the MISS path falls through to the leaves (a stock asset
resolves unchanged — observable: the game boots to menu with stock assets intact).
Build green (`pwsh ./build.ps1`). The FOpen/SEAM-A residue is gone (grep
`src/asset_overlay.{h,cpp}` → no `InstallSeamAProbe` / FOpen probe). (Bytes served
end-to-end is HOOK 2's step-4 bar; this step proves the decision + the MISS
fall-through.) Permanent regression row is step 10.

## Dependencies

**Step 1** (the ordering probe — settles the install point; a falsifying outcome
must be user-resolved before this step). **Step 2** (the DS-bypass probe — a
DS-bypass finding could reshape the texture-coverage claim before the hook ships).
The landed overlay map (`2588b33`). Ordered after both probes so the install point
+ coverage are settled, not assumed (`incremental-delivery.md`).

## Reference

Design authority: [`../../../design/asset-replacement.md`](../../../design/asset-replacement.md)
§7 (HOOK 1 + the call-through leaves) + §8 (install timing) + §10.1
(`src/asset_overlay` responsibility). Shared spec: [`../plan-spec.md`](../plan-spec.md)
§"The seam — TWO coordinated hooks".

## Disassembler-test / author-burden

The hook is engine-internal — no author-facing input. The game facts (ids 152–155)
resolve by NAME through refdb; the hook carries no hand-written RVA/offset (AP1,
AP12). No new seed row (AP18).
