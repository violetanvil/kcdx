# lua_pcall — RVA 0x71A5A4

**Confidence**: verified

## Evidence

### Source (lapi.c)

```c
LUA_API int lua_pcall (lua_State *L, int nargs, int nresults, int errfunc) {
  struct CallS c;
  int status;
  ptrdiff_t func;
  lua_lock(L);
  api_checknelems(L, nargs+1);
  checkresults(L, nargs, nresults);
  if (errfunc == 0)
    func = 0;
  else {
    StkId o = index2adr(L, errfunc);
    api_checkvalidindex(L, o);
    func = savestack(L, o);
  }
  c.func = L->top - (nargs+1);
  c.nresults = nresults;
  status = luaD_pcall(L, f_call, &c, savestack(L, c.func), func);
  adjustresults(L, nresults);
  lua_unlock(L);
  return status;
}
```

Direct callees:
- `index2adr` (static, lapi.c) — called when errfunc != 0
- `luaD_pcall` (ldo.c) — the protected call dispatcher

`f_call` is referenced as a function pointer (passed to luaD_pcall), not
called directly here.

### Anchor

`lua_pcall` was identified by yobson1's mod (predecessor RE work). The
AOB sig `48 89 5C 24 ? 57 48 83 EC 40 33 C0 41 8B F8` matches exactly
one location in WHGame.dll: RVA 0x71A5A4.

### WHGame bytes

```
+0000  48 89 5C 24 08 57 48 83 EC 40 33 C0 41 8B F8 44
```

Translation: `mov [rsp+8], rbx; push rdi; sub rsp, 0x40; xor eax, eax;
mov edi, r9d; mov ...`. The `33 C0 (xor eax, eax)` is PGO-emitted; our
local build doesn't have it.

### Call sites (from callgraph_walk.py)

```
site=0x0071a5c1  target=0x0071dd7c  call (index2adr)
site=0x0071a5fd  target=0x0071a628  call (luaD_pcall)
```

Two direct calls. Matches the two source-level direct callees exactly.
(`f_call` is passed by pointer via LEA, doesn't show as a CALL.)

## Verified callees

| Source name | RVA      | Evidence |
|-------------|----------|----------|
| index2adr   | 0x71DD7C | hot path call from lua_pcall site +0x1D |
| luaD_pcall  | 0x71A628 | hot path call from lua_pcall site +0x59; immediately follows lua_pcall body in .text (linker adjacency); function body shape (thin wrapper, saves state, single hot-path call, conditional jump to cold error path) matches luaD_pcall source exactly |
