"""Disassemble the helper functions referenced by slot 32 and 33 of IConsole vtable.

Targets:
  0x1804f6ac8 - called multiple times with rdx=string, rcx=dst struct slot.
                Suspect string assign / strdup-into-member.
  0x180b9a268 - called with rcx=stack record dst. Suspect ctor for CryEngine command record.
  0x180b9a204 - tail-end call before insert. Suspect map insertion.
  0x180b9a1e0 - cleanup/dtor.
  0x180b9a394 - actual map-insert (called on r14+0xa0 = pConsole->m_cmdMap)
  0x18069090c - record dtor at end of slot 32/33.
  0x1804d4148 - lookup-by-name (called near top, returns existing entry pointer).
  0x1804fc884 - compares hash/string keys.

Dump 64 bytes from each so we can characterize them.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"

HELPERS = {
    "string_assign_helper@1804f6ac8":         0x4f6ac8,
    "record_ctor@180b9a268":                  0xb9a268,
    "make_pair_or_helper@180b9a204":          0xb9a204,
    "record_dtor_inline@180b9a1e0":           0xb9a1e0,
    "map_insert@180b9a394":                   0xb9a394,
    "scratch_dtor@18069090c":                 0x69090c,
    "name_lookup@1804d4148":                  0x4d4148,
    "key_compare@1804fc884":                  0x4fc884,
    "tmp_name_init@1804f692c":                0x4f692c,
}

DISASM_BYTES = 0x200

def main():
    pe = pefile.PE(DLL, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    md = Cs(CS_ARCH_X86, CS_MODE_64); md.skipdata = True
    text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
    text_data = text.get_data()
    text_va = text.VirtualAddress

    for label, rva in HELPERS.items():
        off = rva - text_va
        if off < 0 or off+DISASM_BYTES > len(text_data):
            print(f"== {label} RVA {rva:#x} OUT OF .text"); continue
        body = text_data[off:off+DISASM_BYTES]
        va = base + rva
        print("=" * 78)
        print(f"== {label}   VA={va:#x}  RVA={rva:#x}")
        print("=" * 78)
        for ins in md.disasm(body, va):
            print(f"  {ins.address:#010x}  {ins.bytes.hex():<24} {ins.mnemonic:<7} {ins.op_str}")
            if ins.mnemonic == "ret" and (ins.address - va) > 0x10:
                break
        print()

if __name__ == "__main__":
    main()
