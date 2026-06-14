# Step 3.3 — read family on kcdx CRT

**What.** Build kcdx's read-family slots as real KCDX impls: FReadRaw (39),
FGetCachedFileData (40), FWrite (41), FGets (43), FGetc (44), FUngetc (47), FSeek
(53), FTell (54), FClose (55), FEof (56), FError (57), FGetErrno (58), FFlush (59),
+ the fileno variants (46/66). Each operates the kcdx handle (minted by 3.2)
ENTIRELY on kcdx's CRT — for a Loose source, kcdx's `fread`/`fseek`/`fclose` on the
kcdx `FILE*`; for a Pak source, kcdx's pak reader (Phase 2) seeking+inflating. The
engine never operates a kcdx handle. **This is where the cross-CRT crash class
(KI-0019/KI-0006) dies** — there is no longer any point at which the engine's
`ucrtbase` touches a kcdx handle.

**Scope.** Flip the read-family table rows from `THUNK` to `KCDX(&…)` and
implement them against the kcdx handle representation + the Phase-2 reader. One
commit. After this step, kcdx owns the full open→read→close lifecycle for every
handle it mints.

**Design authority.** Built to `docs/design/file-system-takeover.md` §4.5 (the
read-family slots), §4.4 (kcdx serves every read on its own CRT — the engine never
operates the handle), §9 (this removes the cross-CRT class structurally), §4.3
(flip these table rows to KCDX). Builds to those sections
(`.claude/rules/spec-conformance.md`).

**Test bar.** A regression sub-test: a full open→read→close on a Loose source AND
a Pak source returns the correct bytes, all on kcdx's CRT (a falsifiable
end-to-end read — `.claude/rules/test-suite.md`). **The KI-0019 acceptance row**:
the repro (load save → enter world → open inventory) runs clean — no `FAULTED …
hook=engine.ccrypak_*`, no `ucrtbase` frame operating a kcdx handle (agent-read,
`kcdx-dev.log`). Falsifiable: FAILS if the inventory-open crash reproduces, or a
read returns wrong bytes. Build green.

**Dependencies.** Step 3.2 (the open slots mint the handles this reads) + Phase 2
(the reader this calls for a Pak source). Tightly ordered after 3.2.

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §4.5, §4.4, §9 (the
KI-0019/KI-0006 resolution mechanism); [KI-0019](../../../known-issues/KI-0019-ccrypak-fopen-reentrancy-av-on-inventory-open.md)
(the repro this clears).

**Disassembler-test / author-burden.** N/A — engine-internal slots.
