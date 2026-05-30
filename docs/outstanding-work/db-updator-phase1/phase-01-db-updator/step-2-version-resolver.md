# Step 2 — `.rdata` version resolver

**What.** Add `seeds_shared/version_resolver.py`: given a linked DLL file,
resolve its game version by scanning every `.rdata` section for the
`release_M_N_BUILD_SUB` form and applying a hard intern-agreement check. This is
the per-module version mechanism R12 specifies — an alternative to the importer's
existing `whdlversions.json` path, not a replacement (the JSON path stays valid).
The `apply` mode (step 3+) uses this to resolve the target version of an edit.
See [`../context.md`](../context.md) decision 5, `plan.md` §7.

**Scope (commit-grain).**
- New `seeds_shared/version_resolver.py` with `resolve_version(dll_path) ->
  (tag, ordinal)`:
  1. Open with `pefile` (already an importer dependency).
  2. For each `.rdata` section, scan raw bytes with
     `rb"release_(\d{1,3})_(\d{1,3})_(\d{4,8})_(\d{1,4})"`.
  3. Collect all matches with `(M, N, BUILD, SUB)` + `.rdata` VA.
  4. < 2 matches → raise/refuse ("expected ≥2 interns; found N").
  5. Matches disagree → refuse; surface each match's VA + value.
  6. Agree → return `tag = "{M}.{N}.{BUILD}"`, `ordinal = BUILD`.
- No wiring into `apply` yet (that is step 3) — this step delivers the resolver +
  its test only.

**Disassembler test.** The author/maintainer supplies a DLL *file path*, not an
offset/signature — the resolver derives the version. Compliant (no hex burden).

**Test bar.** Unit test under `data/refdata-extractor/` against the live
`WHGame.dll`: asserts it resolves `1.5.1164953` / ordinal `1164953` and finds
≥2 agreeing interns (the two verified at va=0x183c3edef and va=0x183dba258,
`plan.md` §7). Plus a negative test: a crafted/mismatched-intern input is
refused.

**Dependencies.** Step 1 (lives in `seeds_shared/`). Functionally independent of
the row-builder, but lands after the package exists.

**Reference.** [`../context.md`](../context.md);
[`data/maintainer-tool/plan.md`](../../../../data/maintainer-tool/plan.md) §7;
R12 in [`data/maintainer-tool/requirements.md`](../../../../data/maintainer-tool/requirements.md).
