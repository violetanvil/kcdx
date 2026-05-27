# Find a tiny getter in WHGame.dll suitable for a kcdx smoke-test detour.
# Strategy:
#   1. Resolve the IsInCombat vtable target. We know at 0x180561756 there is
#      `call qword ptr [rax+8]` where `rax` was just loaded from `[rcx]` where
#      `rcx = [object+0x90] + 0xB60`. Ghidra's decompiler should have a typed
#      reference; failing that we walk the function's call references and find
#      the one labeled FUN_* that is small.
#   2. Enumerate ALL small functions (<= 32 bytes) ending in C3 (ret) whose
#      body looks like a primitive getter (reads through [rcx + const],
#      returns in al / eax). Report the top candidates.
#@category KCD2

from ghidra.program.model.symbol import RefType
from ghidra.program.model.listing import CodeUnit

LISTING = currentProgram.getListing()
MEM = currentProgram.getMemory()
FUNCMAN = currentProgram.getFunctionManager()
IMAGE_BASE = currentProgram.getImageBase().getOffset()

print "=" * 80
print "kcdx smoke-test target hunt"
print "Image base: 0x%X" % IMAGE_BASE
print "=" * 80


def addr(off):
    return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(off)


def hex_bytes(b):
    return " ".join(["%02X" % (x & 0xFF) for x in b])


def get_func_bytes(func):
    body = func.getBody()
    out = bytearray()
    it = body.getAddresses(True)
    while it.hasNext():
        a = it.next()
        try:
            out.append(MEM.getByte(a) & 0xFF)
        except:
            return None
        if len(out) > 256:
            break
    return out


def func_summary(func):
    """Decompile-free shape: list of instruction mnemonics + operands."""
    out = []
    ins = LISTING.getInstructionAt(func.getEntryPoint())
    count = 0
    while ins is not None and func.getBody().contains(ins.getAddress()):
        out.append("%s %s" % (ins.getMnemonicString(), ins.toString().split(None, 1)[1] if len(ins.toString().split(None, 1)) > 1 else ""))
        ins = LISTING.getInstructionAfter(ins.getAddress())
        count += 1
        if count > 40:
            break
    return out


def function_at_call_site(call_addr):
    """Try to resolve the target function of the call at call_addr."""
    ins = LISTING.getInstructionAt(call_addr)
    if ins is None:
        return None
    refs = ins.getReferencesFrom()
    for r in refs:
        if r.getReferenceType().isCall():
            f = FUNCMAN.getFunctionAt(r.getToAddress())
            if f is not None:
                return f
    return None


# -- Part 1: walk references at the known outfit-swap site -------------------
print "\n--- Part 1: resolve IsInCombat (call qword ptr [rax+8] at 0x180561756)"
known_call = addr(0x180561756)
ins = LISTING.getInstructionAt(known_call)
print "instruction at %s: %s" % (known_call, ins)
print "  references from this instruction:"
for r in ins.getReferencesFrom():
    print "    %s -> %s  type=%s" % (r.getFromAddress(), r.getToAddress(), r.getReferenceType())
fn = function_at_call_site(known_call)
if fn is None:
    print "  (no direct function reference; this is an indirect vtable call so we expect this)"

# Pcode-level analysis to follow the indirect target requires decompiler. Try:
from ghidra.app.decompiler import DecompInterface, DecompileOptions
from ghidra.util.task import ConsoleTaskMonitor

containing = getFunctionContaining(known_call)
print "  containing function: %s @ %s" % (containing.getName(), containing.getEntryPoint())

di = DecompInterface()
di.setOptions(DecompileOptions())
di.openProgram(currentProgram)
res = di.decompileFunction(containing, 60, ConsoleTaskMonitor())
if res.decompileCompleted():
    hf = res.getHighFunction()
    # Walk the PcodeOps and look for CALLIND at the address near 0x180561756
    op_iter = hf.getPcodeOps()
    while op_iter.hasNext():
        op = op_iter.next()
        if op.getMnemonic() != "CALLIND":
            continue
        seq_addr = op.getSeqnum().getTarget()
        if abs(seq_addr.getOffset() - 0x180561756) < 16:
            print "  CALLIND at %s, %d inputs" % (seq_addr, op.getNumInputs())
            for i in range(op.getNumInputs()):
                v = op.getInput(i)
                print "    input[%d]: %s" % (i, v)
            break
else:
    print "  decompile failed"

# -- Part 2: brute force search for tiny getters -----------------------------
print "\n--- Part 2: scan for small bool/int getters"
# Criteria:
#   - function body <= 24 bytes
#   - first/early ret (single C3)
#   - reads a byte/dword through [rcx + const] or returns small constant /
#     compares and returns 0/1
candidates = []
fit = FUNCMAN.getFunctions(True)
total = 0
small = 0
while fit.hasNext():
    f = fit.next()
    total += 1
    body_size = f.getBody().getNumAddresses()
    if body_size > 24 or body_size < 3:
        continue
    bts = get_func_bytes(f)
    if bts is None or len(bts) < 3:
        continue
    if 0xC3 not in bts:
        continue
    small += 1
    # Inspect mnemonics
    insns = []
    cur = LISTING.getInstructionAt(f.getEntryPoint())
    while cur is not None and f.getBody().contains(cur.getAddress()):
        insns.append(cur)
        cur = LISTING.getInstructionAfter(cur.getAddress())
        if len(insns) > 12:
            break

    if not insns:
        continue
    last = insns[-1]
    if last.getMnemonicString().lower() != "ret":
        continue
    # Heuristic: at least one MOV that writes to eax/al/rax
    writes_eax = False
    reads_rcx_off = False
    for ix in insns:
        s = ix.toString().lower()
        if ix.getMnemonicString().lower() in ("mov", "movzx", "xor", "test", "cmp", "setne", "sete", "movsxd"):
            if "eax" in s or " al" in s or "rax" in s:
                writes_eax = True
            if "[rcx" in s or "[rdx" in s:
                reads_rcx_off = True
    if not writes_eax:
        continue
    candidates.append((f, insns, bts, reads_rcx_off))

print "scanned %d funcs, %d tiny" % (total, small)
print "candidates: %d" % len(candidates)

# Sort: prefer those that read [rcx+off] (real getters) and that have names
def rank(c):
    f, insns, bts, reads = c
    name = f.getName()
    score = 0
    if reads:
        score -= 10  # prefer
    if not name.startswith("FUN_"):
        score -= 5
    score += len(insns)  # smaller is better -> lower score
    return score

candidates.sort(key=rank)

print "\nTop 40 candidates:"
for i, (f, insns, bts, reads) in enumerate(candidates[:40]):
    print "  [%d] %s @ %s   size=%d insns=%d reads_arg=%s" % (
        i, f.getName(), f.getEntryPoint(), f.getBody().getNumAddresses(), len(insns), reads)
    for ix in insns:
        b = ix.getBytes()
        print "       %s  %-20s %s" % (ix.getAddress(), hex_bytes(b), ix.toString())
    print "       bytes: %s" % hex_bytes(bts)

print "\ndone."
