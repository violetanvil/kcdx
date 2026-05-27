"""Walk a function's body and dump every direct CALL target with the
target's 24-byte prologue + uniqueness check. Useful for understanding
what subsystem methods a high-level entry function invokes.

Usage:
    py phase6_dump_function_calls.py <WHGame.dll> <hex_va_of_function>
"""
import sys
from pathlib import Path
import capstone, pefile


PROLOGUE_BYTES = 24


def find_all(data, pat_bytes):
    n = len(pat_bytes)
    out = []
    for i in range(len(data) - n + 1):
        if data[i:i + n] == pat_bytes:
            out.append(i)
    return out


def main():
    if len(sys.argv) < 3:
        sys.exit("usage: phase6_dump_function_calls.py <WHGame.dll> <hex_va>")
    dll = Path(sys.argv[1])
    fn_va = int(sys.argv[2], 16)

    pe = pefile.PE(str(dll), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase

    exec_sections = []
    text_va = text_data = None
    for sec in pe.sections:
        name = sec.Name.rstrip(b"\x00").decode("ascii", errors="replace")
        sec_va = image_base + sec.VirtualAddress
        sec_data = bytes(sec.get_data())
        if name == ".text":
            text_va, text_data = sec_va, sec_data
        if sec.Characteristics & 0x20000000:
            exec_sections.append((sec_va, sec_data))

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True

    print(f"Function entry: 0x{fn_va:016X} (RVA 0x{fn_va - image_base:X})")
    print()
    print("Calls + interesting indirect dispatches inside the function body:")
    print("(stops at first RET — single-bb walk)")
    print()

    off = fn_va - text_va
    # Limit walk to 16 KB (function bodies in this codebase are typically
    # much smaller; the orchestrator is ~500 bytes)
    sub = text_data[off:off + 0x4000]
    seen = set()
    count = 0
    for insn in md.disasm(sub, fn_va):
        count += 1
        if count > 600:
            break
        m = insn.mnemonic
        if m == "call":
            op = insn.operands[0]
            if op.type == capstone.x86.X86_OP_IMM:
                tgt = op.imm
                # Get target's 24-byte prolog
                hit_sec = None
                for sec_va, sec_data in exec_sections:
                    if sec_va <= tgt < sec_va + len(sec_data):
                        hit_sec = (sec_va, sec_data)
                        break
                if hit_sec is None:
                    print(f"  0x{insn.address:016X}  call 0x{tgt:016X}  "
                          f"(OUT OF EXEC)")
                    continue
                sec_va, sec_data = hit_sec
                prolog = sec_data[tgt - sec_va:tgt - sec_va + PROLOGUE_BYTES]
                hits = []
                for sv, sd in exec_sections:
                    hits.extend(find_all(sd, bytes(prolog)))
                # Decode the first 2 inst at target for context
                ctx = []
                for ins2 in md.disasm(prolog, tgt):
                    ctx.append(f"{ins2.mnemonic} {ins2.op_str}")
                    if len(ctx) >= 2:
                        break
                key = tgt
                dup = " [dup]" if key in seen else ""
                seen.add(key)
                print(f"  0x{insn.address:016X}  call 0x{tgt:016X}{dup}"
                      f"  hits={len(hits)}  "
                      f"prolog={prolog.hex(' ')[:23]}...  "
                      f"({'; '.join(ctx)})")
            elif op.type == capstone.x86.X86_OP_MEM:
                # Indirect call — likely vtable
                disp = op.mem.disp
                base = "?"
                if op.mem.base != 0:
                    base = insn.reg_name(op.mem.base)
                print(f"  0x{insn.address:016X}  call [{base}+0x{disp:X}]"
                      f"  (indirect/vtable)")
        elif m == "ret" or m.startswith("retn"):
            print(f"  0x{insn.address:016X}  RET (function ends)")
            break
        elif m == "jmp":
            op = insn.operands[0]
            if op.type == capstone.x86.X86_OP_IMM:
                # Tail call
                tgt = op.imm
                print(f"  0x{insn.address:016X}  jmp 0x{tgt:016X}  "
                      f"(possible tail call)")


if __name__ == "__main__":
    main()
