"""Find COLs referencing C_UISaveLoad type descriptor, then locate the vtable."""
import pefile, struct, sys

pe = pefile.PE(r'E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll', fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase

type_desc_va = 0x184CD7940  # C_UISaveLoad RTTI type descriptor
# RTTI type descriptor layout: vftable ptr (8B), spare (8B), name (variable)
# The "RTTI type descriptor" *itself* sits at this VA; what we want is its
# offset within .data.  The "this" pointer for the name string starts at
# type_desc_va + 16 (after vftable & spare).
name_va = type_desc_va + 16
type_desc_rva = type_desc_va - image_base
print(f'C_UISaveLoad type descriptor @ 0x{type_desc_va:X} (RVA 0x{type_desc_rva:X})')

# COLs in .rdata embed pTypeDescriptor as RVA (4 bytes).
rva_bytes = type_desc_rva.to_bytes(4, 'little')

cols = []
for sec in pe.sections:
    name = sec.Name.rstrip(b'\x00').decode('ascii', errors='replace')
    if name != '.rdata': continue
    data = sec.get_data()
    sec_va = image_base + sec.VirtualAddress
    i = 0
    while True:
        idx = data.find(rva_bytes, i)
        if idx < 0: break
        # COL has the type descriptor RVA at offset +0x0C (signature, offset, cdOffset, pTypeDesc, ...)
        # so the COL itself starts 0x0C before our hit
        col_va = sec_va + idx - 0x0C
        # Validate by checking signature at offset 0 (should be 1 for 64-bit, or 0)
        if idx >= 0x0C:
            sig = int.from_bytes(data[idx-0x0C:idx-0x08], 'little')
            if sig in (0, 1):
                print(f'  COL candidate @ 0x{col_va:016X}  (sig={sig})')
                cols.append(col_va)
        # Also print the raw hit location
        print(f'    rva hit @ 0x{sec_va+idx:016X}')
        i = idx + 1

print()
# Find pointers in .rdata to each COL (these are vtable[-1])
for col in cols:
    col_bytes = col.to_bytes(8, 'little')
    for sec in pe.sections:
        name = sec.Name.rstrip(b'\x00').decode('ascii', errors='replace')
        if name != '.rdata': continue
        data = sec.get_data()
        sec_va = image_base + sec.VirtualAddress
        i = 0
        while True:
            idx = data.find(col_bytes, i)
            if idx < 0: break
            vtable_va = sec_va + idx + 8
            print(f'  vtable for COL 0x{col:X} starts @ 0x{vtable_va:016X}')
            # Dump first 30 slots
            for k in range(30):
                if idx + 8 + 8*k + 8 > len(data): break
                fn_va = int.from_bytes(data[idx+8+8*k:idx+8+8*k+8], 'little')
                marker = ''
                if fn_va == 0x182BA7094: marker = '  <- LoadLastSave'
                if fn_va == 0x182BA6894: marker = '  <- ExitGame'
                print(f'    [{k:2d}] 0x{vtable_va + 8*k:X}: 0x{fn_va:016X}{marker}')
            i = idx + 1
