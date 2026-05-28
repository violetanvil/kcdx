"""Disassemble C_ModManager ctor + SELECT to resolve the 0x68-byte field map.

Driven by the init-cycle-ownership step-1 probe finding: the live capture at
ctor-return shows +0x30 = "mods" ASCII (NOT a vector-begin pointer), but
the seed prose AND the existing select_detour.cpp both treat +0x30/+0x38/+0x40
as a std::vector triple. One of these readings is wrong; we need the binary
truth.

Targets:
  0x180DA0EB0 — FUN_180da0eb0, ModManager_ctor   (seed id 3101)
  0x180DA104C — FUN_180da104c, ModManager_Select (seed id 3100)

Dump strategy:
  1. Disassemble each function to its first ret (or 0x800 bytes whichever first).
  2. Filter for every `mov [rcx+N], X` and `mov [r??+N], X` style write where
     N is small enough to be a C_ModManager member (< 0x100). These are the
     fields the ctor / SELECT touches.
  3. Filter for every `mov rax/r??, [rcx+N]` read where N < 0x100. These are
     fields READ by SELECT — corroborates which offsets are live data.
  4. Print call sites (relative calls — `e8 XX XX XX XX`).
  5. Also dump the literal LEA / MOV-imm values stored at small offsets — a
     literal 0x73646F6D ("mods") stored at some offset is the smoking gun
     for the +0x30 mystery.

Output: human-readable text to stdout; redirect to a file in this dir.
"""
import sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_MEM, CS_OP_IMM
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_OP_REG

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"

TARGETS = {
    "ModManager_ctor@180DA0EB0":   0xDA0EB0,
    "ModManager_Select@180DA104C": 0xDA104C,
}

MAX_BYTES = 0x800

def reg_name_to_idx(name):
    """Map register name (e.g. 'rcx', 'ecx', 'cl') back to a base name."""
    n = name.lower()
    # Capstone gives us lower-case
    if n in ("rcx", "ecx", "cx", "cl", "ch"):  return "rcx"
    if n in ("rdx", "edx", "dx", "dl", "dh"):  return "rdx"
    if n in ("r8", "r8d", "r8w", "r8b"):       return "r8"
    if n in ("r9", "r9d", "r9w", "r9b"):       return "r9"
    if n in ("rax", "eax", "ax", "al", "ah"):  return "rax"
    if n in ("rbx", "ebx", "bx", "bl", "bh"):  return "rbx"
    if n in ("rsi", "esi", "si", "sil"):       return "rsi"
    if n in ("rdi", "edi", "di", "dil"):       return "rdi"
    return n

def main():
    pe = pefile.PE(DLL, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
    text_data = text.get_data()
    text_va = text.VirtualAddress

    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    md.skipdata = True

    for label, rva in TARGETS.items():
        off = rva - text_va
        if off < 0 or off + MAX_BYTES > len(text_data):
            print(f"== {label} RVA {rva:#x} OUT OF .text"); continue

        body = text_data[off:off + MAX_BYTES]
        va = base + rva

        print("=" * 78)
        print(f"== {label}   VA={va:#x}  RVA={rva:#x}")
        print("=" * 78)

        # Pass 1: full disassembly to first ret
        end_reached = False
        instructions = []
        for ins in md.disasm(body, va):
            instructions.append(ins)
            if ins.mnemonic == "ret" and (ins.address - va) > 0x20:
                end_reached = True
                break
            # JMP forward past 0x100 is likely a tail call — keep going if not ret
            if len(instructions) > 600:
                break

        if not end_reached:
            print(f"  WARNING: hit MAX_BYTES or 600-insn cap before first ret")

        print(f"  ({len(instructions)} instructions, {instructions[-1].address - va + instructions[-1].size:#x} bytes)")
        print()

        # Pass 2: writes to [rcx+N] / [reg+N] with small N
        print("--- WRITES to [base+N] where N < 0x100 (filtered) ---")
        for ins in instructions:
            if ins.mnemonic not in ("mov", "movups", "movaps", "movdqa", "movdqu",
                                     "lea", "and", "or", "xor"):
                continue
            # Look for memory operand as destination (operand 0)
            if len(ins.operands) < 2:
                continue
            dst = ins.operands[0]
            src = ins.operands[1]
            if dst.type != X86_OP_MEM:
                continue
            mem = dst.mem
            # We want base reg defined, no index, and small disp
            if mem.base == 0:  # no base register
                continue
            if abs(mem.disp) >= 0x100:
                continue
            # Skip [rsp+N] / [rbp+N] — those are local stack frames, not members
            base_reg = ins.reg_name(mem.base)
            if base_reg in ("rsp", "ebp", "rbp"):
                continue
            # Render src
            if src.type == X86_OP_IMM:
                src_str = f"imm={src.imm:#x}"
            elif src.type == X86_OP_REG:
                src_str = f"reg={ins.reg_name(src.reg)}"
            elif src.type == X86_OP_MEM:
                sm = src.mem
                if sm.base:
                    src_str = f"mem=[{ins.reg_name(sm.base)}+{sm.disp:#x}]"
                else:
                    src_str = f"mem=disp={sm.disp:#x}"
            else:
                src_str = "?"
            print(f"  {ins.address:#010x}  {ins.mnemonic:<7} [{base_reg}+{mem.disp:#04x}]  <- {src_str}")

        print()

        # Pass 3: reads of [rcx+N] / [reg+N]
        print("--- READS from [base+N] where N < 0x100 (filtered, dedup) ---")
        seen_reads = set()
        for ins in instructions:
            if len(ins.operands) < 2:
                continue
            src = ins.operands[1]
            if src.type != X86_OP_MEM:
                continue
            mem = src.mem
            if mem.base == 0:
                continue
            if abs(mem.disp) >= 0x100:
                continue
            base_reg = ins.reg_name(mem.base)
            if base_reg in ("rsp", "ebp", "rbp"):
                continue
            key = (base_reg, mem.disp)
            if key in seen_reads:
                continue
            seen_reads.add(key)
            print(f"  {ins.address:#010x}  {ins.mnemonic:<7} -> [{base_reg}+{mem.disp:#04x}]")

        print()

        # Pass 4: call sites
        print("--- CALL sites ---")
        for ins in instructions:
            if ins.mnemonic != "call":
                continue
            op = ins.operands[0] if ins.operands else None
            if op and op.type == X86_OP_IMM:
                print(f"  {ins.address:#010x}  call {op.imm:#x}  (RVA {op.imm - base:#x})")
            elif op and op.type == X86_OP_MEM:
                m = op.mem
                if m.base:
                    print(f"  {ins.address:#010x}  call [{ins.reg_name(m.base)}+{m.disp:#x}]  (vtable call)")
                else:
                    print(f"  {ins.address:#010x}  call [{m.disp:#x}]")
            else:
                print(f"  {ins.address:#010x}  call {ins.op_str}")

        print()

        # Pass 5: literal immediates that look like ASCII / addresses
        print("--- INTERESTING immediates (potential data-section RVAs or ASCII) ---")
        for ins in instructions:
            for op in ins.operands:
                if op.type != X86_OP_IMM:
                    continue
                v = op.imm
                # 0x73646F6D = "mods" little-endian. Or any 32-bit ASCII run.
                if 0x20202020 <= v <= 0x7F7F7F7F:
                    chars = bytes([(v >> (8*i)) & 0xFF for i in range(4)])
                    if all(0x20 <= c < 0x80 for c in chars):
                        print(f"  {ins.address:#010x}  {ins.mnemonic:<7} ascii={chars.decode('latin-1')!r}  imm={v:#x}")
                # Likely .rdata/.data RVA (image-base aware)
                if base <= v < base + 0x10000000:  # within image
                    section = "?"
                    for s in pe.sections:
                        sva = base + s.VirtualAddress
                        if sva <= v < sva + s.Misc_VirtualSize:
                            section = s.Name.rstrip(b"\x00").decode()
                            break
                    if section != "?" and section != ".text":
                        print(f"  {ins.address:#010x}  {ins.mnemonic:<7} imm={v:#x} (RVA {v - base:#x}, section {section})")

        print()

if __name__ == "__main__":
    main()
