# Maintainer tool — design changelog

Newest-first. Tracks revisions to [`design.md`](design.md) (the TRD) and the UI design
layer [`ui/design.md`](ui/design.md) + [`ui/screens/`](ui/screens/).

## 2026-06-08 — audit-trio identity + verified_date model (D17a / D17b; US-3)

The s04 milestone UAT surfaced two audit-trio corrections. (1) `verified_by` is a free-text
field and the FE sends NO author identity on confirm, so D17's "the commit author and the
audit-trio signer are one identity" is violated on the FE side (the backend always commits as the
configured identity regardless of the typed signer). (2) `verified_date` is hand-editable and
always shown, even on an unverified row — but it is a SYSTEM fact (WHEN verification happened),
not a maintainer choice. Settled:
- **D17a** — `verified_by` is **prefilled from the resolved identity, overrideable, and SENT as
  the confirm `author_name`** (the `_AuthContext`/`X-Kcdx-Author-Name` seam D17 built), so the
  signer becomes the git commit author. Prefill source is the configured `/health` identity today;
  a login portal later injects the per-session identity into that surface (the seam unchanged, the
  portal its own feature). The user chose overrideable (on-behalf / correction) over the stricter
  read-only-locked identity.
- **D17b** — `verified_date` is a **read-only SYSTEM value**: set to today on verify, never
  hand-typed, and **shown only when the row is verified** (`last_verified_at_version` non-empty).
  It leaves the maintainer-editable set; the all-or-null trio coupling sets/clears it with the rest.
  Supersedes the prior overrideable-default-today behavior.

**Integrated in:** §6 US-3, §10 D17a + D17b (new rows). The s04 screen-spec render revisions are in
the UI changelog ([`ui/changelog.md`](ui/changelog.md)).
**Why:** honor D17's one-identity intent on the FE (wire the signer→commit-author seam) + make
`verified_date` a trustworthy system fact (no back-dating, no clutter on unverified rows).

## 2026-06-05 — D30 revised: the DLL link is a Bin-FOLDER pick (the install-set); multi-store dropped from scope

The milestone UAT of the s02 link table surfaced that the maintainer wants to verify addresses in
ANY game DLL (the CryEngine modules — CrySystem.dll etc.), not just WHGame.dll. A
`senior-architect-consult` on the related store-version question grounded that verification stays
byte-correct on any binary (the content_hash check, not the version string, is the verification),
so the store question is separable from the multi-DLL one. Settled:

- **D30 — the install-set model.** The link is now a **single in-session game Bin FOLDER pick**
  (`<input webkitdirectory>` — portable, read in-page, never uploaded), replacing the per-DLL pick.
  The tool resolves the install version from WHGame.dll and finds every referenced module's DLL by
  filename in that folder; a **non-WHGame module inherits the install's version from WHGame.dll**
  (the CryEngine DLLs carry no KCD2 version string). The folder is re-picked each session (no
  persistence — D30 still rejects File-System-Access *persistence*, only the in-session pick is
  used). Degraded states never block: no folder / no WHGame.dll / a module's DLL not found. A new
  CryEngine module is registered as a surfaced step (the AP18 deliberate-addition posture).
- **§9 — multi-STORE dropped from scope.** Steam/Epic/GOG/Game Pass may ship different binaries at
  the same version (the DB is the Steam PGO build — `SteamPGO` token), but **whether they actually
  differ is UNVERIFIED** (no non-Steam binary checked). Per `results-driven.md` the store dimension
  is NOT designed on the un-probed assumption. The content_hash keeps verification correct + fails
  safe regardless. Revisit trigger: a non-Steam no-op report → probe a non-Steam binary → only a
  confirmed divergence warrants a store/build discriminator.

**Integrated in:** §10 D30 (revised); §6 US-11 (acceptance — the folder-pick install-set, the
new-module step, the multi-store out-of-scope note); §9 (the multi-store deferral + its revisit
trigger); `ui/screens/s02-entity-detail.md` §"The version & verify surface" + §"Verify states"
(the folder-pick affordance, the per-module not-found/match/mismatch states, the install-version-
inherited indicator, the explicit no-reflow law-1 note).
**Why:** the milestone UAT (step 2.5) rejected the per-DLL link model — the maintainer needs to
verify any game DLL, and the reflow bug showed the verify line must hold its layout. The install-set
(one folder pick = the install, all DLLs share the game version) is the store-agnostic model that
serves "link any game DLL" without a store dimension; multi-store is a separate, unverified concern
deferred with a probe-gated revisit trigger.

## 2026-06-05 — batch-verify flow settled end-to-end (D33–D35; D28/D29/D32/US-11/§7/s08 revised)

A full end-to-end design pass on the in-game sweep → JSON report → import → bulk re-verify loop,
before Phases 4–5 build the producer plugin + the report ingestion. Pieces existed (D28 producer,
D32 batch mutation, s08 worklist); this settles the seams between them. Three new decisions + the
attribution/interval-honesty model none of them spelled out:

- **D33 — sweep gating + scope.** The batch plugin is dev-mode-gated, runs once at engine startup
  (a normal suite-gated test, self-skips for a player), and sweeps the **curated USER set only**
  (the `kcdx_id` rows — the worklist scale), NOT the ~321k DEV bulk discovery rows.
- **D34 — attribution + gap-pass extension.** The sweep matches the swept bytes against each
  candidate `address_version` row's fingerprint and reports the **matched `address_version_id`**.
  A passing version that fell in a GAP between an entity's intervals is attributed to the matched
  row, and a verify-all **extends that row's `valid_through` forward** to the swept version
  (the 1.4-in-the-gap-of-id-1 case). Still an UPDATE — a passing check found the bytes unchanged.
- **D35 — two-block worklist; a failure CLOSES the over-claimed interval.** The import shows a
  reviewed diff (never auto-write): a **verified block** (verify-all) and a **failing block**
  (close-intervals → each failed row's `valid_through` retracts to its `last_verified_at_version`,
  the last version it passed). A failure needs **no "failed" field** — not advancing
  `last_verified_at_version` already reads UNVERIFIED by the existing derivation
  (`data/seeds/policy.md`); the seed schema is unchanged.
- **Report schema (1.2) consequence.** The frozen `report-schema/verification-report.schema.json`
  gains a nullable per-row `matched_address_version_id` (D34) → a `schema_version` bump 1 → 2. The
  schema FILE edit is a build follow-on (a `/plan`/`/execute` task), not this design revision; the
  contract change is recorded here so the build picks it up.

**Integrated in:** §10 D28 / D29 / D32 (revised) + D33 / D34 / D35 (new); §6 US-11; §7 (batch
mutation); `ui/screens/s08-verification-worklist.md` (two-block worklist, the close-intervals
action, the matched-id column, snake_case verdict tokens).
**Why:** Phases 4 (producer plugin) + 5 (report ingestion) build to this loop; settling the
attribution + interval-honesty model now (before those phases) keeps the executor from inventing
the load-bearing "which row does an uncovered version belong to" + "what does a failure do to the
interval" semantics at build time (`spec-conformance.md`). The model uses only existing seed
fields (no schema churn against the frozen-schema guarantee) and stays all-UPDATE (D32's no-AP18
holds; new/variant rows are authored per-row via [Fix ▸]).

## 2026-06-05 — survivor sweeps: §5 report-ingestion (D31b) + s08 verdict prose (D25)

Two stale references an independent coverage re-derivation caught after the D25 correction +
the D31 follow-on landed (`deletion-hygiene` — sweeping survivors of already-settled decisions):

- **§5** — the report-ingestion unit was described "(frontend + a backend read seam)", superseded
  by **D31b** (frontend File-API only, NO backend read seam). Swept to match D31b.
- **`ui/screens/s08-verification-worklist.md` §"The verdict (per row)"** — the verdict mapping
  still read in the superseded runtime-body-hash framing ("landed on real working code" /
  "resolved to a body that no longer matches"). Re-grounded to the corrected **D25**:
  `resolves+works` = on-disk hash matches (right version for this build) AND resolves into live
  `.text` (reachable); `wrong-target`/`changed` = on-disk hash mismatches (build diverged);
  `dead` = unreachable in live `.text`; not a runtime "does it still work" body hash.

**Integrated in:** §5 (the report-ingestion unit), `ui/screens/s08-verification-worklist.md`.
**Why:** the s08 screen spec is the build authority a UI step conforms to (`spec-conformance.md`);
leaving the superseded verdict prose would have re-introduced the D25 drift at build time. The §5
line was a pre-existing survivor of D31b's "no backend seam" decision.

## 2026-06-05 — D25 correction: "verify" = version-applicability + reachability, NOT a runtime body hash

Phase-0 probe 0.4 (the in-game live-functional probe) + reading `src/survival.cpp` proved the
original D25 wording wrong and the user settled the correction. D25 framed the in-game "LIVE
functional" check as "the body-hash at the *resolved runtime address* matches." That is wrong:
hashing the **loaded** body of a known-good function (`lua_pcall`) reads a false MISMATCH, because
the loaded image carries applied relocations AND kcdx detours the function every session
(overwrites its prologue). The shipping engine check (`survival.cpp::SurvivalCheck`) deliberately
hashes the **ON-DISK file** ("the ON-DISK backing file (NOT live memory — the crux)") — and that
matches; `survival.cpp` was never broken, the TRD wording mis-specced it.

The corrected model (settled with the user):
- **"Verify" = is this DB entry safe to APPLY on the build the user is actually running** — two
  checks, both run ONCE (per-author in the browser; at engine startup in-game — never during
  gameplay, never the hot path; running either during gameplay would be pointless polling):
  1. **Version-applicability (the hash check)** — does the body at the entry's `rva` match the DB
     `content_hash`, hashed **ON-DISK** (not the loaded image)? Match → valid for this build →
     apply; mismatch → the build diverged → AVOID (self-protect, esp. on a version the DB lacks).
     This is what `survival.cpp` + `fingerprint-per-kind.md` §function already do/specify.
  2. **Reachability (the loaded-image check)** — does the address resolve into live `.text` at
     all? Reads the **live loaded image** (the only sense of "live" — it reads memory, it does
     NOT re-run during gameplay). Catches an entry whose on-disk hash matches but whose live
     resolve is dead/wrong.
- **Verdict vocabulary re-grounded:** `resolves+works` = on-disk hash matches (right version) AND
  resolves into live `.text`; `wrong-target`/`changed` = on-disk hash mismatch (build diverged);
  `dead` = unreachable in live `.text`; `cannot-check` = no hash / non-byte or deferred kind.

**Integrated in:** §10 D25 (rewritten), D27 (both checkers hash on-disk; reachability engine-only),
D28 (report verdict meanings re-grounded); §6 US-11 (the in-bulk-in-game bullet + the acceptance
verdict line); §5 (the engine survival-checker unit).
**Why:** the probe correctly triggered a STOP-and-re-design before Phase 3 built on a wrong
assumption. D24 (un-deferral), D26 (client-side/no-upload — the browser already hashes the picked
DLL *file* on-disk), and D28's report round-trip mechanism are unchanged; only the verify
SEMANTICS are corrected. Re-grounds Phase 3's step docs (the engine extension builds on-disk
hashing + a loaded-image reachability check at startup, not a runtime body hash).
**UI follow-on (flagged, not edited here):** the s08/s04 screen-spec wording frames the verdict
as verification; if any of it reads as "does the code still work" rather than "is this version
applicable + reachable," a `/ui-design` pass should re-ground that copy — surfaced, not edited
from `/design`.

## 2026-06-05 — the save spine supports a BATCH mutation (bulk re-verify, D32)

The `/ui-design` pass for the verification engine surfaced one functional gap the TRD should
own (reconciled here via the `/ui-design` §E rectifying pass — user-approved): the save spine,
designed one-mutation-per-transaction (D5/D16, law 5), also supports a **batch mutation**. The
bulk re-verify (s08, the verification report worklist) commits **N audit-trio UPDATEs as ONE
atomic transaction** with **one batched field-delta confirm**, reusing the spine at batch scale
(same validator gate, same deferred-commit + D21 robust rollback — **all-or-nothing**, one row
failing rolls back the whole batch, one git commit/push). The batch is all-UPDATE (re-verify
never creates a row), so the new-row approval gate (law 8/AP18) does not apply. "One mutation =
one transaction" now reads "one confirmed UNIT = one transaction" (a single edit OR an
explicitly-selected batch).

**Integrated in:** §7 (the save-spine paragraph), §10 D32.
**Why:** s08's batched bulk-re-verify confirm needs a save-spine that batches; without this the
TRD's law-5 "one mutation per transaction" reads against the batch path (an executor builds it
wrong, or a reviewer flags the batch as a law-5 violation). The functional contract belongs in
the TRD, not only the s08 screen spec (`spec-conformance.md`).

## 2026-06-05 — verification-engine follow-on forks (settled by `/plan` decomposition)

Decomposing the verification engine (D24–D30) surfaced three forks `/plan` could not resolve
itself; settled before re-planning (D31):

- **Callsite ambiguity posture** — a callsite AOB matching multiple `.text` sites is an
  advisory `Ambiguous` that STEERS the maintainer to extend the pattern (context bytes until
  unique), never a hard refuse (advisory-never-blocks, D9). Settles `fingerprint-per-kind.md`
  §"Open decisions" 3.
- **Report ingest path** — the frontend reads the in-game plugin's `report.json` directly via
  the File API (in-page parse, no backend seam) — resolves D28's "frontend + a backend read
  seam" to frontend-only, consistent with the client-side / DLL-never-uploaded stance (D15).
- **Ingest progress state** — parsing a report over the full curated set (157+ rows) shows an
  ingesting progress bar; it joins the worklist surface's state set (empty / loading-with-progress
  / populated / error). A first-class loading state, not a silent blocking parse.

`/plan` also HALTED on a `/ui-design` gap: the verification engine's 4 new UI surfaces (the
per-module link table, the per-author check-result display, the report-ingestion worklist, the
link-to-create prompt) have NO screen specs in `ui/screens/` (those cover only the version-READ
control). The user chose to author those screen specs via `/ui-design` FIRST, then re-invoke
`/plan` with the specs as each UI step's build authority (`spec-conformance.md`).

**Integrated in:** §10 D31.
**Why:** the forks are settled design the future `/plan` + `/ui-design` need; the screen specs
are the missing build authority `/plan` cannot invent (a UI step with no screen spec is
incomplete — `ux-first-class.md` / `spec-conformance.md`).

## 2026-06-04 — the verification engine (un-defer R5 + restore R12's DLL link table)

The deferred "driven evidence flows" (R5) and the dropped R12 per-module DLL link table are
**pulled back into scope** as one coherent **verification engine**: the maintainer links a game
DLL on their machine and the tool **verifies what they author against the real binary** — not
record-only audit-trio capture. This supersedes §9's "Driven evidence flows (R5) — out of v1"
and corrects D15's narrowing of R12 to a version-read-only one-shot picker (the built step-11
slice reads only the version tag — the original R4/R5 requirement was to *check the author's
RVA/signature/AOB against the binary*).

The settled architecture (D24–D30):

- **Two verify meanings, split by layer (D25):** STATIC (do the authored bytes/AOB/hash still
  match the DLL *file* — needs no game; runs client-side in the browser for instant per-author
  feedback) vs LIVE FUNCTIONAL (does the address *resolve + work* in the running process — needs
  the game; runs as an in-game kcdx test-suite plugin). The static check catches "the binary
  changed"; the live check catches "resolves to wrong/dead code even if the bytes look right."
- **Client-side JS, no upload (D26):** the per-kind static checks port to JS over the picked
  DLL's ArrayBuffer (WHGame.dll ≈ 86 MB, within browser limits) — the DLL never leaves the
  machine (D15). All 9 kinds designed (defer nothing); `instruction_anchor`/`data_slot` need a
  minimal in-browser x86 decoder (RIP-relative `disp32` follow); `vtable_index` *population*
  stays deferred (needs a verified runtime slot target, `fingerprint-per-kind.md`).
- **Two checkers, engine = authority, JS mirrors (D27):** C++ `survival.cpp` (extended
  function-hash-only → all 9 kinds) is the batch in-game authority; the JS browser check is the
  per-author mirror; a cross-impl agreement test pins them (the `version_resolver.py`
  test-of-record pattern, now at full per-kind scale).
- **Batch = an in-game test-suite plugin → JSON report → fed back into the tool (D28):** the
  plugin runs the LIVE check over every DB row, writes a JSON report; the maintainer imports it
  → a worklist (passing rows → one-click bulk re-verify; failing → flagged), every applied
  verdict through the normal validate→field-delta→confirm→commit spine (advisory, nothing lands
  silently).
- **A passing check determines `evidence_kind` (D29):** in-game live → `live_production`,
  browser AOB-unique → `pattern_scan`, manual → `maintainer_ghidra` — composing with the
  just-built audit-trio auto-fill.
- **Link table = re-pick each session, no persistence (D30):** in-memory current pick per
  module; a no-matching-version DLL → check unavailable + noted, never blocks (degraded, D9); a
  DLL newer than any of the entity's rows offers "add a version row at `<v>`" → the
  create-version flow prefilled (AP18-gated) — linking a new build is the on-ramp to versioning
  the entity forward.

**Integrated in:** §5 (the verification units), §6 (US-11), §9 (un-defer R5 / restore R12),
§10 (D24–D30).
**Why:** the user un-deferred the original requirement — "you load the dll in and it can run
the verification checks on your computer for what you author if you have a matching version."
The built D15 slice only read the version tag; this restores the verify-against-the-binary
capability R4/R5/R12 specified. Large addition; `/plan` sequences it into phases.

## 2026-06-04 — `verified_date` defaults to today (overrideable); s04 field grid

Two UX decisions settled during the Phase-3 live acceptance:

- **`verified_date` defaults to today** (US-3 acceptance amended) — pre-filled on the audit-trio
  edit + on create (US-6/US-1), always overrideable; a UI convenience, never a silent write (the
  value still shows in the field delta). Surfaced in Phase 3; built in Phase 4 (the create flows +
  the audit-trio edit are wired there).
- **s04 field layout: a vertical list → a content-sized responsive grid** (`ui/screens/s04-field-editor.md`
  §"Region & position") — short fields narrow + 2–3 per row, long fields full-width, grouped under
  the existing sub-headings, collapses to one column on phone. Law 1 (the per-field reserved
  dirty/was/error space) holds within each grid cell.

## 2026-06-04 — backend CORS allowlist (the served frontend is a separate origin)

The FastAPI backend now sets a `CORSMiddleware` so the browser-served frontend (a separate
origin — vite preview `:4173` / dev `:5173`, or the operator's production origin) can call it
cross-origin; without it the browser blocks every frontend→backend fetch (no
`Access-Control-Allow-Origin` header). Found during live acceptance. The allowed origins are an
env-configurable allowlist (`KCDX_CORS_ORIGINS`, comma-separated; localhost dev default),
joining the operator-wired seams (D17, alongside `KCDX_CHECKOUT` / `KCDX_PUSH_TOKEN`) — the
operator wires the real frontend origin in production (or, in the Docker same-origin deployment
D18, CORS may not apply). **Never a wildcard origin:** the tool writes + commits the Address
Library, so a wildcard CORS on a mutating API is a finding (`security-invariants.md`); allowed
methods are `GET` + `POST` (the API's whole surface), credentials off (auth is the operator's
seam, not built).

- **§5 (amended)** — the backend bullet records the CORS allowlist seam (`KCDX_CORS_ORIGINS`,
  localhost dev default, tight allowlist, GET/POST, credentials off).

**Why:** the served frontend and the backend run on different ports = different origins; the
browser's same-origin policy blocks the call until the backend sends the allow-origin header.
The configurable-allowlist-with-localhost-default approach (env-wired in production) was the
user's settled choice; a wildcard was rejected (a mutating Address-Library API must not accept
any origin).

## 2026-06-04 — `GET /modules` joins the read API (the s04 `module` Select source)

A thin module-registry read endpoint `GET /modules` joins the browse/view read API, backed
by a new data-core read seam `read_modules(out_dir)` that reads the `modules` table from the
curated DB (`[{id, name, path}]`). Added because the s04 field editor's `module` field is an
editable `Select` over the real module list, and Phase 2 exposed no module-list read. The
backend derives nothing (law 6) — the data-core seam does the DB read; the endpoint returns
the same 200 empty signal on a missing checkout as the other read endpoints.

- **§5 (amended)** — the backend bullet records the read API's four endpoints incl. `GET /modules`.

**Why:** the s04 spec requires `module` to be a Select over the real module registry; the
producer (the read seam + endpoint) is built ahead of the s04 field-editor frontend that consumes it.

## 2026-06-03 — the frontend is a SEPARATE git repository (D23)

The React frontend (`data/maintainer-tool/frontend/`) becomes its own git repository nested at
its design-specified path, NOT an in-tree kcdx package. kcdx gitignores the path; the frontend
carries its own MIT LICENSE (matching kcdx) and pushes to its own remote. The path (§5) and the
dependency direction are unchanged — only version-control ownership moves out of the kcdx tree,
so the frontend's npm dependency tree / lockfile / source never enter kcdx's history, build gate,
or publish allowlist.

- **D23 (new) — the frontend is its own repo.** Gitignored from kcdx; own MIT LICENSE; own
  remote; own gate (`npm run build` + Vitest in the nested repo). The kcdx `/feature` ledger
  flip + build gate do not apply to frontend commits.
- **§5 (amended)** — the frontend bullet records the separate-repo ownership (path + dependency
  direction unchanged).

**Integrated in:** §10 D23, §5.
**Why:** the user wants the maintainer-tool frontend separately versioned, licensed, and pushed
— out of the kcdx tree — while keeping its design-specified path under `data/maintainer-tool/`.

## 2026-06-03 — robust rollback: two mechanisms split at the irreversible commit (D21); correct the contradictory D19/§7 text

The earlier D19/§7 text claimed "the deferred-commit ROLLBACK gives the robust post-failure
rollback on ANY downstream failure (export/CSV/git)." Architect-review proved that internally
contradictory: the data-core's commit(handle) COMMITs+closes both connections one-way (after it
the deferred rollback raises), AND the export MUST run post-commit (export_seeds opens its own
fresh connection and cannot read the uncommitted held txn). So the deferred rollback covers only
a PRE-commit failure; export/integrity/git all run POST-commit, where it is unavailable.

- **D21 (new) — the robust rollback is TWO mechanisms split at the commit.** (a) PRE-commit
  failure (validation) → the deferred-commit ROLLBACK (4a) discards the held txn incl.
  sqlite_sequence/PK bumps. (b) POST-commit failure (export/integrity/git) → a SCOPED
  restore-point, a DATA-CORE capability (D13/law 6 — it owns the write semantics + the open
  connections + knows the touched rows): before the irreversible commit it captures ONLY the
  touched rows (address_versions/survival/game_versions/address_names, both DBs) + each DB's
  sqlite_sequence + the data/db-export/ CSVs; on a post-commit failure it restores those rows,
  resets the sequence, reverts the CSVs. A few-KB capture regardless of DB size — the ~1.3GB
  DEV DB is never copied. Rejected: a full-file snapshot (copies 1.3GB per confirm — the
  cornerstone order picks the cheaper mechanism for the same guarantee); git-failure-leaves-
  DB-ahead-retryable (contradicts "nothing lands"); a backend restore-point (write-semantics
  rule logic in the backend = a D13/law-6 violation).
- **D19 + §5 + §7 + the frontmatter banner corrected** — the deferred rollback covers
  PRE-commit; the scoped restore-point (D21) covers POST-commit; together a robust rollback on
  ANY failure.

**Integrated in:** §10 D21 (new) + D19 (rollback clause corrected), §5 (the db_editor mechanism
paragraph), §7 (the save spine — Save-previews/Confirm-transacts + the two-mechanism rollback),
the frontmatter banner.
**Why:** the deferred-commit rollback cannot undo a post-commit failure (the commit is
irreversible + the export must read the committed DB), so "on failure nothing lands" needs a
separate pre-commit-captured restore-point; the scoped (touched-rows) form delivers the
guarantee at a few-KB cost vs a 1.3GB file copy. User-settled 2026-06-03 (architect-review).

## 2026-06-03 — write mechanism: DIRECT-DB writes (not seed-rebuild); CSV export to data/db-export/

The maintainer write path is corrected to match D1's vision (DB is the originator). The
original D13 mechanism wrapped `import_to_sqlite.apply_seeds` — the seed-CSV-REBUILD bridge
(export the DB → edit a temp seed CSV → re-apply by diffing the prospective seed against the
DB). That contradicts D1 (the DB is rebuilt from seeds on every edit) and could not create a
version at a new game tag (the seed-rebuild's `GAME_VERSION_TAG`/baseline-matcher gate
materialised zero rows). This revision makes the mechanism match the vision.

- **D19 — the write mechanism is DIRECT-DB.** A maintainer edit is a direct INSERT/UPDATE
  through the applier's EXISTING `_apply_one_db` write helpers (fed edit parameters, not
  CSV-diff actions) — preserving the 8 load-bearing behaviors (the 1:1 survival INSERT, the
  interval-close, the function-kind promote-vs-mint + fingerprint + `BaselineRefusal` gate,
  per-DB column projection, FK-id resolution-never-minting). The same single validator gate
  runs, re-targeted to the **prospective DB state**. The write runs inside the deferred-commit
  transaction (4a, reused verbatim); its `ROLLBACK` discards the whole txn incl.
  `sqlite_sequence`/PK-autoincrement bumps — the robust post-failure rollback the user
  required. `create-version`-at-a-new-tag now works (a direct INSERT bypasses the baseline
  gate). The `run_rebuild` bootstrap (Ghidra dump + seeds → DB, one-time) is unchanged.
- **D20 — seeds vs the derived export, physically separated.** `data/seeds/*.csv` = the
  bootstrap seeds (genesis; read once by `run_rebuild`, then frozen). `data/db-export/*.csv`
  = the derived export record (the living git-tracked history the tool writes on every save).
  The export no longer writes back to `data/seeds/`. `data/db-export/` is a new private path
  (private by default — the publish allowlist is opt-in).
- **D13 — mechanism superseded by D19; the gate-reuse insight preserved.** The "one
  whole-state validator, all six jobs, zero rule logic in `db_editor`" decision stands; only
  the seed-rebuild bridge mechanism is replaced by direct writes.

**Integrated in:** §10 D19 + D20 (new) + D13 (mechanism superseded), §5 (the `db_editor`
mechanism paragraph), §1 (vision: direct write + the `data/db-export/` target), §2 (glossary:
bootstrap seeds vs the derived export), §3 (the inversion diagram + the export target + the
privacy note), the frontmatter banner.
**Why:** D13's seed-rebuild mechanism contradicted D1 (DB-as-originator) and made
create-version-at-a-new-tag a silent no-op. A ground-truth probe of the data-core showed
`apply_seeds`/`_apply_one_db` ALREADY run the real direct SQL — the seed-rebuild was only the
wrapper — so reusing those write helpers directly is the surgical correction (preserves the 8
behaviors, reuses the deferred-commit seam, fixes the new-tag gap). User-settled 2026-06-03.

## 2026-06-02 — web-app pivot: Dockerized web app supersedes the PySide6 desktop tool

The tool becomes a **hostable web app** instead of a PySide6 desktop `.exe`, so maintainers
manage the Address Library from any browser (including a phone) "on the go." The entire
headless data-core (Phase 1: csv_exporter, db_editor, field_delta, the validator, the
round-trip oracle) carries over UNCHANGED — only the shell changes.

- **D14 — Delivery:** a Dockerized web app — a Python (FastAPI/Flask) backend wrapping the
  data-core + a React frontend (a component lib strong on forms/lists/dropdowns/modals).
  Sub-decisions explicit-but-open: the component lib (Mantine vs MUI); the image packaging
  (single image vs compose).
- **D15 — DLL resolver client-side:** the `.rdata` version scan is ported to a small JS
  function that runs in the browser on a locally-picked DLL (File API, no upload — only the
  version tag reaches the server). `version_resolver.py` stays the test-of-record (a
  cross-impl agreement test). A version dropdown is the phone-friendly default.
- **D16 — Server commit + push:** the backend commits to its volume-mounted checkout AND
  pushes to GitHub on confirm (D6's exact-path / live-lock / self-authored-message discipline
  unchanged; the writer moved server-side).
- **D17 — Auth out of scope, auth-ready seams:** the commit identity comes from the request
  context the operator's login supplies; the push credential is env-injected. No
  login/auth/hosting/portal code from the build — only the seams + a dev default for local
  testing.
- **D18 — Container data layout:** the git checkout (`data/seeds/` + the reference DB) lives
  on a mounted volume at a configured path; the image carries only app code; the operator
  provides the volume + credential.
- **Superseded:** D6 local-commit → server commit+push (D16); §8 PySide6 `.exe` distribution
  → Docker (D14/D18); US-10/R12 server-side DLL resolver → client-side (D15); D9/D10 "DLL
  link" framing → version-pick + client check.

**Integrated in:** the delivery note (frontmatter), §1, §5 (the structure — backend +
frontend units + the data-core tree), §6 US-10, §7 (the verification-context + accessibility
states), §8 (R9 distribution, R12 resolver, the commit constraint, R3), §9 (in: the web
stack; out: auth/hosting), §10 (D6/D9/D10 amended; D14–D18 added).

### UI-layer hand-off (action for `/ui-design` — NOT done here)

The UI design layer (`ui/design.md` + the 7 screens `ui/screens/s01–s07`) was authored for
**PySide6 desktop** — a desktop window skeleton, Qt component silhouettes, a desktop two-pane
layout. It needs a **desktop→web re-expression**, which is `/ui-design`'s job, not `/design`'s
(`/design` edits the TRD + this changelog only):

- The window skeleton → a **responsive web layout** (the two-pane navigator+detail must
  reflow for a phone viewport — likely a list→detail drill-down on narrow screens).
- The component silhouettes (PySide6 widgets) → the **React component library's** primitives
  (table, form inputs, dropdowns, modals).
- The DLL-link surface (s07) → the **version dropdown + the client-side "check against a local
  DLL" control** (D15), not a desktop file-link.
- **Carries over unchanged:** the information architecture (navigator / entity detail /
  version compare / field-delta confirm / the states), the interaction laws (layout
  stability, advisory verification, atomic confirmed transaction, the single-gate, read-only
  identity), and the field-delta confirm as the human surface (D8). ~80% of the UI design is
  toolkit-agnostic and survives.

Run `/ui-design` to re-express the UI layer for web, then `/plan` to re-decompose Phase 2+
for the web stack (Phase 1 data-core carries over).

## 2026-06-02 — db_editor reuses the existing run_apply path (D13)

Surfaced mid-build (Phase 1 step 3): `db_editor` must run every write through the single
validator gate (design §5/§8, R3 — no rule reimplemented), but the validator has no
row-level entry point (its per-row rules are inline in the CSV-file reader), and
`db_editor`'s writes reach cross-row invariants (supersession acyclicity for Jobs 4/5,
tuple-uniqueness for Jobs 1/6) a row-level check cannot see.

- **Decision (D13):** `db_editor` is the headless in-process entry point to the EXISTING
  `import_to_sqlite.run_apply` validated atomic applier — refactor `run_apply`'s CLI shape
  (`sys.exit`/prints/`--dll`) into a library-callable form `db_editor` invokes. Zero rule
  logic in `db_editor`; the whole-state gate covers row-level AND cross-row invariants;
  one path generalises to all six jobs.
- **Constraint:** the refactor preserves the existing apply==rebuild oracle byte-identically.
- **Rejected:** A (export→validate→commit per edit — the verbatim-gate-reuse fallback if
  the refactor proves too heavy), B (validate-before-open), C (add a row-level validator
  entry — modifies the shared gate + cannot see cross-row invariants, so the fork would
  reappear worse at steps 4/5).
- **Surfaced via architect-review** (it found `run_apply` already covers the whole v1 job
  catalog, which the first framing missed); the user chose D.

**Integrated in:** §5 (db_editor responsibility — the mechanism) + §10 D13. Reshapes plan
steps 3/4/5 — each becomes a thin caller of the refactored run_apply library entry for its
job, not a separate write shape.

## 2026-06-02 — importer persists NULL for a blank authored field (round-trip fix)

Surfaced while building the CSV exporter (Phase-1 step 1 of the maintainer-tool plan): the
importer (`row_builder.build_curated_row`) promoted the bulk-dump `abi_walker` floor
signature (`? (...)`) onto curated `function_no_sig` / `function_variadic` rows whose seed
`signature` cell was blank. The DB then carried a signature the seed left empty, so the
exporter could not reconstruct the blank cell and the byte-identity round-trip
(`export(import(CSVs)) == CSVs`, D2) failed on 12 rows.

- **Decision:** the importer persists NULL for any authored field the seed left blank — it
  must not promote a bulk-dump value onto a curated row's blank cell. A curated
  function-kind row with a blank seed `signature` keeps the DB `signature` NULL.
- **Why safe:** the survival/fingerprint path keys these kinds on the body-hash
  (`function_hash`), not the signature — NULLing the floor signature on a curated row
  changes no survival behavior.
- **Why it matters:** restores DB↔CSV information-equivalence (design §4) at the source —
  the invariant the whole round-trip contract rests on.

**Integrated in:** §4 (the information-equivalence consequence). Lands as plan step 1b
(`row_builder` fix + a re-import round-trip oracle on the affected rows), ordered before
the exporter (step 1) can commit byte-identical.
**Why:** the exporter (step 1) demands the exact round-trip the §4 invariant promises; the
pre-existing import behavior violated it. Settled by the user 2026-06-02 during the build.

## 2026-06-02 — UI design layer authored; v1 expanded to the full six-job tool

Authored the UI design layer ([`ui/design.md`](ui/design.md) + seven screen specs under
[`ui/screens/`](ui/screens/)) through the UI design dialogue, and revised the TRD to match
the decisions that emerged. A maintainer-tool, function-first design: stable layout (no
elements jump on state change), searchable/filterable navigation, editable forms with
gated dropdowns, side-by-side version compare, a plain-language field-delta confirm.

- **v1 is now the complete six-job tool** (D7) — create entity (Job 1), re-verify (Job 2),
  supersede (Job 4), deprecate (Job 5), create version (Job 6), plus edit-any-version and
  side-by-side compare. The first draft's Job-2-only MVP scope (and the deferral of the
  rest) is removed. v1 may be built in steps, but no catalog job is out of scope.
- **The confirm surface is a plain-language field delta** (`field: old → new`), not the
  literal CSV diff (D8) — the human reasons about record fields; the CSV diff is
  oracle-verified and lands in the commit for a reviewer, invisible to the maintainer.
- **The DLL link is advisory, never required** (D9) — every action proceeds unlinked with
  a "can't verify" warning; a resolver failure or unlinked state is overridable ("I accept
  — save anyway"). Replaces the blocking "degraded mode" framing. Unlinked default-selects
  the newest authored row (D10).
- **New entity / new version are approval-gated in the confirm step** (D11, AP18); a new
  version identical to its source is blocked with steering copy routing to re-verify (D12).
- **The UI design layer** (window skeleton, nine interaction laws, token system, seven
  screen specs) is the artifact a builder conforms to (`spec-conformance.md`).

**Integrated in:** TRD frontmatter, §1, §6 (US-1…US-10), §7, §8, §9, §10 (D3/D5 amended;
D7–D12 added); new `ui/design.md` + `ui/screens/*.md`.
**Why:** the work-plan tree was authored before the UI was designed; the design dialogue
both specified the UI and surfaced that the maintainer's real needs (manage all items, see
+ compare past versions, edit and create versions, clarity about exactly what changes) span
the whole catalog, not Job 2 alone — the user settled v1 = the complete tool.

### Plan hand-off (action for `/plan` — NOT done here)

The work-plan tree `docs/outstanding-work/maintainer-tool-db-direct/` was authored against
the **Job-2-only** scope and is now under-scoped:

- **Phase 2 (GUI shell)** assumes one Job-2 screen (steps 4–8: load → entity-list-pick →
  audit-trio-edit → save-chain-diff → commit). The settled UI is seven screens covering
  six jobs + compare. Phase 2 needs re-decomposition against `ui/screens/` (one or more
  steps per screen/job, dependency-ordered per `incremental-delivery.md`).
- **Phase 1 (data-core)** needs the editor shapes the new jobs require: INSERT (Jobs 1/6),
  the lifecycle-UPDATE (Jobs 4/5) — beyond step-3's audit-trio UPDATE. The exporter +
  round-trip oracle (steps 1–2) are unaffected.
- **`plan-spec.md`'s coverage map** maps design elements to steps against the old §-numbers
  and the Job-2 scope; it needs re-mapping to the revised §6 (US-1…US-10) + the UI screens.

**Stale prescriptive references outside the design-doc edit boundary** (NOT edited here —
`/design`/`ui-design` edit the design doc + changelog only; these are surfaced for the
reconciliation, per `deletion-hygiene.md`):

- `requirements.md` R7 ("The six jobs (full scope; **MVP is Job 2 only**)") and R6's
  "Job-2 MVP" framing now contradict D7 (v1 = the full catalog). R7's workflow list +
  phase-order is still useful; its "MVP is Job 2 only / the other five are later phases"
  assertion is superseded. Reconcile when re-planning (a banner note at R6/R7 pointing to
  design.md §9/§10 D7, matching how R1/R6 already banner-note the source-of-truth inversion).
- `plan.md` §-references to "the Job-2 MVP" (e.g. §27) inherit the same staleness; the plan
  tree re-decomposition covers them.

Run `/plan` to re-decompose the tree against this revised design + the `ui/` screen specs.
(`/design` does not decompose — `spec-conformance.md`: a plan is a pointer to the design,
re-pointed here.)
