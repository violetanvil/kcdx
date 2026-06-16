# Step 3.3 — existence/metadata + directory-enumeration slots

**What.** Build kcdx's existence/metadata + directory-enumeration slots as real
KCDX impls: existence/metadata-by-name — IsFolder (13), GetFileSize (45),
IsFileExist 3-arg (67), GetFileAttributes (68), GetFileStat (69), IsFileExist
2-arg (70), GetFileSizeOnDisk (92), GetFileSizeCompressed (93); directory
enumeration — ForEachFile (14) + its per-file callback (15), FindFirst (101, the
`CCryPakFindData` factory). Each answers from the unified index (a vpath's
existence/size is the index entry; enumeration walks the index's vpaths for a
prefix) rather than the engine's per-call search-path walk.

**Scope.** Flip the table rows for these slots from `THUNK` to `KCDX(&…)` and
implement them against the unified index. One commit. These are the by-name
surfaces beyond open/read — owning them is why the takeover is the full vtable, not
a FOpen-only hook (the recon's front-1 finding: a FOpen-only hook misses the 9
other by-name surfaces).

**Design authority.** Built to `docs/design/file-system-takeover.md` §4.5 (the
existence/metadata + enumeration slot set), §5 (they answer from the index), §4.3
(flip these rows). Builds to those sections (`.claude/rules/spec-conformance.md`).

**Test bar.** A regression sub-test: IsFileExist/GetFileSize return correct answers
for a vanilla vpath, a loose override, and a non-existent vpath; ForEachFile/
FindFirst enumerate the expected set for a directory prefix (falsifiable
assertions over the index — `.claude/rules/test-suite.md`). Build green.

**Dependencies.** Phase 2 step 2.4 (the index these read) + step 3.2 (the
resolution slot they share the lookup with). Independently verifiable once the
index is live.

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §4.5, §5;
`_research/phase8.5-pak-resolver/front1-full-vtable-surface.md` (the existence/
enumeration slot roles).

**Disassembler-test / author-burden.** N/A — engine-internal slots.
