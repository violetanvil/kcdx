"""KI-0028 — full body of the 0x869c39 function from its real entry, with CFG arrows.

Find the function entry (back-scan from 0x869b00 to the int3 pad / aligned prologue),
disassemble the whole body, and for every conditional/unconditional branch annotate
whether the target is FORWARD (toward the ret) or BACK (a loop re-entry), and tag the
ret/epilogue. Goal: state, unambiguously, which conditional branch is THE loop's exit
(fall-through-or-jump to the epilogue) and which exact field its predecessor test reads.
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
    raw = pe.__data__[off:off+maxn]; end = raw.find(b"\x00")
    if end <= 2: return None
    try:
        s = raw[:end].decode("ascii", "ignore"); return s if s.isprintable() else None
    except Exception: return None
def rip_disp_rva(insn):
    op = insn.op_str
    if "rip + " in op:
        try: d = int(op.split("rip + ")[1].split("]")[0].strip(), 16)
        except Exception: return None
    elif "rip - " in op:
        try: d = -int(op.split("rip - ")[1].split("]")[0].strip(), 16)
        except Exception: return None
    else: return None
    return (insn.address + insn.size - IB) + d

# Find entry: back-scan for the int3 pad before the function (the loop body sits ~0x869b00,
# wedge at 0x869c36; epilogue ends ~0x869c5b; the prologue is before 0x869b00).
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
tdata = text.get_data(); tva = text.VirtualAddress
# scan backward from 0x869b00 for a CC pad boundary (int3; int3 then real opcode)
start = 0x869b00 - tva
j = start
while j > start - 0x400:
    if tdata[j] == 0xCC and tdata[j+1] != 0xCC:
        entry = tva + j + 1; break
    j -= 1
else:
    entry = 0x869a00  # fallback

END = 0x869c5c  # first int3-bounded epilogue end region; we print through the back-edge clusters too
END = 0x869cc6
data = pe.get_data(entry, END - entry)
print(f"=== fn entry 0x{entry:x} .. 0x{END:x} ===\n")
for insn in md.disasm(data, IB + entry):
    rva = insn.address - IB
    note = ""
    if insn.mnemonic.startswith("j") and insn.op_str.startswith("0x"):
        t = int(insn.op_str, 16) - IB
        note += f"  [{'BACK' if t < rva else 'fwd'} -> 0x{t:x}]"
    if insn.mnemonic == "call" and insn.op_str.startswith("0x"):
        note += f"  -> fn 0x{int(insn.op_str,16)-IB:x}"
    tgt = rip_disp_rva(insn)
    if tgt is not None:
        s = read_cstr(tgt)
        note += f"  ;[{sect(tgt)}]0x{tgt:x}" + (f' "{s}"' if s else "")
    if rva == 0x869c36: note += "   <<< WEDGE"
    if insn.mnemonic == "ret": note += "   <<< RET (clean exit)"
    print(f"0x{rva:08x} {insn.mnemonic:<8}{insn.op_str}{note}")
