"""Read the if/else branch around the metadata-slot call for the two strongest boot
candidates: 0x89682d (Menu.gfx required-asset check) + 0x244dd9c (level-cache-pak
GetFileSize 'does not exist' gate). Show the call + the test/branch right after, and
where each arm goes. Also re-run slot70 correlation with a wider window."""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL); base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
tdata = text.get_data(); ts = base + text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64)

def dump(func_rva, n=0x130, label=""):
    fva = base + func_rva
    off = fva - ts
    print(f"\n===== {label}  func RVA {func_rva:#x} (VA {fva:#x}) =====")
    for ins in md.disasm(tdata[off:off+n], fva):
        mark = ""
        if ins.mnemonic=="call" and ("0x168" in ins.op_str or "0x218" in ins.op_str or "0x230" in ins.op_str):
            mark = "   <<< METADATA SLOT CALL"
        if ins.mnemonic.startswith("j") and ins.mnemonic!="jmp":
            mark = mark or "   <branch>"
        print(f"  {ins.address:#011x}: {ins.mnemonic:7s} {ins.op_str}{mark}")
        if ins.mnemonic in ("ret","int3"): break

dump(0x89682d, 0x160, "Menu.gfx REQUIRED-ASSET check (slot67)")
dump(0x244dd9c, 0x170, "level-cache-pak GetFileSize (slot45)")
