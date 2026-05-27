"""Phase 8.5a step 3: dump the full CCryPak vtable @ 0x183A95FA8 and identify
the open-by-path (FOpen) method.

Strategy:
 (1) Dump all vtable slots until the run of .text pointers ends.
 (2) Robust LEA-xref finder for the FOpen error string and CryPak.cpp asserts:
     scan ALL `48 8D /r rip` and `4C 8D /r rip` forms across .text; for any
     whose computed target lands in a window around the FOpen anchor, report
     the owning region.
 (3) For each vtable slot fn, check whether the function body contains an LEA
     to the FOpen error string (0x1846ABA99) or the "CryPak.cpp" assert string
     (0x183A3B95D) -- that slot is FOpen. Bounded linear decode of each slot fn.
"""
import sys, struct
import pefile, capstone

DLL = sys.argv[1]
pe = pefile.PE(DLL, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase
secs = []
for sec in pe.sections:
    name = sec.Name.rstrip(b"\x00").decode("latin1")
    sva = image_base + sec.VirtualAddress
    secs.append((name, sva, sec.get_data()))
def sec_for(va):
    for n, sva, d in secs:
        if sva <= va < sva+len(d): return n, sva, d
    return None
def rq(va):
    s=sec_for(va);
    if not s: return None
    _,sva,d=s; o=va-sva
    return struct.unpack_from("<Q",d,o)[0] if o+8<=len(d) else None

_, text_va, text_data = sec_for(0x180001000)
text_end = text_va + len(text_data)
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True

VTABLE = 0x183A95FA8
FOPEN_STR = 0x1846ABA99       # "ERROR FOpen '%s'"
CRYPAK_CPP = 0x183A3B95D      # "System\CryPak.cpp"

# (1) Dump vtable until non-.text run
print(f"=== CCryPak vtable @ 0x{VTABLE:X} ===")
slots = []
for i in range(64):
    fn = rq(VTABLE + i*8)
    if fn is None: break
    in_text = text_va <= fn < text_end
    if not in_text:
        # allow a couple of nulls? stop at first non-text.
        print(f"  slot[{i:2d}] @ +0x{i*8:02X} -> 0x{fn:X}  (NOT .text) -- stop")
        break
    slots.append((i, fn))
    print(f"  slot[{i:2d}] @ +0x{i*8:02X} -> 0x{fn:X}")
print(f"  ({len(slots)} method slots)\n")

# helper: does function body (bounded walk) LEA-reference target VA?
def body_lea_targets(fn_va, max_insns=4000):
    """Linear-decode the function (follow no branches) up to max_insns or a
    likely end; collect all rip-relative LEA targets. Returns set of VAs.
    Stops at an int3 padding run or after max_insns."""
    targets = set()
    o = fn_va - text_va
    count = 0
    int3run = 0
    while count < max_insns and 0 <= o < len(text_data):
        chunk = text_data[o:o+15]
        ins = None
        for x in md.disasm(chunk, text_va+o):
            ins = x; break
        if ins is None:
            break
        count += 1
        mn = ins.mnemonic
        if ins.bytes == b"\xcc":
            int3run += 1
            if int3run >= 4: break
        else:
            int3run = 0
        if mn == "lea":
            for op in ins.operands:
                if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
                    tgt = ins.address + ins.size + op.mem.disp
                    targets.add(tgt)
        # stop heuristic: a `ret` followed by int3 padding usually ends; but
        # functions have multiple rets. Just rely on max_insns + int3 run.
        o += ins.size
    return targets

print("=== Which slot references the FOpen error string / CryPak.cpp assert? ===")
WIN = 0x60  # accept LEA targets within a small window of the anchor (format-string families sit adjacent)
for i, fn in slots:
    tg = body_lea_targets(fn)
    hit_fopen = any(abs(t - FOPEN_STR) < WIN for t in tg)
    hit_cpp   = any(abs(t - CRYPAK_CPP) < 0x400 for t in tg)
    if hit_fopen or hit_cpp:
        flags = []
        if hit_fopen: flags.append("FOPEN-STR")
        if hit_cpp: flags.append("CryPak.cpp")
        print(f"  slot[{i}] fn 0x{fn:X}  <== {' + '.join(flags)}")
print()

# Also report which functions ANYWHERE reference the FOpen string (not just vtable)
print("=== global owners of FOpen error string (scan first .text fn containing an LEA to it) ===")
# brute scan .text for any LEA (48/4c 8D modrm=05) whose target == FOPEN_STR
i = 0; n = len(text_data); hits=[]
while True:
    j = text_data.find(b"\x8d", i)
    if j < 0 or j+5 > n: break
    if j>=1 and 0x48 <= text_data[j-1] <= 0x4f and (text_data[j+1] & 0xC7)==0x05:
        disp = struct.unpack_from("<i", text_data, j+2)[0]
        iva = text_va + (j-1)
        tgt = iva + 7 + disp
        if tgt == FOPEN_STR:
            hits.append(iva)
    i = j+1
print(f"  LEA->FOpenStr count: {len(hits)}")
for h in hits[:10]:
    print(f"    LEA @ 0x{h:X}")
