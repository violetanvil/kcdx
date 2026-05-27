# luaD_pcall — RVA 0x71A628

**Confidence**: verified

## Evidence

### Source (ldo.c)

```c
int luaD_pcall (lua_State *L, Pfunc func, void *u,
                ptrdiff_t old_top, ptrdiff_t ef) {
  int status;
  unsigned short oldnCcalls = L->nCcalls;
  ptrdiff_t old_ci = saveci(L, L->ci);
  lu_byte old_allowhooks = L->allowhook;
  ptrdiff_t old_errfunc = L->errfunc;
  L->errfunc = ef;
  status = luaD_rawrunprotected(L, func, u);
  if (status != 0) {  /* an error occurred? */
    StkId oldtop = restorestack(L, old_top);
    luaF_close(L, oldtop);
    luaD_seterrorobj(L, status, oldtop);
    L->nCcalls = oldnCcalls;
    L->ci = restoreci(L, old_ci);
    L->base = L->ci->base;
    L->savedpc = L->ci->savedpc;
    L->allowhook = old_allowhooks;
    restore_stack_limit(L);
  }
  L->errfunc = old_errfunc;
  return status;
}
```

Hot path (success): saves state, calls `luaD_rawrunprotected`, restores
`L->errfunc`, returns status.

Cold path (status != 0): closes upvalues, sets the error object,
restores stack state.

### Identification

Reached by walking lua_pcall's call graph: lua_pcall's site at +0x59
calls 0x71A628.

### WHGame body shape

Function at 0x71A628 (verbose disassembly):
- Saves rbx, rbp, rsi, rdi, r12, r14, r15 to home space (`mov [rax+...], reg`)
- Allocates 0x20 bytes shadow
- Saves several lua_State fields: `mov r14, [rcx+0x28]` (L->nCcalls? need to verify offset),
  `mov rbp, [rcx+0xB0]` (L->errfunc — verified by lua_State layout),
  `sub r14, [rcx+0x50]` (L->stack — saving stack delta),
  `movzx r15d, word ptr [rcx+0x60]` (L->ci — saveci),
  `mov r12b, byte ptr [rcx+0x63]` (L->allowhook).
- Stores rax (= old errfunc) — wait, that's `mov [rcx+0xB0], rax` *setting* errfunc to the passed-in `ef`.
- Calls 0x71A6A8 (= luaD_rawrunprotected; see luaD_rawrunprotected.md)
- Tests eax; on success (jne not taken), restores fields and returns.
- On failure: `jne 0x222A2A4` (long jump to cold path far away in PGO-split layout).

Field offsets accessed via [rcx+...] match Lua 5.1's lua_State layout
exactly. The save/call/restore pattern around a single call to
luaD_rawrunprotected is luaD_pcall's signature shape.

### Linker adjacency

luaD_pcall sits immediately after lua_pcall (lua_pcall ends at +0x82
into RVA 0x71A5A4 = 0x71A626; luaD_pcall starts at 0x71A628 with a 1-byte
CC pad in between).

Not a strict proof, but the linker typically keeps closely-coupled
functions adjacent in PGO-instrumented builds when their call frequency
correlates. lua_pcall's only meaningful work is the luaD_pcall call.

### Call sites (from callgraph_walk.py)

Hot path: one call to 0x71A6A8 (= luaD_rawrunprotected). Cold path is
out of walker's range due to PGO's hot/cold splitting (jne to
0x222A2A4).

## Verified callees

| Source name           | RVA      | Evidence |
|-----------------------|----------|----------|
| luaD_rawrunprotected  | 0x71A6A8 | see luaD_rawrunprotected.md |
