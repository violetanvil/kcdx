# Declare-store value-string arena

## Status

Designed. Not built. **Committed completion of the `kcdxDeclareInterface::Get` string-pointer-lifetime contract** — the broad "the pointer survives any subsequent Declare from any plugin" wording the contract sites carried until the narrowing change immediately preceding this entry was honest about the cross-triple case (deque-node-stable storage handles it correctly) but false in the same-triple case: a re-`Declare` of the SAME `(author, plugin, name)` triple from the owning plugin runs `g_entries[existing] = e;` at `src/declared_targets.cpp:564`, which copy-assigns the inner `std::vector<VersionEntry>` and destroys every prior inner `std::string valueStr` and the chars they pointed at. A `stringValue` the author cached from a prior `Get` becomes a dangling pointer with no diagnostic — silent use-after-free, the worst-shape UX foot-gun.

The contract sites currently carry the interim narrow wording ("survives a Declare on a different triple; same-triple re-Declare invalidates — re-`Get` after re-Declaring"). That wording is the honest description of what the code does today. **It is not the final contract.** The final contract is the broad "process-lifetime; survives any subsequent Declare from any plugin" — author-expected behavior, no foot-gun. This entry is what makes that final contract true.

The author's mental model of a re-`Declare` is "I re-registered this name," not "I destroyed every pointer anyone holds into this name." A re-`Declare` is a re-registration, not a destruction. The store's job is to make the author's mental model accurate.

## Trigger to revisit

The narrowing change immediately preceding this entry merged with the interim narrow wording on `kcdxDeclareInterface::Get`'s `stringValue` contract. That merge IS the trigger — this entry is ready to land in the next change that touches the declared-targets store.

## Design

A process-lifetime arena for every `valueStr` ever declared by any plugin. `VersionEntry::valueStr` stops being an owned `std::string` and becomes a stable pointer/view into the arena. Same-triple overwrite at `src/declared_targets.cpp:564` appends the new triple's strings to the arena (returning new indices/views); the old arena strings stay valid until process exit. Orphaned strings accumulate per session — trivially fine at the scale this surface operates (tens to low hundreds of value-string declarations per plugin per session; total bytes are kilobytes, not megabytes).

**Shape:**

- New file-local `std::deque<std::string>` arena in `src/declared_targets.cpp` (the same node-stability property the `g_entries` deque already relies on — string element addresses survive subsequent `push_back` on the arena, so a `const char*` into one element remains valid forever).
- `VersionEntry::valueStr` changes from `std::string` to either:
  - **A small index** (`size_t arenaIdx`) into the arena, OR
  - **A `std::string_view`** (or bare `const char*`) into the arena's owned string.

  Pick the shape that keeps `VersionEntry`'s copy-assign trivial for the same-triple overwrite case AND keeps the reader-side accessor cheap (`Thunk_Get` reads it on every `Get` call). The index form is more defensive (a stale view from a serialization round-trip would point at freed memory; an index doesn't); the view/`const char*` form is fewer cycles per `Get`. Pick at build time; the store-internal contract doesn't change.

- `Register`'s same-triple path at `src/declared_targets.cpp:564` no longer destroys prior strings:
  - For each `VersionEntry` in the incoming `e.versions`, `arena.push_back(v.valueStr)` (or the empty string for non-string entries) and rewrite the entry to hold the new arena index/view BEFORE the `g_entries[existing] = e;` assign.
  - The old `VersionEntry::valueStr` indices/views in the prior `g_entries[existing]` are not touched; the arena entries they point to stay alive for the process lifetime.
- `Thunk_Get` in `src/declare_interface.cpp` reads `stringValue` through the arena instead of through the destroyed inner `std::string`. With the index shape: one extra `arena[idx].c_str()`. With the view shape: the view IS the pointer.
- Memoization (`g_memo`) is unaffected — it keys on `entryIdx`, not on string contents.

**When the arena lands, restore the broad unconditional wording at all 4 contract sites + the 2 impl comments.** The broad wording becomes TRUE; it is the final contract. The 6 narrowed sites are:

- `include/kcdx/Interfaces.h` `kcdxDeclaredValue::stringValue` ABI contract — restore broad "process-lifetime; survives any subsequent Declare from any plugin" wording.
- `src/declared_targets.h` `ResolvedDeclared::entry` doc — restore broad wording.
- `src/declared_targets.h` `FindPickedVersionEntry` doc — restore broad wording.
- `docs/cpp/declare.md` `stringValue` row — restore broad wording.
- `src/declared_targets.cpp` storage-narrative paragraph — restore broad wording.
- `src/declare_interface.cpp` `Thunk_Get` essay-collapse one-liner — restore broad wording.

## Falsifiability — the test row that distinguishes B-landed from B-not-landed

The arena fix is not landed until a test plugin proves the broad contract holds. New sub-row(s) on the existing `cap-62-cpp-declare-interface` plugin:

- **`CAP-62-stringvalue-content-survives-same-triple-redeclare`** — Declare a canary value `"canary"`; cache the `stringValue` pointer via `K.declare->Get`; re-`Declare` the SAME triple with payload `"second"`; assert the cached pointer still reads `"canary"` via `strcmp(cached, "canary") == 0`. This row goes RED against today's code (the inner string was destroyed) and GREEN after the arena fix.
- **`CAP-62-stringvalue-address-survives-same-triple-redeclare`** (optional sister row) — Declare a value; cache the pointer; re-`Declare` the SAME triple; assert pointer-equality (`cachedBefore == GetAgain()`). Same shape as the existing cross-triple address row, mirrored to same-triple.

The first row is the load-bearing one. It is the falsifiable claim that distinguishes "arena fix landed" from "arena fix not landed"; it must go red against today's code and green after the arena fix.

## Files that need to change

When the trigger fires:

- `src/declared_targets.h` — `VersionEntry::valueStr` shape change (owned `std::string` → arena index/view); the new arena's file-local-or-public accessor shape (likely file-local, exposed only through `FindPickedVersionEntry`'s return shape).
- `src/declared_targets.cpp` — the file-local `std::deque<std::string>` arena; `Register`'s same-triple path appends new strings to the arena instead of destroying the prior ones; the matcher reads strings through the arena.
- `src/declare_interface.cpp` — `Thunk_Get` reads `stringValue` from the arena (likely a one-line change: `.c_str()` on the arena entry instead of on the `VersionEntry`).
- `include/kcdx/Interfaces.h` — restore broad wording at the `kcdxDeclaredValue::stringValue` ABI contract.
- `src/declared_targets.h` — restore broad wording at the two doc sites (`ResolvedDeclared::entry` + `FindPickedVersionEntry`).
- `docs/cpp/declare.md` — restore broad wording at the `stringValue` row.
- `src/declared_targets.cpp` — restore broad wording at the storage-narrative paragraph.
- `src/declare_interface.cpp` — restore broad wording at the `Thunk_Get` essay-collapse one-liner.
- `test-plugins/cap-62-cpp-declare-interface/cap-62.cpp` — NEW sub-row `CAP-62-stringvalue-content-survives-same-triple-redeclare` (load-bearing); optional sister row `CAP-62-stringvalue-address-survives-same-triple-redeclare`.
- `test-plugins/cap-62-cpp-declare-interface/kcdx.toml` — add the new sub-row(s) to `test_names`.
- `test-plugins/README.md` — add the new sub-row(s) to the matrix.

Related: `cornerstones.md` (UX > Capability > Performance — silently invalidating a previously-handed-out pointer on a re-registration is a UX foot-gun whose failure mode is a silent UAF; the cornerstone order forces the source fix, not a doc-caveat workaround); `declare-scoped-clear.md` (the scoped-clear API is the ONLY legitimate path that invalidates a cached pointer — author opts in, accepts the contract; same-triple re-Declare must NOT carry that semantic implicitly).
