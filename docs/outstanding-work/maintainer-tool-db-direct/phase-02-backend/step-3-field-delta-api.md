# Step 3 — field-delta API (D8 — saved-vs-prospective)

**What.** Add the endpoint the confirm surface (s06) calls to render the **plain-language
field delta** (D8): given the saved record + the prospective edited record, return the
changed fields as `field → (old, new)`. It wraps the Phase-1 `field_delta` /
`is_new_version_nothing_changed` pure functions — the backend computes nothing itself, it
calls the data-core (law 6). The delta is the human's acceptance signal; the literal CSV
diff is not exposed.

**Scope.** The field-delta endpoint + its response shape (the `field: old → new` list +
the D12 nothing-changed verdict for a new version). Wraps `field_delta` (step 6 of Phase 1).
No write/commit (steps 4–5).

**Test bar.** A backend test (`pytest`): a prospective edit yields exactly the changed fields
as `old → new` (unchanged absent); a new version identical to its source yields the
nothing-changed verdict; the output matches the Phase-1 `field_delta` directly (the API is a
thin wrapper, not a re-computation). Runnable now (`field_delta` exists, landed Phase 1).

**Dependencies.** Step 1 (the backend + data-core seam). Phase 1 step 6 (`field_delta` —
landed). Sequenced after step 1; independent of step 2.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§10 D8 (the field delta is the human's acceptance signal) + §7 (the field-delta confirm
state) + D12 (nothing-changed). The frontend surface:
[`ui/screens/s06-save-confirm.md`](../../../../data/maintainer-tool/ui/screens/s06-save-confirm.md)
(the field-delta list it feeds).

**Disassembler-test / author-burden.** N/A — pure data computation wrapped in an endpoint.
