# Probe 0.4 finding — the LIVE-functional resolves+works / dead / wrong-target signal in-game (D25)

**Kind:** scratch / verification probe (throwaway). The FINDING + reusable wiring are durable process-output; the probe source itself is removed from the tree post-launch (no residue — `working-artifacts.md`).
**Trust level:** the probe DESIGN + the ground-truth facts below are primary evidence (source read at file:line this session + seed-row facts). **The VERDICT is filled by the manager after the user's launch reads `kcdx-dev.log`** — until then the outcome rows are pre-committed flat (results-driven).
**Date:** 2026-06-05.
**Step:** maintainer-tool verification-engine Phase 0, step 0.4 ([TEST] probe — the live-functional signal). De-risks Phase 3 step 3 (the LIVE check) + the Phase 4 batch plugin BEFORE they are built.

## Question

Does D25's LIVE functional check produce an **observable, DISCRIMINATING** in-game signal — for a known-good DB function-kind row → `resolves+works`, and for a synthetically-broken one → `dead` / `wrong-target`? I.e. resolve the address via the REAL engine path, hash the body at the RESOLVED RUNTIME (live-module) address, compare to the stored `content_hash`; confirm the verdict DISTINGUISHES working code from a wrong/dead target.

## Outcome → meaning map (pre-committed, flat — the manager reads these from `kcdx-dev.log` after launch)

The probe self-reports via the canonical suite signal. The manager greps `kcdx-dev_<ts>.log` for the `ACCEPT-RESULT` / `suite:` lines (the kcdx adapter: `kcdx::test::ReportResult` → the aggregator's `suite: X/Y passing` line IS the repo's `ACCEPT-SUITE`; each row's PASS/FAIL IS an `ACCEPT-RESULT`).

| Outcome (read from `kcdx-dev.log`) | Meaning | Next action (verdict) |
|---|---|---|
| `probe-0.4-good` PASS **and** `probe-0.4-broken-discriminates` PASS | good row → `resolves+works`; the synthetic break → `wrong-target` (hash differs) AND `dead` (off-`.text` RVA) both read distinctly. The live signal EXISTS + DISCRIMINATES. | **ROW 1** — Phase 3 step 3 + Phase 4 build the real check on this live signal. |
| `probe-0.4-good` PASS but `probe-0.4-broken-discriminates` FAIL | the good row resolves+works, but the synthetic break read the SAME verdict (no discrimination) — the check can't tell working from wrong/dead as modeled. | **ROW 2 — STOP.** Re-observe ground truth; re-design the live check before Phase 3 (`results-driven.md`). |
| `probe-0.4-good` FAIL with reason `module_not_mapped` / `db_not_loaded` / `no_content_hash` | the engine-path resolve (refdb content_hash, or `pe::OpenModule` live view) did not produce the inputs the live check needs at the report point. | **ROW 3 — RETURN.** Surface the missing seam (`headless-testable.md` / `design-authority.md`). NOT a faked signal. |
| Neither `probe-0.4-*` row appears in the log (suite total short by 2) | the self-check never ran (dev mode off, or the boot self-report block never reached it). | Re-confirm deploy + `dev_mode = true`; if still absent, the boot-self-report wiring is the seam — RETURN. |

## VERDICT (filled from `kcdx-dev_2026-06-05_11-21-42.log`, launch 2026-06-05)

**ROW 2 input — the discrimination machinery WORKS, but the naive "hash the live body" check is WRONG for a function body. The probe did its job: it killed a bad Phase-3 assumption before it was built.**

Read from the log:
- `probe-0.4-broken-discriminates` = **PASS** — a wrong in-`.text` RVA (`good+0x1000` = 0x71B5A4) read `wrong-target`; an off-image RVA (0x5B2C000) read `dead`; NEITHER read `resolves+works`. **The live check DISCRIMINATES** working code from wrong/dead targets — the verdict machinery is sound.
- `probe-0.4-good` = **FAIL (ROW 2 input / live-vs-ondisk)** — id 1 (`lua_pcall`, function, live_production) resolved cleanly (rva=0x71A5A4, len=130), but the **live body hash read `wrong-target`** — the live image bytes diverge from the on-disk-computed `content_hash`.

**The mechanism (falsifiable, confirmed):** `lua_pcall` is a `live_production` function that genuinely works in-game — yet its body hashed at the *resolved runtime address on the live loaded module* does NOT match the stored `content_hash`. The cause is the on-disk-vs-live gap probe 0.3 §"on-disk-vs-live" flagged and this probe was designed to settle: **the loaded image carries applied relocations / loader patches** (and/or the running body has been detoured — kcdx itself hooks `lua_pcall` every session via the chain, overwriting the prologue with a jump). The on-disk `content_hash` was computed pre-load (no ASLR, no relocations, no detour); the live body is not byte-identical to it. So hashing the LIVE body against the on-disk hash reads `wrong-target` for a genuinely-good row — a false negative.

**CORRECTION (2026-06-05, after reading `src/survival.cpp` with the user):** the probe's first-pass conclusion ("the function-body-hash basis is wrong, re-design the live check") was an OVER-CONCLUSION born of a PROBE-DESIGN error, NOT an engine defect. The probe deliberately hashed the **LIVE loaded image** (`view.baseBytes + rva`) to test D25's "hash the live body" framing — and that read `wrong-target` for `lua_pcall` because **kcdx itself detours `lua_pcall` every session** (the chain-mediated hook overwrites its prologue with a jump), so the live body is not byte-identical to the stored hash. But the SHIPPING check (`survival.cpp::SurvivalCheck`) deliberately hashes the **ON-DISK file**, not live memory (its own comment, line 143: "Read the ON-DISK backing file (NOT live memory — the crux)") — the on-disk `lua_pcall` body has no detour and no applied relocations, so the on-disk hash MATCHES. The existing on-disk check was never broken.

**What the hash check is actually FOR (the user's framing, confirmed against `survival.cpp`):** a **version-applicability gate** — "does the body at this DB-recorded address in the game the user is *actually running* match what the DB recorded?" Run once at **startup over all entries** (NOT the hot path). **Match → the DB entry is valid for this build → safe to apply** (the hook/patch resolves to the right code). **Mismatch → the build diverged from the DB's recorded version → avoid applying** (most useful when the user runs a game version the DB has no row for). `survival.cpp` already implements exactly this; `content_hash` always meant this.

**The real (small) design action — a TRD fix, settled with the user:** the only defect is the TRD's D25 *wording* ("hash the LIVE body"), which the `/design` dialogue wrote wrong. D25 is amended to: **(1) the on-disk hash IS the version-applicability gate** (what `survival.cpp` does + the user stated — startup, all entries, on-disk, match→apply / mismatch→avoid); **(2) a SEPARATE, smaller live check** confirms the resolved address lands in live `.text` at all (a reachability check — catches an entry whose on-disk hash matches but whose live resolve is dead/wrong — NOT a body hash). Two checks, two purposes. Routes to `/design` to amend D25 + the s08 live-verdict framing; Phase 3 then extends `survival.cpp`'s on-disk hash to the other 8 kinds (already the plan).

**What this probe DID validate (still true):** the discrimination machinery works — `probe-0.4-broken-discriminates` PASS confirmed a wrong RVA reads `wrong-target` and an off-image RVA reads `dead`, distinct from a match. The in-game signal is observable + readable. Only the probe's *choice to hash the live image* (D25's mis-framing) produced the false `wrong-target` on the good row — corrected here, not propagated to Phase 3.

---

## The probe design (what it does, and the ONE load-bearing deviation from the step doc's framing)

### Load-bearing finding — the live resolve is ENGINE-INTERNAL, so the probe is an ENGINE-SIDE boot self-check, NOT a `test-plugins/` C++ DLL

The step doc framed this as "a throwaway suite-gated probe **plugin** in `test-plugins/`." **The live engine-path resolve is not reachable from a plugin.** Every primitive D25's live check needs is an engine-internal symbol with NO `include/kcdx/` export:

- `kcdx::refdb::ResolveById` (→ `content_hash` raw 32 bytes + `length`) — `src/refdb.h`, engine-internal.
- `kcdx::pe::OpenModule` / `ModuleView` (the LIVE loaded image of WHGame.dll) — `src/pe_helpers.h`, engine-internal.
- `kcdx::blake3::Hash256` (the body hash) — `src/blake3.h`, engine-internal.
- `kcdx::refdb::WhgameBase()` (RVA→VA) — `src/refdb.cpp`, file-static (not even header-exported).

A `test-plugins/` C++ DLL compiles against `include/kcdx/` only — it CANNOT see any of these. This is the SAME constraint that already forced `cap-59` (blake3) and `cap-60` (survival cache + pass) to live as ENGINE self-tests (`src/blake3_selftest.cpp`, `src/version_check_selftest.cpp`), reporting from engine code via `kcdx::test::ReportResult`. Their headers state the reason verbatim ("X is an engine-internal symbol, not a plugin export — so it self-reports from ENGINE code").

So this probe is built in the EXACT same shape: an engine-side `RunSelfTestOnce()` wired into the boot self-report block in `src/hooks.cpp` (next to the `cap-59` / `cap-60` calls at ~line 676–687), reporting two rows through the suite. It is still **boot-only** (no hook-fire / "ready" dependency — refdb is Open() at worker-thread boot, WHGame.dll is mapped, the hash is deterministic), still self-reports the canonical signal, still throwaway.

This deviation does NOT change the probe's QUESTION or its outcome map — it is the in-game live signal, read the same way. It is surfaced here (not silently resolved) because the step doc's "plugin" framing is not buildable as written; the engine-self-test shape is the only way to reach the live engine path, and it is the established precedent for exactly this. The probe FILES live at `test-plugins/probe-0.4-live-functional/` per the step's naming (the `.cpp`/`.h` pair + this finding's pointer), and the manager wires the one call into `hooks.cpp` for the probe launch, then removes both the files and the call post-finding (no residue).

### What the probe resolves + hashes (the live check, mirroring survival.cpp but at the LIVE address)

`survival.cpp::SurvivalCheck` hashes the **on-disk** file (it reads `WHGame.dll` from disk via `RvaToFileOffsetOnDisk`, deliberately — the stored `content_hash` was computed on-disk, no ASLR). D25's LIVE check instead hashes at the **resolved runtime address on the live loaded module**. So the probe does NOT call `SurvivalCheck`; it reuses survival's hashing IDEA (`blake3::Hash256` over the span) applied to the live `ModuleView`:

1. `pe::OpenModule(L"WHGame.dll", view)` → the live image (`view.baseBytes` = relocated in-process bytes; `view.base` = the HMODULE base).
2. `refdb::ResolveById(GOOD_ID)` → `{ rva, content_hash (raw 32B), length, kind }`.
3. Live body span = `view.baseBytes + rva`, length = `length`; `blake3::Hash256(span, length, got)`.
4. Compare `got` to the row's `content_hash`.

NOTE on the on-disk-vs-live caveat (probe 0.3 §"on-disk-vs-live"): the live image carries APPLIED RELOCATIONS, the on-disk `content_hash` does not. For a pure-`.text` FUNCTION body with no RIP-absolute relocations inside the hashed span, the live bytes == on-disk bytes and the hash MATCHES. KCD2's WHGame.dll x64 code is RIP-relative (no in-`.text` absolute relocations in a normal function body), so a function-kind body hash is expected to match live == on-disk. **This is itself part of what the probe confirms** — if `probe-0.4-good` reads `wrong-target` (hash differs) for a genuinely-good row, the finding is that the live span carries relocations the on-disk hash doesn't, i.e. D25's live check must hash on-disk (like survival.cpp) OR normalize relocations — a real Phase-3 design input, surfaced by ROW 2.

### Row A — `probe-0.4-good` (GOOD row → expect `resolves+works`)

- **GOOD row: `kcdx_id = 1`, name `lua_pcall`, kind `function`, `rva = 0x0071A5A4`, evidence `live_production`** (`data/seeds/address_versions_seed.csv` line 2; `address_names_seed.csv` id 1).
  - WHY this row: kind=`function` (the simplest, body-hash-checkable kind — the step's instruction); `live_production` evidence (the engine HOOKS it every session, so it genuinely resolves to real working code today); it is id 1 (an early-verified entity). Its `content_hash` + `length` live in the DB (`address_versions.content_hash` / `length`) and are returned by `refdb::ResolveById(1)` (`NameResolution`/`IdResolution.content_hash` + `.length`, raw 32-byte blob + span). The seed CSV does NOT carry `content_hash` (it's a DB-only column populated at baseline by the bulk function-row promote — `fingerprint-per-kind.md` §function: "already populated at baseline … Maintainer never hand-authors it"); the GROUND TRUTH comes from the DB via refdb, not the CSV.
- **Live check:** OpenModule → ResolveById(1) → hash `[base+rva, +length)` on the live image → compare to the row's `content_hash`.
- **PASS (resolves+works) iff:** ResolveById(1) found, content_hash present (32 bytes) + length > 0, the live body hash == stored content_hash.
- **FALSIFIABLE:** FAIL if ResolveById misses / content_hash empty (`no_content_hash` → ROW 3), module not mapped (`module_not_mapped` → ROW 3), or the hash DIFFERS (which, for a genuinely-good live-production row, means the live-vs-on-disk relocation gap is real — ROW 2 input).

### Row B — `probe-0.4-broken-discriminates` (SYNTHETIC break → expect `dead` AND `wrong-target`, both distinct from `resolves+works`)

The synthetic break is **IN THE PROBE, not the DB** (no seed edit — AP18). It runs TWO synthetic-break sub-resolves against the SAME good row's stored content_hash and asserts BOTH read a verdict DISTINCT from `resolves+works`:

- **Break 1 — wrong-target (hash differs):** hash the live body at a DELIBERATELY-WRONG RVA inside `.text` (`good_rva + 0x1000`, still in the executable image but a different function body), compare to the GOOD row's stored content_hash. EXPECT: hash DIFFERS → `wrong-target`. (A different function's body cannot BLAKE3 to lua_pcall's recorded hash.)
- **Break 2 — dead (off-`.text`):** point at an RVA outside the module image (`view.size + 0x1000`, past the mapped image end), attempt the live read. EXPECT: the span is not inside the image → `dead` (the read is refused / out-of-range — the probe bounds-checks `rva + length <= view.size` before reading, exactly as survival.cpp bounds-checks against the file size; an out-of-range span yields the `dead` verdict, never a crash, never a fabricated hash).
- **PASS (discriminates) iff:** Break 1 reads `wrong-target` (hash differs from the good hash) AND Break 2 reads `dead` (off-image), i.e. NEITHER synthetic break reads `resolves+works`. This is the FALSIFIABLE discrimination claim — the check tells working code from wrong/dead.
- **FALSIFIABLE:** FAIL if EITHER synthetic break reads `resolves+works` (the check fabricated a pass for a wrong/dead target — no discrimination → ROW 2 STOP), OR if Break 2's off-image read is not cleanly refused (a crash/garbage instead of a clean `dead` → the bounds-check is the Phase-3 seam to harden, surfaced).

### The canonical-signal lines the probe emits (what the manager greps)

The probe calls `kcdx::test::ReportResult(row, pass, reason)` + `EmitSummaryIfChanged("probe-0.4 live-functional")` (same as cap-59/60). The aggregator rolls them into the dev-log:

- `ACCEPT-RESULT: PASS probe-0.4-good` — the good row resolved + the live body hash matched the stored content_hash (`resolves+works`).
- `ACCEPT-RESULT: PASS probe-0.4-broken-discriminates` — the synthetic wrong-RVA read `wrong-target` (hash differs) AND the off-image read `dead`, both distinct from `resolves+works`.
- `ACCEPT-SUITE: <n>/<total> passing` (the kcdx `suite: X/Y passing` line) — both probe rows green ⇒ ROW 1.

(kcdx's native suite signal is `suite: X/Y passing` + per-row `FAIL <row>:` lines; that IS the repo's bound form of the canonical `ACCEPT-SUITE` / `ACCEPT-RESULT` grammar — `acceptance-signal.md` + `agent-builds-and-deploys.md` §4. The manager greps the `suite:` line + the two `probe-0.4-*` rows.)

## Declared mode: BOOT-ONLY

No console gesture, no in-game gesture, no save load. The user's only action is Launch → reach main menu → tell the manager it ran → Quit. The self-check fires from the boot self-report block (no hook-fire / "ready" dependency — refdb is Open() and WHGame.dll is mapped at that point); the manager reads the verdict from `kcdx-dev.log`.

## Surfaced to the caller (per `deferral-authority.md` / `design-authority.md` — not the probe's to decide)

- **The "plugin" → "engine-side self-test" shape change** (above) is the one deviation from the step doc's literal framing. It is forced by the engine-internal reachability of the live resolve (refdb/pe/blake3 are not plugin exports), and it is the established precedent (cap-59/cap-60). Surfaced for visibility; it does not change the probe's question, signal, or outcome map. If the user wants the probe to instead live as a `test-plugins/` DLL, that would require EXPOSING the refdb/pe/survival live-resolve path through `include/kcdx/` — a Phase-3 design decision (a new plugin-facing verification interface), not this probe's to make.
- **The on-disk-vs-live relocation question** (above) is a real Phase-3 design input the probe's good-row result informs: if the good row reads `wrong-target`, D25's live check must hash on-disk (like survival.cpp) or normalize relocations. The probe surfaces it; Phase 3 decides.
- **No seed CSV edited; the synthetic break is in the probe** (AP18 respected). **No build run, no deploy — the manager does that and hands the user the launch** (`agent-builds-and-deploys.md`).
