"""Read the CCryPak vtable entry at +0x210 (slot 66 FGetModificationTime) to get the engine body addr.
VTABLE_VA = 0x183A95FA8 (from fs-takeover-pak-mount-recon, RTTI .?AVCCryPak@@)."""
import pefile
DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
VTABLE_VA = 0x183A95FA8
SLOT_OFF = 0x210
pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
# the vtable lives in .rdata; read 8 bytes at VTABLE_VA + SLOT_OFF
target_va = VTABLE_VA + SLOT_OFF
rva = target_va - base
data = pe.get_data(rva, 8)
fn = int.from_bytes(data, "little")
print(f"vtable slot 66 entry @ VA {target_va:#x} (rva {rva:#x})")
print(f"  -> engine FGetModificationTime body VA {fn:#x}  (RVA {fn-base:#x})")
