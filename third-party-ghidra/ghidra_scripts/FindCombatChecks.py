# Locate xrefs to KCD2 combat-restriction localization keys and dump surrounding code.
# Run via: analyzeHeadless <proj_dir> <proj_name> -import WHGame.dll -postScript FindCombatChecks.py
#@category KCD2

from ghidra.program.model.symbol import RefType
from ghidra.program.model.address import AddressSet

TARGETS = ["cant_change_outfit_in_combat", "cant_use_in_combat"]

def find_string_addrs(name):
    """Return list of addresses where the C string `name` (with terminating NUL) appears in memory."""
    mem = currentProgram.getMemory()
    needle = bytearray(name + "\x00", "ascii")
    results = []
    for block in mem.getBlocks():
        if not block.isInitialized():
            continue
        try:
            addr = mem.findBytes(block.getStart(), block.getEnd(), needle, None, True, monitor)
        except:
            addr = None
        while addr is not None:
            results.append(addr)
            next_start = addr.add(1)
            if next_start.compareTo(block.getEnd()) > 0:
                break
            try:
                addr = mem.findBytes(next_start, block.getEnd(), needle, None, True, monitor)
            except:
                addr = None
    return results

def dump_function_context(xref_addr):
    listing = currentProgram.getListing()
    func = getFunctionContaining(xref_addr)
    if func is None:
        print "  [no enclosing function]"
        return
    print "  func: %s  @%s  size=%d" % (func.getName(), func.getEntryPoint(), func.getBody().getNumAddresses())
    # Dump 8 instructions before and 12 after the xref.
    insns_before = []
    cur = listing.getInstructionBefore(xref_addr)
    for _ in range(8):
        if cur is None:
            break
        insns_before.append(cur)
        cur = listing.getInstructionBefore(cur.getAddress())
    insns_before.reverse()
    cur = listing.getInstructionAt(xref_addr)
    insns_at = []
    for _ in range(12):
        if cur is None:
            break
        insns_at.append(cur)
        cur = listing.getInstructionAfter(cur.getAddress())
    for ins in insns_before + insns_at:
        b = ins.getBytes()
        hexb = " ".join(["%02X" % (x & 0xFF) for x in b])
        marker = " <-- XREF" if ins.getAddress().equals(xref_addr) else ""
        print "    %s  %-30s  %s%s" % (ins.getAddress(), hexb, ins.toString(), marker)
    print

print "=" * 80
print "KCD2 combat-restriction xref hunt"
print "=" * 80

for target in TARGETS:
    print "\n### String: %r" % target
    str_addrs = find_string_addrs(target)
    if not str_addrs:
        print "  (string not found in memory)"
        continue
    for sa in str_addrs:
        print "  string at %s" % sa
        refs = list(getReferencesTo(sa))
        if not refs:
            # Try the address as a pointer target via direct byte search.
            print "    (no xrefs from listing; trying raw 64-bit address search)"
            target_va = sa.getOffset()
            mem = currentProgram.getMemory()
            le_bytes = bytearray()
            for i in range(8):
                le_bytes.append((target_va >> (8 * i)) & 0xFF)
            for block in mem.getBlocks():
                if not block.isInitialized():
                    continue
                try:
                    found = mem.findBytes(block.getStart(), block.getEnd(), le_bytes, None, True, monitor)
                except:
                    found = None
                while found is not None:
                    print "      raw match @%s (block %s)" % (found, block.getName())
                    next_start = found.add(1)
                    if next_start.compareTo(block.getEnd()) > 0:
                        break
                    try:
                        found = mem.findBytes(next_start, block.getEnd(), le_bytes, None, True, monitor)
                    except:
                        found = None
            continue
        for ref in refs:
            from_addr = ref.getFromAddress()
            print "    xref from %s  type=%s" % (from_addr, ref.getReferenceType())
            dump_function_context(from_addr)

print "done."
