# Finding — step-1 ordering probe: AdjustFileName (slot 1) hook NEVER fired on boot/menu

Captured 2026-06-02 (live, dev log `kcdx-dev_2026-06-02_20-25-51.log`). The
asset-system step-1 probe (commit 4e9cb48) set out to time `ModManager_ctor` vs
the engine's first overridable asset read. It returned a FOURTH, unmapped
outcome: the read-side marker never fired at all.

## The raw observation (ground truth, theory-independent)

- `[20:25:53.331] SEAMA_PROBE: AdjustFileName Around hook installed (id 152, via
  hook_chain::AddCEngine; Around) at 00007FF85FF8205C` — the hook INSTALLED, at
  the correct VA (image base + 0x6205C).
- `[20:25:53.419] PROBE_CTOR_VS_READ ctor_fired` — the ready-bracket's HookedCtor
  ran (ModManager_ctor fired).
- `first_adjustfilename_call` — **ABSENT.** Never logged.
- `nflags_bit28_set` / `nflags_bit28_clear` — **ABSENT.** The seam body's OTHER
  first-call markers also never logged.
- `SUMMARY ... passing=143 ... total=174` at `20:26:07` — the game booted fully
  to menu (16 s of run), test suite ran. The menu's assets loaded normally.

**Conclusion (what holds regardless of theory):** the SEAM-A Around hook on
`CCryPak::AdjustFileName` (vtable slot 1, id 152) was installed at the correct
address but its body NEVER EXECUTED through boot→menu — while the menu's assets
demonstrably loaded. So `AdjustFileName` (slot 1) is NOT on the boot/menu
asset-read path as installed, OR the installed Around hook is inert.

## Init timeline (for the timing context the probe was meant to settle)

- 20:25:51.604 RefdbOpened (ids 152–155 name-resolvable from here)
- 20:25:52.521 EngineHooksInstalled
- 20:25:52.590 CtorBracketInstalled
- 20:25:53.120 PluginsLoaded
- 20:25:53.122 EnabledListBuiltAndReady
- 20:25:53.331 SEAM-A AdjustFileName hook installed
- 20:25:53.419 ctor_fired (HookedCtor)
- 20:26:07     menu reached, test SUMMARY

The ordering question (ctor vs first read) is UNANSWERABLE from this run: the
read-side event never occurred to be ordered.

## What this falsifies / opens (the discriminator the next probe owes)

The design (docs/design/asset-replacement.md §7) rests on: "every by-name asset
consumer + both asset classes route vpath → AdjustFileName (slot 1) BEFORE any
disk/pak touch; owning slot 1 owns resolution." This run is the FIRST live test
of that claim for the boot/menu path, and it did not hold there: slot 1 was
never called, yet menu assets resolved.

Two candidate explanations — a future probe must DISCRIMINATE, not assume:

- **(a) Wrong/incomplete seam.** The 5-front research's "9+ consumer slots call
  `*(vtable+0x8)`" may hold for the slots it decompiled but NOT for the entry the
  menu's asset reads actually use (a different ICryPak method, a cached path, a
  pre-resolved manifest, or a non-vtable direct call). slot 1 is then not the
  universal chokepoint the design claims.
- **(b) Install-inert.** The Around hook registered through
  `hook_chain::AddCEngine` but the detour is not actually intercepting slot-1
  calls (the conflict-engine Around path for a vtable-slot-reached function vs a
  direct-RVA-call function — the hook is on the RVA body; if the menu reaches the
  function only through the vtable pointer and the detour patched the RVA prologue,
  a vtable call still lands in the original prologue... but a prologue detour
  should catch that too. Needs direct verification).

Discriminator for the NEXT probe (theory-independent, observe ground truth):
1. Does the FOpen hook (id 131, slot 36) — which DID work for the cap-44 .dds
   live-test — fire on this same boot? (It's installed via the same AddCEngine
   path. If FOpen fires but AdjustFileName does not, the install mechanism works
   and the issue is that the menu path reaches FOpen WITHOUT going through slot 1
   first → falsifies the "FOpen calls slot 1" chain for this path → explanation
   (a), and a serious hit to the §7 single-chokepoint claim.)
2. Instrument the RVA body of 0x6205C directly (a raw first-instruction probe, not
   through the conflict engine) to confirm whether the function is called at all
   during boot — separating "function not called" (a) from "our detour inert" (b).

NOTE the cap-44-fopen-override plugin + probe-asset-overlay are loaded
(`zone=after_game`). The earlier .dds live-test (memory-mapped override rendered
in-game) went through the FOpen hook, NOT through slot 1 — which is itself
evidence for (a): the working override path may not traverse AdjustFileName.

## DISCRIMINATING PROBE RESULT (2026-06-02 20:51, log kcdx-dev_2026-06-02_20-51-47)

Added a one-shot first-fire marker to the WORKING FOpen hook (OverlayFOpen, id
131 slot 36) alongside the AdjustFileName marker, one launch:

- `[20:51:49.241] PROBE_CTOR_VS_READ first_fopen_call` — **FOpen FIRED.**
- `first_adjustfilename_call` — **SILENT** (AdjustFileName hook body never ran;
  install line confirms it WAS installed at `...205C`).
- `[20:51:49.241] ctor_fired` — same thread (tid=17088), same millisecond as
  first_fopen_call.
- menu reached (SUMMARY passing=143).

**OUTCOME (a), unambiguous: the boot/menu path reaches files via `CCryPak::FOpen`
(slot 36) WITHOUT going through `CCryPak::AdjustFileName` (slot 1).** FOpen is
live on the asset-read path; slot 1 is not called at all for these reads.

This FALSIFIES design §7's load-bearing claim — "every by-name consumer + both
asset classes route vpath → AdjustFileName (slot 1) BEFORE any disk/pak touch;
owning slot 1 owns resolution." For the boot/menu path, FOpen does NOT call slot 1
first (or the menu's reads do not go through the by-name consumers the 5-front
research decompiled). The "single universal chokepoint = AdjustFileName" premise
does not hold live.

Corroborating prior evidence: the cap-44 .dds override that DID render in-game
went through the FOpen hook, never slot 1 (this finding's original NOTE). FOpen
is the empirically-confirmed seam; AdjustFileName is not.

## RESOLVED (2026-06-03, log kcdx-dev_2026-06-03_11-20-20) — ctor strictly precedes the first FOpen

The v2 re-probe (sequence counter, commit `d0eadc5`) settled the ordering the
millisecond clock couldn't:
- `ctor_fired order_seq=0` — HookedCtor (the ready-bracket) fired FIRST.
- `first_fopen_call order_seq=1` — the engine's first CCryPak::FOpen fired SECOND.
- Same thread (tid=18988); menu reached (143 passing).

**Outcome 1 of the map: the ready-bracket install point HOLDS.** The ctor strictly
precedes the first FOpen, so installing both seam hooks (steps 3/4) before
`SetEvent(g_kcdxReadyEvent)` makes them live before the first asset open — the
game-init thread blocks at ModManager_ctor until kcdx signals ready, and that
signal is before the first FOpen. The v2 §8 install-timing assumption is VERIFIED.
No design fork; steps 3/4 install in the ready-bracket as designed.

(The earlier sections below are the v1-framed history — AdjustFileName-never-fired,
the discriminating probe, the ms-coincident data — preserved as the trail to this
resolution.)

## Status — (historical) design fork surfaced (NOT decided)

Step 1's probe did its job: it falsified the seam choice BEFORE the feature was
built on it. The design's §7 mechanism (replace AdjustFileName / slot 1) rests on
a premise the live binary contradicts for the boot/menu path. The seam choice is
now a DESIGN fork for the user (design-authority.md): the FOpen hook (slot 36,
id 131) is the empirically-working seam — the same hook that carried the .dds
override and the one the original (pre-supersession) design named before the
5-front research re-pointed to slot 1. Surfaced to the user; routed to
/senior-architect-consult / /design to settle the §7 mechanism on the verified
fact. Do NOT re-decompose or re-hook on a guess — the user decides the seam.
