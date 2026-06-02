import pefile, bisect
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_OP_MEM

pe = pefile.PE("third-party-ghidra/WHGame.dll", fast_load=True)
pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
text = next(s for s in pe.sections if s.Name.rstrip(b'\x00')==b'.text')
text_rva = text.VirtualAddress; data = text.get_data(); n=len(data)
entries = sorted({f.struct.BeginAddress for f in pe.DIRECTORY_ENTRY_EXCEPTION})
md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail=True

def count_aob(b):
    cnt=0;first=b[0];nb=len(b);i=0;rng=n-nb
    while i<=rng:
        if data[i]==first and data[i:i+nb]==b: cnt+=1
        i+=1
    return cnt
def dist_prev(rva):
    i=bisect.bisect_right(entries,rva)-1
    return rva-entries[i] if i>=0 else None

# FINAL CANDIDATE: verify by disassembling the WHOLE function the window sits in
# so alignment is correct, then confirm the window's instructions are operand-free.
# Candidate function: a large stable function. Use 0x3C000 region's containing fn.
# Disassemble from the prev entry to get aligned instruction stream, then locate
# the window and check its instructions.
def analyze(window_rva, W=16):
    off = window_rva - text_rva
    pi = bisect.bisect_right(entries, window_rva)-1
    fn_entry = entries[pi]
    fn_off = fn_entry - text_rva
    # disassemble from fn entry forward up to window+W
    code = bytes(data[fn_off: off+W+16])
    insns = list(md.disasm(code, fn_entry))
    # find instruction boundaries; check the window [window_rva, window_rva+W)
    # is covered by whole instructions and none is rel/rip
    win_ins = [ins for ins in insns if ins.address >= window_rva and ins.address < window_rva+W]
    aligned_start = any(ins.address == window_rva for ins in insns)
    unstable = False; detail=[]
    for ins in win_ins:
        rel = (ins.mnemonic.startswith('j') or ins.mnemonic=='call') and ins.op_str.startswith('0x')
        rip = any(op.type==CS_OP_MEM and op.mem.base==0 and op.mem.disp!=0 for op in ins.operands)
        if rel or rip: unstable=True
        detail.append(f"{ins.mnemonic} {ins.op_str}{' [REL]' if rel else ''}{' [RIP]' if rip else ''}")
    b=bytes(data[off:off+W])
    print(f"  RVA 0x{window_rva:X} in fn 0x{fn_entry:X} (dist {window_rva-fn_entry}): aligned_start={aligned_start} unstable={unstable} count={count_aob(b)}")
    print(f"    aob={b.hex(' ').upper()}")
    for d in detail: print(f"      {d}")

for rva in (0x1A000, 0x3C000, 0x9800):
    analyze(rva)
