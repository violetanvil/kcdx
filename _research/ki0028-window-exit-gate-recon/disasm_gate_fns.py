"""KI-0028 window-exit-gate — read fn 0x1c1e988 and 0x1c1e91c fully.

These are the two functions the 0x869c39 loop calls on the counters 0x56628d8/dc.
0x1c1e988 is called BEFORE the `cmp [counter],-1; jne <back-edge>` test — it is the
function that DECIDES whether the counter becomes -1 (the loop's exit token).
0x1c1e91c is called on the loop-continue arm (registration/increment side).

Read both bodies. Resolve every rip-global (note .data targets) and direct call
(EnterCriticalSection / LeaveCriticalSection import names via IAT). The goal: state
exactly what writes the counter to -1, and what the counter represents (a once-init
token / a deferred-task id).
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
pe.parse_data_directories()
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

# Build IAT name map: VA of import slot -> name
iat = {}
if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
    for mod in pe.DIRECTORY_ENTRY_IMPORT:
        for imp in mod.imports:
            if imp.name:
                iat[imp.address] = imp.name.decode()  # imp.address is a VA already

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
    return insn.address + insn.size + disp  # VA

def dump(start_rva, span, label):
    print(f"\n=== {label}: fn 0x{start_rva:x} (span 0x{span:x}) ===")
    data = pe.get_data(start_rva, span)
    for insn in md.disasm(data, IB + start_rva):
        rva = insn.address - IB
        note = ""
        if insn.mnemonic == "call" and insn.op_str.startswith("0x"):
            t = int(insn.op_str, 16) - IB
            note += f"  -> fn 0x{t:x}"
        tgt = rip_target(insn)
        if tgt is not None:
            # call/jmp [rip+x] => IAT import?
            if insn.mnemonic in ("call", "jmp") and "[rip" in insn.op_str and tgt in iat:
                note += f"  ; IMPORT {iat[tgt]}"
            else:
                trva = tgt - IB
                s = read_cstr(trva)
                note += f"  ; [{sect(trva)}] 0x{trva:x}" + (f' "{s}"' if s else "")
        print(f"0x{rva:08x} {insn.mnemonic:<8}{insn.op_str}{note}")
        if insn.mnemonic == "ret" or insn.mnemonic == "int3":
            break

dump(0x1c1e988, 0xa0, "EXIT-TOKEN fn (called before cmp counter,-1)")
dump(0x1c1e91c, 0xa0, "REGISTER/INC fn (loop-continue arm)")
