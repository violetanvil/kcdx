"""KI-0028 P-J.3 static read (architect-review owed step): identify the function
Main was running compute in — RVA 0x536120 (`movss xmm0,[rcx+0x1460]`) — and its
whole frame chain. KI-0026 string-ref method on the CONTAINING functions (walk to
entry, scan full body for string refs that name the subsystem). The P-J.3 stack was:
  0x536120 (movss [rcx+0x1460]) <- 0x536018 <- 0x534135 <- 0x53322e <- 0x53212e <- 0x36eb39
Scan each for string refs + identify likely render/present/device markers."""
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

def read_cstr(rva, maxn=200):
    off = rva_off(rva)
    if off is None: return None
    raw = pe.__data__[off:off + maxn]
    end = raw.find(b'\x00')
    if end <= 0: return None
    try:
        s = raw[:end].decode('ascii', errors='ignore')
        return s if s.isprintable() and len(s) > 2 else None
    except Exception:
        return None

def find_entry(ret_rva, back=0x4000):
    off = rva_off(ret_rva)
    if off is None: return None
    raw = pe.__data__[off - back:off + 16]
    best = None; i = 0
    while i < len(raw) - 3:
        if raw[i] == 0xcc and raw[i+1] == 0xcc:
            j = i
            while j < len(raw) and raw[j] == 0xcc: j += 1
            if j < len(raw): best = (off - back) + j
        i += 1
    if best is None: return None
    for s in pe.sections:
        if s.PointerToRawData <= best < s.PointerToRawData + s.SizeOfRawData:
            return s.VirtualAddress + (best - s.PointerToRawData)
    return None

# P-J.3 frame chain (return-into RVAs from the stack)
FRAMES = [
    ('main_rip_movss_0x536120', 0x536120),
    ('caller_0x536018',         0x536018),
    ('caller_0x534135',         0x534135),
    ('caller_0x53322e',         0x53322e),
    ('caller_0x53212e',         0x53212e),
    ('caller_0x36eb39',         0x36eb39),
]

for name, rva in FRAMES:
    print(f'\n========== {name} (RVA 0x{rva:x}) ==========')
    entry = find_entry(rva)
    if entry is None:
        print('   (no entry found)'); continue
    span = (rva - entry) + 0x120
    if span <= 0 or span > 0x4000: span = 0x1000
    print(f'   entry ~0x{entry:x}  body span 0x{span:x}')
    try:
        data = pe.get_data(entry, span)
    except Exception as e:
        print(f'   (read failed: {e})'); continue
    strs = []; calls_to_imports = []
    for insn in md.disasm(data, IB + entry):
        if insn.mnemonic == 'lea' and 'rip' in insn.op_str:
            try:
                if 'rip + ' in insn.op_str:
                    disp = int(insn.op_str.split('rip + ')[1].rstrip(']').strip(), 16)
                else:
                    disp = -int(insn.op_str.split('rip - ')[1].rstrip(']').strip(), 16)
                tgt = (insn.address + insn.size - IB) + disp
                s = read_cstr(tgt)
                if s: strs.append((hex(insn.address - IB), s))
            except Exception:
                pass
    seen = set()
    print(f'   string refs ({len(strs)}):')
    for r, s in strs:
        if s in seen: continue
        seen.add(s)
        print(f'      {r}: "{s}"')
    if not strs:
        print('      (none — compute/leaf with no string literals)')
