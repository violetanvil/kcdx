#!/usr/bin/env python3
# F4 (audio) recon — find WHGame.dll callers of FMOD imports via IAT-relative `call [rip+disp]`.
# Primary-evidence tool: scans .text for `FF 15 <disp32>` whose RIP-relative target is an
# FMOD IAT slot, prints the call-site RVA. Reproducible (pefile + capstone), per RE methodology.
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_MEM, x86

DLL = "third-party-ghidra/WHGame.dll"

# FMOD IAT slot RVAs (from the import-table dump, this build).
IAT = {
    0x3a040a0: "setFileSystem",
    0x3a040c8: "seekData",
    0x3a040d0: "readData",
    0x3a04140: "createSound",
    0x3a04150: "createStream",
    0x3a04230: "loadBankFile",
}

pe = pefile.PE(DLL, fast_load=True)
ib = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
data = text.get_data()
text_rva = text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True

hits = {k: [] for k in IAT}
for ins in md.disasm(data, ib + text_rva):
    # call [rip+disp] (FF /2) — find IAT-relative
    if ins.mnemonic != "call":
        continue
    for op in ins.operands:
        if op.type == CS_OP_MEM and op.mem.base == x86.X86_REG_RIP:
            target_va = ins.address + ins.size + op.mem.disp
            target_rva = target_va - ib
            if target_rva in IAT:
                hits[target_rva].append(ins.address - ib)

for rva, name in IAT.items():
    sites = hits[rva]
    print("%-14s (IAT rva 0x%x): %d call site(s)" % (name, rva, len(sites)))
    for s in sites:
        print("    call at RVA 0x%x  (VA 0x%x)" % (s, s + ib))
