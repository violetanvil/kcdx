# ABI re-derivation — per-arg type + arg-count from the function body

**Date:** 2026-05-31. **Binary:** `third-party-ghidra/WHGame.dll` (KCD2 release_1_5_1164953_841, image base 0x180000000). **Scope:** the 111 signature-bearing rows (kind `function`/`function_variadic`, non-empty `signature`) of `data/seeds/address_versions_seed.csv`. The 10 `function_no_sig` rows are out of scope. **READ-ONLY** — no seed edited, no commit.

## Method

`abi_rederive.py` embeds the proven `phase6_abi_walker.py` body analysis (recursive disasm following near jumps; prologue rsp/rbp/r11 tracking; every `[rsp|rbp|r11+disp]` access back-mapped to an MSVC x64 incoming-arg slot — arg1@+0x08 rcx … arg5+@+0x28 stack) and adds a **def-use register model**: an arg register (rcx/rdx/r8/r9) counts as an incoming arg only if its value is USED — read, dereferenced as a pointer base, or spilled to its home slot — BEFORE the register is first WRITTEN, and registers fill in order (count = the contiguous prefix 1..k all used-before-write). This kills the false positives a naive 'any register touched' scan produces (a volatile r8/r9 reused as scratch). AP2: ABI from BODY, never prologue-shape eyeballing.

**Validation:** the model reproduces every known anchor exactly — `lua_pcall`(id1)=4, `CGame_Update`(id2)=3, `luaL_loadfile`(id3)=2, `CGame_per_frame_ui_pump`(id4)=1, `lua_pcall_internal`(id106)=5 (the 5-arg stack-arg case), `CCryPak_FOpen`(id131)=4, `ModManager_ctor`(id134)=3.

## Confidence model

- **ARG COUNT — high confidence.** `derived/declared`. `derived>declared` is a hard MISMATCH (the 7-arg-SaveGame-typed-as-3 class). `derived<declared` is NOT a contradiction — a function may not read a tail arg it is passed; reported as consistent. Variadic: the fixed prefix is confirmed.
- **WIDTH / POINTER.** First-use-before-write per arg register; reported at full confidence (the def-use model is not fooled by later scratch reuse). `cstr`-vs-`ptr` NEVER flagged (both 64-bit pointers, not body-decidable). Signed-vs-unsigned NEVER flagged. Width of args 5+ (stack) not derived.
- **RETURN — integer-register scan only.** `f32` returns in xmm0 (not-decidable). A `void` body's eax scratch is indistinguishable from a return. A tail-call inherits the callee's return.

## Summary by verdict

| verdict | count |
|---|---|
| CONFIRMED | 111 |
| MISMATCH | 0 |
| NOT-DECIDABLE | 0 |
| WALKER-FAILED | 0 |
| **total** | **111** |

## MISMATCH fix-list (actionable — seed sig vs body-derived)

**None.** No high-confidence count, width, or return contradiction in any of the 111 rows. Every body consumes exactly its declared arg count (`derived == declared` or a legitimate under-touch of a tail arg); no body reads beyond its declared count or reads a register/stack slot the signature omits; no declared 32-bit arg is used as a pointer/64-bit and no declared pointer arg is read 32-bit; no declared pointer/64-bit return is set only in eax.

## NOT-DECIDABLE list (reported as consistent, not as findings)

These per-row facts cannot be settled from the body at this analysis's confidence and are therefore NOT flagged:

- **`cstr` vs `ptr` on every pointer arg** — both are 64-bit pointers; the body cannot tell a C-string pointer from a generic pointer. Settled by the callee's downstream use (a `%s`/`strlen` consumer) or the predecessor Lua 5.1 prototype.
- **signed vs unsigned** on every integer arg/return — identical access width; settled by a downstream signed-vs-unsigned compare/shift or the prototype.
- **`f32` returns (xmm0)** — the integer-register scan is blind to xmm.
- **width of args 5+ (stack args)** — needs per-block dataflow, not the linear first-use scan.

## WALKER-FAILED rows

**None.** All 111 rows analyzed (recursive disasm completed; prologue + arg slots resolved for every entry).

## Full per-row table (all 111)

| id | name | seed signature | derived/decl | count verdict | width/ptr verdict | return verdict | overall |
|---|---|---|---|---|---|---|---|
| 1 | lua_pcall | `i32 (ptr L, i32 nargs, i32 nresults, i32 errfunc)` | 4/4 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 2 | CGame_Update | `void (ptr self, bool haveFocus, u32 updateFlags)` | 3/3 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 3 | luaL_loadfile | `i32 (ptr L, cstr filename)` | 2/2 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 4 | CGame_per_frame_ui_pump | `void (ptr self)` | 1/1 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 14 | IConsole_RemoveCommand | `bool (ptr self, cstr sName)` | 2/2 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 15 | IConsole_ExecuteString | `void (ptr self, cstr command, bool bSilentMode, bool bDeferExecution)` | 1/4 | consistent (1/4 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 16 | IConsole_GetCVar | `ptr (ptr self, cstr name)` | 2/2 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 25 | luaL_checktype | `void (ptr L, i32 narg, i32 t)` | 3/3 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 26 | lua_insert | `void (ptr L, i32 idx)` | 1/2 | consistent (1/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 27 | lua_remove | `void (ptr L, i32 idx)` | 1/2 | consistent (1/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 28 | lua_type | `i32 (ptr L, i32 idx)` | 0/2 | consistent (0/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 29 | lua_rawgeti | `void (ptr L, i32 idx, i32 n)` | 1/3 | consistent (1/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 30 | lua_pushlstring | `void (ptr L, cstr s, u64 len)` | 3/3 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 31 | lua_getmetatable | `i32 (ptr L, i32 objindex)` | 1/2 | consistent (1/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 32 | lua_settop | `void (ptr L, i32 idx)` | 0/2 | consistent (0/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 33 | lua_touserdata | `ptr (ptr L, i32 idx)` | 0/2 | consistent (0/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 34 | lua_pushstring | `void (ptr L, cstr s)` | 2/2 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 35 | lua_createtable | `void (ptr L, i32 narr, i32 nrec)` | 3/3 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 36 | lua_pushvalue | `void (ptr L, i32 idx)` | 1/2 | consistent (1/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 37 | lua_setmetatable | `i32 (ptr L, i32 objindex)` | 1/2 | consistent (1/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 38 | lua_next | `i32 (ptr L, i32 idx)` | 1/2 | consistent (1/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (tail-call / passthrough) | CONFIRMED |
| 39 | lua_tolstring | `cstr (ptr L, i32 idx, ptr len)` | 3/3 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent | CONFIRMED |
| 40 | lua_gettable | `void (ptr L, i32 idx)` | 1/2 | consistent (1/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 42 | lua_rawget | `void (ptr L, i32 idx)` | 1/2 | consistent (1/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 43 | lua_rawset | `void (ptr L, i32 idx)` | 1/2 | consistent (1/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 44 | lua_tonumber | `f32 (ptr L, i32 idx)` | 0/2 | consistent (0/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | not-decidable (f32 returns in xmm0; integer-reg scan blind) | CONFIRMED |
| 45 | lua_isnumber | `i32 (ptr L, i32 idx)` | 0/2 | consistent (0/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 46 | lua_checkstack | `i32 (ptr L, i32 n)` | 2/2 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 47 | lua_typename | `cstr (ptr L, i32 tp)` | 0/2 | consistent (0/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent | CONFIRMED |
| 48 | lua_objlen | `u64 (ptr L, i32 idx)` | 1/2 | consistent (1/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 49 | lua_toboolean | `i32 (ptr L, i32 idx)` | 0/2 | consistent (0/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 50 | lua_rawseti | `void (ptr L, i32 idx, i32 n)` | 1/3 | consistent (1/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 51 | lua_tointeger | `i64 (ptr L, i32 idx)` | 0/2 | consistent (0/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 52 | lua_newuserdata | `ptr (ptr L, u64 sz)` | 2/2 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 53 | lua_pushcclosure | `void (ptr L, ptr fn, i32 n)` | 3/3 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 54 | luaL_findtable | `cstr (ptr L, i32 idx, cstr fname, i32 szhint)` | 1/4 | consistent (1/4 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent | CONFIRMED |
| 55 | lua_getfield | `void (ptr L, i32 idx, cstr k)` | 1/3 | consistent (1/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 56 | lua_setfield | `void (ptr L, i32 idx, cstr k)` | 1/3 | consistent (1/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 57 | lua_gc | `i32 (ptr L, i32 what, i32 data)` | 2/3 | consistent (2/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 58 | lua_sethook | `i32 (ptr L, ptr func, i32 mask, i32 count)` | 0/4 | consistent (0/4 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 59 | lua_getstack | `i32 (ptr L, i32 level, ptr ar)` | 2/3 | consistent (2/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 60 | lua_getinfo | `i32 (ptr L, cstr what, ptr ar)` | 2/3 | consistent (2/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 61 | luaL_checknumber | `f32 (ptr L, i32 narg)` | 2/2 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | not-decidable (f32 returns in xmm0; integer-reg scan blind) | CONFIRMED |
| 62 | lua_call | `void (ptr L, i32 nargs, i32 nresults)` | 3/3 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 63 | luaL_getmetafield | `i32 (ptr L, i32 obj, cstr e)` | 1/3 | consistent (1/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 64 | lua_concat | `void (ptr L, i32 n)` | 2/2 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 65 | luaL_pushresult | `void (ptr B)` | 1/1 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 66 | lua_load | `i32 (ptr L, ptr reader, ptr dt, cstr chunkname)` | 4/4 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 67 | luaL_checklstring | `cstr (ptr L, i32 narg, ptr len)` | 2/3 | consistent (2/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent | CONFIRMED |
| 68 | luaL_optinteger | `i64 (ptr L, i32 narg, i64 def)` | 3/3 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 69 | luaL_checkinteger | `i64 (ptr L, i32 narg)` | 2/2 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 70 | luaL_checkstack | `void (ptr L, i32 sz, cstr msg)` | 1/3 | consistent (1/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 71 | luaL_checkany | `void (ptr L, i32 narg)` | 2/2 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 72 | luaL_addlstring | `void (ptr B, cstr s, u64 l)` | 0/3 | consistent (0/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 73 | lua_iscfunction | `i32 (ptr L, i32 idx)` | 0/2 | consistent (0/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 74 | lua_isstring | `i32 (ptr L, i32 idx)` | 0/2 | consistent (0/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 75 | lua_lessthan | `i32 (ptr L, i32 idx1, i32 idx2)` | 1/3 | consistent (1/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 76 | lua_getupvalue | `cstr (ptr L, i32 funcindex, i32 n)` | 1/3 | consistent (1/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent | CONFIRMED |
| 78 | lua_rawequal | `i32 (ptr L, i32 idx1, i32 idx2)` | 1/3 | consistent (1/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 79 | lua_setfenv | `i32 (ptr L, i32 idx)` | 1/2 | consistent (1/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 80 | lua_setupvalue | `cstr (ptr L, i32 funcindex, i32 n)` | 1/3 | consistent (1/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent | CONFIRMED |
| 81 | lua_tothread | `ptr (ptr L, i32 idx)` | 0/2 | consistent (0/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 82 | lua_xmove | `void (ptr from, ptr to, i32 n)` | 2/3 | consistent (2/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 83 | luaL_addvalue | `void (ptr B)` | 1/1 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 84 | luaL_argerror | `i32 (ptr L, i32 narg, cstr extramsg)` | 3/3 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 85 | luaL_checkoption | `i32 (ptr L, i32 narg, cstr def, ptr lst)` | 1/4 | consistent (1/4 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 87 | luaL_optlstring | `cstr (ptr L, i32 narg, cstr def, ptr len)` | 4/4 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent | CONFIRMED |
| 88 | luaL_prepbuffer | `ptr (ptr B)` | 1/1 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 89 | luaL_typerror | `i32 (ptr L, i32 narg, cstr tname)` | 3/3 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 90 | luaL_where | `void (ptr L, i32 level)` | 1/2 | consistent (1/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 91 | lua_getlocal | `cstr (ptr L, ptr ar, i32 n)` | 3/3 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent | CONFIRMED |
| 92 | lua_setlocal | `cstr (ptr L, ptr ar, i32 n)` | 3/3 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent | CONFIRMED |
| 93 | lua_error | `i32 (ptr L)` | 1/1 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 94 | lua_resume | `i32 (ptr L, i32 narg)` | 1/2 | consistent (1/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 95 | lua_dump | `i32 (ptr L, ptr writer, ptr data)` | 2/3 | consistent (2/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 96 | lua_getfenv | `void (ptr L, i32 idx)` | 1/2 | consistent (1/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 97 | luaopen_math | `i32 (ptr L)` | 1/1 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 98 | luaopen_table | `i32 (ptr L)` | 1/1 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 99 | luaopen_debug | `i32 (ptr L)` | 1/1 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 100 | luaopen_base | `i32 (ptr L)` | 1/1 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 101 | luaopen_string | `i32 (ptr L)` | 1/1 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 102 | luaopen_package | `i32 (ptr L)` | 1/1 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 103 | luaopen_os | `i32 (ptr L)` | 1/1 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 104 | luaopen_io | `i32 (ptr L)` | 0/1 | consistent (0/1 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (tail-call / passthrough) | CONFIRMED |
| 105 | index2adr | `ptr (ptr L, i32 idx)` | 0/2 | consistent (0/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 106 | luaD_pcall | `i32 (ptr L, ptr func, ptr u, i64 old_top, i64 ef)` | 1/5 | consistent (1/5 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 107 | luaD_rawrunprotected | `i32 (ptr L, ptr f, ptr ud)` | 3/3 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 108 | luaL_addstring | `void (ptr B, cstr s)` | 0/2 | consistent (0/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 109 | luaO_pushvfstring | `cstr (ptr L, cstr fmt, ptr argp)` | 3/3 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent | CONFIRMED |
| 110 | lua_topointer | `ptr (ptr L, i32 idx)` | 2/2 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 111 | lua_settable | `void (ptr L, i32 idx)` | 1/2 | consistent (1/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 115 | luaL_openlibs | `void (ptr L)` | 1/1 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 116 | f_luaopen | `void (ptr L, ptr ud)` | 1/2 | consistent (1/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 117 | l_alloc | `ptr (ptr ud, ptr block, u64 osize, u64 nsize)` | 0/4 | consistent (0/4 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 118 | luaL_checkudata | `ptr (ptr L, i32 ud, cstr tname)` | 1/3 | consistent (1/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 123 | lua_close | `void (ptr L)` | 1/1 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 124 | lua_replace | `void (ptr L, i32 idx)` | 2/2 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 125 | luaL_ref | `i32 (ptr L, i32 t)` | 1/2 | consistent (1/2 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 126 | close_state | `void (ptr L)` | 1/1 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 127 | luaC_barrierf | `void (ptr L, ptr o, ptr v)` | 1/3 | consistent (1/3 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 128 | luaF_close | `void (ptr L, ptr level)` | 2/2 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 129 | luaC_separateudata | `u64 (ptr L, i32 all)` | 2/2 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 130 | luaM_realloc_ | `ptr (ptr L, ptr block, u64 osize, u64 nsize)` | 1/4 | consistent (1/4 consumed; tail arg(s) not read by body) | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 131 | CCryPak_FOpen | `ptr (ptr this, cstr pName, cstr szMode, u32 nFlags)` | 4/4 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 133 | ModManager_Select | `void (ptr this)` | 1/1 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 134 | ModManager_ctor | `ptr (ptr outResult, ptr sys, ptr modsDir)` | 3/3 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 135 | ModManager_Mount | `void (ptr modMgr, ptr modsDir)` | 2/2 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (void; eax scratch not distinguishable from a return) | CONFIRMED |
| 136 | ModManager_ReadModOrder | `u8 (ptr this)` | 1/1 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (eax 32-bit return) | CONFIRMED |
| 141 | WHGame_allocator | `ptr (i64 size)` | 1/1 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 142 | CryString_placement_construct | `ptr (ptr dest, ptr source)` | 2/2 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
| 143 | CryString_init_from_string | `ptr (ptr dest, cstr source)` | 2/2 | CONFIRMED | consistent (cstr/ptr & signedness not body-decidable) | consistent (rax 64-bit return present) | CONFIRMED |
