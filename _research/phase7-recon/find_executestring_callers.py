"""Find call sites to slot 35 (ExecuteString) via vtable [rax+0x118] (35*8 = 0x118)."""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"

pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
data = text.get_data(); text_va = text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64); md.skipdata = True
n = len(data)
DISP = 0x118

hits = []
for i in range(n - 6):
    b0 = data[i]
    if b0 == 0xFF:
        modrm = data[i + 1]
        disp = int.from_bytes(data[i + 2:i + 6], "little", signed=True)
        instr_va = base + text_va + i
    elif b0 in (0x41, 0x44, 0x45, 0x4D, 0x4C, 0x48, 0x49, 0x4A):
        if data[i + 1] != 0xFF: continue
        modrm = data[i + 2]
        disp = int.from_bytes(data[i + 3:i + 7], "little", signed=True)
        instr_va = base + text_va + i
    else:
        continue
    mod = (modrm >> 6) & 3
    reg = (modrm >> 3) & 7
    rm = modrm & 7
    if mod != 2 or reg != 2: continue
    if rm == 4: continue
    if disp == DISP:
        hits.append(instr_va)

print(f"call [reg+0x118] ({DISP/8:.0f}*8): {len(hits)} hits")
# Print first 20 with surrounding context (only those where preceding instructions
# look like an AddCommand setup — but let's just print first 10 raw).
for v in hits[:25]:
    i = v - base - text_va
    start = max(0, i - 32); end = min(n, i + 16)
    print("---")
    for ins in md.disasm(bytes(data[start:end]), base + text_va + start):
        marker = "  *" if ins.address == v else "   "
        print(f"  {marker} {ins.address:#012x}  {ins.bytes.hex():<24} {ins.mnemonic:<7} {ins.op_str}")
