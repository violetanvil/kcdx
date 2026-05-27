# Resolve vtable slot[1] called from FUN_180561700 at +0x56
# Run via: analyzeHeadless <proj_dir> KCD2 -process WHGame.dll -postScript FindIsInCombatSlot.py -noanalysis -readOnly
#@category KCD2

from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

TARGET_FUNC_RVA = 0x561700
CALL_SITE_RVA   = 0x561756  # the call qword ptr [rax+8] site
BASE = 0x180000000

def addr(rva):
    return currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(BASE + rva)

def hex_bytes(byte_array, count=None):
    if count is None:
        count = len(byte_array)
    return " ".join(["%02X" % (byte_array[i] & 0xFF) for i in range(min(count, len(byte_array)))])

print "=" * 80
print "Resolving slot[1] of vtable called at 0x180561756"
print "=" * 80

func_addr = addr(TARGET_FUNC_RVA)
func = getFunctionAt(func_addr)
if func is None:
    func = getFunctionContaining(func_addr)
if func is None:
    print "No function found at/containing %s -- creating one" % func_addr
    func = createFunction(func_addr, "FUN_180561700")

print "\nFunction: %s @ %s  size=%d" % (func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses())

# 1. Decompile the function and print the slot[1] line in context
print "\n--- Decompiler output ---"
di = DecompInterface()
di.openProgram(currentProgram)
res = di.decompileFunction(func, 60, ConsoleTaskMonitor())
if res and res.decompileCompleted():
    code = res.getDecompiledFunction().getC()
    # Print the lines around the call -- look for vtable-y patterns
    for i, line in enumerate(code.split("\n")):
        print "  %3d: %s" % (i, line)
else:
    print "  Decompilation failed: %s" % (res.getErrorMessage() if res else "no result")

# 2. Inspect the call site directly
print "\n--- Call site instruction inspection ---"
listing = currentProgram.getListing()
call_addr = addr(CALL_SITE_RVA)
call_insn = listing.getInstructionAt(call_addr)
print "  Instruction @%s : %s" % (call_addr, call_insn)
print "  Bytes: %s" % hex_bytes(call_insn.getBytes())

# Look for references from this call site (Ghidra may have resolved the vtable target)
refs_from = call_insn.getReferencesFrom()
print "  References from this call site:"
for r in refs_from:
    to_addr = r.getToAddress()
    print "    -> %s  type=%s primary=%s" % (to_addr, r.getReferenceType(), r.isPrimary())
    sym = currentProgram.getSymbolTable().getPrimarySymbol(to_addr)
    if sym is not None:
        print "       symbol: %s" % sym.getName()

# 3. Find the vtable -- search for `mov [r?? + 0xB60], reg` near `lea rax, [vftable]`
# More direct: search for the bytes `48 81 C1 60 0B 00 00 48 8B 01 FF 50 08` to see if Ghidra has
# resolved the operand of `mov rax, [rcx]` to a known address (likely not since rcx is runtime).
# Instead, look for the constructor: code that does `lea rax, vftable; mov [this+0xB60], rax`.
# Pattern: 48 8D 05 ?? ?? ?? ?? 48 89 ?? ?? 60 0B 00 00
#
# More productive: in C++, the object at +0xB60 is typically embedded. Its vtable is initialized
# in the OUTER class constructor. We search for any code that stores something at offset 0xB60.

print "\n--- Searching for constructor pattern (lea + mov [obj+0xB60]) ---"
mem = currentProgram.getMemory()

# Pattern for: lea rax, [rip+disp32]   (48 8D 05 ?? ?? ?? ??)
# Followed within ~16 bytes by:  mov [r?? + 0xB60], rax   pattern bytes: 48 89 ?? 60 0B 00 00 (3-byte ModR/M+disp32)
# Actual encoding for mov [reg+disp32], rax with rax as src:  48 89 83 60 0B 00 00 (rbx+0xB60)
#                                                              48 89 87 60 0B 00 00 (rdi+0xB60)
#                                                              48 89 86 60 0B 00 00 (rsi+0xB60)
# Search a generic 7-byte tail.
candidates = []
for variant in [0x83, 0x86, 0x87, 0x81, 0x82, 0x84, 0x85]:
    needle = bytearray([0x48, 0x89, variant, 0x60, 0x0B, 0x00, 0x00])
    for block in mem.getBlocks():
        if not block.isInitialized():
            continue
        if not block.isExecute():
            continue
        try:
            a = mem.findBytes(block.getStart(), block.getEnd(), needle, None, True, monitor)
        except:
            a = None
        while a is not None:
            candidates.append(a)
            try:
                a = mem.findBytes(a.add(1), block.getEnd(), needle, None, True, monitor)
            except:
                a = None

print "  Found %d candidate `mov [reg+0xB60], rax` sites" % len(candidates)
# For each, check if preceded within 16 bytes by a `lea rax, [rip+disp32]` (48 8D 05 d0 d1 d2 d3)
vtable_candidates = {}
for c in candidates:
    # Look back up to 16 bytes for `48 8D 05`
    for back in range(0, 17):
        try:
            probe = c.subtract(back)
        except:
            continue
        try:
            b0 = mem.getByte(probe) & 0xFF
            b1 = mem.getByte(probe.add(1)) & 0xFF
            b2 = mem.getByte(probe.add(2)) & 0xFF
        except:
            continue
        if b0 == 0x48 and b1 == 0x8D and b2 == 0x05:
            # disp32 follows
            try:
                d0 = mem.getByte(probe.add(3)) & 0xFF
                d1 = mem.getByte(probe.add(4)) & 0xFF
                d2 = mem.getByte(probe.add(5)) & 0xFF
                d3 = mem.getByte(probe.add(6)) & 0xFF
            except:
                continue
            disp = d0 | (d1 << 8) | (d2 << 16) | (d3 << 24)
            if disp & 0x80000000:
                disp -= 0x100000000
            # RIP after the lea instr = probe + 7
            rip_after = probe.add(7).getOffset()
            vtable_va = rip_after + disp
            vtable_candidates.setdefault(vtable_va, []).append(probe)
            break

print "  Distinct vtable VAs found: %d" % len(vtable_candidates)
for vt_va, sites in sorted(vtable_candidates.items()):
    print "    vtable @ 0x%X  (%d constructor sites)" % (vt_va, len(sites))

# 4. For each candidate vtable, dump slot[0] and slot[1]
print "\n--- Vtable contents ---"
for vt_va, sites in sorted(vtable_candidates.items()):
    vt_addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(vt_va)
    try:
        slot0 = mem.getLong(vt_addr) & 0xFFFFFFFFFFFFFFFF
        slot1 = mem.getLong(vt_addr.add(8)) & 0xFFFFFFFFFFFFFFFF
        slot2 = mem.getLong(vt_addr.add(16)) & 0xFFFFFFFFFFFFFFFF
        slot3 = mem.getLong(vt_addr.add(24)) & 0xFFFFFFFFFFFFFFFF
    except Exception as e:
        print "  vtable @ 0x%X UNREADABLE: %s" % (vt_va, e)
        continue
    print "  vtable @ 0x%X:" % vt_va
    print "    slot[0] = 0x%X" % slot0
    print "    slot[1] = 0x%X  <-- TARGET" % slot1
    print "    slot[2] = 0x%X" % slot2
    print "    slot[3] = 0x%X" % slot3
    # Check first constructor caller site
    for s in sites[:2]:
        f = getFunctionContaining(s)
        if f:
            print "    constructor func: %s @ %s" % (f.getName(), f.getEntryPoint())

# 5. For each plausible vtable, look at slot[1] function
print "\n--- Slot[1] function analysis ---"
for vt_va, sites in sorted(vtable_candidates.items()):
    vt_addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(vt_va)
    try:
        slot1 = mem.getLong(vt_addr.add(8)) & 0xFFFFFFFFFFFFFFFF
    except:
        continue
    if slot1 < BASE or slot1 > BASE + 0x10000000:
        continue
    slot1_addr = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(slot1)
    fn = getFunctionAt(slot1_addr)
    if fn is None:
        fn = getFunctionContaining(slot1_addr)
    print "\n  Slot[1] target: 0x%X" % slot1
    if fn:
        print "    name: %s  size=%d" % (fn.getName(), fn.getBody().getNumAddresses())
    # Dump first 32 bytes
    try:
        bytes_buf = bytearray()
        for i in range(32):
            bytes_buf.append(mem.getByte(slot1_addr.add(i)) & 0xFF)
        print "    first 32 bytes: %s" % hex_bytes(bytes_buf)
    except Exception as e:
        print "    cannot read bytes: %s" % e

    # Dump first ~12 instructions
    if fn:
        ins = listing.getInstructionAt(fn.getEntryPoint())
        for _ in range(12):
            if ins is None:
                break
            b = ins.getBytes()
            hexb = " ".join(["%02X" % (x & 0xFF) for x in b])
            print "      %s  %-25s  %s" % (ins.getAddress(), hexb, ins)
            ins = listing.getInstructionAfter(ins.getAddress())
            if ins is None:
                break
            # stop after ret
            prev_b = b
            if (prev_b[0] & 0xFF) == 0xC3:
                break

    # Count callers
    if fn:
        refs_to_fn = list(getReferencesTo(slot1_addr))
        print "    references to this function: %d" % len(refs_to_fn)
        # show a few
        for r in refs_to_fn[:6]:
            from_a = r.getFromAddress()
            caller = getFunctionContaining(from_a)
            print "      <- %s  in %s" % (from_a, caller.getName() if caller else "<no func>")

print "\ndone."
