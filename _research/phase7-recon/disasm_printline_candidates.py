"""Disassemble IConsole vtable slots 24..31 to identify PrintLine EMPIRICALLY.

Canonical IConsole.h order between GetCVar (verified slot 23) and
AddCommand-func (verified slot 33) is:
  24 GetVariable(char*,char*,char*)->char*
  25 GetVariable(char*,char*,float)->float
  26 PrintLine(const char* s)            <-- target
  27 PrintLinePlus(const char* s)
  28 GetStatus()->bool
  29 Clear()
  30 Update()
  31 Draw()
  32 AddCommand(script)  [verified]
  33 AddCommand(func)    [verified]

We do NOT trust that mapping; we read each body. PrintLine's signature is
void(this /*rcx*/, const char* s /*rdx*/) and it appends s to the console's
on-screen line ring-buffer. We disassemble each candidate and look for:
  - single meaningful pointer arg consumed from rdx (2nd arg),
  - a call into a string/buffer-append helper (the line buffer),
  - NOT a varargs Exit (which formats + aborts), NOT a getter (returns a value).
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\WHGame.dll"
IMAGE_BASE = 0x180000000

SLOTS = {
    24: 0x01a72da0,
    25: 0x0066cf70,
    26: 0x008dff08,   # canonical PrintLine
    27: 0x0247c878,   # canonical PrintLinePlus
    28: 0x00863dd0,   # canonical GetStatus
    29: 0x024754a4,   # canonical Clear
    30: 0x0052f65c,   # canonical Update
    31: 0x009aec44,   # canonical Draw
}

def main():
    pe = pefile.PE(DLL, fast_load=True)
    text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
    data = text.get_data(); tva = text.VirtualAddress
    md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = False
    n = len(data)

    for slot, rva in SLOTS.items():
        off = rva - tva
        if off < 0 or off >= n:
            print(f"slot {slot}: RVA {rva:#x} outside .text"); continue
        chunk = data[off:off + 200]
        print(f"\n================ slot[{slot}]  RVA {rva:#010x}  (VA {IMAGE_BASE+rva:#x}) ================")
        count = 0
        for ins in md.disasm(chunk, IMAGE_BASE + rva):
            print(f"  {ins.address:#012x}  {ins.bytes.hex():<22} {ins.mnemonic:<8} {ins.op_str}")
            count += 1
            # stop at first ret / jmp tail to keep prologue+early-body focused, but
            # allow up to ~45 instrs so we see the first helper call(s)
            if ins.mnemonic in ("ret",) or count >= 45:
                break

if __name__ == "__main__":
    main()
