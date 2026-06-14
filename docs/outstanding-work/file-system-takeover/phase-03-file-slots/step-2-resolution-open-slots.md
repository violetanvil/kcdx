# Step 3.2 — slot-1 AdjustFileName + open slots

**What.** Build kcdx's resolution + open slots as real KCDX impls in the per-slot
table (replacing the Phase-1 stub thunks for these slots): slot 1 `AdjustFileName`
(the unified-index lookup — design §4.5, §5) and the open slots 36 `FOpen`, 35
`FOpenRaw`, 38 `FOpen-by-pak-index`, each minting a kcdx handle in the
representation P3 (step 3.1) settled. This is where the unified index (Phase 2)
becomes the engine's resolution path and kcdx starts minting its own handles.

**Scope.** Flip the table rows for slots 1/35/36/38 from `THUNK` to `KCDX(&…)` and
implement them: slot 1 does the O(1) index lookup (Loose or Pak ByteSource); the
open slots mint a kcdx handle bound to that ByteSource's open state. One commit.
The read family (3.3) operates these handles next; until 3.3 lands, the open slots
mint the handle but the (still-stub-thunked) read slots would read it — so this
step + 3.3 are tightly ordered (3.2 mints, 3.3 reads). To keep 3.2 independently
verifiable, its test asserts the OPEN + the minted-handle shape, not a full read
(that is 3.3's bar).

**Design authority.** Built to `docs/design/file-system-takeover.md` §4.5 (the
resolution + open slots), §5 (slot 1 = the index lookup), §4.4 (the handle the
open slots mint), §4.3 (the per-slot table — flip these rows to KCDX). Builds to
those sections, not this summary (`.claude/rules/spec-conformance.md`).

**Test bar.** A regression sub-test: a vpath resolves through kcdx's slot-1 to the
correct ByteSource and FOpen mints a kcdx handle of the P3-settled shape (a
falsifiable assertion on the resolution + handle — `.claude/rules/test-suite.md`).
Build green. A launch confirms slot-1/FOpen serve a known vpath (agent-read).

**Dependencies.** Step 3.1 (the handle representation) + Phase 2 step 2.4 (the
index slot 1 looks up). Ordered before 3.3 (3.3 reads the handles 3.2 mints).

**Disassembler-test / author-burden.** N/A — engine-internal slots. Game-binary
targets (the slot RVAs, the vtable) resolve by name/id through the Address
Library; any new seed entity is AP18 user-approval-gated before the row lands.
