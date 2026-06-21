"""KI-0028 — pin the exit condition of the window/display-mode loop at RVA 0x869c39.

The loop encloses the wedge call site (0x869c36 `call [rax+0x40]`) via back-edges to
0x869bb9 and 0x869b2d, gated by retry counters 0x56628d8 / 0x56628dc (cmp vs -1) and
result flags 0x556d080 (byte) / 0x556d084 (dword). This dumps the WHOLE loop region
(0x869b00..0x869ce0) with FULL annotation: every cmp/test and its branch, every
rip-global read/write (the spun-on state), every call target resolved (direct E8 →
absolute RVA; vtable [rax+off] noted with the offset). Goal: state, as a flat
exit-condition map, WHAT value the loop waits to become true.

Also resolves the direct-call targets in the region so the manager/object functions
can be named, and reads any string at each rip-global (some .data globals are near
string tables).
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
IB = 0x180000000

def rva_off(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.PointerToRawData + (rva - s.VirtualAddress)
    return None

def sect(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.Name.rstrip(b"\x00").decode()
    return "?"

def read_cstr(rva, maxn=120):
    off = rva_off(rva)
    if off is None: return None
    raw = pe.__data__[off:off + maxn]
    end = raw.find(b"\x00")
    if end <= 2: return None
    try:
        s = raw[:end].decode("ascii", errors="ignore")
        return s if s.isprintable() else None
    except Exception:
        return None

def rip_target(insn):
    op = insn.op_str
    if "rip + " in op:
        try: disp = int(op.split("rip + ")[1].split("]")[0].strip(), 16)
        except Exception: return None
    elif "rip - " in op:
        try: disp = -int(op.split("rip - ")[1].split("]")[0].strip(), 16)
        except Exception: return None
    else:
        return None
    return (insn.address + insn.size - IB) + disp

# The full loop region (a little before the loop head, through the back-edges).
START = 0x869b00
END   = 0x869ce0
data = pe.get_data(START, END - START)

print(f"=== window/display-mode loop body 0x{START:x}..0x{END:x} (RVA, real) ===\n")
direct_calls = set()
for insn in md.disasm(data, IB + START):
    rva = insn.address - IB
    note = ""
    # back-edge?
    if insn.mnemonic.startswith("j") and insn.op_str.startswith("0x"):
        try:
            t = int(insn.op_str, 16) - IB
            if t < rva: note += f"  <== BACK-EDGE to 0x{t:x}"
        except Exception:
            pass
    # direct call target
    if insn.mnemonic == "call" and insn.op_str.startswith("0x"):
        try:
            t = int(insn.op_str, 16) - IB
            direct_calls.add(t)
            note += f"  -> fn 0x{t:x}"
        except Exception:
            pass
    # rip-global
    tgt = rip_target(insn)
    if tgt is not None:
        s = read_cstr(tgt)
        note += f"  ; [{sect(tgt)}] 0x{tgt:x}" + (f' "{s}"' if s else "")
    if rva == 0x869c36:
        note += "   <<< WEDGE CALL SITE (return-into 0x869c39)"
    print(f"0x{rva:08x} {insn.mnemonic:<7}{insn.op_str}{note}")

# Name the direct-call targets by scanning each for its first string ref (KI-0026 method).
print(f"\n=== direct-call targets in the loop region: name by first string ref ===")
def first_strings(fn_rva, span=0x400, limit=4):
    try:
        d = pe.get_data(fn_rva, span)
    except Exception:
        return []
    out = []
    for insn in md.disasm(d, IB + fn_rva):
        if insn.mnemonic == "lea" and "rip" in insn.op_str:
            t = rip_target(insn)
            if t is not None:
                s = read_cstr(t)
                if s and s not in out:
                    out.append(s)
                    if len(out) >= limit: break
    return out

for t in sorted(direct_calls):
    ss = first_strings(t)
    print(f"  fn 0x{t:x}: " + ("; ".join(f'"{x}"' for x in ss) if ss else "(no early strings)"))
