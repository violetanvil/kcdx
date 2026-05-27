# luaD_rawrunprotected — RVA 0x71A6A8

**Confidence**: verified

Not part of the LUA_API/LUALIB_API surface (it's an internal helper in
ldo.c), but identified along the call graph because lua_pcall reaches
it through luaD_pcall. Recorded here so future walkers don't re-discover
it independently.

## Evidence

### Source (ldo.c)

```c
int luaD_rawrunprotected (lua_State *L, Pfunc f, void *ud) {
  struct lua_longjmp lj;
  lj.status = 0;
  lj.previous = L->errorJmp;  /* chain new error handler */
  L->errorJmp = &lj;
  LUAI_TRY(L, &lj,
    (*f)(L, ud);
  );
  L->errorJmp = lj.previous;  /* restore old error handler */
  return lj.status;
}
```

The LUAI_TRY macro in CryEngine's build is the standard
setjmp/longjmp variant: `if (setjmp(lj.b) == 0) { (*f)(L, ud); }`.

### WHGame body shape (RVA 0x71A6A8, verbose disasm)

```
+0x00  mov [rsp+0x20], rbx              ; save rbx
+0x05  push rbp; lea rbp, [rsp-0x70]    ; frame setup
+0x0b  sub rsp, 0x170                   ; stack alloc (room for jmp_buf)
+0x12  mov rax, [rip+...]               ; load __security_cookie
+0x19  xor rax, rsp; mov [rbp+0x60], rax ; store stack guard
+0x20  mov dword ptr [rbp+0x50], 0      ; lj.status = 0
+0x27  mov rax, [rcx+0xA8]              ; load L->errorJmp (lj.previous)
+0x2e  mov [rsp+0x40], rax
+0x33  lea rax, [rsp+0x40]; mov [rcx+0xA8], rax ; L->errorJmp = &lj
+0x3f  mov [rsp+0x20], rcx              ; save L
+0x44  lea rcx, [rsp+0x50]              ; rcx = &lj.b (jmp_buf)
+0x49  mov [rsp+0x30], rdx              ; save f
+0x4e  mov rdx, rsp                     ; rdx = frame ptr (2nd setjmp arg)
+0x51  mov [rsp+0x28], r8               ; save ud
+0x56  call 0x1d938e3                   ; _setjmp(jmp_buf, frame)
+0x5b  mov rbx, [rsp+0x20]              ; reload L
+0x60  test eax, eax
+0x62  jne 0x71A718                     ; setjmp returned non-zero (longjmp path)
+0x64  mov rdx, [rsp+0x28]              ; reload ud
+0x69  mov rcx, rbx                     ; L
+0x6c  call qword ptr [rsp+0x30]        ; (*f)(L, ud)
+0x70  mov rax, [rsp+0x40]              ; ... restore L->errorJmp
```

Key signature:
- `lj.status = 0` stored at fixed offset
- `L->errorJmp` chain manipulation through [rcx+0xA8]
- Call to `_setjmp` (RVA 0x1d938e3 — distinctive CRT routine)
- Indirect call through saved function pointer

This is unambiguously `luaD_rawrunprotected`.

### Linker adjacency

Immediately follows `luaD_pcall` (which ends at ~0x71A6A6 and calls
0x71A6A8 on its hot path).

## Verified callees

| Target RVA | Identity | Evidence |
|------------|----------|----------|
| 0x1D938E3  | `_setjmp` (CRT) | distinctive Windows amd64 setjmp prologue / xref count; not Lua surface, not harvested |
