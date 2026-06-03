#!/usr/bin/env python3
# F4 (audio) recon — byte-wise scan for RIP-relative references to FMOD IAT slots.
# Linear capstone sweep over a 50MB+ .text desyncs on embedded data; this scans the raw
# bytes for the two RIP-relative forms that reach an IAT slot, independent of alignment:
#   FF 15 <disp32>  : call qword [rip+disp]   (direct import call)
#   FF 25 <disp32>  : jmp  qword [rip+disp]   (import thunk / tail-call)
#   48 8B 05 <disp32>: mov rax,[rip+disp]      (load the fn ptr into a register — indirect use)
#   any 48 8B xx <disp32> reg-load of the slot (callback registration copies the ptr)
# It reports every code RVA whose RIP-relative disp resolves to an FMOD IAT slot.
import struct, pefile

DLL = "third-party-ghidra/WHGame.dll"
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
# scan ALL executable sections
hits = {k: [] for k in IAT}
for s in pe.sections:
    if not (s.Characteristics & 0x20000000):  # IMAGE_SCN_MEM_EXECUTE
        continue
    data = s.get_data()
    base_rva = s.VirtualAddress
    n = len(data)
    i = 0
    while i < n - 6:
        b0 = data[i]
        # FF 15 / FF 25 : 2-byte opcode + disp32, instr len 6, RIP after instr
        if b0 == 0xFF and data[i+1] in (0x15, 0x25):
            disp = struct.unpack_from("<i", data, i+2)[0]
            ins_rva = base_rva + i
            tgt = ins_rva + 6 + disp
            if tgt in IAT:
                kind = "call" if data[i+1] == 0x15 else "jmp"
                hits[tgt].append((ins_rva, kind))
        # 48 8B 05 disp32 : mov rax,[rip+disp]  (len 7) — also 48 8B 0D/15/1D/25/2D/35/3D for other regs
        if b0 == 0x48 and data[i+1] == 0x8B and (data[i+2] & 0xC7) == 0x05:
            disp = struct.unpack_from("<i", data, i+3)[0]
            ins_rva = base_rva + i
            tgt = ins_rva + 7 + disp
            if tgt in IAT:
                reg = (data[i+2] >> 3) & 7
                hits[tgt].append((ins_rva, "mov r%d" % reg))
        # 4C 8B xx disp32 : mov r8..r15,[rip+disp] (REX.R) len 7
        if b0 == 0x4C and data[i+1] == 0x8B and (data[i+2] & 0xC7) == 0x05:
            disp = struct.unpack_from("<i", data, i+3)[0]
            ins_rva = base_rva + i
            tgt = ins_rva + 7 + disp
            if tgt in IAT:
                hits[tgt].append((ins_rva, "mov r8+"))
        i += 1

for rva, name in IAT.items():
    refs = hits[rva]
    print("%-14s (IAT rva 0x%x): %d ref(s)" % (name, rva, len(refs)))
    for ins_rva, kind in refs:
        print("    %-8s at RVA 0x%x  (VA 0x%x)" % (kind, ins_rva, ins_rva + ib))
