"""KI-0026 NGX/FSR2 0xC8-raise-site static recon — read the null check that aborts.

The dump (kcdx_2026-06-19_23-09-24.dmp) settled: the 0xC8 is a DELIBERATE
RaiseException(0xC8) from inside the NGX/FSR2 graphics-init, on an
ffxFsr2ResourceIsNull null check, whole chain in WHGame. Culprit
WHGame.DLL rva=38115914 (=0x245F2CA) = the raise site. Two live NGX handles
ride into the raise (r12=0x2b3fbb8d3c0, r13=0x2b47bf61cc0).

This reads the BINARY (no launch) to answer, theory-independent:

  Q1. What does the code at/around 0x245F2CA actually do? Confirm it is a
      RaiseException(0xC8) (push/mov 0xC8 -> call RaiseException) and read the
      branch that REACHES it — the null check whose failure raises.

  Q2. WHAT pointer/resource is null-checked just before the raise? Is it read
      from a global (gEnv-style), from one of the NGX handles (r12/r13), or
      from a CCryPak-object field? This decides the P-live hook shape:
        - read from a gEnv global the swap never touches -> the swap-write
          breaks something UPSTREAM that populates that global (widen).
        - read from the CCryPak object [pCryPak+0xNN] -> the swap-write
          (overwriting [pCryPak+0x00]) corrupts a field NGX reads -> the
          hook watches that field.
        - read from an NGX handle (r12/r13) -> the handle itself is built
          wrong; trace its producer.

  Q3. The caller CreateGameStartup+0xda687 — what does it pass / set up before
      the raising callee? (Establishes whether the null resource is produced
      on this same frame or inherited.)

All raw; the disassembly dictates, not a theory about it. Outcome map lives in
the KI Trail row (P-static-2). Reuses the pefile+capstone pattern from
ki0012-modmanager-size-recon/disasm_select_helpers_and_graphics.py.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"

pe = pefile.PE(DLL, fast_load=True)
pe.parse_data_directories(directories=[
    pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_IMPORT"],
    pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXPORT"],
])
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
text_data = text.get_data()
text_va = text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.skipdata = True

# Resolve RaiseException's IAT thunk RVA so a `call [rip+x] -> RaiseException`
# is unambiguous in the dump.
raise_thunks = []
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    dll = entry.dll.decode(errors="replace").lower()
    if "kernel32" not in dll:
        continue
    for imp in entry.imports:
        nm = (imp.name or b"").decode(errors="replace")
        if nm in ("RaiseException",):
            raise_thunks.append((nm, imp.address - base))
print("RaiseException IAT thunks:", [(n, hex(r)) for n, r in raise_thunks])
print()


def find_func_start(rva, max_back=0x800):
    """Scan backward for a likely function prologue / int3 padding boundary.
    Returns the RVA just after the nearest run of int3 (0xCC) padding, which
    in MSVC output reliably precedes a function start."""
    off = rva - text_va
    lo = max(0, off - max_back)
    window = text_data[lo:off]
    # find the LAST occurrence of >=2 consecutive 0xCC before rva
    last = -1
    i = 0
    while i < len(window) - 1:
        if window[i] == 0xCC and window[i + 1] == 0xCC:
            j = i
            while j < len(window) and window[j] == 0xCC:
                j += 1
            last = j  # first byte AFTER the int3 run
            i = j
        else:
            i += 1
    if last < 0:
        return None
    return text_va + lo + last


def disasm(label, rva, n, back=0):
    """Disassemble [rva-back, rva-back+n). Annotate rip-relative targets and
    flag any immediate 0xC8 and any call to a RaiseException thunk."""
    start = rva - back
    off = start - text_va
    print("=" * 80)
    print(f"== {label}   VA={base+start:#x}  RVA={start:#x}  (anchor {base+rva:#x}/{rva:#x})")
    print("=" * 80)
    if off < 0 or off + n > len(text_data):
        print(f"   OUT OF .text (off={off:#x}, textlen={len(text_data):#x})"); print(); return
    for ins in md.disasm(text_data[off:off + n], base + start):
        note = ""
        if "rip" in ins.op_str:
            try:
                d = ins.op_str.split("rip")[1].split("]")[0]
                disp = int(d.replace("+", "").replace(" ", ""), 16) if d.strip() else 0
                tgt = (ins.address + ins.size + disp) - base
                note = f"   -> RVA {tgt:#x}"
                if any(tgt == r for _, r in raise_thunks):
                    note += "  <== RaiseException"
            except Exception:
                pass
        if "0xc8" in ins.op_str.lower() or "0c8h" in ins.op_str.lower():
            note += "   <== imm 0xC8 (=200, the exception code)"
        mark = ">>" if ins.address - base == rva else "  "
        print(f"{mark}{ins.address:#012x}  {ins.bytes.hex():<24} {ins.mnemonic:<7} {ins.op_str}{note}")
    print()


# Q1+Q2: the REAL raise site. Derived from the dump (NOT the GUARD decimal):
#   WHGame base = 0x7fffc7850000; frame-01 return (after call RaiseException)
#   = 0x7fffc9ca9a4a -> RVA 0x2459A4A. Find the containing function's start
#   (int3-padding boundary) and disassemble FORWARD so instruction boundaries
#   are correct, reaching the raise.
RAISE_RVA = 0x2459A4A
fstart = find_func_start(RAISE_RVA)
print(f"raise-site containing-function start (int3 boundary) = {hex(fstart) if fstart else 'NOT FOUND'}  "
      f"(VA {hex(base+fstart) if fstart else '-'})")
print()
if fstart:
    disasm("NGX raising function (start -> past the RaiseException call)",
           fstart, (RAISE_RVA - fstart) + 0x20, back=0)

# Q3: the caller frame CreateGameStartup+0xda687. CreateGameStartup is an export;
# resolve its RVA from the export table, add 0xda687.
cgs_rva = None
for exp in pe.DIRECTORY_ENTRY_EXPORT.symbols:
    if exp.name and exp.name.decode(errors="replace") == "CreateGameStartup":
        cgs_rva = exp.address
        break
print(f"CreateGameStartup export RVA = {hex(cgs_rva) if cgs_rva else 'NOT FOUND'}")
print()
if cgs_rva:
    caller_rva = cgs_rva + 0xda687
    disasm("CreateGameStartup+0xda687 (caller of the raising callee)",
           caller_rva, 0x100, back=0x80)
