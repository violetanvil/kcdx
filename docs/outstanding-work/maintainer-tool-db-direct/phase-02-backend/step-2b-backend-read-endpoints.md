# Step 2b — backend read endpoints (call the data-core read seam, serialize JSON)

**What.** Add the read endpoints the frontend's browse/view surfaces (s01/s02/s03)
call: the **curated entity set** (s01 list + filters), an **entity's detail**
(s02 header + lifecycle), and its **version rows** (s02 version table / s03
history+compare). Each endpoint is a THIN caller of step 2a's data-core read seam —
it calls `read_curated_set` / `read_entity_detail` / `read_version_rows` against the
configured checkout and serializes the result to JSON. The backend computes NOTHING
(no status derivation, no SQL, no rule logic — law 6/D13); the derived status arrives
already computed from the data-core (step 2a).

**Scope.** The FastAPI read routes + their response models (the JSON the frontend
binds), wired into `app/main.py` (or a read router module if `main.py` is growing —
`structure-by-responsibility.md`). Each route resolves the checkout via the step-1
config + calls the step-2a function. The error path (DB/seeds not resolvable at the
checkout) returns the same empty/error signal s01's empty-state names (the read API's
counterpart to /health's state) AND logs the failure (`logging.md` — the step-1
follow-up: wire the failure-branch log here, where the read path makes it load-bearing).
Read-only; no write/save (step 4), no field-delta (step 3).

**Endpoints (shape from the s01/s02 design + the step-2a return):**
- `GET /entities` → the curated set: `[{kcdx_id, name, status, kind}]` (s01 list +
  status chip + the status/kind filters; filtering/search is client-side per s01
  §Contents "local, no write" — the endpoint returns the full ~143-entity set).
- `GET /entities/{kcdx_id}` → entity detail: `{kcdx_id, name, superseded_by,
  superseded_at_version, is_deprecated, deprecated_at_version, deprecation_replacement,
  notes}` (s02 identity + lifecycle).
- `GET /entities/{kcdx_id}/versions` → the version rows newest-first, each with its
  derived status + full columns (s02 version table, s03 history/compare).
- A `404` for an unknown `kcdx_id`; the empty/error state when the checkout resolves no
  DB (named cause, logged).

**Test bar.** A backend test (`pytest` + FastAPI `TestClient`) over the mini-dump
fixture checkout (the step-1 test's `_build_resolved_checkout` pattern): `GET /entities`
returns the curated set with name/kcdx_id/status/kind; `GET /entities/{id}` returns the
identity+lifecycle; `GET /entities/{id}/versions` returns rows newest-first with derived
status; an unknown id is 404; the no-DB checkout returns the empty/error state (and the
failure is logged). The status values come from the data-core (step 2a) — the backend
test asserts the endpoint SURFACES them, while step-2a's test is the status-derivation
oracle (no re-testing the rule here — the backend has no rule). Runnable now (step 2a +
the fixture + the step-1 config exist).

**Dependencies.** Step 2a (the data-core read seam — the functions these endpoints
call) + step 1 (the backend skeleton + the checkout config). Ordered AFTER 2a
(`.claude/rules/incremental-delivery.md` — the consumer after its producer).

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-1/US-2/US-9 (load, browse, the version rows + compare data) + §5 (the backend is
a thin caller; zero rule logic) + §10 D13. The frontend surfaces this feeds:
[`ui/screens/s01-navigator.md`](../../../../data/maintainer-tool/ui/screens/s01-navigator.md)
(the list + status chips + filters + the empty/error states),
[`s02-entity-detail.md`](../../../../data/maintainer-tool/ui/screens/s02-entity-detail.md)
(header + version table),
[`s03-version-history-compare.md`](../../../../data/maintainer-tool/ui/screens/s03-version-history-compare.md)
(history + compare).

**UX.** N/A directly — a JSON API; but its response shapes + the empty/error signal
are what make s01/s02/s03's states renderable. The empty/error copy is the frontend's
(s01 §States); this endpoint provides the machine signal (the state + the named cause)
those states bind, the same contract /health established in step 1.

**Disassembler-test / author-burden.** N/A — read API; no author-facing input.
