"""Find LEA xrefs to CScriptBind_System::ExecuteCommand string."""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"

TARGETS = [
    0x1847a8520,  # CScriptBind_System::ExecuteCommand
    0x1847a86f8,  # CScriptBind_System::ShowConsole
]

pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
data = text.get_data(); text_va = text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64); md.skipdata = True
n = len(data)
found = {t: [] for t in TARGETS}
for i in range(n - 7):
    b0 = data[i]
    if b0 not in (0x48, 0x4C): continue
    if data[i + 1] != 0x8D: continue
    modrm = data[i + 2]
    if (modrm >> 6) != 0 or (modrm & 7) != 5: continue
    disp32 = int.from_bytes(data[i + 3:i + 7], "little", signed=True)
    target = base + text_va + i + 7 + disp32
    if target in found:
        found[target].append(base + text_va + i)

for t, lst in found.items():
    print(f"\n=== {t:#x}  ({len(lst)} xrefs) ===")
    for v in lst[:5]:
        i = v - base - text_va
        start = max(0, i - 64); end = min(n, i + 96)
        for ins in md.disasm(bytes(data[start:end]), base + text_va + start):
            marker = "  *" if ins.address == v else "   "
            print(f"  {marker} {ins.address:#012x}  {ins.bytes.hex():<24} {ins.mnemonic:<7} {ins.op_str}")
        print()
