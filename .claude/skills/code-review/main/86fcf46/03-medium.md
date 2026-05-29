# Medium findings

## M1 — `kcdx.bytes` / `kcdx.hook` change Lua type from `function` to `table`

**Files:**
- src/lua_bind_bytes.cpp:660-674 (`bind` now pushes a table)
- src/lua_bind_hook.cpp:1637-1656 (`bind` now pushes a table)

Both `kcdx.bytes` and `kcdx.hook` previously registered as a plain C function on the `kcdx` table:

```cpp
lua_pushcfunction(L, Lua_Bytes);
lua_setfield(L, -2, "bytes");
```

They now register as TABLES with `__call` + `__index` metamethods. Author-facing behavior is preserved for the legacy `kcdx.bytes{...}` shape (the metamethod forwards), but `type(kcdx.bytes)` now returns `"table"` instead of `"function"`. Any plugin (or future plugin) that did `assert(type(kcdx.bytes) == "function")`, or `debug.getinfo(kcdx.bytes)`, or `kcdx.bytes:something()`, or stashed `kcdx.bytes` and called it through a path that doesn't honor `__call`, will break.

The grep for that pattern in the current tree is clean (no in-tree plugin relies on the function type). The risk is forward — the user-facing surface contract `kcdx.bytes` is a callable, and the underlying type changed.

**Fix options:**

- (a) Accept the change and document it in the per-call docs (`docs/lua/bytes.md` / `docs/lua/hook.md`) — the smart-resolver shape needs `__index`, which forces table. State plainly: "kcdx.bytes is a table that is callable via __call." Adds a glossary line.
- (b) Find a Lua-5.1 path that gives a cfunction-shaped object `__index` (there isn't a clean one — Lua 5.1 cfunctions don't have a metatable). Means: (a) is the right answer; the doc clarification is the fix.

Surface (a) vs (b) as a recommendation, not a directive — the change is intentional; the doc gap is the fixable part.

---

## M2 — Reserved-but-unused upvalues 3+4 in `Lua_HookModeInstall`

**File:** src/lua_bind_hook.cpp:1503-1515

The closure carries 4 upvalues (`name`, `mode`, `author`, `plugin`); the body fetches 1 and 2, then explicitly discards 3 and 4 with `(void)lua_upvalueindex(3)` / `(void)lua_upvalueindex(4)`. The comment claims they're "reserved for future use (e.g. an attribution stamp on the synthesized table if a divergence ever matters)."

Two reasons this is a defect, not a feature:

1. **YAGNI.** Per workspace discipline — *don't add features, refactor, or introduce abstractions beyond what the task requires*. The closure stamps and discards two strings on every `.mode` access; the strings live in the upvalue table for the closure's lifetime. Cost is small; the principle is what's at stake.

2. **The comment claims the install path re-walks the stack via `OwningPluginForCurrentCall`, returning "the SAME owner this closure was minted under because the closure runs on the same call stack as the original kcdx.hook.<name>.<mode>(cb) call."** This is true for the direct fire case; it is FALSE if the author passes the closure to another plugin (`peer.install_hook(kcdx.hook.IsInCombat.before)` — the peer then invokes the closure and `OwningPluginForCurrentCall` walks the peer's stack frame, attributing the install to the peer). The captured upvalues 3+4 hold the ORIGINAL author/plugin, which is the correct value for the closure's identity. If the design intends "the install is attributed to whoever invokes the closure" (today's behavior), then 3+4 are dead weight and should be removed. If the design intends "the install is attributed to the plugin that obtained the closure via `__index`", then 3+4 must be USED — passed into the install path so the synthesized table carries the original owner, instead of letting `OwningPluginForCurrentCall` re-walk to the wrong frame.

**Fix:** decide which semantics are correct (the closure-bound owner vs the caller-walked owner) and either delete upvalues 3+4 OR plumb them through. This is a design decision — surface to user.

(The same closure-laundering risk exists for the flat-table form when one plugin passes a callback to another, so the net-new exposure is small. But the smart-resolver shape produces a *bound* closure that explicitly captured an owner; pretending to capture and then discarding is the inconsistency.)
