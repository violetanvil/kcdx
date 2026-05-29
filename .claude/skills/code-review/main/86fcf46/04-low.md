# Low findings

## L1 — `kcdx.hook.<name>` __index fires the refdb SUPERSEDED/DEPRECATED warn at keystroke time

**File:** src/lua_bind_hook.cpp (KindForResolvedName) — the `ResolveByName` call inside the kind probe.

The smart-resolver `__index` metamethod calls `KindForResolvedName`, which in turn calls `kcdx::refdb::ResolveByName` to read the entry's kind tag. `ResolveByName` fires the deduped SUPERSEDED / DEPRECATED / UNVERIFIED warn for state-flagged entries. The warn fires at __index time — before the author has committed to installing anything (they may be inspecting in a debug REPL, autocompleting in an IDE that mirrors the runtime, or just typing a long name).

The dedup key (`pluginHandle`, `callType`, `name`) collapses repeats to one warn per session, so the flood risk is bounded. The behavior is documented in the comment as intentional ("makes it visible earlier, never duplicates"), and the install-path warn fires only on the actual install — so the smart-resolver warn IS earlier-visible.

But this conflates "the author indexed the name" with "the author committed to using it." For a state-flagged entry, the author sees the warn on the first `kcdx.hook.<name>` access — without ever needing to install. The contract is reasonable for a CLI-style workflow; less ergonomic if the author is reading and the warn is meant to gate writes.

**Fix options (surface as a design choice, do not pick):**

- (a) Keep as-is. The warn fires on the first inspection, dedups for the rest of the session, and the install-time warn is a no-op on the dedup key. State the intent in `docs/lua/hook.md`'s smart-resolver section.
- (b) Move the kind probe to the `.mode` access (`Lua_HookResolvedIndex`) — the author has committed by then. The __index just stamps the name + owner; the kind+warn fires on .mode. Adds one extra refdb call per `.mode` access but separates inspection from intent.
- (c) A NON-WARN kind probe path on refdb (e.g. `refdb::KindByName(name)` that reads the kind without firing the state warn). __index uses it; install fires the warn.

Each preserves the dedup; the difference is in WHEN the warn fires relative to author intent.
