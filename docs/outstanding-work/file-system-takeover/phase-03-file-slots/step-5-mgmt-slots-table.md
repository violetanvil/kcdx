# Step 3.5 — pak-mgmt / search-path / delete slots + finalize the table

**What.** Build the remaining kcdx-owned slots as real KCDX impls and FINALIZE the
per-slot declarative table: pak/archive-management — AddPakToValidate (7),
pak-membership (17), FindPakByCRC (32), GetPakInfo (33) + free (34), OpenPack/mount
(71), TestArchive (72), GetPakPriority (91), ClosePakByIndex (100); search-path/
alias/mods — AddMod (19), RemoveMod (20), GetMod (21), SetAlias (22), alias-insert
(23), GetAlias (24), RegisterSystemSearchPath (94); delete/copy — RemoveFile (49),
RemoveDir (50), CopyFile (52). Then finalize the table (design §4.3): every
remaining pure-internal slot (pool 60/61/98/99, CRC/MD5 81/82/83, %USER% 77,
dir-casing/MakeDir 28, the dtor 0, thin config getters) is explicitly a
`THUNK(original)` row — the reversibility property (each is a one-line flip to
KCDX).

**Scope.** Flip the table rows for the mgmt/search-path/delete slots to KCDX +
implement them (pak-management answers from kcdx's mounted-pak state / the index;
search-path/alias register into kcdx's own structures); then audit the FULL
102-row table — every slot is exactly one of KCDX or THUNK, no slot unaccounted,
no code outside the table assuming a slot's owner (the §4.3 invariant). One commit.

**Design authority.** Built to `docs/design/file-system-takeover.md` §4.5 (the
mgmt/search-path/delete slots), §4.3 (the per-slot table finalized — the
reversibility invariant: the table is the single point of slot ownership, no
external code assumes "slot N is the engine's"). Builds to those sections
(`.claude/rules/spec-conformance.md`).

**Test bar.** A regression sub-test: a kcdx-mounted pak is enumerable via
GetPakInfo; a search-path/alias register+lookup round-trips; a delete/copy
operates correctly (falsifiable per-family assertions — `.claude/rules/test-suite.md`).
**The table-completeness assertion**: a test (or a reviewed invariant) confirms all
102 slots have a table row and each is KCDX-or-THUNK — the reversibility property
is structurally present, not assumed. Build green.

**Dependencies.** Steps 3.2–3.4 (the file/read/existence slots — this completes
the kcdx-owned set) + Phase 2 (the index/reader the mgmt slots reference). The
table finalization depends on every prior slot step having set its rows.

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §4.5, §4.3 (the
reversibility constraint — load-bearing); `_research/phase8.5-pak-resolver/front1-full-vtable-surface.md`
(the full 102-slot role map).

**Disassembler-test / author-burden.** N/A — engine-internal slots. A new
Address-Library seed entity for any slot resolved by name is AP18 user-approval-
gated before the row lands.
