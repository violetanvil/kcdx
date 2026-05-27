"""Find the EXACT function that owns the 'ERROR FOpen' string LEA, using the
.pdata exception table for true function boundaries (no overrun).

.pdata is an array of RUNTIME_FUNCTION { u32 BeginRVA, u32 EndRVA, u32 UnwindRVA }.
We:
 1. Brute-scan .text for LEA rip targeting the FOpen string family (any string
    in 0x1846ABA80..0x1846ABB00 -- the FOpen error format strings cluster).
 2. For each LEA VA, find the containing RUNTIME_FUNCTION -> the function start.
 3. Report the function start VA(s). Cross-check against the CCryPak vtable.
"""
import sys, struct, bisect
import pefile

DLL = sys.argv[1]
pe = pefile.PE(DLL, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase
secs=[]
for s in pe.sections:
    secs.append((s.Name.rstrip(b"\x00").decode("latin1"), image_base+s.VirtualAddress, s.get_data()))
def sec_for(va):
    for n,sva,d in secs:
        if sva<=va<sva+len(d): return n,sva,d
    return None
_,text_va,text_data = sec_for(0x180001000)
_,pdata_va,pdata = sec_for(0x185688000)

# parse .pdata
funcs=[]  # (beginVA, endVA)
for o in range(0, len(pdata)-12+1, 12):
    b,e,u = struct.unpack_from("<III", pdata, o)
    if b==0 and e==0: continue
    funcs.append((image_base+b, image_base+e))
funcs.sort()
starts=[f[0] for f in funcs]
def owner(va):
    i = bisect.bisect_right(starts, va)-1
    if 0<=i<len(funcs) and funcs[i][0]<=va<funcs[i][1]:
        return funcs[i][0]
    return None

# CCryPak vtable slots (from prior dump)
def rq(va):
    s=sec_for(va);
    if not s: return None
    _,sva,d=s;o=va-sva
    return struct.unpack_from("<Q",d,o)[0] if o+8<=len(d) else None
VTABLE=0x183A95FA8
slot_fns={}
for i in range(64):
    fn=rq(VTABLE+i*8)
    if fn is None: break
    slot_fns[fn]=i

# scan for LEAs targeting the FOpen string cluster
LO, HI = 0x1846ABA80, 0x1846ABB40
hits={}
i=0;n=len(text_data)
while True:
    j=text_data.find(b"\x8d",i)
    if j<0 or j+5>n: break
    if j>=1 and 0x48<=text_data[j-1]<=0x4f and (text_data[j+1]&0xC7)==0x05:
        disp=struct.unpack_from("<i",text_data,j+2)[0]
        iva=text_va+(j-1); tgt=iva+7+disp
        if LO<=tgt<HI:
            hits.setdefault(tgt,[]).append(iva)
    i=j+1

print(f"FOpen-string-cluster LEA xrefs (targets in 0x{LO:X}..0x{HI:X}):")
seen_owners=set()
for tgt in sorted(hits):
    for iva in hits[tgt]:
        own=owner(iva)
        tag = f"  ==> CCryPak vtable slot[{slot_fns[own]}]" if own in slot_fns else ""
        own_s = f"0x{own:X}" if own else "None"
        print(f"  str 0x{tgt:X}  LEA @ 0x{iva:X}  owner fn {own_s}{tag}")
        if own: seen_owners.add(own)
print()
print("owners that ARE CCryPak vtable slots:")
for own in seen_owners:
    if own in slot_fns:
        print(f"  fn 0x{own:X} = slot[{slot_fns[own]}] (+0x{slot_fns[own]*8:X})")
