"""Disassemble the candidate IConsole vtable slot functions.

We look at the four RVAs the live probe captured and dump the first ~200 bytes
of each, so we can identify the function-pointer vs script-string overload.
"""
import sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"

SLOTS = {
    "vtable[23] GetCVar?":            0x009DF818,
    "vtable[32] AddCommand A?":       0x0100A3D4,
    "vtable[33] AddCommand B?":       0x00B9A2B0,
    "vtable[34] RemoveCommand?":      0x0100955C,
    "vtable[35] ExecuteString?":      0x007A5818,
}

DISASM_BYTES = 0x300  # 768 bytes per slot - usually plenty of prologue/body

def main():
    pe = pefile.PE(DLL, fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = False
    md.skipdata = True
    # Locate .text
    text = None
    for s in pe.sections:
        name = s.Name.rstrip(b"\x00").decode(errors="replace")
        if name == ".text":
            text = s
            break
    if not text:
        print("no .text section"); return
    text_data = text.get_data()
    text_va_start = text.VirtualAddress

    for label, rva in SLOTS.items():
        off = rva - text_va_start
        if off < 0 or off + DISASM_BYTES > len(text_data):
            print(f"=== {label} RVA {rva:#x} -- OUT OF .text! ===")
            continue
        body = text_data[off:off+DISASM_BYTES]
        va = image_base + rva
        print("=" * 78)
        print(f"=== {label}    VA={va:#x}  RVA={rva:#x} ===")
        print("=" * 78)
        for ins in md.disasm(body, va):
            print(f"  {ins.address:#010x}  {ins.bytes.hex():<24} {ins.mnemonic:<7} {ins.op_str}")
            # Stop at a likely "end of function" (RET) if we've seen at least 60 bytes
            if ins.mnemonic == "ret" and (ins.address - va) > 0x20:
                # peek ahead 16 bytes - if mostly NOPs/INT3 we likely hit end of fn
                break
        print()

if __name__ == "__main__":
    main()
