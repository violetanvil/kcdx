# Step 3.2 — open + read cutover (one atomic flip)

**What.** Cut the resolution + open + read slot families over from the Phase-1
stub thunks to real KCDX impls in ONE atomic step, and wire `BuildAssetIndex`
into the seating path so the index the slots read actually exists at runtime.
This is where the unified index (Phase 2) becomes the engine's resolution path,
kcdx mints its own handle-ids, and **the cross-CRT crash class (KI-0019/KI-0006)
dies** — there is no longer any point at which the engine's `ucrtbase` operates a
kcdx handle.

**Why 3.2 + 3.3 are ONE step (merged, user-decided).** The open slots mint kcdx
**handle-ids** (the P3-settled representation, step 3.1). A handle-id is opaque to
the engine's CRT — only kcdx's own read slots can operate it. If the open slots
flipped to KCDX while the read family stayed THUNK, a thunked read slot would
`fread`/`fseek`/`fclose` a kcdx handle-id on the ENGINE's CRT — the exact
cross-CRT straddle this takeover exists to remove (the invariant in
`../plan-spec.md` §"Cross-step invariants"). Open and read therefore flip
TOGETHER, in one cutover; there is no intermediate state where a kcdx handle-id
is reachable by a thunked slot. (Original decomposition split these as 3.2 mints /
3.3 reads; the handle-id representation made the split unsafe — merged per the
RESUME's recorded decision.)

**Scope.** One commit:

- **Wire `BuildAssetIndex` into the seating path.** Today `seating_hook.cpp` only
  swaps the vtable; the index is not built. Call `BuildAssetIndex` in the seating
  sequence (after the swap seats, before the first file call the slots serve) so
  slot 1 has an index to look up.
- **Resolution + open — flip slots 1 / 35 / 36 THUNK→KCDX:**
  - slot 1 `AdjustFileName` — the O(1) unified-index lookup (Loose or Pak
    ByteSource), the path-resolution chokepoint (design §4.5, §5).
  - slot 36 `FOpen` (seed id 131) + slot 35 `FOpenRaw` (seed kcdx_id 160) — each
    mints a kcdx handle-id bound to the resolved ByteSource's open state (design
    §4.4, §4.5).
- **Read family — flip slots 38 / 39 / 40 / 41 / 53 / 54 / 55 / 56 (+ the
  variants 43/44/46/47/57/58/59/66) THUNK→KCDX:** each operates the kcdx handle-id
  ENTIRELY on kcdx's CRT — for a Loose source, kcdx's `fread`/`fseek`/`fclose` on
  the kcdx `FILE*`; for a Pak source, kcdx's pak reader (Phase 2) seeking +
  inflating. **Slot 38 is `FReadRaw-by-pak-index`, a READ, not an open** (slot-map
  recon, `4ca0bae`) — it flips here, with the read family, not with the open
  slots.

The per-slot table (design §4.3) is the single point of slot ownership; flip
exactly these rows to KCDX and leave the rest THUNK.

**Design authority.** Built to `docs/design/file-system-takeover.md` §4.5 (the
resolution + open + read slots, as corrected v1.5 — slot 38 in the read family,
slot 35 ABI recorded), §5 (slot 1 = the index lookup), §4.4 (the handle-id the
open slots mint + kcdx serves every read on its own CRT), §9 (this removes the
cross-CRT class structurally), §4.3 (the per-slot table — flip these rows to
KCDX). Builds to those sections, not this summary
(`.claude/rules/spec-conformance.md`).

**Test bar.** A permanent regression plugin — next free **cap-113** — exercising
the full open→read→close lifecycle end-to-end on BOTH source kinds: a vpath
resolves through kcdx's slot-1 to the correct ByteSource, FOpen/FOpenRaw mint a
kcdx handle-id of the P3-settled shape, and a full read returns the correct bytes
on kcdx's CRT, for a Loose source AND a Pak source (a falsifiable end-to-end
read — `.claude/rules/test-suite.md`). This also satisfies the OWED test plugin
for CCryPak_FOpenRaw (kcdx_id 160, the slot-35 exercise — `data/maintainer-tool/policy.md`).
Build green. **The KI-0019 acceptance row**: the repro (load save → enter world →
open inventory) runs clean — no `FAULTED … hook=engine.ccrypak_*`, no `ucrtbase`
frame operating a kcdx handle (agent-read, `kcdx-dev.log`). Falsifiable: FAILS if
the inventory-open crash reproduces, or a read returns wrong bytes.

**Dependencies.** Step 3.1 (the handle-id representation these slots mint +
operate) + Phase 2 (the unified index slot 1 looks up + the pak reader the read
family calls for a Pak source). The open and read families have a hard internal
dependency on each other (a minted handle-id must be operable only by kcdx), which
is WHY they are one step.

**Disassembler-test / author-burden.** N/A — engine-internal slots. Game-binary
targets (the slot RVAs, the vtable) resolve by name/id through the Address
Library; the slot-35 entity (kcdx_id 160) is already AP18-approved + seeded.
