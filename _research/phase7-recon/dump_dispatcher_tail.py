"""Dump the rest of the dispatcher at 0x1807a5f88 (find where the func pointer is called)."""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"
START = 0x1807a6080
END   = 0x1807a6400

pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
data = text.get_data(); text_va = text.VirtualAddress

off_start = START - base - text_va
off_end = END - base - text_va
md = Cs(CS_ARCH_X86, CS_MODE_64); md.skipdata = True
for ins in md.disasm(bytes(data[off_start:off_end]), START):
    print(f"  {ins.address:#012x}  {ins.bytes.hex():<24} {ins.mnemonic:<7} {ins.op_str}")
