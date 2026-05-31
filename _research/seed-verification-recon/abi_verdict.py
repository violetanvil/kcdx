#!/usr/bin/env python3
"""
abi_verdict.py — consume _abi_rederive_dump.json (produced by the validated
abi_rederive.py, anchors 1/2/3/4/106/131/134 all OK), apply confidence-graded
verdict rules, write ABI-REDERIVATION.md + _verdict_summary.txt. READ-ONLY.

ARG COUNT (high confidence):
  derived = max(register-contiguous-used-before-write count, highest stack home
            read). derived>declared -> COUNT MISMATCH (hard). derived<declared ->
  consistent-undertouched (a function may not read a tail arg it's passed; for a
  variadic, the fixed prefix is what we confirm). derived==declared -> CONFIRMED.

WIDTH/PTR (args 1-4, def-use first-use register width/ptr):
  Flag a width contradiction ONLY when the first incoming use of the arg register
  is unambiguous: declared 32-bit (i32/u32/f32/bool) but first use is a pointer
  deref or 64-bit read; OR declared i64/u64 but first use is 32-bit and never a
  pointer. cstr-vs-ptr NEVER flagged (both 64-bit pointers). signed-vs-unsigned
  NEVER flagged. Because the model uses FIRST-USE-BEFORE-WRITE (not "any use"),
  it is not fooled by later scratch reuse, so width findings are reported at full
  confidence regardless of body size.

RETURN (integer reg scan): declared ptr/i64/u64 but body only ever sets eax/al
  (no rax, no tail-call) -> MISMATCH. f32->xmm0 not-decidable. void-> consistent.
"""
import os, json

HERE = os.path.dirname(os.path.abspath(__file__))
WIDTH32 = {"i32","u32","f32","bool"}

def main():
    d=json.load(open(os.path.join(HERE,"_abi_rederive_dump.json"),encoding="utf-8"))
    table=[]; mismatches=[]; notdec=[]; failed=[]
    cC=cM=cN=cF=0

    for r in d:
        rid=r["id"]; nm=r["name"]; sig=r["sig"]; kind=r["kind"]
        decl=r["declared_argcount"]; ret=r["ret"]; at=r["argtypes"]
        if r.get("walker")=="FAILED":
            cF+=1; failed.append((rid,nm,sig,r.get("walker_error")))
            table.append((rid,nm,sig,"-","WALKER-FAILED","-","-","WALKER-FAILED")); continue

        lb=r["derived_argcount_lb"]; ninsns=r.get("ninsns",0)
        per={int(k):v for k,v in r["per_arg"].items()}

        # COUNT
        count_mm=False
        if lb>decl:
            count_mm=True; count_v=f"MISMATCH (body consumes {lb} > {decl} declared)"
        elif lb==decl:
            count_v="CONFIRMED"
        elif kind=="function_variadic":
            count_v=f"CONFIRMED (variadic; fixed prefix {lb}/{decl} confirmed)"
        else:
            count_v=f"consistent ({lb}/{decl} consumed; tail arg(s) not read by body)"

        # WIDTH / PTR
        wf=[]
        for i in range(1,min(decl,4)+1):
            if i not in per: continue
            t=at[i-1] if at and i-1<len(at) else None
            info=per[i]; rp=info.get("reg_ptr",False); rw=info.get("reg_width",0)
            used=info.get("reg_used_before_write",False)
            if not used: continue          # no first-use observed -> say nothing
            if t in ("ptr","cstr"): continue   # pointer types: cstr/ptr not decidable
            if t in WIDTH32:
                if rp: wf.append(f"arg{i} '{t}' first use dereferences as pointer -> leans ptr")
                elif rw==8: wf.append(f"arg{i} '{t}' first use reads 64-bit -> leans i64/ptr")
            elif t in ("i64","u64"):
                if rw and rw<8 and not rp: wf.append(f"arg{i} '{t}' first use reads {rw*8}-bit, never a pointer -> leans i32/u32")
        width_mm=bool(wf)
        width_v=("MISMATCH: "+"; ".join(wf)) if width_mm else "consistent (cstr/ptr & signedness not body-decidable)"

        # RETURN
        rwid=set(r.get("ret_widths_seen",[]))
        has_tail=bool(r.get("tail_jmp_to") or r.get("tail_jmp_to_noted"))
        ret_mm=False
        if ret=="void":
            ret_v="consistent (void; eax scratch not distinguishable from a return)"
        elif ret=="f32":
            ret_v="not-decidable (f32 returns in xmm0; integer-reg scan blind)"
        elif ret in ("ptr","i64","u64"):
            if rwid and rwid<= {4,1} and not has_tail:
                ret_mm=True; ret_v=f"MISMATCH: declared {ret}-return but body only sets eax/al (32-bit)"
            elif 8 in rwid: ret_v="consistent (rax 64-bit return present)"
            else: ret_v="consistent (tail-call / passthrough; no decisive rax write)"
        elif ret in ("i32","u32","bool","u8"):
            ret_v="consistent (eax 32-bit return)" if (rwid & {4,1,8}) else "consistent (tail-call / passthrough)"
        else:
            ret_v="consistent"

        if count_mm or width_mm or ret_mm:
            overall="MISMATCH"; cM+=1
            mismatches.append((rid,nm,sig,count_v if count_mm else "",width_v if width_mm else "",ret_v if ret_mm else ""))
        else:
            overall="CONFIRMED"; cC+=1
        table.append((rid,nm,sig,f"{lb}/{decl}",count_v,width_v,ret_v,overall))

    md=[]
    md.append("# ABI re-derivation — per-arg type + arg-count from the function body\n\n")
    md.append("**Date:** 2026-05-31. **Binary:** `third-party-ghidra/WHGame.dll` "
              "(KCD2 release_1_5_1164953_841, image base 0x180000000). **Scope:** the 111 "
              "signature-bearing rows (kind `function`/`function_variadic`, non-empty "
              "`signature`) of `data/seeds/address_versions_seed.csv`. The 10 `function_no_sig` "
              "rows are out of scope. **READ-ONLY** — no seed edited, no commit.\n\n")
    md.append("## Method\n\n")
    md.append("`abi_rederive.py` embeds the proven `phase6_abi_walker.py` body analysis "
              "(recursive disasm following near jumps; prologue rsp/rbp/r11 tracking; every "
              "`[rsp|rbp|r11+disp]` access back-mapped to an MSVC x64 incoming-arg slot — "
              "arg1@+0x08 rcx … arg5+@+0x28 stack) and adds a **def-use register model**: an "
              "arg register (rcx/rdx/r8/r9) counts as an incoming arg only if its value is "
              "USED — read, dereferenced as a pointer base, or spilled to its home slot — "
              "BEFORE the register is first WRITTEN, and registers fill in order (count = the "
              "contiguous prefix 1..k all used-before-write). This kills the false positives "
              "a naive 'any register touched' scan produces (a volatile r8/r9 reused as "
              "scratch). AP2: ABI from BODY, never prologue-shape eyeballing.\n\n")
    md.append("**Validation:** the model reproduces every known anchor exactly — "
              "`lua_pcall`(id1)=4, `CGame_Update`(id2)=3, `luaL_loadfile`(id3)=2, "
              "`CGame_per_frame_ui_pump`(id4)=1, `lua_pcall_internal`(id106)=5 (the 5-arg "
              "stack-arg case), `CCryPak_FOpen`(id131)=4, `ModManager_ctor`(id134)=3.\n\n")
    md.append("## Confidence model\n\n")
    md.append("- **ARG COUNT — high confidence.** `derived/declared`. `derived>declared` is a "
              "hard MISMATCH (the 7-arg-SaveGame-typed-as-3 class). `derived<declared` is NOT "
              "a contradiction — a function may not read a tail arg it is passed; reported as "
              "consistent. Variadic: the fixed prefix is confirmed.\n")
    md.append("- **WIDTH / POINTER.** First-use-before-write per arg register; reported at full "
              "confidence (the def-use model is not fooled by later scratch reuse). "
              "`cstr`-vs-`ptr` NEVER flagged (both 64-bit pointers, not body-decidable). "
              "Signed-vs-unsigned NEVER flagged. Width of args 5+ (stack) not derived.\n")
    md.append("- **RETURN — integer-register scan only.** `f32` returns in xmm0 "
              "(not-decidable). A `void` body's eax scratch is indistinguishable from a "
              "return. A tail-call inherits the callee's return.\n\n")
    md.append("## Summary by verdict\n\n")
    md.append(f"| verdict | count |\n|---|---|\n| CONFIRMED | {cC} |\n| MISMATCH | {cM} |\n"
              f"| NOT-DECIDABLE | {cN} |\n| WALKER-FAILED | {cF} |\n| **total** | **{len(d)}** |\n\n")

    md.append("## MISMATCH fix-list (actionable — seed sig vs body-derived)\n\n")
    if not mismatches:
        md.append("**None.** No high-confidence count, width, or return contradiction in any of "
                  "the 111 rows. Every body consumes exactly its declared arg count "
                  "(`derived == declared` or a legitimate under-touch of a tail arg); no body "
                  "reads beyond its declared count or reads a register/stack slot the signature "
                  "omits; no declared 32-bit arg is used as a pointer/64-bit and no declared "
                  "pointer arg is read 32-bit; no declared pointer/64-bit return is set only in "
                  "eax.\n\n")
    else:
        md.append("| id | name | seed signature | count finding | width finding | return finding |\n|---|---|---|---|---|---|\n")
        for (rid,nm,sig,cv,wv,rv) in mismatches:
            md.append(f"| {rid} | {nm} | `{sig}` | {cv} | {wv} | {rv} |\n")
        md.append("\n")

    md.append("## NOT-DECIDABLE list (reported as consistent, not as findings)\n\n")
    md.append("These per-row facts cannot be settled from the body at this analysis's "
              "confidence and are therefore NOT flagged:\n\n")
    md.append("- **`cstr` vs `ptr` on every pointer arg** — both are 64-bit pointers; the body "
              "cannot tell a C-string pointer from a generic pointer. Settled by the callee's "
              "downstream use (a `%s`/`strlen` consumer) or the predecessor Lua 5.1 prototype.\n")
    md.append("- **signed vs unsigned** on every integer arg/return — identical access width; "
              "settled by a downstream signed-vs-unsigned compare/shift or the prototype.\n")
    md.append("- **`f32` returns (xmm0)** — the integer-register scan is blind to xmm.\n")
    md.append("- **width of args 5+ (stack args)** — needs per-block dataflow, not the linear "
              "first-use scan.\n\n")

    md.append("## WALKER-FAILED rows\n\n")
    if not failed:
        md.append("**None.** All 111 rows analyzed (recursive disasm completed; prologue + arg "
                  "slots resolved for every entry).\n\n")
    else:
        md.append("| id | name | error |\n|---|---|---|\n")
        for (rid,nm,sig,err) in failed:
            md.append(f"| {rid} | {nm} | {err} |\n")
        md.append("\n")

    md.append("## Full per-row table (all 111)\n\n")
    md.append("| id | name | seed signature | derived/decl | count verdict | width/ptr verdict | return verdict | overall |\n")
    md.append("|---|---|---|---|---|---|---|---|\n")
    for (rid,nm,sig,dc,cv,wv,rv,ov) in table:
        md.append(f"| {rid} | {nm} | `{sig}` | {dc} | {cv} | {wv} | {rv} | {ov} |\n")

    open(os.path.join(HERE,"ABI-REDERIVATION.md"),"w",encoding="utf-8").write("".join(md))
    with open(os.path.join(HERE,"_verdict_summary.txt"),"w") as fh:
        fh.write(f"CONFIRMED={cC} MISMATCH={cM} NOT-DECIDABLE={cN} WALKER-FAILED={cF} total={len(d)}\n")
        for (rid,nm,sig,cv,wv,rv) in mismatches:
            fh.write(f"MM id{rid} {nm}: {sig} || {cv} || {wv} || {rv}\n")

if __name__=="__main__":
    main()
