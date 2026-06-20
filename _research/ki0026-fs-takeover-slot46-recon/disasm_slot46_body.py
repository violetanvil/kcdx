"""KI-0026 / fs-takeover slot-46 return-contract recon — read FUN_180460c08's body.

The decisive body for the gated claim: CCryPak vtable slot 46 (+0x170,
FUN_180460c08, RVA 0x460C08). Two prior fronts CONTRADICT on its return:
  - front3 (read-path call-site reading): "FGetSize via vtable" — the FRead
    body (FUN_18051cd00) stores slot-46's return as *param_3, a size-out.
  - front1 / readslot-abi: "fileno / handle-int — leaf _fileno" — a `_fileno`
    leaf-call FINGERPRINT only (i / LEAF-IDENTIFIED-ARGS-INFERRED), NOT a body read.

The slot-66 precedent (same dir's fs-takeover-readslot-abi FINDINGS) shows a
`_fileno` leaf is NEVER decisive: slot 66's `_fileno` was only the first hop of
an `_fileno -> _get_osfhandle -> GetFileTime` chain that returned a TIME. So a
`_fileno`-leaf fingerprint cannot settle whether slot 46 returns an fd or a size.

This reads the BINARY (no launch), theory-independent. Ground truth first:
disassemble the full FUN_180460c08 body and resolve EVERY call target against
the import table + internal funcs. The body's calls dictate the answer:

  Outcome A — body computes a SIZE (e.g. _fileno -> _get_osfhandle -> GetFileSizeEx,
              or _filelengthi64(_fileno(fp)), or fseek-end + ftell):
              the CLAIM is SUPPORTED. slot 46 = a size getter.
  Outcome B — body returns a BARE _fileno (just _fileno(fp), no size step):
              the CLAIM is REFUTED. slot 46 = fileno; FRead's *param_3 holds an
              fd it further processes; KI-0026's -1 has a different origin.
  Outcome C — body is genuinely ambiguous (no resolvable leaf, indirect-only):
              HALT-ambiguous; do not guess (AP2).

Reuses the pefile+capstone wiring from ki0026-ngx-raise-site-recon/.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"
SLOT46_RVA = 0x460C08

pe = pefile.PE(DLL, fast_load=True)
pe.parse_data_directories(directories=[
    pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"],
])
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
text_data = text.get_data()
text_va = text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.skipdata = True

# Map every imported function's IAT-thunk RVA -> "dll!name" so a
# `call [rip+x]` lands on a named import (the size/fileno discriminators).
iat = {}
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    dll = entry.dll.decode(errors="replace")
    for imp in entry.imports:
        nm = (imp.name or b"").decode(errors="replace") if imp.name else f"ord#{imp.ordinal}"
        iat[imp.address - base] = f"{dll}!{nm}"

DISCRIMINATORS = (
    "_fileno", "_get_osfhandle", "GetFileSizeEx", "GetFileSize",
    "_filelengthi64", "_filelength", "_lseeki64", "_lseek",
    "fseek", "ftell", "_ftelli64", "_fseeki64", "feof", "ferror",
    "GetFileInformationByHandle", "GetFileTime",
)


def find_func_start(rva, max_back=0x800):
    off = rva - text_va
    lo = max(0, off - max_back)
    window = text_data[lo:off]
    last = -1
    i = 0
    while i < len(window) - 1:
        if window[i] == 0xCC and window[i + 1] == 0xCC:
            j = i
            while j < len(window) and window[j] == 0xCC:
                j += 1
            last = j
            i = j
        else:
            i += 1
    if last < 0:
        return None
    return text_va + lo + last


def find_func_end(rva, max_fwd=0x600):
    """First int3-padding run AT/AFTER a ret, scanning forward from rva."""
    off = rva - text_va
    hi = min(len(text_data), off + max_fwd)
    i = off
    while i < hi - 1:
        if text_data[i] == 0xCC and text_data[i + 1] == 0xCC:
            return text_va + i
        i += 1
    return text_va + hi


fstart = SLOT46_RVA  # slot46 RVA IS the function entry (vtable binding)
fend = find_func_end(fstart)
n = fend - fstart
print(f"== FUN_180460c08  VA={base+fstart:#x}  RVA={fstart:#x}  len={n:#x} (end RVA {fend:#x})")
print("=" * 88)

hits = []
off = fstart - text_va
for ins in md.disasm(text_data[off:off + n], base + fstart):
    note = ""
    if "rip" in ins.op_str:
        try:
            d = ins.op_str.split("rip")[1].split("]")[0]
            disp = int(d.replace("+", "").replace(" ", ""), 16) if d.strip() else 0
            tgt = (ins.address + ins.size + disp) - base
            if tgt in iat:
                note = f"   -> import {iat[tgt]}"
                if any(disc in iat[tgt] for disc in DISCRIMINATORS):
                    note += "   <== DISCRIMINATOR"
                    hits.append((ins.address - base, iat[tgt]))
            else:
                note = f"   -> RVA {tgt:#x}"
        except Exception:
            pass
    if ins.mnemonic in ("call", "jmp") and ins.op_str.startswith("0x"):
        try:
            tgt = int(ins.op_str, 16) - base
            note = f"   -> intern RVA {tgt:#x}"
        except Exception:
            pass
    print(f"  {ins.address:#012x}  {ins.bytes.hex():<22} {ins.mnemonic:<7} {ins.op_str}{note}")

print()
print("DISCRIMINATOR import calls in body:", [(hex(a), n) for a, n in hits] or "NONE")
