"""Find C_UISaveLoad vtable by searching for the type descriptor pointer in .rdata.
RTTI Complete Object Locators store the type-descriptor *pointer* (8B absolute) on
older runtimes, but MSVC 2017+ stores 4-byte image-base-relative RVAs. Try both.
"""
import pefile, struct, sys

DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase
type_desc_va = 0x184CD7934  # corrected — mangled name @ +0x10 starts ".?AVC_UISaveLoad..."
type_desc_rva = type_desc_va - image_base

print(f"image_base = 0x{image_base:X}")
print(f"type descriptor VA = 0x{type_desc_va:X}, RVA = 0x{type_desc_rva:X}")
print()

# Gather all sections + their bounds
sections = []
for sec in pe.sections:
    name = sec.Name.rstrip(b"\x00").decode("ascii", errors="replace")
    sva = image_base + sec.VirtualAddress
    data = sec.get_data()
    sections.append((name, sva, sva + len(data), data))
    print(f"  section {name:8s}: 0x{sva:X}..0x{sva+len(data):X} ({len(data)} B)")
print()

# Strategy 1: RVA search in .data and .rdata (4 bytes little-endian)
print("=== Strategy 1: RVA search ===")
rva_bytes = type_desc_rva.to_bytes(4, "little")
for name, sva, eva, data in sections:
    if name not in (".rdata", ".data"):
        continue
    i = 0
    while True:
        idx = data.find(rva_bytes, i)
        if idx < 0:
            break
        addr = sva + idx
        # Decode a few bytes context to disambiguate
        ctx_before = data[max(0, idx-12):idx].hex()
        ctx_after = data[idx:idx+16].hex()
        print(f"  hit @ 0x{addr:X}  ctx_before={ctx_before}  ctx_after={ctx_after}")
        i = idx + 1
print()

# Strategy 2: absolute pointer search
print("=== Strategy 2: absolute pointer search ===")
ptr_bytes = type_desc_va.to_bytes(8, "little")
for name, sva, eva, data in sections:
    if name not in (".rdata", ".data"):
        continue
    i = 0
    while True:
        idx = data.find(ptr_bytes, i)
        if idx < 0:
            break
        addr = sva + idx
        print(f"  hit @ 0x{addr:X}")
        i = idx + 1
print()

# Strategy 3: find the COL by looking for "structure that ends with the RVA"
# COL layout (64-bit MSVC): { uint32 signature, uint32 offset, uint32 cdOffset,
#                            uint32 pTypeDescriptor_RVA, uint32 pClassDescriptor_RVA,
#                            uint32 pSelf_RVA }
# So if we find a 4-byte RVA hit, the COL starts 12 bytes before it.
print("=== Strategy 3: COL candidates (RVA hit - 12) ===")
for name, sva, eva, data in sections:
    if name != ".rdata":
        continue
    i = 0
    while True:
        idx = data.find(rva_bytes, i)
        if idx < 0:
            break
        if idx >= 12:
            col_va = sva + idx - 12
            sig = int.from_bytes(data[idx-12:idx-8], "little")
            off = int.from_bytes(data[idx-8:idx-4], "little")
            cd = int.from_bytes(data[idx-4:idx], "little")
            if sig in (0, 1) and off < 0x10000:
                print(f"  COL candidate @ 0x{col_va:X}  sig={sig} off=0x{off:X} cd=0x{cd:X}")
                # Now find the pointer to this COL in .rdata (which lives at vtable[-1])
                col_ptr_bytes = col_va.to_bytes(8, "little")
                for name2, sva2, eva2, data2 in sections:
                    if name2 != ".rdata":
                        continue
                    j = 0
                    while True:
                        jdx = data2.find(col_ptr_bytes, j)
                        if jdx < 0:
                            break
                        vtable_va = sva2 + jdx + 8
                        print(f"    vtable @ 0x{vtable_va:X}")
                        # Dump first 30 slots
                        for k in range(30):
                            off_in_data = jdx + 8 + 8*k
                            if off_in_data + 8 > len(data2):
                                break
                            fn_va = int.from_bytes(data2[off_in_data:off_in_data+8], "little")
                            marker = ""
                            if fn_va == 0x182BA7094: marker = "  <- LoadLastSave"
                            if fn_va == 0x182BA6894: marker = "  <- ExitGame"
                            if fn_va == 0x182BA7CE0: marker = "  <- OnLoadButton_FlashHandler"
                            if fn_va == 0x1807D5FC8: marker = "  <- BindFlashEventHandlers"
                            print(f"      [{k:2d}] 0x{vtable_va + 8*k:X}: 0x{fn_va:016X}{marker}")
                        j = jdx + 1
        i = idx + 1
