# Critical findings

## C1 — `kcdxDeclaredValue::stringValue` lifetime contract is unsound

**Files:**
- src/declare_interface.cpp:282-294
- src/declared_targets.h:230-237 (new doc comment)
- include/kcdx/Interfaces.h:2087-2091 (public ABI contract)
- docs/cpp/declare.md:104-105 ("Process-lifetime")

**Claim made in three places, by the new code:**

- src/declare_interface.cpp:282-286 — *"the picked VersionEntry by pointer and surface ITS storage — that pointer is process-stable for the same reason (aliases into rd.entry->versions, which never moves)"*
- src/declared_targets.h:230-237 — *"Returned pointer aliases into `e.versions` and is process-stable as long as `e` itself stays alive (the registry never relocates after launch-time appends; a Register overwrite writes in place at the same slot)"*
- include/kcdx/Interfaces.h:2087-2091 — *"`stringValue` is a stable pointer into the declared-targets store. The store never relocates after launch-time appends (a Register overwrite writes in place at the same slot), so the pointer is valid for the process lifetime"*

**Actual storage shape:** src/declared_targets.cpp:73 — `std::vector<DeclaredEntry> g_entries;` — and src/declared_targets.cpp:555 — `g_entries.push_back(e);` on first-time Register of a (author, plugin, name) triple.

**Defect:** `std::vector<T>::push_back` reallocates when the existing capacity is exhausted. Reallocation copies/moves every element to a new buffer; every pointer or reference into the old buffer becomes dangling. The "registry never relocates" assertion is false for the data structure chosen — it is true for the SLOT INDEX (overwrite at `g_entries[existing] = e` is in-place), but not for the ELEMENT ADDRESS (a `push_back` after the read invalidates the pointer the caller is holding).

**Concrete reproduction:**

1. C++ plugin A calls `K.declare->Declare("WHGame.dll", "X", ..., K.self)` with a string-value row → push_back grows `g_entries`.
2. Plugin A reads `kcdxDeclaredValue v = K.declare->Get("X", K.self)` → `v.stringValue` points into `g_entries[N-1].versions[k].valueStr`'s std::string buffer.
3. Plugin A caches `v.stringValue` in a global, as the public contract invites.
4. Lua plugin B (or any plugin) later calls `kcdx.declare(...)` for a NEW (author, plugin, name) → `g_entries.push_back(...)` reallocates the vector.
5. Plugin A dereferences `v.stringValue` → use-after-free.

Lua's `kcdx.declared(name)` (lua_bind_declare.cpp:618-630-ish) is unaffected because Lua `lua_pushstring` *copies* the bytes — only the C++ surface promises a process-lifetime pointer.

**The pointer-stability claim is the gate, the gate is broken (AP-style "passes-every-gate-yet-wrong"). Build green; tests would pass; the contract is wrong.**

**Fix:** one of:
- (a) Pre-reserve `g_entries` to a hard cap at engine init (e.g. 4096) so push_back never reallocates; assert if exceeded.
- (b) Change storage to `std::deque<DeclaredEntry>` (element-stable on push_back) or `std::list` / a node-based map.
- (c) Allocate the string payload off the vector (a separate string-pool with stable addresses) and store char-pointer references in the VersionEntry.
- (d) Drop the process-lifetime contract from `stringValue`; document it as "stable until next Declare from any plugin; copy if you intend to cache" and update Interfaces.h + the doc + the in-code comment.

Surface (a)/(b)/(c) vs (d) as a design decision — the surface contract is a UX call (does the author bear the copy burden, or does the engine).

---

## C2 — Three broken `../lua/declare.md` links in the new `docs/cpp/declare.md`; the Lua peer doc is missing

**File:** docs/cpp/declare.md:7, 135, 243

The new C++ declare doc opens with *"The C++ mirror of the Lua `kcdx.declare(module, name, versions_kv)` write surface + `kcdx.declared(name)` read accessor ([../lua/declare.md](../lua/declare.md))"*, links to it again from the error-table row at line 135, and references it in the "See also" section at line 243. **`docs/lua/declare.md` does not exist.** `ls docs/lua/` returns no `declare*` file.

The Lua `kcdx.declare` IS implemented (src/lua_bind_declare.cpp, registered at lua_bind_declare.cpp:643+), but it has no per-call doc file AND no row in `docs/lua/index.md`'s map. The C++ doc landed pointing at three deadends.

Two findings under one:

- **The 3 broken links** ship a 404 on the private repo (the public repo doesn't have any of this yet) — a reader following the C++ doc into "the Lua peer" hits a missing file. Each link is a glance defect in `docs-discipline.md` §4 Discoverable.

- **The Lua peer doc is the bigger gap.** `docs-discipline.md` §3 ("the cross-surface entry") requires both surfaces to have a doc entry in the same change a capability ships — even when only one side is built. The Lua side IS built; its doc is missing AND its row is not in `docs/lua/index.md`'s map. `docs/lua/index.md`'s own contract says *"if a call is not in this map, it does not exist yet"* — the missing row makes a built capability invisible to a Lua-side author.

This was already a gap before this change; this change makes it newly load-bearing by landing the C++ peer that points at it.

**Fix:** in the same change:
- Create `docs/lua/declare.md` with the full reference for `kcdx.declare(module, name, versions_kv)` + `kcdx.declared(name)` (call shape, args, return, errors, snippet — same shape as the other per-call docs, e.g. `docs/lua/bytes.md`).
- Add the row to `docs/lua/index.md`'s map (alongside the existing `kcdx.alias` / `author-declared targets` rows).
- Add a glossary entry for "declared target" + "smart resolver" mirroring the new entries added to `docs/cpp/index.md:111-127`.
