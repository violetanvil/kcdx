# KI-0028 — PROBE Z: the swap MECHANISM is exonerated; kcdx's slot LOGIC is the cause

**Date:** 2026-06-22 · **Trust:** primary evidence (live measurement, 4 witnesses).
**Verdict: the CCryPak vtable swap MECHANISM does NOT cause the black screen. A no-op
swap (kcdx's vtable installed, every slot THUNKING to the engine original) BOOTS TO THE
MENU and renders (draw_indexed 0→29772). The differentiator is kcdx's actual FS slot
LOGIC — not the swap itself.**

## The probe

The whole investigation assumed the FS-takeover swap causes the black screen (swap-ON
black, swap-OFF menu). With FS SERVE (PROBE W) and ENUMERATE (PROBE Y) both exonerated
by direct measurement, the premise needed re-verification: is the SWAP even causal, or
is it correlated-but-not-causal?

PROBE Z bisects the swap MECHANISM from kcdx's slot LOGIC. A `kcdx-thunkswap` marker
makes the swap install kcdx's OWN vtable on the CCryPak object (the full mechanism — the
[obj+0x00] overwrite, object identity, seat timing, the index build) but with EVERY slot
forced to thunk to the engine original. So the mechanism happens; zero kcdx FS logic runs.

## Result (FINAL — 4 witnesses, swap-ON thunkswap)

`kcdx-dev_2026-06-22_20-25-24.log`:
- **probe_z_thunkswap_active** — marker read, thunkswap path taken.
- **kcdx_vtable_built slots=102 kcdx_owned=0 probe_z_force_all_thunk=1** — every slot
  thunked; ZERO kcdx slot logic wired.
- **vtable_swapped** — the swap mechanism happened (kcdx's vtable installed at [obj+0x00]).
- **seating_swap_skipped=0 / probe_f_swap_suppressed=0** — a REAL swap, not suppressed.
- **DRAW_PROBE: draw_indexed=29772 ia_set_ib=27828** (vs the black baseline draw_indexed=0,
  ia_set_ib=0). **The geometry draws fire. The menu rendered. Boot was fast.**

## Consequence — the premise is OVERTURNED

The swap MECHANISM (object layout, the vtable-pointer overwrite, seat timing, the index
build, any member the swap touches) is INNOCENT — installing kcdx's vtable with original
bodies renders fine. The black screen is caused by **kcdx's actual FS slot LOGIC**.

Combined with the prior exonerations, this is a sharp paradox that is the next clue:
- kcdx SERVES every byte correctly (PROBE W — want==got, all index-pak).
- kcdx ENUMERATES identically to vanilla (PROBE Y — strict superset, zero real drops).
- YET kcdx's slot logic causes the black screen (PROBE Z — original bodies render, kcdx
  bodies do not).

∴ the break is NOT in WHAT kcdx answers (outputs are byte-identical to vanilla) but in
HOW it answers — a side effect of the slot impls invisible in their return values.

## Next — bisect WHICH slot family (re-enable one at a time)

The slot families (each a candidate; re-enable from the all-thunk baseline one family at
a time, keep the rest thunking):
- **open family (1/35/36)** — AdjustFileName + FOpen. kcdx mints (id<<1)|1 handles vs the
  engine's pointers; a handle-identity/lifetime side effect is the prime suspect.
- **read family (38..66)** — FReadRaw/FSeek/FClose etc. operate kcdx handles.
- **metadata family (13/45/67/68/69/70/92/93)** — existence/size; PROBE W showed outputs
  match vanilla, but timing/state side effects were not isolated.
- **enum family (14/63/64/65)** — PROBE Y showed the SET matches vanilla; a state/handle
  side effect of the iterator mint was not isolated.

The mechanism: extend the thunkswap switch from all-or-nothing to a per-family mask
(a marker / config naming which families go live), bisect until the black-screen family
is found, then probe that family's HOW-not-WHAT side effect. The handle-identity angle
(open/read mint kcdx handles the engine holds opaquely) is the strongest lead given the
serve/enumerate outputs are already proven correct.

## Reuse

- `kcdx-thunkswap` marker + `SwapVtableOnObject(pCryPak, forceAllThunk)` = the no-op-swap
  bisection tool. Extend forceAllThunk → a per-family live-mask for the family bisection.
- The swap mechanism is exonerated — do NOT re-investigate object layout / overwrite /
  timing as the cause. The cause is in a slot family's logic.
