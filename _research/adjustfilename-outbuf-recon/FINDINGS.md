# Finding — `CCryPak::AdjustFileName` caller-side `outBuf` is a 2048-byte buffer (3 callers, in-body)

Verified 2026-06-04 against `WHGame.dll` (release_1_5_1164953_841), fresh Ghidra
(tier 5; tiers 1–2 partially answered). Settles the build authority for kcdx's
HOOK-1 resolver replacement (asset-system step 3): can the HIT-path write its
overlay path back through `outBuf`, and bounded to what size?

## The verified fact

**Every `CCryPak::AdjustFileName` (id 152, vtable slot 1, `*(this+0x8)`) caller
read allocates a 2048-byte stack buffer and passes it as `outBuf` (arg 3).** Read
in each caller's own decompiled body (not inferred from the callee):

| Caller (front-1 vtable surface) | RVA | `outBuf` decl | slot-1 dispatch |
|---|---|---|---|
| slot 36 **FOpen** (id 131) | 0x4614A0 | `local_858 [2048]` | `(**(*this+8))(this, pName, local_858, nFlags)` |
| slot 35 **FOpenRaw** (open-into-caller-buffer) | 0x2418DE4 | `local_838 [2048]` | `(**(*this+8))(this, pName, local_838, 0)` |
| slot 45 **GetFileSize-by-name** | 0x2418B48 | `local_828 [2048]` | `(**(*this+8))(this, pName, local_828, 2)` |

2048 = CryEngine `ICryPak::g_nMaxPath` (the universal path cap). It matches the
callee normalizer's own 2048-byte buffer (`FUN_1804621bc`, captured at
`_research/phase8.5-pak-resolver/_subresolver_findings.md:27-29`) — the caller
allocates exactly what the callee writes into. The 2048 is now read in 3 distinct
CALLER bodies, closing the gap the SEAM-A probe deferred ("caller-side outBuf
capacity unconfirmed").

## Return value vs outBuf — both consumed

`AdjustFileName` returns a `char*` (the resolved path string) AND writes the
resolved path into `outBuf`. FOpen consumes the RETURN (`uVar6` from the dispatch,
used at the open + handle-register sites — `_fopen_handle_decomp.txt:190,199,268`);
the callers also pass `outBuf` as the write target. So a faithful kcdx replacement
should satisfy BOTH channels: write the overlay path into `outBuf` AND return a
pointer to it.

## The kcdx HOOK-1 write contract (the build authority this settles)

On a declared-overlay HIT, kcdx's `AdjustFileName` replacement:
- **Writes the overlay's concrete path into `outBuf`, bounded to 2048 bytes**
  (a `snprintf`/bounded-copy into `outBuf`, cap 2048 — never an unbounded copy;
  a real filesystem path is always well under 2048). Bounding to 2048 is safe
  against all three callers (each allocates ≥2048) and matches the engine's own
  cap; an over-2048 path is truncated loud, never an OOB write (the cap-72 crash
  class is structurally excluded).
- **Returns a `char*` to that resolved path** (point it at `outBuf` after the
  write, matching the engine's own return==outBuf-contents convention, so the
  return-consuming callers like FOpen get the overlay path too).
- On a MISS, calls through to the original (stock resolution byte-identical).

## Gate (research-disassembly §4.5 — this becomes a build authority)

**PROCEED** (2026-06-04). An independent verifier re-decompiled all three callers
cold (the synthesizer's leaning WITHHELD) and confirmed each passes a `[2048]`
local as arg3 to the single slot-1 dispatch in its body — byte-for-byte matching
this dump, consistency confirmed (no size mix).

**Sharpened framing (the verifier's load-bearing concern):** only 3 of N
AdjustFileName consumers were read (it is a general CryPak path primitive with
more callers). 3-consistent-at-2048 + 2048 = `ICryPak::g_nMaxPath` is strong, but
not proof every caller allocates 2048. So the kcdx write contract bounds to 2048
**AS the engine's universal path cap (`g_nMaxPath`), an invariant** — NOT merely
"because these 3 callers allocate it." The bound is the same number; the
cap-as-invariant framing is robust to the unread callers and is the bounded-write
discipline the KI-0004 stack-overflow class demands. A bounded
`snprintf(outBuf, 2048, ...)` with loud truncation is correct regardless of any
individual caller's frame. PROCEED on that framing; NEVER an unbounded write that
trusts caller capacity.

## Status — verified, no new seed row (AP18 N/A)

ids 152 (AdjustFileName) + 131 (FOpen) already exist; this reads their caller
context, adds no entity. The finding is the build authority for asset-system
step-3's HIT write — recorded here as the tier-2 reuse artifact; the step doc's
"HIT decides kcdx's overlay path" now has its bounded-2048 write contract.
