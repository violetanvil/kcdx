"""Linear disassembly of a VA window; flags indirect calls through [reg+disp]
(vtable dispatch) and annotates the CCryPak slot when disp matches a known slot.

Usage: python disasm_window.py <WHGame.dll> <start_va_hex> <len_bytes_dec>
"""
import sys, struct
import pefile, capstone

DLL = sys.argv[1]
start = int(sys.argv[2], 16)
length = int(sys.argv[3])
pe = pefile.PE(DLL, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase

for sec in pe.sections:
    name = sec.Name.rstrip(b"\x00").decode("latin1")
    if name == ".text":
        text_va = image_base + sec.VirtualAddress
        text_data = sec.get_data()
        break

# Known CCryPak vtable slot offsets -> slot name (from front1-full-vtable-surface)
SLOTS = {
    0x8: "slot1 AdjustFileName",
    0x68: "slot13 IsFolder",
    0x70: "slot14 ForEachFile",
    0x78: "slot15 perfile-cb",
    0x100: "slot32 FindPakByCRC",
    0x108: "slot33 GetPakInfo",
    0x118: "slot35 FOpenRaw",
    0x120: "slot36 FOpen",
    0x168: "slot45 GetFileSize",
    0x218: "slot67 IsFileExist3",
    0x220: "slot68 GetAttributes",
    0x230: "slot70 IsFileExist2",
    0x238: "slot71 OpenPack",
    0x328: "slot101 FindFirst(CCryPakFindData)",
}

off = start - text_va
code = text_data[off:off+length]
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True

for insn in md.disasm(code, start):
    note = ""
    # LEA rip-relative -> resolve target
    if insn.mnemonic == "lea" and "rip" in insn.op_str:
        try:
            disp = insn.operands[1].mem.disp
            tgt = insn.address + insn.size + disp
            note = f"   ; -> 0x{tgt:X}"
        except Exception:
            pass
    # indirect call/jmp through [reg+disp]
    if insn.mnemonic in ("call", "jmp") and "[" in insn.op_str and "rip" not in insn.op_str:
        for op in insn.operands:
            if op.type == capstone.x86.X86_OP_MEM and op.mem.disp:
                d = op.mem.disp
                slot = SLOTS.get(d)
                note = f"   ; *** INDIRECT disp=+0x{d:X}" + (f" = {slot}" if slot else " (vtable?)") + " ***"
    if insn.mnemonic == "call" and insn.op_str.startswith("0x"):
        note = f"   ; direct call"
    print(f"0x{insn.address:X}: {insn.mnemonic:7s} {insn.op_str}{note}")
