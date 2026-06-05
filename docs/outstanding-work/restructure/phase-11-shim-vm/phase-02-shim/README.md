# Phase 2 — the symbol shim

`src/lua_shim.{h,cpp}` — the function-pointer table resolving every `lua_*`/`luaL_*`
against WHGame.dll's compiled Lua, plus kcdx-side stubs for the inlined/stripped set.
At this phase the shim COEXISTS with kcdx's static-linked Lua (the static drop is
Phase 5); the shim is verified in isolation here.

## Step ledger (step-grain)

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash when
`DONE`, `—` otherwise.

| Step | Status | Commit |
|---|---|---|
| [1 — forward the 90 resolved fns by name + Resolve() + gating](step-1-shim-forward-resolved.md) | DONE | 3f6e09e |
| [2 — kcdx-side stubs for the 31 catalogued inlined/stripped (GC-barrier-safe; 3 unclassified + 2 not-usable carried)](step-2-shim-stub-inlined.md) | DONE | 54d98c8 |

## Phase verification gate

- **Build green.** A shim call through `kcdx::lua_shim::g_api` (e.g.
  `g_api.lua_pushinteger(L, 42)`) lands the value on the stack — verified by a
  permanent regression row that self-reports the canonical acceptance signal.
- `kcdx::lua_shim::Resolve()` returns true with every required symbol resolved; a
  forced required-miss makes Resolve bail loud (a negative test row).
- PROBE Q stays silent (the shim coexisting with static Lua introduces no new
  sentinel).
- Every GC-pointer-writing stub calls `luaC_barrierf` — a per-stub test row asserts
  it (the GC-barrier safety invariant, `plan-spec.md`).
