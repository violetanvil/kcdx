#!/usr/bin/env python3
"""
abi_rederive.py — re-derive arg COUNT, per-arg WIDTH/pointer-ness, and RETURN
register width for every signature-bearing function row in the Address Library
seed, directly from the function BODY in WHGame.dll. READ-ONLY.

Embeds the proven phase6_abi_walker.py body analysis (recursive disasm following
near jumps; prologue rsp/rbp/r11 tracking; [rsp|rbp|r11+disp] -> incoming-arg-slot
back-map) and adds a DEF-USE register model over the ENTRY STRAIGHT-LINE PATH so
the arg count is a sound lower bound.

ARG-COUNT model (the load-bearing check), validated against ground truth:
  MSVC x64 passes args in rcx,rdx,r8,r9 then the stack (+0x28,+0x30,...).
  Register args: walk the entry fall-through path; an arg register is a CONFIRMED
  incoming arg iff its value is USED (read / mem-index / deref) BEFORE the
  register is first WRITTEN. Args fill in order -> regcount = the contiguous
  prefix 1..k all used-before-write. A spill-to-home (mov [rsp+home],reg) is NOT
  a use (MSVC spills shadow regs regardless of arity). lea on an arg reg is an
  arithmetic USE (counts) but NOT a pointer deref.
  Stack args (arg5+): a CONTIGUOUS run of pre-first-call straight-line LOADS from
  [rsp+0x28..] (entry-relative), gated on regcount==4. (High-rsp reads after a
  call or after the frame grows are outgoing-arg staging / saved-reg restores,
  NOT incoming args.)
  Validated: lua_pcall(1)=4, CGame_Update(2)=3, luaL_loadfile(3)=2,
  ui_pump(4)=1, lua_pcall_internal(106)=5 [the 5-arg stack case], FOpen(131)=4,
  ModManager_ctor(134)=3.

WIDTH/PTR: per arg-register, the width + pointer-deref of its FIRST consuming use
on the entry path (NOT the home-spill width — a spill is always 64-bit). lea =
arithmetic (not ptr). cstr-vs-ptr is NOT body-distinguishable.
RETURN: rax/eax/al writes scanned; f32(xmm0) is blind. Thin `jmp rel32` thunks
are followed to the callee (ABI = callee's).

Usage: python abi_rederive.py   ->   writes _abi_rederive_dump.json
"""
import csv, os, re, json
import pefile
import capstone
from capstone import x86 as X

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = r"c:/Users/Michael/Documents/KCD2 Mods/kcdx"
DLL  = os.path.join(REPO, "third-party-ghidra", "WHGame.dll")
NAMES = os.path.join(REPO, "data/seeds/address_names_seed.csv")
VERS  = os.path.join(REPO, "data/seeds/address_versions_seed.csv")
IMAGE_BASE = 0x180000000

pe = pefile.PE(DLL, fast_load=True)
exec_sections = []
for sec in pe.sections:
    if sec.Characteristics & 0x20000000:
        sva = IMAGE_BASE + sec.VirtualAddress
        sdata = sec.get_data()
        exec_sections.append((sva, sva + len(sdata), sdata))

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True

def sec_for(va):
    for sva, end, sdata in exec_sections:
        if sva <= va < end:
            return sva, sdata
    return None, None

def disasm_at(addr):
    sva, sdata = sec_for(addr)
    if sdata is None:
        return None
    off = addr - sva
    for ins in md.disasm(sdata[off:off+16], addr):
        return ins
    return None

COND_JMPS = {"je","jne","jz","jnz","jg","jl","jge","jle","ja","jb","jae","jbe",
             "js","jns","jo","jno","jp","jnp","jpe","jpo","jcxz","jecxz","jrcxz"}
UNCOND_JMPS = {"jmp"}
TERMINATORS = {"ret","retn","retf","int3"}

ARGREGS = {1:("rcx","ecx","cx","cl"),2:("rdx","edx","dx","dl"),
           3:("r8","r8d","r8w","r8b"),4:("r9","r9d","r9w","r9b")}
SUB_TO_ARG = {}
for k,(r64,r32,r16,r8) in ARGREGS.items():
    for nm in (r64,r32,r16,r8): SUB_TO_ARG[nm]=k
WIDTH_OF_SUB = {}
for k,(r64,r32,r16,r8) in ARGREGS.items():
    WIDTH_OF_SUB[r64]=8; WIDTH_OF_SUB[r32]=4; WIDTH_OF_SUB[r16]=2; WIDTH_OF_SUB[r8]=1

def walk(entry):
    if disasm_at(entry) is None:
        return None, None
    visited=set(); queue=[entry]
    while queue:
        addr=queue.pop(0)
        if addr in visited: continue
        cur=addr; safety=0
        while cur not in visited and safety<4000:
            safety+=1
            ins=disasm_at(cur)
            if ins is None: break
            visited.add(cur)
            mn=ins.mnemonic.lower()
            if mn in TERMINATORS: break
            if mn in UNCOND_JMPS:
                if ins.operands and ins.operands[0].type==X.X86_OP_IMM:
                    t=ins.operands[0].imm
                    if abs(t-entry)<0x200000: queue.append(t)
                break
            if mn in COND_JMPS:
                if ins.operands and ins.operands[0].type==X.X86_OP_IMM:
                    t=ins.operands[0].imm
                    if abs(t-entry)<0x200000: queue.append(t)
            cur=ins.address+ins.size
    if not visited: return None,None
    addrs=sorted(visited); end=addrs[-1]
    li=disasm_at(end)
    if li: end+=li.size
    return set(addrs), end

PROLOGUE_OK = ("push","mov","sub","lea","xor","movsxd")

def analyze(entry):
    res={"entry":entry}
    first=disasm_at(entry)
    if first is None:
        res["error"]="no-disasm-at-entry"; return res
    if first.mnemonic.lower()=="jmp" and first.operands and first.operands[0].type==X.X86_OP_IMM:
        res["tail_jmp"]=first.operands[0].imm

    visited,end=walk(entry)
    if visited is None:
        res["error"]="walk-failed"; return res
    res["ninsns"]=len(visited)

    # ---- prologue rsp/rbp/r11 tracking (linear from entry) ----
    rsp_off=0; rbp_off=None; r11_off=None
    cur=entry; prologue=[]
    while cur<end:
        ins=disasm_at(cur)
        if ins is None: break
        mn=ins.mnemonic.lower()
        if mn not in PROLOGUE_OK: break
        prologue.append(ins)
        if mn=="push": rsp_off-=8
        elif mn=="sub" and ins.op_str.startswith("rsp,"):
            try: rsp_off-=int(ins.op_str.split(",",1)[1].strip(),16)
            except ValueError: pass
        elif mn=="mov" and ins.op_str.startswith("rbp, rsp"): rbp_off=rsp_off
        elif mn=="lea" and "rbp," in ins.op_str:
            if len(ins.operands)>=2 and ins.operands[1].type==X.X86_OP_MEM:
                m=ins.operands[1].mem; base=ins.reg_name(m.base) if m.base else None
                if base=="rsp": rbp_off=rsp_off+m.disp
                elif base=="rbp" and rbp_off is not None: rbp_off=rbp_off+m.disp
        elif mn=="mov" and ins.op_str.startswith("r11, rsp"): r11_off=rsp_off
        cur=ins.address+ins.size
    prologue_end = prologue[-1].address+prologue[-1].size if prologue else entry

    # ---- r11/rbp redefinition guard (epilogue restores) ----
    r11_redef=rbp_redef=None
    for addr in sorted(visited):
        if addr<prologue_end: continue
        ins=disasm_at(addr)
        if not ins: continue
        mn=ins.mnemonic.lower()
        if mn in ("lea","mov") and ins.operands and ins.operands[0].type==X.X86_OP_REG:
            d=ins.reg_name(ins.operands[0].reg)
            if d=="r11" and r11_redef is None: r11_redef=addr
            if d=="rbp" and rbp_redef is None: rbp_redef=addr

    # ---- incoming-arg stack slots (whole-body home/slot reads, for diagnostics) ----
    slots={}
    def note_slot(off,w,kind):
        s=slots.setdefault(off,{"reads":0,"writes":0,"maxw":0})
        if w>s["maxw"]: s["maxw"]=w
        if kind=="read": s["reads"]+=1
        elif kind=="write": s["writes"]+=1
    def slot_kind(ins,opidx):
        mn=ins.mnemonic.lower()
        if mn in ("mov","movzx","movsxd","movsx","lea"): return "write" if opidx==0 else "read"
        if mn in ("cmp","test","push"): return "read"
        return "either"
    for addr in sorted(visited):
        ins=disasm_at(addr)
        if not ins: continue
        ar11=(r11_redef is not None and addr>=r11_redef)
        arbp=(rbp_redef is not None and addr>=rbp_redef)
        for opidx,op in enumerate(ins.operands):
            if op.type!=X.X86_OP_MEM: continue
            m=op.mem
            base=ins.reg_name(m.base) if m.base else None
            idx=ins.reg_name(m.index) if m.index else None
            if idx is not None: continue
            eo=None
            if base=="rsp": eo=rsp_off+m.disp
            elif base=="rbp" and rbp_off is not None and not arbp: eo=rbp_off+m.disp
            elif base=="r11" and r11_off is not None and not ar11: eo=r11_off+m.disp
            if eo is None or not (0x08<=eo<=0x100): continue
            note_slot(eo,op.size,slot_kind(ins,opidx))

    # ---- DEF-USE register model + stack-arg detection over the ENTRY STRAIGHT
    # LINE (fall through; stop at the first branch; step over calls). Args are
    # consumed at the function top before any branch in normal MSVC codegen. ----
    written=set()
    used_before_write={}        # arg-index -> {"width","ptr"} (first use)
    stack_arg_reads=set()       # arg5+ slot indices loaded pre-first-call
    seen_call=False
    cur=entry; steps=0
    while steps<400:
        steps+=1
        ins=disasm_at(cur)
        if ins is None: break
        mn=ins.mnemonic.lower()
        # stack-arg evidence (pre-first-call straight-line loads from [rsp+0x28..])
        if not seen_call:
            for opidx,op in enumerate(ins.operands):
                if op.type!=X.X86_OP_MEM: continue
                m=op.mem
                if not m.base or ins.reg_name(m.base)!="rsp": continue
                if mn=="lea": continue
                if opidx==0 and mn not in ("cmp","test"): continue   # a store
                eo=rsp_off+m.disp
                if 0x28<=eo<=0x80: stack_arg_reads.add(eo//8)
        # movsxd/movsx/movzx record SOURCE-operand width (a sign-extend of edx is
        # a 32-bit arg widened, NOT a 64-bit arg)
        srcw=None
        if mn in ("movsxd","movsx","movzx") and len(ins.operands)==2 and ins.operands[1].type==X.X86_OP_REG:
            srcw=WIDTH_OF_SUB.get(ins.reg_name(ins.operands[1].reg))
        # USES: memory base (deref, EXCEPT lea = arithmetic); memory index; reg read
        for op in ins.operands:
            if op.type==X.X86_OP_MEM and op.mem.base:
                k=SUB_TO_ARG.get(ins.reg_name(op.mem.base))
                if k and k not in written and k not in used_before_write:
                    if mn=="lea":
                        # lea uses the arg reg in address arithmetic. Capstone
                        # always names the base as the 64-bit reg (`[rdx+1]` even
                        # when the logical value is a 32-bit nargs), so the base
                        # name is NOT reliable width evidence. Record the arg as
                        # USED (so it counts) but width=0/unknown and ptr=False,
                        # so a lea never produces a width finding (the id62
                        # `lea eax,[rdx+1]` false-positive class).
                        used_before_write[k]={"width":0,"ptr":False}
                    else:
                        used_before_write[k]={"width":8,"ptr":True}
            if op.type==X.X86_OP_MEM and op.mem.index:
                xn=ins.reg_name(op.mem.index); k=SUB_TO_ARG.get(xn)
                if k and k not in written and k not in used_before_write:
                    used_before_write[k]={"width":WIDTH_OF_SUB.get(xn,8),"ptr":False}
        for opidx,op in enumerate(ins.operands):
            if op.type!=X.X86_OP_REG: continue
            rn=ins.reg_name(op.reg); k=SUB_TO_ARG.get(rn)
            if not k or k in written: continue
            is_dest=(opidx==0 and mn not in ("cmp","test"))
            if not is_dest and k not in used_before_write:
                w=srcw if (srcw is not None and opidx==1) else WIDTH_OF_SUB.get(rn,8)
                used_before_write[k]={"width":w,"ptr":False}
        # DEFs
        if ins.operands and ins.operands[0].type==X.X86_OP_REG and mn not in ("cmp","test","push"):
            k=SUB_TO_ARG.get(ins.reg_name(ins.operands[0].reg))
            if k is not None: written.add(k)
        if mn=="call":
            written.update({1,2,3,4}); seen_call=True
        if mn in TERMINATORS: break
        if (mn in UNCOND_JMPS or mn in COND_JMPS): break   # end straight-line region
        cur=ins.address+ins.size

    regcount=0
    for k in (1,2,3,4):
        if k in used_before_write: regcount=k
        else: break
    stack_max=0
    if regcount==4:
        k=5
        while k in stack_arg_reads:
            stack_max=k; k+=1
    derived=max(regcount,stack_max)

    res["slots"]=slots
    res["used_before_write"]=used_before_write
    res["regcount"]=regcount
    res["stack_max"]=stack_max
    res["derived_argcount_lb"]=derived

    # ---- return register scan ----
    raxw=[]
    for addr in sorted(visited):
        ins=disasm_at(addr)
        if not ins or not ins.operands: continue
        if ins.operands[0].type!=X.X86_OP_REG: continue
        d=ins.reg_name(ins.operands[0].reg)
        w = 8 if d=="rax" else 4 if d=="eax" else 1 if d=="al" else 0
        if not w: continue
        raxw.append((addr,w,ins.mnemonic.lower()=="lea",ins.mnemonic,ins.op_str))
    res["ret_widths_seen"]=sorted(set(w for (_,w,_,_,_) in raxw))
    res["rax_writes_tail"]=[f"{'lea ' if l else ''}{mn} {ops} (w{w})" for (_,w,l,mn,ops) in raxw[-4:]]
    return res

def parse_sig(sig):
    m=re.match(r"^\s*(\w+)\s*\((.*)\)\s*$", sig.strip())
    if not m: return None,None
    ret=m.group(1); inner=m.group(2).strip()
    if inner=="" or inner.lower()=="void": return ret,[]
    return ret,[p.strip().split()[0] for p in inner.split(",")]

def main():
    names={r["id"]:r["name"] for r in csv.DictReader(open(NAMES,newline="",encoding="utf-8"))}
    rows=list(csv.DictReader(open(VERS,newline="",encoding="utf-8")))
    targets=[r for r in rows if r["kind"] in ("function","function_variadic") and r["signature"].strip()]

    out=[]
    for r in targets:
        rid=r["kcdx_id"]; nm=names.get(rid,"?"); sig=r["signature"]; kind=r["kind"]
        entry=IMAGE_BASE+int(r["rva"],16)
        ret,argtypes=parse_sig(sig)
        rec={"id":rid,"name":nm,"kind":kind,"rva":r["rva"],"sig":sig,"ret":ret,
             "argtypes":argtypes,"declared_argcount":len(argtypes) if argtypes is not None else None}
        a=analyze(entry)
        if a.get("tail_jmp") and a.get("derived_argcount_lb",0)==0 and "error" not in a:
            rec["tail_jmp_to"]=f"0x{a['tail_jmp']:X}"
            a2=analyze(a["tail_jmp"])
            if "error" not in a2: a=a2
        elif a.get("tail_jmp"):
            rec["tail_jmp_to_noted"]=f"0x{a['tail_jmp']:X}"
        if "error" in a:
            rec["walker"]="FAILED"; rec["walker_error"]=a["error"]; out.append(rec); continue
        rec["walker"]="OK"; rec["ninsns"]=a["ninsns"]
        rec["derived_argcount_lb"]=a["derived_argcount_lb"]
        rec["regcount"]=a["regcount"]; rec["stack_max"]=a["stack_max"]
        ubw=a["used_before_write"]; slots=a["slots"]
        per={}
        ub=max(len(argtypes) if argtypes else 0, a["derived_argcount_lb"])
        for i in range(1,ub+1):
            info={}
            s=slots.get(i*8)
            if i in (1,2,3,4):
                u=ubw.get(i)
                info["reg_used_before_write"]=bool(u)
                info["reg_ptr"]=u["ptr"] if u else False
                info["reg_width"]=u["width"] if u else 0   # FIRST-USE width only
            info["slot_reads"]=s["reads"] if s else 0
            info["slot_maxw"]=s["maxw"] if s else 0
            per[i]=info
        rec["per_arg"]=per
        rec["ret_widths_seen"]=a["ret_widths_seen"]
        rec["rax_writes_tail"]=a["rax_writes_tail"]
        out.append(rec)

    with open(os.path.join(HERE,"_abi_rederive_dump.json"),"w",encoding="utf-8") as fh:
        json.dump(out,fh,indent=1)
    byid={r["id"]:r for r in out}
    with open(os.path.join(HERE,"_abi_rederive_count.txt"),"w") as fh:
        fh.write("targets=%d failed=%d\n"%(len(out),sum(1 for r in out if r.get("walker")=="FAILED")))
        eq=sum(1 for r in out if r.get("derived_argcount_lb")==r["declared_argcount"])
        over=[(r["id"],r["name"],r["derived_argcount_lb"],r["declared_argcount"]) for r in out if r.get("derived_argcount_lb",0)>r["declared_argcount"]]
        fh.write("count: equal=%d over(hard-mismatch)=%d under=%d\n"%(eq,len(over),len(out)-eq-len(over)))
        fh.write("over=%r\n"%over)
        for i,exp in (("1",4),("2",3),("3",2),("4",1),("106",5),("131",4),("134",3)):
            if i in byid:
                got=byid[i].get("derived_argcount_lb")
                fh.write("anchor id%s: derived=%s expect=%s %s\n"%(i,got,exp,"OK" if got==exp else "DIFF"))

if __name__=="__main__":
    main()
