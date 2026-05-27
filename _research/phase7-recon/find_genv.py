"""find_genv.py — replicate muyuanjin's gEnv resolution path against
the current WHGame.dll, emit the resolved gEnv RVA + pConsole RVA.

Pipeline:
  1. Find 'exec autoexec.cfg' string in .rdata
  2. Find LEA rdx, [rip+...] instructions in .text that reference it
  3. For each match, classify by the 7 bytes BEFORE the LEA:
       - V1.4+ pattern: 4C 8B 92 18 01 00 00 (mov r10, [rdx+0x118])
       - V1.2/V1.3:     48 8B 0D ? ? ? ?     (mov rcx, [rip+pConsole])
  4. From the pConsole MOV, compute the pConsole-pointer address (where
     gEnv->pConsole lives in memory), then gEnv = pConsole_ptr - 0xA8.
"""

import struct
import sys
from pathlib import Path

import pefile

DEFAULT_DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"
TARGET_STR = b"exec autoexec.cfg"
V14_CONTEXT = bytes.fromhex("4C 8B 92 18 01 00 00".replace(" ", ""))
V12_CONTEXT_OPCODE = bytes.fromhex("48 8B 0D")


def main():
    dll = Path(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DLL)
    pe = pefile.PE(str(dll), fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase

    text_sec = None
    rdata_sec = None
    sections = {}
    for s in pe.sections:
        name = s.Name.rstrip(b"\x00").decode()
        sections[name] = s
        if name == ".text":
            text_sec = s
        elif name == ".rdata":
            rdata_sec = s

    text_data = text_sec.get_data()
    text_rva_base = text_sec.VirtualAddress

    # Step 1: find string address
    rdata = rdata_sec.get_data()
    soff = rdata.find(TARGET_STR)
    if soff < 0:
        print("ERR: 'exec autoexec.cfg' not found in .rdata")
        sys.exit(1)
    str_rva = rdata_sec.VirtualAddress + soff
    str_va = image_base + str_rva
    print(f"# string 'exec autoexec.cfg'  rva={hex(str_rva)}  va={hex(str_va)}")

    # Step 2: scan .text for LEA rdx, [rip+disp32]  -> opcodes 48 8D 15 ?? ?? ?? ??
    lea_prefix = bytes.fromhex("48 8D 15".replace(" ", ""))
    i = 0
    leas_referencing = []
    while i <= len(text_data) - 7:
        if text_data[i:i + 3] == lea_prefix:
            disp = struct.unpack_from("<i", text_data, i + 3)[0]
            # RIP after the lea = next-instruction = +7 bytes
            ref_rva = text_rva_base + i + 7 + disp
            if ref_rva == str_rva:
                leas_referencing.append(i)
        i += 1

    print(f"# LEAs to that string: {len(leas_referencing)}")
    for off in leas_referencing:
        lea_rva = text_rva_base + off
        lea_va = image_base + lea_rva
        # check context 7 bytes BEFORE lea
        ctx_off = off - 7
        if ctx_off < 0:
            continue
        ctx = text_data[ctx_off:ctx_off + 7]
        ctx_rva = text_rva_base + ctx_off
        kind = "?"
        if ctx == V14_CONTEXT:
            kind = "V1.4+"
            # In V1.4+, the pConsole-MOV instruction is 0x17 bytes before the LEA
            pconsole_mov_off = off - 0x17
        elif ctx[:3] == V12_CONTEXT_OPCODE:
            kind = "V1.2/V1.3"
            pconsole_mov_off = ctx_off
        else:
            print(f"  lea@{hex(lea_va)}  ctx={ctx.hex()}  kind=UNKNOWN")
            continue
        # The pConsole MOV is `48 8B 0D ?? ?? ?? ??` -> opcode at pconsole_mov_off
        mov_op = text_data[pconsole_mov_off:pconsole_mov_off + 3]
        if mov_op != V12_CONTEXT_OPCODE:
            print(f"  lea@{hex(lea_va)}  kind={kind}  "
                  f"pconsole_mov opcode mismatch: {mov_op.hex()}")
            continue
        disp = struct.unpack_from("<i", text_data, pconsole_mov_off + 3)[0]
        # pConsole pointer address = mov_va + 7 + disp32
        pconsole_ptr_rva = text_rva_base + pconsole_mov_off + 7 + disp
        pconsole_ptr_va = image_base + pconsole_ptr_rva
        genv_va = pconsole_ptr_va - 0xA8
        genv_rva = pconsole_ptr_rva - 0xA8
        pconsole_mov_rva = text_rva_base + pconsole_mov_off
        pconsole_mov_va = image_base + pconsole_mov_rva
        print(f"  lea@{hex(lea_va)}  kind={kind}")
        print(f"    pConsole_MOV    rva={hex(pconsole_mov_rva)}  va={hex(pconsole_mov_va)}")
        print(f"    pConsole_ptr    rva={hex(pconsole_ptr_rva)}  va={hex(pconsole_ptr_va)}")
        print(f"    gEnv (computed) rva={hex(genv_rva)}          va={hex(genv_va)}")


if __name__ == "__main__":
    main()
