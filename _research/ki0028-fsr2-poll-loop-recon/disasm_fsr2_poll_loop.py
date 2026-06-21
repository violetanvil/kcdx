"""KI-0028 FSR2 poll-loop static recon — read the loop exit condition.

P-H (live cdb) settled: Main is in a NON-PROGRESSING SleepEx poll-loop inside
C_Game::CreateInstance -> FSR2 code. The SleepEx return address is
WHGame!ffxFsr2ResourceIsNull+0x36af90 (the instruction AFTER the SleepEx call,
i.e. the top of the poll loop's tail). Two samples 2s apart were byte-identical.

This reads the BINARY (no launch), theory-independent, to answer:
  Q1. Resolve ffxFsr2ResourceIsNull's export RVA, add 0x36af90 -> the SleepEx
      return site. Disasm a window BEFORE and AFTER it.
  Q2. What is the LOOP EXIT TEST? After SleepEx returns, what does the code
      check to decide loop-again vs proceed? (a memory load + cmp + conditional
      jump back to before the SleepEx call = the polled condition.)
  Q3. WHERE does the polled value come from — a global, an NGX/FSR2 handle field,
      a CCryPak-object field, a file-backed resource? This is what the FS takeover
      must make satisfiable (no thunk-back).

All raw; the disassembly dictates. Reuses the pefile+capstone pattern from
ki0026-ngx-raise-site-recon/disasm_ngx_raise_site.py.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
SLEEP_OFF = 0x36af90  # ffxFsr2ResourceIsNull + this = the SleepEx return site

pe = pefile.PE(DLL, fast_load=True)
pe.parse_data_directories(directories=[
    pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])

# 1. Resolve the ffxFsr2ResourceIsNull export RVA.
exp_rva = None
for e in pe.DIRECTORY_ENTRY_EXPORT.symbols:
    if e.name and b"ffxFsr2ResourceIsNull" in e.name:
        exp_rva = e.address
        print(f"EXPORT ffxFsr2ResourceIsNull RVA = 0x{exp_rva:x}")
        break
if exp_rva is None:
    print("ffxFsr2ResourceIsNull NOT in export table; listing fsr2-ish exports:")
    for e in pe.DIRECTORY_ENTRY_EXPORT.symbols:
        if e.name and (b"fsr" in e.name.lower() or b"ffx" in e.name.lower()):
            print(f"  {e.name.decode(errors='replace')} @ 0x{e.address:x}")
    raise SystemExit(0)

target_rva = exp_rva + SLEEP_OFF
print(f"SleepEx return site RVA = 0x{target_rva:x} (export + 0x{SLEEP_OFF:x})")

# 2. Map RVA -> file offset, read a window AROUND the target (before+after).
def rva_to_off(rva):
    for s in pe.sections:
        if s.VirtualAddress <= rva < s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData):
            return s.PointerToRawData + (rva - s.VirtualAddress)
    return None

WIN_BEFORE = 0x120
WIN_AFTER  = 0x80
start_rva = target_rva - WIN_BEFORE
off = rva_to_off(start_rva)
if off is None:
    print(f"RVA 0x{start_rva:x} not in any section"); raise SystemExit(0)

data = pe.get_data(start_rva, WIN_BEFORE + WIN_AFTER)
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True
IMAGE_BASE = 0x180000000
print(f"\n=== disasm RVA 0x{start_rva:x} .. 0x{start_rva+WIN_BEFORE+WIN_AFTER:x} "
      f"(target SleepEx-return 0x{target_rva:x} marked '>>>') ===")
for insn in md.disasm(data, IMAGE_BASE + start_rva):
    rva = insn.address - IMAGE_BASE
    mark = ">>>" if abs(rva - target_rva) < 8 else "   "
    print(f"{mark} 0x{rva:08x}  {insn.mnemonic:<7}{insn.op_str}")
