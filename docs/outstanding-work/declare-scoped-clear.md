# Declare-store scoped clear API

## Status

Designed. Not built. The current `kcdx::declared_targets::Reset()` is engine-internal-only (declared in `src/declared_targets.h:228`, documented "For tests and self-test isolation. Never called during normal operation"). No public API on `kcdxDeclareInterface` exposes Reset, so production plugins cannot wipe declared-store state today.

What's blocking ship: TC-mod authors will eventually want a way to clear their own plugin's declarations (e.g. on a hot-reload / config-change re-init path). The unsafe shape — an engine-wide nuke any plugin can call — would let any plugin silently destroy every other plugin's declarations, breaking the cross-plugin contracts the precedence model + namespace law exist to make robust. The safe shape — scoped to the caller's own `(author, plugin)` triple — exists in principle but needs to be designed against the registry's existing invalidation paths (memoization keyed on `entryIdx`, the once-per-session warn-dedup keyed on `(author, plugin, name, runtimeVersion)`, etc.) before the public API ships.

## Trigger to revisit

Either of:
- A user-shipped TC mod surfaces friction needing scoped wipe (the author has no path to drop their own declarations and is working around it with `kcdx.declare` overwrites of every prior triple, or with stale leftover entries causing apply-pass noise).
- A future hot-reload / re-init feature in the orchestrator-loop needs a per-plugin teardown step that includes declared-state.

## Design

**Two public surfaces on `kcdxDeclareInterface` (append-only per AP11) + `kcdx.declare.*` Lua peer:**

| Surface | Semantics | Why safe |
|---|---|---|
| **`K.declare->ClearOwn(kcdxPluginHandle owningPlugin) -> size_t (dropped count)`** + Lua `kcdx.declare.clear_own()` | Drops every `DeclaredEntry` whose `(declaringAuthor, declaringPlugin)` matches the caller's `(author, plugin)`. Also drops every `MemoEntry` whose `entryIdx` referenced a dropped entry. Returns the count of entries removed (diagnostic; not error-bearing). | The caller can only destroy what they themselves declared. Cross-plugin contracts are preserved by construction. Matches `naming-namespaces.md`'s self-ownership model (each plugin owns its `<author>.<plugin>.*` namespace, no other plugin reaches in). |
| **`K.declare->Clear(kcdxPluginHandle owningPlugin, const char* bareName) -> bool`** + Lua `kcdx.declare.clear(name)` | Drops the single `DeclaredEntry` matching `(caller.author, caller.plugin, bareName)` if present. Also drops its `MemoEntry`. Returns `true` if the entry existed, `false` if it didn't. | Even narrower than `ClearOwn`; same self-ownership invariant. The bareName arg accepts ONLY the 1-segment SELF form — explicitly NOT the 3-segment `<author>.<plugin>.<name>` explicit form, because that would re-introduce the cross-plugin destruction the API exists to prevent. A 3-segment input is a rejection with a teaching error ("Clear accepts only your own bare name; you cannot clear another plugin's declaration"). |

**Explicitly NOT shipped:**

- `K.declare->ClearAll()` / `kcdx.declare.clear_all()` — engine-wide nuke. No legitimate use case requires destroying every plugin's declarations. The `declared_targets::Reset()` engine-internal helper stays engine-internal; no public exposure.
- `K.declare->ClearOther(const char* fullTriple)` — drops another plugin's declaration. Breaks `naming-namespaces.md` self-ownership; no TC use case justifies it (a TC overriding another mod uses the existing precedence / aliasing / override mechanisms, not destruction).

**Memoization invalidation contract:** when `ClearOwn` / `Clear` drops one or more entries, the registry MUST also drop every `MemoEntry` whose `entryIdx` referenced a dropped entry. The existing `DropMemoForEntry(size_t)` helper handles single-entry drops; `ClearOwn` walks `g_entries` to find every match before dropping and calls `DropMemoForEntry` for each. The once-per-session warn-dedup set (`g_warned`) stays untouched (the warns recorded for a now-dropped entry stay recorded; this is correct — a plugin that drops + re-Declares a name shouldn't get the same first-launch warn twice).

**Lifetime contract on previously-handed-out pointers:** when `ClearOwn` / `Clear` drops a `DeclaredEntry`, every `stringValue` pointer the C++ side previously handed out via `K.declare->Get` for that entry becomes invalid. The author calling `ClearOwn` accepts that any cached pointers they hold into their own declared entries are now dangling — same shape as any other lifetime contract where the OWNER of the data is also the one who can destroy it (the caller can't accidentally invalidate ANOTHER plugin's cached pointer because they can only clear their own entries). Documented in the per-call doc; no engine-side guard.

**Indexing stability:** the deque-node-stable storage (landed in the C1 fix) means dropping entries from anywhere in `g_entries` does NOT invalidate other entries' addresses, regardless of which container method drops them (`erase`, swap-pop, etc.). The `MemoEntry::entryIdx` indexing pattern survives any single-entry drop via swap-pop OR survives multi-entry drops via stable iteration — pick the lower-complexity shape at build time.

**Phase gating:** ClearOwn / Clear run at any phase past `RefdbOpened` (same gate `LookupForCaller` carries), with the caveat that calling them BEFORE any of the caller's Declares is a no-op + structured KV info log (not an error — re-init flows may legitimately call ClearOwn before knowing whether prior Declares happened).

## Files that need to change

When the trigger fires:

- `include/kcdx/Interfaces.h` — append `ClearOwn` + `Clear` method pointers to `kcdxDeclareInterface`; bump `kcdxDeclareInterface_Version` to 2u; document the lifetime contract on each.
- `src/declared_targets.h` — add public `ClearOwnedBy(callerAuthor, callerPlugin) -> size_t` + `ClearOne(callerAuthor, callerPlugin, bareName) -> bool` accessors above `Reset()`; keep `Reset()` engine-internal (test/self-test only).
- `src/declared_targets.cpp` — implement the two accessors; the multi-entry drop in `ClearOwnedBy` walks `g_entries` for matching `(declaringAuthor, declaringPlugin)`, collects indices, drops each + drops their `MemoEntry`s. The single-entry drop in `ClearOne` reuses `FindEntryIndex` + `DropMemoForEntry`.
- `src/declare_interface.cpp` — implement `Thunk_ClearOwn` + `Thunk_Clear` thunks; structured KV logs for the diagnostic counts + the 3-segment rejection on Clear.
- `src/lua_bind_declare.cpp` — add Lua bindings `kcdx.declare.clear_own()` + `kcdx.declare.clear(name)` per `lua-api-surface.md` rule 4 (positional, no opts table — the args ARE the full call).
- `docs/cpp/declare.md` — new section "Clearing your own declarations" with both forms documented + the lifetime caveat on cached pointers.
- `docs/lua/declare.md` — same.
- Test plugin — `cap-NN-declare-clear` (new) exercising both forms: plugin Declares N entries, calls `ClearOwn()`, asserts `Get` returns `{found=false}` for all of them; plugin Declares + Gets + caches pointer + `Clear(name)` + asserts cache pointer is now stale (the lifetime caveat in action).

Related: `naming-namespaces.md` (the self-ownership law that scopes the API); `cornerstones.md` (UX > Capability > Performance — the unsafe engine-wide nuke fails the cornerstone test because it weakens UX for other plugins).
