"""KI-0028 / KI-0026-method: identify the REAL functions the RenderThread is
blocked in. The cdb frame labels (NVSDK_NGX_UpdateFeature / ffxFsr2ResourceIsNull)
are nearest-export NOISE (offsets 2-9 MB past the export). For each real RVA:
  1. walk BACKWARD from the return-into RVA to the function ENTRY (prologue:
     'mov [rsp+...],reg' / 'push rbx' / 'sub rsp,imm' preceded by int3/ret pad),
  2. scan the WHOLE function body for lea rcx/rdx,[rip+str] string refs,
  3. report the strings -> they name the subsystem (KI-0026's identify-by-strings).
No tight +-0x80 window this time; scan entry..entry+0x600."""
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

def read_cstr(rva, maxn=160):
    off = rva_off(rva)
    if off is None: return None
    raw = pe.__data__[off:off + maxn]
    end = raw.find(b'\x00')
    if end <= 0: return None
    try:
        s = raw[:end].decode('ascii')
        return s if s.isprintable() else None
    except Exception:
        return None

def find_entry(ret_rva, back=0x2000):
    """Scan backward for the nearest function prologue. Heuristic: a run of
    int3 (0xcc) padding immediately followed by a standard prologue start."""
    off = rva_off(ret_rva)
    if off is None: return None
    start_off = off - back
    raw = pe.__data__[start_off:off + 16]
    # find last occurrence of int3 padding (>=2 0xcc) -> next byte is likely entry
    best = None
    i = 0
    while i < len(raw) - 2:
        if raw[i] == 0xcc and raw[i+1] == 0xcc:
            j = i
            while j < len(raw) and raw[j] == 0xcc: j += 1
            if j < len(raw):
                best = start_off + j
        i += 1
    if best is None: return None
    return best - pe.sections[0].PointerToRawData + pe.sections[0].VirtualAddress if False else (best, ret_rva)

def entry_rva_from_off(off):
    for s in pe.sections:
        if s.PointerToRawData <= off < s.PointerToRawData + s.SizeOfRawData:
            return s.VirtualAddress + (off - s.PointerToRawData)
    return None

TARGETS = {
    'renderwait_cnd_caller_0x1de928e': 0x1de928e,  # the frame holding _Cnd_wait
    'renderwait_caller2_0x9acdfb':     0x9acdfb,    # one frame up
    'workerthread_entry_0xa62b86':     0xa62b86,    # shared bottom frame (suspect: thread_start trampoline)
}

for name, ret_rva in TARGETS.items():
    print(f'\n========== {name} (return-into RVA 0x{ret_rva:x}) ==========')
    res = find_entry(ret_rva)
    if res is None:
        print('   (could not locate entry)'); continue
    entry_off, _ = res
    entry_rva = entry_rva_from_off(entry_off)
    print(f'   function ENTRY ~ RVA 0x{entry_rva:x}  (size to ret ~0x{ret_rva-entry_rva:x})')
    # disasm entry .. ret+0x40, collect every rip-relative string ref
    span = (ret_rva - entry_rva) + 0x80
    if span <= 0 or span > 0x3000: span = 0x800
    data = pe.get_data(entry_rva, span)
    strs = []
    calls = []
    for insn in md.disasm(data, IB + entry_rva):
        r = insn.address - IB
        if insn.mnemonic == 'lea' and 'rip' in insn.op_str:
            try:
                if 'rip + ' in insn.op_str:
                    disp = int(insn.op_str.split('rip + ')[1].rstrip(']').strip(), 16)
                else:
                    disp = -int(insn.op_str.split('rip - ')[1].rstrip(']').strip(), 16)
                tgt = (insn.address + insn.size - IB) + disp
                s = read_cstr(tgt)
                if s and len(s) > 3:
                    strs.append((hex(r), s))
            except Exception:
                pass
    print(f'   string refs in body ({len(strs)}):')
    for r, s in strs[:20]:
        print(f'      {r}: "{s}"')
    if not strs:
        print('      (none — deep helper with no string literals)')
