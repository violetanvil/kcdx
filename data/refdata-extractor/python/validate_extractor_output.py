"""validate_extractor_output.py -- THE GATE: the output-validation harness for the
WHOLE reference-data extractor (parallel-ghidra-research.md §4/§9; the agreed
substitute for a test-plugins/cap-NN row, since headless tooling has no in-game
surface).

WHAT IT DOES
------------
1. Runs the FULL extractor over a bounded fixture (the first 256 functions):
     - the Java launcher (produce-reference-data.ps1 -Limit 256) for
       functions/ + statements/ + referenced_vars/ + call_edges/, then
     - produce_signatures.py + produce_caller_reg_args.py (-Limit 256) for
       signatures/ + caller_reg_args/
   all into a scratch out dir.
2. Asserts 25 checks against INDEPENDENT known answers. Each check reads an
   answer the extractor did NOT produce (the enumeration CSV from a DIFFERENT
   tool; an independent BLAKE3 recompute via the vetted Blake3Hex oracle; the
   binary's own bytes via pefile+capstone) and has a NAMEABLE extractor-broken
   state (AP15). No tautologies.
3. Prints per-check PASS/FAIL, an overall VERDICT, and sys.exit(1) on any FAIL
   (mirrors Blake3SelfTest's shape).

The AP15 record + each check's anchor + broken-state are in
VALIDATE-EXTRACTOR-README.md.

RUN
---
    python validate_extractor_output.py
(no args; paths are resolved relative to the repo layout. Pass a -ProjectDir
override env if the Ghidra project moves.)
"""

import csv
import os
import shutil
import subprocess
import sys
import tempfile

import pefile

# ----------------------------------------------------------------------------
# Paths (resolved from this file's location: data/refdata-extractor/python/).
# ----------------------------------------------------------------------------
HERE = os.path.dirname(os.path.abspath(__file__))
EXTRACTOR_ROOT = os.path.dirname(HERE)                 # data/refdata-extractor
GHIDRA_DIR = os.path.join(EXTRACTOR_ROOT, "ghidra")
BLAKE3_ROOT = os.path.join(GHIDRA_DIR, "blake3")
LAUNCHER = os.path.join(GHIDRA_DIR, "produce-reference-data.ps1")
REPO_ROOT = os.path.dirname(os.path.dirname(EXTRACTOR_ROOT))  # kcdx repo root

DLL = (r"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2"
       r"\Bin\Win64MasterMasterSteamPGO\WHGame.dll")
ENUM_CSV = os.path.join(REPO_ROOT, "_research", "parallel-ghidra-research",
                        "inventory", "WHGame.dll.functions.csv")
GHIDRA_PROJECT_DIR = os.path.join(REPO_ROOT, "third-party-ghidra", "ghidra_project")
GHIDRA_PROJECT_NAME = "KCD2"

FIXTURE_LIMIT = 256

# Anchor functions (verified independently against the enumeration CSV).
ANCHOR_1020 = 0x1020
ANCHOR_1050 = 0x1050
ANCHOR_11D0 = 0x11d0

# ----------------------------------------------------------------------------
# Check accounting.
# ----------------------------------------------------------------------------
_results = []


def check(name, ok, detail=""):
    _results.append((name, bool(ok), detail))
    status = "PASS" if ok else "FAIL"
    line = "  [%s] %s" % (status, name)
    if detail:
        line += "  -- " + detail
    print(line, flush=True)


# ----------------------------------------------------------------------------
# CSV shard readers.
# ----------------------------------------------------------------------------
def read_table(out_dir, table):
    """Read all rows across a sharded table dir as list[dict]."""
    d = os.path.join(out_dir, table)
    rows = []
    if not os.path.isdir(d):
        return rows
    for fn in sorted(os.listdir(d)):
        if not fn.endswith(".csv"):
            continue
        with open(os.path.join(d, fn), "r", encoding="utf-8", newline="") as fh:
            rows.extend(csv.DictReader(fh))
    return rows


def norm_rva(s):
    try:
        return int(s, 16)
    except (TypeError, ValueError):
        return None


# ----------------------------------------------------------------------------
# Independent BLAKE3 oracle (the vetted Blake3Hex.java, compiled to scratch).
# ----------------------------------------------------------------------------
def compile_blake3hex(scratch):
    """Compile Blake3.java + Blake3Hex.java to a scratch classpath. Returns the
    classpath root, or raises."""
    out = os.path.join(scratch, "blake3-classes")
    os.makedirs(out, exist_ok=True)
    blake3_src = os.path.join(BLAKE3_ROOT, "org", "apache", "commons", "codec",
                              "digest", "Blake3.java")
    hex_src = os.path.join(BLAKE3_ROOT, "Blake3Hex.java")
    subprocess.run(["javac", "-d", out, blake3_src, hex_src],
                   check=True, capture_output=True, text=True)
    return out


def blake3_hex(classpath, data):
    """Pipe `data` bytes to the Blake3Hex filter; return the 64-char hex line."""
    p = subprocess.run(["java", "-cp", classpath, "Blake3Hex"],
                       input=data, capture_output=True)
    if p.returncode != 0:
        raise RuntimeError("Blake3Hex failed: " + p.stderr.decode("utf-8", "replace"))
    return p.stdout.decode("ascii").strip()


# ----------------------------------------------------------------------------
# pefile reader: on-disk bytes of [rva, rva+length).
# ----------------------------------------------------------------------------
class OnDisk:
    def __init__(self, dll):
        self.pe = pefile.PE(dll, fast_load=True)

    def bytes_at(self, rva, length):
        return self.pe.get_data(rva, length)


# ----------------------------------------------------------------------------
# Run the extractor over the fixture.
# ----------------------------------------------------------------------------
def run_extractor(out_dir):
    print("==> running Java extractor (limit %d)..." % FIXTURE_LIMIT, flush=True)
    cmd = ["pwsh", "-NoProfile", "-File", LAUNCHER,
           "-ProjectDir", GHIDRA_PROJECT_DIR,
           "-ProjectName", GHIDRA_PROJECT_NAME,
           "-OutDir", out_dir,
           "-Module", "WHGame.dll",
           "-VersionTag", "release_1_5_1164953_841",
           "-Limit", str(FIXTURE_LIMIT)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    sys.stdout.write(r.stdout[-2000:] if r.stdout else "")
    if r.returncode != 0:
        sys.stderr.write(r.stderr[-3000:] if r.stderr else "")
        raise RuntimeError("Java extractor failed (exit %d)" % r.returncode)

    print("==> running produce_signatures.py...", flush=True)
    subprocess.run([sys.executable, os.path.join(HERE, "produce_signatures.py"),
                    DLL, ENUM_CSV, out_dir, str(FIXTURE_LIMIT)],
                   check=True, capture_output=True, text=True)

    print("==> running produce_caller_reg_args.py...", flush=True)
    subprocess.run([sys.executable, os.path.join(HERE, "produce_caller_reg_args.py"),
                    DLL, ENUM_CSV, out_dir, str(FIXTURE_LIMIT)],
                   check=True, capture_output=True, text=True)


# ----------------------------------------------------------------------------
# The checks.
# ----------------------------------------------------------------------------
def run_checks(out_dir, classpath):
    functions = read_table(out_dir, "functions")
    statements = read_table(out_dir, "statements")
    call_edges = read_table(out_dir, "call_edges")
    referenced_vars = read_table(out_dir, "referenced_vars")
    signatures = read_table(out_dir, "signatures")
    caller_reg_args = read_table(out_dir, "caller_reg_args")

    fn_by_rva = {norm_rva(r["rva"]): r for r in functions}

    # Independent enumeration CSV (a DIFFERENT tool, the harness anchor).
    enum = {}
    with open(ENUM_CSV, "r", encoding="utf-8", newline="") as fh:
        for r in csv.DictReader(fh):
            rv = norm_rva(r["rva"])
            if rv is not None:
                enum[rv] = r

    ondisk = OnDisk(DLL)

    # --- A. functions/ rva+auto_name+length vs the enumeration CSV (6 checks) ---
    for rva in (ANCHOR_1020, ANCHOR_1050, ANCHOR_11D0):
        frow = fn_by_rva.get(rva)
        erow = enum.get(rva)
        # check 1/3/5: anchor present in functions/ with auto_name matching enum.
        ok_name = (frow is not None and erow is not None
                   and frow["auto_name"] == erow["auto_name"])
        check("functions/ 0x%x auto_name matches enum CSV (%s)"
              % (rva, erow["auto_name"] if erow else "?"),
              ok_name,
              "got %r" % (frow["auto_name"] if frow else None))
        # check 2/4/6: length matches enum size_bytes.
        ok_len = (frow is not None and erow is not None
                  and frow["length"] == erow["size_bytes"])
        check("functions/ 0x%x length matches enum size_bytes (%s)"
              % (rva, erow["size_bytes"] if erow else "?"),
              ok_len,
              "got %r" % (frow["length"] if frow else None))

    # --- B. content_hash vs an INDEPENDENT BLAKE3 recompute (6 checks) ---
    for rva in (ANCHOR_1020, ANCHOR_1050, ANCHOR_11D0):
        frow = fn_by_rva.get(rva)
        ch = frow["content_hash"] if frow else ""
        # well-formedness: 64 lowercase hex chars.
        wf = (len(ch) == 64 and all(c in "0123456789abcdef" for c in ch))
        check("functions/ 0x%x content_hash is 64-char lowercase hex" % rva, wf,
              "len=%d" % len(ch))
        # independent recompute over the on-disk [rva, rva+length) bytes.
        recomputed = None
        if frow:
            length = int(frow["length"])
            data = ondisk.bytes_at(rva, length)
            recomputed = blake3_hex(classpath, data)
        check("functions/ 0x%x content_hash == independent Blake3Hex recompute"
              % rva, recomputed is not None and ch == recomputed,
              "extractor=%s oracle=%s" % (ch[:16], (recomputed or "")[:16]))

    # --- C. statements/: 0x11d0 has a stmt at byte_range_start 0x1243, callee 0x1050 ---
    s_11d0 = [r for r in statements if norm_rva(r["function_rva"]) == ANCHOR_11D0]
    stmt_call = [r for r in s_11d0
                 if norm_rva(r["byte_range_start"]) == 0x1243
                 and norm_rva(r["callee_rva"]) == ANCHOR_1050]
    check("statements/ 0x11d0 has a statement @ byte_range_start 0x1243 "
          "with callee_rva 0x1050", len(stmt_call) >= 1,
          "matches=%d (of %d stmts for 0x11d0)" % (len(stmt_call), len(s_11d0)))

    # --- D. call_edges/: resolved direct (0x11d0,0x1050,0x1243) + >=1 indirect ---
    direct = [r for r in call_edges
              if norm_rva(r["caller_rva"]) == ANCHOR_11D0
              and norm_rva(r["callee_rva"]) == ANCHOR_1050
              and norm_rva(r["callsite_rva"]) == 0x1243]
    check("call_edges/ contains resolved direct edge (0x11d0 -> 0x1050 @ 0x1243)",
          len(direct) >= 1, "matches=%d" % len(direct))
    indirect = [r for r in call_edges
                if r.get("edge_reason") == "indirect" and r.get("callee_rva", "") == ""]
    check("call_edges/ contains >=1 indirect edge (edge_reason=indirect, "
          "empty callee_rva)", len(indirect) >= 1, "indirect rows=%d" % len(indirect))

    # --- E. caller_reg_args/: 0x1050 -> 3, AND no row > 4 (7 checks total here) ---
    cra_by_rva = {norm_rva(r["rva"]): r for r in caller_reg_args}
    cra_1050 = cra_by_rva.get(ANCHOR_1050)
    check("caller_reg_args/ 0x1050 caller_reg_arg_count == 3 (rcx/rdx/r8)",
          cra_1050 is not None and cra_1050["caller_reg_arg_count"] == "3",
          "got %r" % (cra_1050["caller_reg_arg_count"] if cra_1050 else None))
    over4 = [r for r in caller_reg_args
             if r["caller_reg_arg_count"] not in ("", None)
             and int(r["caller_reg_arg_count"]) > 4]
    check("caller_reg_args/ NO row has caller_reg_arg_count > 4 "
          "(guards the dropped stack-side regressing back in)",
          len(over4) == 0, "violations=%d" % len(over4))

    # --- F. referenced_vars/: 0x1050 register row naming RCX arg1; storage_kind set ---
    rv_1050 = [r for r in referenced_vars
               if norm_rva(r["function_rva"]) == ANCHOR_1050]
    reg_rcx = [r for r in rv_1050
               if r["storage_kind"] == "register"
               and r["storage_detail"].upper() == "RCX"]
    check("referenced_vars/ 0x1050 has >=1 register row naming reg RCX (arg1)",
          len(reg_rcx) >= 1,
          "rcx register rows=%d (of %d vars)" % (len(reg_rcx), len(rv_1050)))
    valid_kinds = {"register", "stack", "memory", "unique", "const", "other"}
    bad_kind = [r for r in rv_1050 if r["storage_kind"] not in valid_kinds]
    check("referenced_vars/ every 0x1050 row storage_kind in "
          "{register,stack,memory,unique,const,other}",
          len(bad_kind) == 0, "bad=%d" % len(bad_kind))
    X86_REGS = {
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
        "eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp",
        "r8d", "r9d", "r10d", "r11d", "r12d", "r13d", "r14d", "r15d",
        "ax", "bx", "cx", "dx", "si", "di", "bp", "sp",
        "al", "bl", "cl", "dl", "ah", "bh", "ch", "dh",
        "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
        "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",
    }
    bad_reg = []
    for r in rv_1050:
        if r["storage_kind"] != "register":
            continue
        nm = r["storage_detail"].lower()
        sz = r.get("size_bytes", "0")
        if nm not in X86_REGS or not (sz.isdigit() and int(sz) > 0):
            bad_reg.append(r)
    check("referenced_vars/ every 0x1050 register row names a real x86-64 reg "
          "with positive size_bytes", len(bad_reg) == 0,
          "bad register rows=%d" % len(bad_reg))

    # --- G. NO callee_rva is a >10-hex-digit underflow (statements/ + call_edges/) ---
    def has_underflow(rows, col):
        for r in rows:
            v = r.get(col, "")
            if v and v.startswith("0x") and len(v) - 2 > 10:
                return v
        return None
    uf_stmt = has_underflow(statements, "callee_rva")
    check("statements/ NO callee_rva is a >10-hex-digit underflow value "
          "(inModuleImage external-underflow guard)",
          uf_stmt is None, "found %s" % uf_stmt)
    uf_edge = has_underflow(call_edges, "callee_rva")
    check("call_edges/ NO callee_rva is a >10-hex-digit underflow value "
          "(inModuleImage external-underflow guard)",
          uf_edge is None, "found %s" % uf_edge)

    # --- H. AP14 accounting (4 checks) ---
    # every emitted functions/ rva exists in the enum CSV.
    missing = [r["rva"] for r in functions if norm_rva(r["rva"]) not in enum]
    check("AP14: every functions/ rva exists in the enum CSV",
          len(missing) == 0, "missing=%d" % len(missing))
    check("AP14: functions/ row count == 256", len(functions) == FIXTURE_LIMIT,
          "got %d" % len(functions))
    check("AP14: signatures/ row count == 256", len(signatures) == FIXTURE_LIMIT,
          "got %d" % len(signatures))
    sig_rvas = {norm_rva(r["rva"]) for r in signatures}
    fn_rvas = {norm_rva(r["rva"]) for r in functions}
    check("AP14: signatures/ is 1:1 with functions/ by rva",
          sig_rvas == fn_rvas and len(sig_rvas) == len(functions),
          "sig=%d fn=%d sym_diff=%d"
          % (len(sig_rvas), len(fn_rvas), len(sig_rvas ^ fn_rvas)))


def main():
    if not os.path.isfile(LAUNCHER):
        sys.exit("launcher not found: " + LAUNCHER)
    if not os.path.isfile(ENUM_CSV):
        sys.exit("enumeration CSV not found: " + ENUM_CSV)
    if not os.path.isfile(DLL):
        sys.exit("WHGame.dll not found: " + DLL)

    scratch = tempfile.mkdtemp(prefix="kcdx-extractor-validate-")
    out_dir = os.path.join(scratch, "out")
    os.makedirs(out_dir, exist_ok=True)
    try:
        classpath = compile_blake3hex(scratch)
        run_extractor(out_dir)
        print("\n" + "=" * 70, flush=True)
        print("CHECKS", flush=True)
        print("=" * 70, flush=True)
        run_checks(out_dir, classpath)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
        pycache = os.path.join(HERE, "__pycache__")
        shutil.rmtree(pycache, ignore_errors=True)

    passed = sum(1 for _, ok, _ in _results if ok)
    total = len(_results)
    print("-" * 70, flush=True)
    print("VERDICT: %d/%d checks PASS" % (passed, total), flush=True)
    if passed == total:
        print("RESULT: PASS -- extractor output matches all independent anchors.",
              flush=True)
        sys.exit(0)
    else:
        for name, ok, detail in _results:
            if not ok:
                print("  FAILED: %s -- %s" % (name, detail), flush=True)
        print("RESULT: FAIL -- %d/%d." % (passed, total), flush=True)
        sys.exit(1)


if __name__ == "__main__":
    main()
