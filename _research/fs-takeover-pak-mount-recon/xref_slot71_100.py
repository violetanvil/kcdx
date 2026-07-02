"""fs-takeover pak-mount recon — verify the slots + find callers (Q2).

(1) Confirm the vtable slot of 0x7ad468 (alleged slot 71) and 0x2418f78 (alleged
    slot 100 ClosePakByIndex) by reading the CCryPak vtable @ VA 0x183A95FA8 directly
    from .rdata: which +offset (slot) actually holds each RVA.
(2) Find DIRECT calls (call rel32) to each RVA across .text — the AP19 caller set.
(3) Find INDIRECT vtable-dispatch call sites: `call qword ptr [reg+0x238]` (slot 71)
    and `call qword ptr [reg+0x320]` (slot 100). These are caller-agnostic (we cannot
    prove the reg is a CCryPak* from the opcode alone) — reported as candidate sites
    to read in the caller's body, NOT asserted edges.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
VTABLE_VA = 0x183A95FA8
RVA_71 = 0x7ad468
RVA_100 = 0x2418f78

sections = {}
for s in pe.sections:
    nm = s.Name.rstrip(b"\x00").decode("latin1")
    sections[nm] = (s.VirtualAddress, s.get_data())

text_va, text_data = sections[".text"]
rdata_va, rdata_data = sections[".rdata"]


def read_qword(va):
    rva = va - base
    if rdata_va <= rva < rdata_va + len(rdata_data):
        off = rva - rdata_va
        return int.from_bytes(rdata_data[off:off + 8], "little")
    return None


print("=" * 86)
print(f"== VTABLE slot scan @ VA {VTABLE_VA:#x} — which slot holds 0x7ad468 / 0x2418f78?")
print("=" * 86)
for slot in range(0, 102):
    va = VTABLE_VA + slot * 8
    fn = read_qword(va)
    if fn is None:
        print(f"  slot {slot:>3} +{slot*8:#06x}: <out of .rdata>")
        continue
    frva = fn - base
    mark = ""
    if frva == RVA_71:
        mark = "   <==== 0x7ad468 (alleged slot 71)"
    if frva == RVA_100:
        mark = "   <==== 0x2418f78 (alleged slot 100)"
    if mark:
        print(f"  slot {slot:>3} +{slot*8:#06x}: RVA {frva:#x}{mark}")
print("  (only matching slots printed)")
print()

# Direct rel32 call scan across .text
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = False


def scan_direct(target_rva, label):
    print("=" * 86)
    print(f"== DIRECT call sites to {label} (RVA {target_rva:#x})")
    print("=" * 86)
    target_va = base + target_rva
    found = 0
    # E8 rel32 call: scan byte-by-byte for 0xE8 then verify decoded target
    i = 0
    while i < len(text_data) - 5:
        if text_data[i] == 0xE8:
            rel = int.from_bytes(text_data[i + 1:i + 5], "little", signed=True)
            site_va = base + text_va + i
            tgt = site_va + 5 + rel
            if tgt == target_va:
                print(f"  call site VA {site_va:#x}  RVA {site_va-base:#x}  -> {label}")
                found += 1
        i += 1
    print(f"  total direct call sites: {found}")
    print()


scan_direct(RVA_71, "0x7ad468")
scan_direct(RVA_100, "0x2418f78")


def scan_indirect(disp, label):
    print("=" * 86)
    print(f"== INDIRECT vtable-dispatch sites: call qword ptr [reg+{disp:#x}] ({label})")
    print(f"   (candidate sites — reg-is-CCryPak not proven from opcode; read caller body)")
    print("=" * 86)
    # FF /2 with disp32: opcode FF, modrm where reg field (bits 3-5)=010, mod=10 (disp32)
    # forms: ff 90+rm disp32 (no SIB) and ff 94 SIB disp32. Just decode the whole .text
    # and match mnemonic 'call' with op_str containing the disp.
    found = 0
    hexdisp = f"0x{disp:x}"
    for ins in md.disasm(text_data, base + text_va):
        if ins.mnemonic == "call" and "ptr" in ins.op_str and "+ " + hexdisp + "]" in ins.op_str:
            print(f"  VA {ins.address:#x}  RVA {ins.address-base:#x}  {ins.mnemonic} {ins.op_str}")
            found += 1
    print(f"  total indirect [+{disp:#x}] dispatch sites: {found}")
    print()


scan_indirect(0x238, "slot 71")
scan_indirect(0x320, "slot 100")
