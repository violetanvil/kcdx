"""KI-0012 full-ctor-replacement graphics-AV recon.

Two questions, theory-independent:

Q1. Does SELECT (FUN_180da104c) or any helper it calls WRITE into the
    C_ModManager object (the rcx/rbx arg), specifically +0x18..+0x58? The
    ctor zero-inits +0x18..+0x58 then calls SELECT; if SELECT populates
    +0x48/+0x50/+0x58 the kcdx replacement (which skips SELECT) leaves them
    zero/garbage and the engine reads them. Disassemble SELECT + every E8
    callee that takes the modMgr (rbx) as an arg, and flag every
    `mov [rbx+N], ...` / `mov [<reg holding modMgr>+N], ...`.

Q2. The graphics path. Frame-4 dispatch FUN_19C6268 reads a gEnv-style global,
    virtual-calls a getter, gets "the C_ModManager", then `lea rcx,[this+0x30];
    call FUN_1DBBE20`. That is the enabled-list path that AVed in the
    return-obj bug. But the CURRENT bug is a DLSS/FSR2 graphics AV at a tiny
    copy helper iterating a {ptr,count@+8,cap@+0xC,stride 0x10} array. Find
    where that {stride-0x10} structure comes from on the C_Game::CreateInstance
    path. Disassemble:
      - FUN_19C6268 (frame-4 dispatch) fully.
      - C_Game::CreateInstance graphics frames (need RVAs from the new crash
        dump; here disasm the known modMgr-getter chain + the global).
      - the copy helper region (ffxFsr2ResourceIsNull+0x633120) — locate it.

Outcome map (Q1):
  - SELECT writes NONE of +0x18..+0x58 of the modMgr  -> those slots are
    populated by the INNER helpers SELECT calls (the scanned/enabled vector
    builders), NOT by SELECT directly; +0x48/+0x50/+0x58 are then either (a)
    written by an inner helper = a real field kcdx must replicate, or (b)
    never written = genuinely unused (FINDINGS.md's claim), and the genuine
    object's non-zero +0x48..+0x58 are a SECOND std::vector the ctor/SELECT
    build.
  - SELECT (or a callee) writes [modMgr+0x48/0x50/0x58] <- a vector triple ->
    +0x48..0x58 IS a second std::vector; kcdx leaving it zero is the bug
    candidate.

All raw; the evidence dictates, not the reverse.
"""
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"C:\Users\Michael\Documents\KCD2 Mods\kcdx\third-party-ghidra\WHGame.dll"

pe = pefile.PE(DLL, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
text = next(s for s in pe.sections if s.Name.rstrip(b"\x00") == b".text")
text_data = text.get_data()
text_va = text.VirtualAddress
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.skipdata = True


def disasm(label, rva, n, stop_on_ret=True):
    off = rva - text_va
    print("=" * 78)
    print(f"== {label}   VA={base+rva:#x}  RVA={rva:#x}")
    print("=" * 78)
    if off < 0 or off + n > len(text_data):
        print(f"   OUT OF .text (off={off:#x})"); print(); return
    for ins in md.disasm(text_data[off:off + n], base + rva):
        note = ""
        if "rip" in ins.op_str and ins.mnemonic in ("lea", "mov", "call", "cmp"):
            try:
                d = ins.op_str.split("rip")[1].split("]")[0]
                disp = int(d.replace("+", "").replace(" ", ""), 16) if d.strip() else 0
                note = f"   -> RVA {(ins.address+ins.size+disp)-base:#x}"
            except Exception:
                pass
        print(f"  {ins.address:#010x}  {ins.bytes.hex():<22} {ins.mnemonic:<7} {ins.op_str}{note}")
        if stop_on_ret and ins.mnemonic == "ret":
            break
    print()


# === Q1: SELECT's inner helpers — what writes the modMgr object ===============
# SELECT is FUN_180da104c. Its E8 callees (from the modmanager filtered dump):
#   0x180da0fb0  (called 1st; reads [modMgr.vtable+0x38])
#   0x180da1178  (the big one — reads [rsi+8], calls vtable+0x228 etc.)
#   0x180da1294  (ModManager_ReadModOrder per the design doc)
# rbx = modMgr inside SELECT. These helpers take rcx = modMgr or rcx = modMgr+N.
for label, rva, n in [
    ("ModManager_Select FULL", 0xDA104C, 0x12C),
    ("SELECT_helper_0fb0 (1st call)", 0xDA0FB0, 0x40),
    ("SELECT_helper_1178 (2nd call, big)", 0xDA1178, 0x120),
    ("ModManager_ReadModOrder_1294 (3rd call)", 0xDA1294, 0x140),
]:
    disasm(label, rva, n, stop_on_ret=False)


# === Q2: the modMgr-getter dispatch + graphics structure ======================
# Frame-4 dispatch from the post-step-4 AV doc: FUN_19C6268 loads a gEnv global,
# virtual+0xB8 getter -> modMgr, then iterates [modMgr+0x30,+0x38). The CURRENT
# graphics AV is on a DIFFERENT consumer reading a {ptr,count@+8,cap@+0xC,
# stride 0x10} structure on the CreateInstance path. Disasm the dispatch fn to
# see EVERY modMgr field it reads (does it read +0x48/+0x50/+0x58 too?).
disasm("frame4_dispatch FUN_19C6268 (modMgr getter + enabled-list walk)",
       0x19C6268, 0x100, stop_on_ret=False)

# ModManager_Mount (id 3102, RVA 0x4D9058) — the native MOUNT. Does it read
# +0x48/+0x50/+0x58 of the modMgr (a second vector) in addition to +0x30..+0x40?
disasm("ModManager_Mount_4D9058 (MOUNT driver)", 0x4D9058, 0x180, stop_on_ret=False)
