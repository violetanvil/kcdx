# §36 pattern author-target: a verified .text-unique entry-prologue AOB for an unhooked function

> **Status: RESOLVED (2026-05-23).** The missing RE datum was minted: a
> verified, `.text`-unique, function-ENTRY-prologue AOB for **luaL_openlibs**
> (Address Library id 1190, RVA 0x01449600), which nothing entry-hooks. The
> 16-byte AOB `48 89 5C 24 08 57 48 83 EC 20 48 8B F9 48 8D 1D` was scanned
> against `WHGame.dll`'s `.text` (EXACTLY 1 match, at the function's own RVA)
> by the now-built `_research/phase8-fix-a/aob_scan.py` (the previously-unbuilt
> uniqueness scanner). It is recorded in the seed CSVs under `data/seeds/`
> (id 1190's notes; the reference DB regenerates from them). `cap-33-author-targets`'s `CAP-33-pattern-by-name` row was repointed
> from the blocked id-1003 prologue AOB (`ui_pump_by_pattern`) to a pattern
> locator on the luaL_openlibs AOB (`openlibs_by_pattern`) + its verified
> signature, so the §36 "author names a target BY AOB PATTERN and hooks it BY
> NAME" proof now resolves end-to-end. The history below is retained as the
> record of why earlier candidates failed.
>
> _Original status (BLOCKED) preserved below for the record._

## 1. What is blocked

`test-plugins/cap-33-author-targets/`'s **`CAP-33-pattern-by-name`** row —
the cornerstones.md §36 "share guarantee" proof in its purest form: an
expert names an AOB site ONCE in `targets.toml`, and a non-expert hooks it
BY NAME (`kcdx.hook{ target = "ui_pump_by_pattern" }`) with no hex and no
hand-written ABI — the pattern resolves the WHERE, the target's `signature`
carries the ABI, both delivered by the bare name.

The row points at the only entry-prologue AOB the repo has verified
`.text`-unique: the 25-byte CGame_per_frame_ui_pump entry prologue
(Address Library id 1003). That target **cannot apply**, because cap-03
hooks the SAME function in production: the detour overwrites the entry
prologue the AOB matches, so a later pattern scan finds 0 matches (the
post-patch-scan caveat, `src/patch_engine.cpp:124-130`). A by-NAME pattern
hook therefore parse-skips / fails at apply.

The four sibling rows that prove the SAME feature's other resolution paths
(engine-tier, prefixed, alias, bytes-by-name) were repointed at verified
`address_id` locators (RVA-resolved, no scan → immune to the
prologue-overwrite problem) so they prove their actual capability and pass.
Only `CAP-33-pattern-by-name` stays on a pattern locator, because the
pattern path IS what it proves — repointing it would silence the row, not
fix it (AP9).

## 2. Why reuse is exhausted

The reuse-first ladder (`reverse-engineering.md`) has been walked and turns
up no usable AOB:

- The phase8-fix-a lua RVAs (seed ids 1100–1205) carry **NO entry-prologue
  AOB patterns**. They were identified by string-anchor xref walks +
  call-graph bootstrap from anchors; relocation-aware prologue-AOB matching
  was DEMOTED to a weak cross-check, OUTVOTED by those stronger signals
  (`_research/phase8-fix-a/README.md:83-85`). The reason is that PGO builds
  diverge from a local /O2 build at the *instruction* level inside
  prologues — WHGame's `lua_pcall` prologue has an extra `33 C0` the
  profile emitted, shifting all subsequent bytes, and reloc-aware masking
  cannot recover that because it is a real instruction, not a linker patch
  (`README.md:64-81`). So the lua-leaf seed rows carry body-shape evidence
  and RVAs, not entry AOBs.
- The only lua function with a validated unique entry AOB is `lua_pcall`
  (seed id 1000), whose AOB was re-verified on this build
  (`_research/phase8-fix-a/README.md:22-24`) — but kcdx **production-hooks
  it** (`src/hooks.cpp:37 + 257`), so it has the same prologue-overwrite
  problem as id 1003.
- This is NOT a case of the AOBs being "deliberately not validated" — they
  were validated against a different, stronger signal set; an entry AOB was
  simply not the identification method and was not recorded per row.

So no verified `.text`-unique entry-prologue AOB for an UNHOOKED function
exists in the repo today.

## 3. Concrete next step (bounded tier-5)

Mint ONE verified `.text`-unique entry-prologue AOB for a function that
nothing entry-hooks:

1. **Pick an unhooked verified leaf.** `lua_toboolean` (seed id 1124, RVA
   `0x00B9C1AC`, sig `i32 (ptr L, i32 idx)`) is a reasonable candidate — it
   is verified, and nothing entry-hooks it (production hooks `lua_pcall`
   id 1000 + `CGame::Update`; cap-03/cap-33 hook
   `CGame_per_frame_ui_pump` id 1003). Its entry bytes begin
   `48 83 EC 28 E8 ...` (read directly from `WHGame.dll`).
2. **Read its ACTUAL entry bytes** from `WHGame.dll` (the pre-analyzed
   Ghidra project at `third-party-ghidra/`, or a `pefile + capstone` read —
   file offset for a `.text` RVA = `rva - 0x1000 + 0x400`).
3. **Take a short prologue slice** up to the first variable/reloc byte
   (the `E8` rel32 call target after `48 83 EC 28` is reloc/variable — slice
   before it, masking any non-fixed bytes).
4. **Confirm `.text`-uniqueness** by scanning all of `WHGame.dll`'s `.text`
   with capstone and asserting EXACTLY ONE match. NOTE: there is **no
   ready `aob_scan.py`** — `_research/phase8-fix-a/README.md:101` lists it
   as an unbuilt "when populated" deliverable. The uniqueness scan is work
   to WRITE, not run.
5. **Record the verified AOB** in that seed row's notes under `data/seeds/`
   (the reference DB regenerates from the seed rows — no in-source mirror to
   sync, per `address-library.md`).

## 4. Revisit trigger

When the verified `.text`-unique entry AOB is minted →
`CAP-33-pattern-by-name` flips to a real by-name pattern hook on that
unhooked function and goes green. At that point:

- Repoint `cap-33-author-targets/targets.toml`'s `ui_pump_by_pattern`
  (or add a new pattern target) at the newly-minted AOB + its signature.
- Drop the `[manual]`/blocked marker on the `CAP-33-pattern-by-name` row in
  `test-plugins/README.md`.
- Remove the BLOCKED notes from the cap-33 fixtures.

## 5. Why deferred (not cut)

This IS in-scope capability (the §36 share guarantee is the disassembler
test's sharpest edge — `cornerstones.md`). It is deferred only because the
missing piece is a verified RE datum that must be minted with a uniqueness
scanner that does not yet exist (`README.md:101`), not because the engine
path is incomplete. The engine resolves a pattern author-target by name
today; it simply has no unhooked entry AOB to resolve. Minting one is
bounded, mechanical RE work — gated on writing the scanner, not on any
design decision.

## 6. Related

- `cornerstones.md` §"The disassembler test" — the share guarantee this row
  proves.
- `_research/phase8-fix-a/README.md` — why reloc-aware prologue AOBs are a
  weak signal on a PGO build (lines 64–85); the unbuilt `aob_scan.py`
  (line 101).
- `src/patch_engine.cpp:124-130` — the post-patch-scan caveat (a hooked
  prologue defeats a later scan of the same pattern).
- `test-plugins/README.md` — the CAP-33 matrix rows.
