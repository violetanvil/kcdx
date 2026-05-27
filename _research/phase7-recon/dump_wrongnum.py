"""Dump the function containing the 'wrong number of arguments' LEA."""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"

# Found xref at 0x182ea7b15. Dump from a likely function head (search backwards
# for a typical 'sub rsp' or '40 53' prologue) to a few hundred bytes after.

VA_OF_INTEREST = 0x182ea7b15
DUMP_START = 0x182ea7a00
DUMP_END   = 0x182ea7c80

# Also: the slot 35 ExecuteString helper around 0x1807a586c-0x1807a5fb6 region
# Dump that too.
RANGES = [
    ("wrongnum_xref_region", 0x182ea7a00, 0x182ea7c80),
    ("ExecuteString_helper", 0x1807a586c, 0x1807a6080),
    ("Unknown_command_path", 0x1807a5c80, 0x1807a5e00),
    ("Console_executing",    0x1807a5f60, 0x1807a6080),
    # The "wrong number of args" string at 0x18473f3b0 has only 1 xref. But the
    # surrounding function might also be referenced via call-only. Let me also
    # dump the function right before VA_OF_INTEREST = 0x182ea7b15.
]


def main():
    pe = pefile.PE(DLL, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
    data = text.get_data()
    text_va_image = text.VirtualAddress

    md = Cs(CS_ARCH_X86, CS_MODE_64); md.skipdata = True

    for label, va_start, va_end in RANGES:
        # convert VA to offset
        off_start = va_start - base - text_va_image
        off_end = va_end - base - text_va_image
        if off_start < 0 or off_end > len(data):
            print(f"=== {label} skip - out of range"); continue
        print("=" * 78)
        print(f"=== {label}  VA={va_start:#x} .. {va_end:#x}")
        print("=" * 78)
        body = data[off_start:off_end]
        for ins in md.disasm(bytes(body), va_start):
            print(f"  {ins.address:#012x}  {ins.bytes.hex():<24} {ins.mnemonic:<7} {ins.op_str}")
        print()


if __name__ == "__main__":
    main()
