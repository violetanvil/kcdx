"""fs-takeover recon — Q2 via the REAL mount machinery (front-2 VERIFIED), not slot 71.

Slot 71 (0x7ad468) is NOT a mount fn (separate body decompile proves it: bool existence
/folder check, no pak-vector touch, no archive). The real mount entry tree (front-2):
  OpenPack slot6 0xda4e5c / slot7 0x193cb14 ; OpenPacks slot9 0x4d9bb0 / slot10 0x197c598
    -> glob worker 0x4d9c4c
    -> register worker 0x4d4824
       -> split-aware open 0x4d495c -> per-part mount leaf 0x4d526c -> factory slot72 0x4d5580
                                                                    -> rank-insert 0x4d70a4
ClosePakByIndex slot100 0x2418f78 (front-1 VERIFIED: indexes [+0x40] pak-stream vec, releases i-th).

DIRECT (rel32) callers of each — these ARE readable edges (AP19): the call is in the caller body.
We do NOT attempt to attribute the over-common [reg+disp] vtable dispatches.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
data = text.get_data()
tva = text.VirtualAddress

TARGETS = {
    "OpenPack slot6 0xda4e5c": 0xda4e5c,
    "OpenPack slot7 0x193cb14": 0x193cb14,
    "OpenPacks slot9 0x4d9bb0": 0x4d9bb0,
    "OpenPacks slot10 0x197c598": 0x197c598,
    "glob worker 0x4d9c4c": 0x4d9c4c,
    "register worker 0x4d4824": 0x4d4824,
    "split-open 0x4d495c": 0x4d495c,
    "per-part mount leaf 0x4d526c": 0x4d526c,
    "factory slot72 0x4d5580": 0x4d5580,
    "rank-insert 0x4d70a4": 0x4d70a4,
    "ClosePakByIndex slot100 0x2418f78": 0x2418f78,
    "release-i-th leaf 0x4607e4": 0x4607e4,
}


def direct_callers(target_rva):
    target_va = base + target_rva
    sites = []
    for i in range(len(data) - 5):
        if data[i] == 0xE8:
            rel = int.from_bytes(data[i + 1:i + 5], "little", signed=True)
            site_va = base + tva + i
            if site_va + 5 + rel == target_va:
                sites.append(site_va)
    return sites


for label, rva in TARGETS.items():
    sites = direct_callers(rva)
    print("=" * 86)
    print(f"== DIRECT callers of {label} (RVA {rva:#x}) — {len(sites)} site(s)")
    print("=" * 86)
    for s in sites:
        print(f"  caller call-site VA {s:#x}  RVA {s-base:#x}")
    print()
