# Step 7 — foreign-hook detection (prologue classifier)

**What.** Before kcdx installs a hook, read the target's prologue and classify it:
clean game instructions, a jump into a kcdx trampoline (already-in-a-kcdx-chain),
or a FOREIGN jump (an E9 rel32 / FF25 absolute pointing outside any kcdx
trampoline). This is the detection half of foreign-hook coexistence — Step 8
chains onto what this classifies as foreign. Covers E13 (`../context.md`).

**Scope (commit-grain).**
- The prologue classifier (design §6.1): read the target's first bytes; classify
  as one of —
  - **clean** (real game instructions, no jump) → install normally.
  - **kcdx trampoline** (a jump whose target falls inside a kcdx-owned trampoline
    allocation — the branch-pool ranges + safetyhook's allocator ranges, both
    kcdx-owned) → the existing chain path (`CanCoexist` / append a `ChainEntry`),
    unchanged.
  - **foreign** (an E9/FF25 jump pointing OUTSIDE any kcdx trampoline range) →
    flag as foreign (Step 8 chains onto it).
- The discriminator (design §6.1): a jump target inside kcdx's known trampoline
  allocations is kcdx's; a jump elsewhere is foreign. The classifier needs a way
  to ask "is this address in a kcdx trampoline range?" — wire it to the
  branch-pool's known ranges + safetyhook's allocator ranges.
- Decode the jump forms: 5-byte E9 rel32 (read the disp32, compute the target)
  and 14-byte FF25 absolute (read the [rip]+8-byte target). A prologue that is
  neither clean nor a recognized jump form is logged and treated as clean-or-
  unknown per the design's conservative default (do not mis-chain an unrecognized
  shape — surface it).
- Detection-only this step: the classifier returns a verdict; it does NOT yet
  follow a foreign jump (that's Step 8). Wiring it into the install path's
  decision point can land here (the foreign branch is a no-op-but-logged stub
  until Step 8) or with Step 8 — keep this step's diff the classifier + its test.

**Test bar.** A unit-level classifier regression (`test-plugins/` or an
engine-side test) feeding synthetic prologues: clean game bytes → clean; a jump
into a known kcdx-trampoline range → kcdx; a foreign E9 into an unknown range →
foreign; a foreign FF25 → foreign. Each a FALSIFIABLE row (the foreign-E9 case
FAILS if classified as clean or kcdx). Runnable at this step (synthetic prologues
need no live game). Build green; the full cap-NN suite unregressed (the classifier
is additive — it must not change any existing clean-prologue install).

**Dependencies.** Phase 2 step 4 (`SafetyhookBackend` — the classifier asks about
safetyhook's allocator ranges among the kcdx-trampoline ranges). The detection is
above the patcher (design §4.3), so it rests on the backend layer existing.

**Design authority.** [`hook-backend-marriage.md §6.1`](../../../design/hook-backend-marriage.md)
— the classification + the kcdx-vs-foreign discriminator are built to the design.

**Disassembler-test / author-burden note.** None — detection is engine-internal;
no author surface. The byte-pattern decoding (E9/FF25) is the engine reading a
prologue it already owns the address of, not an author supplying hex.

**Reference.** [`../context.md`](../context.md) E13.
