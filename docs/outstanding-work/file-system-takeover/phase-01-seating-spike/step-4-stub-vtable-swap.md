# Step 1.4 — stub vtable + swap + probe P2/P4

**What.** Build a MINIMAL kcdx `CCryPak` vtable — 101 slots thunk to the original
engine bodies, slot 36 (`FOpen`) carries a kcdx log-marker then thunks through —
and swap it into the live object at `*(gEnv+0x50)` at the P1-confirmed seating
point. This proves the two load-bearing seating mechanisms on a tiny, fully
reversible spike: **P2** (the swap holds + the engine dispatches into kcdx — the
slot-36 marker fires on first vanilla open) and **P4** (every thunked slot runs
correctly against the swapped object — the game boots + reaches the world
normally, since 101 thunks are exercised throughout boot). The real file slots
build on this proven ground in Phase 3.

**Scope.** The per-slot declarative vtable table SCAFFOLD (design §4.3) — all rows
`THUNK(original)` except slot 36 = a kcdx marker-then-thunk; the swap mechanism
(write the kcdx vtable pointer into `[pCryPak+0x00]` at the seating point); and the
P2/P4 probe instrumentation. This is the spike: it changes no resolution behavior
(slot 36 thunks through after logging), so the game must behave identically while
proving the swap is live. One commit. The declarative table established here is
the same table Phase 3 fills with real KCDX impls — its scaffold is permanent (not
a throwaway probe), so it stays; only the P2/P4 marker logging is probe-residue to
capture+remove once proven.

**Design authority.** Built to `docs/design/file-system-takeover.md` §4.1 (the
swap), §4.3 (the per-slot declarative table — the reversibility property is
load-bearing: the table is the single point of slot ownership), §8 P2 + P4 (the
probe outcome maps). The executor builds to those sections, not this summary
(`.claude/rules/spec-conformance.md`).

**Outcome→meaning map** (pre-committed, design §8 P2/P4):
- slot-36 marker fires on first vanilla open AND game boots normally → P2 + P4
  both PASS → the seating + thunk approach are proven → Phase 3 may build real
  slots.
- file calls happen but no marker → the swap did not take (engine cached the
  vtable / an integrity check) → P2 FAIL → fall back to per-function MinHook
  detours (design §4.2) — a re-seating decision surfaced to the user.
- marker fires but a thunked slot crashes/misbehaves (game does not boot) → P4
  FAIL → a thunked original body cannot run against the swapped object → surface:
  own that slot too, or revisit the swap model.

**Test bar.** A permanent matrix row `cap-NN-fs-takeover-seating` (boot-only):
PASS = the slot-36 marker fired (P2) AND the game reached the world with the stub
vtable swapped in (P4). Falsifiable: FAILS if the marker never logs, or if boot
crashes with the swap active. Agent-read from `kcdx-dev.log` (`suite: X/Y`); the
user performs only the launch. This row stays permanent (it guards the seating
mechanism against regression even after the real slots land).

**Dependencies.** Step 1.3 (P1 — the seating point must be confirmed before the
swap is wired). Per `.claude/rules/incremental-delivery.md` the probe (1.3) lands
before the step that rests on its outcome (1.4).

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §4.1, §4.3, §8 P2/P4;
`.claude/rules/test-suite.md` (the regression row).

**Disassembler-test / author-burden.** N/A — engine-internal. Game-binary targets
(gEnv/pCryPak/the CCryPak vtable) resolve by name/id through the Address Library;
the CCryPak vtable address (`0x183A95FA8`) — if a seed row is needed to resolve it
by name — is an AP18 user-approval gate BEFORE the row lands (surface the entity to
the user first). Resolving the existing ids (1010/132/131) needs no new row.
