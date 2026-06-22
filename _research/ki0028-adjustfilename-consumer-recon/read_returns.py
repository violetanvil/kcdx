"""KI-0028 DIVERGENCE-C — read what each consumer DOES with AdjustFileName's RETURN.

The divergence: kcdx returns the RAW input pName on a pak hit; the engine returns the
NORMALIZED Data/-rooted path. A consumer that BRANCHES ON THE RETURNED STRING'S FORM
(prefix test, string-compare on the result) diverges; one that passes the result straight
to a file op (FOpen/IsFileExist/GetFileSize/fopen-equivalent) does not.

AdjustFileName is __fastcall: result returned in rax (it returns a char* — the engine
AdjustFileName fills a caller buffer and returns a pointer to it; rax = the resolved path).
So we read the WINDOW AFTER the slot-1 call and look at how rax (or the buffer it points to)
is consumed:
  - call [rN+slot] (another vtable slot, e.g. FOpen +0x10) with rax/buf as arg  -> file op, CLEAN
  - mov rcx,rax ; call <strcmp/strncmp/_stricmp/path-compare>                    -> FORM BRANCH (read closer)
  - cmp byte ptr [rax], 'X' / movzx + cmp on first chars                         -> PREFIX TEST (read closer)
  - lea/mov into another buffer then file op                                     -> file op, CLEAN

This dumps the post-call window for every call site; flag CALL/CMP/TEST on the result.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL); base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
tdata = text.get_data(); ts = base + text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True

# (func_rva, call_rva) pairs from _slot1_callers.txt
SITES = [
 (0x463a24,0x463a57),(0x4881d0,0x488251),(0x4d3190,0x4d31dc),(0x4d4198,0x4d428c),
 (0x7aebcc,0x7aecd1),(0x8968e8,0x8968f8),(0x9b6d24,0x9b6e2b),(0xb0479c,0xb0482c),
 (0xb05604,0xb0566f),(0xbb1fe4,0xbb2061),(0x11a43b0,0x11a4682),(0x11a4896,0x11a4922),
 (0x19d4a48,0x19d4b2e),(0x19dfb54,0x19dfbf1),(0x1e03c30,0x1e03ca8),(0x224f0aa,0x224f0f7),
 (0x241b340,0x241b40b),(0x241dad0,0x241db0a),(0x24265d8,0x242679d),(0x2428354,0x24284b8),
 (0x244f8e8,0x244f9db),(0x244fb14,0x244fb4e),(0x245b080,0x245b16b),(0x245b5cc,0x245b5df),
 (0x245dd38,0x245dddc),(0x245df70,0x245e039),(0x460c34,0x2460e0d),(0x24f7399,0x24f73e1),
 (0x2547ec4,0x2547f8f),(0x3498130,0x3498271),(0x370f3a0,0x370f3dc),
]

# common CCryPak vtable slots so we can name a follow-on file op
FILEOPS = {0x10:"FOpen", 0x168:"GetFileSize", 0x218:"IsFileExist3", 0x230:"IsFileExist2",
           0x18:"FClose", 0x20:"FRead", 0x28:"FWrite"}
CMP_CALLS = ("strcmp","strncmp","stricmp","memcmp")  # by op only; we mark cmp/test mnemonics

def dump_after(call_rva, n=0x60):
    cva = base + call_rva
    off = cva - ts
    out = []
    started = False
    for ins in md.disasm(tdata[off:off+n], cva):
        mark = ""
        op = ins.op_str
        if ins.address == cva:
            mark = "  <<< AdjustFileName (slot1 +0x8)"
        if ins.mnemonic == "call" and "+ 0x" in op:
            for so, nm in FILEOPS.items():
                if f"0x{so:x}]" in op:
                    mark = f"  <-- file-op slot {nm} (+{so:#x})"
        if ins.mnemonic in ("cmp","test") and ("[rax" in op or ", rax" in op or "rax," in op):
            mark = "  <-- CMP/TEST on result rax (FORM?)"
        if ins.mnemonic == "movzx" and "[rax" in op:
            mark = "  <-- read byte at *rax (PREFIX?)"
        out.append(f"  {ins.address:#011x}: {ins.mnemonic:8s} {op}{mark}")
        if ins.mnemonic in ("ret","int3"):
            break
    return out

for fr, cr in SITES:
    print(f"\n===== func RVA {fr:#x}  call RVA {cr:#x} =====")
    for line in dump_after(cr):
        print(line)
