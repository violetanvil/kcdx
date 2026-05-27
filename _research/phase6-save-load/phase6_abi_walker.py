"""ROUND 3 ABI recon: walk each save/load function body, find every
incoming-arg read offset (relative to entry_rsp), classify width and
guess type. Recursive disassembly: follow conditional + unconditional
short/near jumps within the function so we don't miss branches.

Outputs both raw linear disassembly and a per-function summary of:
- prologue layout (frame setup, base register used for stack args)
- every memory operand whose base is rsp, rbp, or (if used) r11
- mapping each access to "Nth incoming arg" assuming MSVC x64 ABI:
    arg1 = rcx (home @ entry_rsp+0x08)
    arg2 = rdx (home @ entry_rsp+0x10)
    arg3 = r8  (home @ entry_rsp+0x18)
    arg4 = r9  (home @ entry_rsp+0x20)
    arg5 = first true stack arg @ entry_rsp+0x28
    arg6 @ entry_rsp+0x30
    arg7 @ entry_rsp+0x38
"""
import sys
import pefile
import capstone
from collections import OrderedDict

DLL = sys.argv[1]
ENTRY_VA = int(sys.argv[2], 16)
MAX_INSNS = int(sys.argv[3]) if len(sys.argv) > 3 else 2000

pe = pefile.PE(DLL, fast_load=True)
image_base = pe.OPTIONAL_HEADER.ImageBase

# Locate the .text section that contains entry
text_data = None
text_va = None
for sec in pe.sections:
    if not (sec.Characteristics & 0x20000000):
        continue
    sva = image_base + sec.VirtualAddress
    sdata = sec.get_data()
    if sva <= ENTRY_VA < sva + len(sdata):
        text_data = sdata
        text_va = sva
        break

if text_data is None:
    sys.exit("entry VA not in any executable section")

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True

def disasm_at(addr):
    off = addr - text_va
    if off < 0 or off >= len(text_data):
        return None
    sub = text_data[off:off + 16]
    for ins in md.disasm(sub, addr):
        return ins
    return None

# Recursive walker — track visited addresses + queue of branch targets.
visited = set()
order = []  # discovery order (for printing)
queue = [ENTRY_VA]
entry_addr = ENTRY_VA

# Branch mnemonics we follow as additional targets (in addition to
# falling through). For unconditional jumps we only follow the target.
COND_JMPS = {
    "je", "jne", "jz", "jnz", "jg", "jl", "jge", "jle", "ja", "jb",
    "jae", "jbe", "js", "jns", "jo", "jno", "jp", "jnp", "jpe", "jpo",
    "jcxz", "jecxz", "jrcxz",
}
UNCOND_JMPS = {"jmp"}
TERMINATORS = {"ret", "retn", "retf", "int3"}

while queue:
    addr = queue.pop(0)
    if addr in visited:
        continue
    # Disassemble linearly from `addr` until a terminator/branch.
    cur = addr
    safety = 0
    while cur not in visited and safety < MAX_INSNS:
        safety += 1
        ins = disasm_at(cur)
        if ins is None:
            break
        visited.add(cur)
        order.append(cur)
        mn = ins.mnemonic.lower()
        if mn in TERMINATORS:
            break
        # Track jumps
        if mn in UNCOND_JMPS:
            # follow the target (if immediate) and stop the linear walk
            if ins.operands and ins.operands[0].type == capstone.x86.X86_OP_IMM:
                tgt = ins.operands[0].imm
                # only follow if reasonably close (within 2 MB of entry)
                if abs(tgt - entry_addr) < 0x200000:
                    queue.append(tgt)
            break
        if mn in COND_JMPS:
            if ins.operands and ins.operands[0].type == capstone.x86.X86_OP_IMM:
                tgt = ins.operands[0].imm
                if abs(tgt - entry_addr) < 0x200000:
                    queue.append(tgt)
            # fall through too
        cur = ins.address + ins.size

# Sort discovered addresses for clean linear printout
sorted_addrs = sorted(visited)

# Build a function "extent" — entry up to highest visited insn address
extent_end = max(sorted_addrs)
# Add the insn at extent_end its size
last_ins = disasm_at(extent_end)
if last_ins:
    extent_end += last_ins.size

print(f"=== Function @ 0x{entry_addr:016X} ===")
print(f"  Visited {len(visited)} unique addresses, extent 0x{entry_addr:X}..0x{extent_end:X}")
print()

# --- PROLOGUE INSPECTION ---
# Convention used below:
#   "X_offset_from_entry_rsp" = (X - entry_rsp). So if rsp drops by 40
#   from entry_rsp due to pushes, rsp_offset_from_entry_rsp = -40, and
#   then `[rsp + disp]` resolves to entry_rsp + (-40 + disp).
#
# Prologue tracks:
#   - mov r11, rsp                  => r11 mirrors entry_rsp at *that point*
#   - push rXX                      => rsp drops 8
#   - sub rsp, N                    => rsp drops N
#   - mov rbp, rsp                  => rbp = current rsp (so far)
#   - lea rbp, [rsp+/-K]            => rbp = current rsp + K
#
# Then [rbp+N] / [rsp+N] / [r11+N] mem reads can be back-mapped to
# entry_rsp+M and from there to arg slot indices.

print("--- Linear disassembly (prologue + body in addr order) ---")

push_count = 0
sub_rsp = 0
rbp_offset_from_entry_rsp = None  # rbp = entry_rsp + this value (usually negative)
r11_offset_from_entry_rsp = None  # r11 = entry_rsp + this value (usually 0 or negative)
rsp_offset_from_entry_rsp = 0  # rsp = entry_rsp + this value (negative after pushes/sub)
prologue_done = False
in_prologue_addrs = []

# We'll need to know per insn whether we're still in prologue.
# Heuristic: prologue continues as long as we see only the canonical
# prologue insns (push, mov-to-home, sub rsp, mov rbp/r11). The first
# instruction that isn't one of those (or once we see a call) ends prologue.
PROLOGUE_OK = ("push", "mov", "sub", "lea", "xor", "movsxd")

# Walk in sorted address order — linear prologue.
def is_prologue_ish(ins):
    mn = ins.mnemonic.lower()
    if mn not in PROLOGUE_OK:
        return False
    # call ends prologue; ret/jump ends prologue
    return True

# We re-disassemble linearly from entry without following branches for
# the prologue scan.
cur = entry_addr
prologue_insns = []
while cur < extent_end:
    ins = disasm_at(cur)
    if ins is None:
        break
    mn = ins.mnemonic.lower()
    if not is_prologue_ish(ins):
        break
    prologue_insns.append(ins)
    # Update tracking
    if mn == "push":
        # 8 bytes
        push_count += 1
        rsp_offset_from_entry_rsp -= 8
    elif mn == "sub" and ins.op_str.startswith("rsp,"):
        # parse immediate
        try:
            imm = int(ins.op_str.split(",", 1)[1].strip(), 16)
            rsp_offset_from_entry_rsp -= imm
        except ValueError:
            pass
    elif mn == "mov" and ins.op_str.startswith("rbp, rsp"):
        rbp_offset_from_entry_rsp = rsp_offset_from_entry_rsp
    elif mn == "lea" and "rbp," in ins.op_str:
        # parse "rbp, [rsp - 0x37]" or "rbp, [rsp + 0xC9]" etc.
        if len(ins.operands) >= 2 and ins.operands[1].type == capstone.x86.X86_OP_MEM:
            mem = ins.operands[1].mem
            base_reg = ins.reg_name(mem.base) if mem.base else None
            disp = mem.disp
            if base_reg == "rsp":
                rbp_offset_from_entry_rsp = rsp_offset_from_entry_rsp + disp
            elif base_reg == "rbp" and rbp_offset_from_entry_rsp is not None:
                rbp_offset_from_entry_rsp = rbp_offset_from_entry_rsp + disp
    elif mn == "mov" and ins.op_str.startswith("r11, rsp"):
        r11_offset_from_entry_rsp = rsp_offset_from_entry_rsp
    cur = ins.address + ins.size

print(f"  Prologue insns: {len(prologue_insns)}")
print(f"  Final rsp offset from entry_rsp: {rsp_offset_from_entry_rsp} ({hex(rsp_offset_from_entry_rsp)})")
print(f"  rbp offset from entry_rsp: {rbp_offset_from_entry_rsp}")
print(f"  r11 offset from entry_rsp: {r11_offset_from_entry_rsp}")
print()

# Now: for every MEM operand in the visited insns that uses rsp/rbp/r11
# as base, compute the "entry_rsp + N" mapping and decide if N is a
# plausible incoming-arg slot.

# For rsp-based reads we have a problem: rsp changes through the
# function. The simple assumption is that *after* the prologue rsp is
# constant at rsp_offset_from_entry_rsp until epilogue. This is mostly
# true for MSVC functions that don't use alloca / dynamic stack alloc.
# We'll flag rsp reads with offset > sub_rsp_amount as "above the
# saved frame" = stack args. Reads with offset <= sub_rsp are local
# vars in the new frame.

def fmt_disp(d):
    if d >= 0:
        return f"+0x{d:X}"
    return f"-0x{-d:X}"

arg_offsets = OrderedDict()  # entry_rsp_offset -> list of (addr, ins_str, width)

# Track r11/rbp redefinitions inside the body. After the epilogue's
# `lea r11, [rsp + K]` (or similar), [r11+N] reads are register
# restores, NOT incoming-arg reads. We flag those addresses to
# exclude them from arg accounting.
prologue_end = (prologue_insns[-1].address + prologue_insns[-1].size
                if prologue_insns else entry_addr)

r11_redefined_after = None
rbp_redefined_after = None
for addr in sorted_addrs:
    if addr < prologue_end:
        continue
    ins = disasm_at(addr)
    if not ins:
        continue
    mn = ins.mnemonic.lower()
    if mn in ("lea", "mov") and ins.operands and ins.operands[0].type == capstone.x86.X86_OP_REG:
        dest = ins.reg_name(ins.operands[0].reg)
        if dest == "r11" and r11_redefined_after is None:
            r11_redefined_after = addr
        if dest == "rbp" and rbp_redefined_after is None:
            rbp_redefined_after = addr

print(f"  r11 redefined at: {hex(r11_redefined_after) if r11_redefined_after else 'never (still mirrors entry_rsp)'}")
print(f"  rbp redefined at: {hex(rbp_redefined_after) if rbp_redefined_after else 'never (stable post-prologue)'}")
print()
print("--- Memory operands referencing rsp/rbp/r11 ---")

for addr in sorted_addrs:
    ins = disasm_at(addr)
    if not ins:
        continue
    is_after_r11_redef = (r11_redefined_after is not None and addr >= r11_redefined_after)
    is_after_rbp_redef = (rbp_redefined_after is not None and addr >= rbp_redefined_after)
    for op in ins.operands:
        if op.type != capstone.x86.X86_OP_MEM:
            continue
        mem = op.mem
        base = ins.reg_name(mem.base) if mem.base else None
        idx = ins.reg_name(mem.index) if mem.index else None
        disp = mem.disp
        if idx is not None:
            continue
        if base == "rsp":
            entry_rsp_offset = rsp_offset_from_entry_rsp + disp
            if 0x08 <= entry_rsp_offset <= 0x100:
                arg_offsets.setdefault(entry_rsp_offset, []).append((addr, str(ins), op.size, "rsp"))
        elif base == "rbp" and rbp_offset_from_entry_rsp is not None and not is_after_rbp_redef:
            entry_rsp_offset = rbp_offset_from_entry_rsp + disp
            if 0x08 <= entry_rsp_offset <= 0x100:
                arg_offsets.setdefault(entry_rsp_offset, []).append((addr, str(ins), op.size, "rbp"))
        elif base == "r11" and r11_offset_from_entry_rsp is not None and not is_after_r11_redef:
            entry_rsp_offset = r11_offset_from_entry_rsp + disp
            if 0x08 <= entry_rsp_offset <= 0x100:
                arg_offsets.setdefault(entry_rsp_offset, []).append((addr, str(ins), op.size, "r11"))

# Also: track LEA writes to home slots, e.g. `lea` storing pointers into
# [rsp+8] is mov-to-home for callees — these are NOT reads of the
# incoming args, but they tell us the function does use home slots.
# Discard them by only flagging reads — Capstone marks operand access.

def access_kind(ins, op):
    """Heuristic: is this operand a read or a write?"""
    # For most mnemonics:
    # mov [mem], reg   => write
    # mov reg, [mem]   => read
    # cmp [mem], imm   => read
    # The first operand is the destination for most x86 insns.
    if not ins.operands:
        return "unknown"
    # Find this op's index among ins.operands
    for i, o in enumerate(ins.operands):
        if o is op:
            if ins.mnemonic.lower() in ("mov", "movzx", "movsxd", "movsx", "lea"):
                return "write" if i == 0 else "read"
            if ins.mnemonic.lower() in ("cmp", "test", "push"):
                return "read"
            return "either"
    return "unknown"

# Refine arg_offsets: split reads vs writes.
arg_reads = OrderedDict()
for off, accesses in arg_offsets.items():
    for (addr, insn_str, size, base) in accesses:
        ins = disasm_at(addr)
        # Find the matching op
        for op in ins.operands:
            if op.type != capstone.x86.X86_OP_MEM:
                continue
            mem = op.mem
            base_reg = ins.reg_name(mem.base) if mem.base else None
            if base_reg != base:
                continue
            disp = mem.disp
            if base == "rsp":
                cur_off = rsp_offset_from_entry_rsp + disp
            elif base == "rbp":
                cur_off = (rbp_offset_from_entry_rsp or 0) + disp
            elif base == "r11":
                cur_off = (r11_offset_from_entry_rsp or 0) + disp
            else:
                continue
            if cur_off != off:
                continue
            kind = access_kind(ins, op)
            arg_reads.setdefault(off, []).append((addr, insn_str, size, base, kind))
            break

print()
print("--- Incoming-arg slot accesses (entry_rsp+offset) ---")
print()
print("MSVC x64 ABI slot map:")
print("  +0x08  = arg1 home (rcx)")
print("  +0x10  = arg2 home (rdx)")
print("  +0x18  = arg3 home (r8)")
print("  +0x20  = arg4 home (r9)")
print("  +0x28  = arg5 stack")
print("  +0x30  = arg6 stack")
print("  +0x38  = arg7 stack")
print("  +0x40  = arg8 stack")
print()

for off in sorted(arg_reads.keys()):
    accesses = arg_reads[off]
    arg_idx = off // 8  # arg1 is at +0x08 = idx 1
    print(f"  entry_rsp+0x{off:02X}  (arg #{arg_idx}):")
    for (addr, insn_str, size, base, kind) in accesses:
        print(f"    [{kind:5s}] 0x{addr:X}: {insn_str}  (width={size}B, via {base})")
    print()

# And also dump the full linear disassembly for grep-ability
print()
print("=" * 70)
print("FULL DISASSEMBLY (sorted address order)")
print("=" * 70)
for addr in sorted_addrs:
    ins = disasm_at(addr)
    if not ins:
        continue
    print(f"  0x{ins.address:016X}: {ins.bytes.hex():24s} {ins.mnemonic:<8} {ins.op_str}")
