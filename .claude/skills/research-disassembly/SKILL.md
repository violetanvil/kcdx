---
name: research-disassembly
description: Use this skill to verify a game-function fact about WHGame.dll from the binary — an address/RVA, ABI signature, return type, argument count, vtable slot index, or calling convention. Runs the reuse-first evidence ladder (existing seed prose → prior _research/ dumps → predecessor sigs → analyzed Ghidra project → fresh disassembly LAST), enforces AP1/AP2/AP3, lands findings as verified provenance. NOT for a runtime bug (crash/hang/wrong-output) — that's /debug. NOT for editing seed.csv/kEntries[] — that's the working flow once the fact is verified.
---

# Research disassembly — verify a game-function fact, reuse-first

For any "what is this WHGame.dll function's X?" question where X is a static fact of the binary: address, signature/ABI, return type, arg count, vtable slot, calling convention. The deliverable is a **verified fact + its evidence**, ready to become Address Library provenance.

A runtime question (does it crash? what's on the stack at fault?) is `/debug`, not this skill.

Fresh disassembly is the LAST tier, not the first.

---

## 1. The reuse-first evidence ladder — walk IN ORDER, stop at the first tier that answers

A task prompt saying "read the decompilation" or "use Ghidra" named the *last* tier. Run the ladder from the top regardless. State which tier you're at and what you found before descending.

1. **Existing Address Library row.** Read `data/address-library/seed.csv` and `src/address_library.cpp::kEntries[]` for the function (by id, name, or RVA). The `description`/notes prose and the `signature` field may already carry the fact (address, ABI, return). If the fact you need is already there and marked verified → cite it, done. (Per `address-library.md`.)
2. **Prior `_research/<phase>/` disassembly dumps.** Grep `_research/` for the function's address, name, or the symbols around it — `_research/phase6-save-load/`, `phase6b-recon/`, `phase7-recon/`, `phase8-fix-a/`, `cap03-recon/` hold `_abi_<address>.txt` dumps, `FINDINGS.md`/recon docs, and the worker scripts that produced them. The IConsole work (ids 2000–2005) lives in `_research/phase7-recon/DISPATCH-INVESTIGATION.md` + its `disasm_*.py`. If a prior dump already decompiled this function, the return path / arg layout is likely already captured — read it, don't re-run.
3. **Predecessor sigs.** `_research/predecessor-sigs/` — `yobson1/kcd2lua` (KCD2 1.5; `lua_pcall`, `update`, `luaL_loadfile`) and `muyuanjin/kcd2db` (gEnv resolver, 12+ verified vtable offsets). If they cover the sig, the work is done. (Per `reverse-engineering.md`.)
4. **Cached Warhorse wiki.** `_research/warhorse_wiki/` via the YouTrack REST API (never WebFetch the SPA URL). Rarely carries ABI, sometimes carries behavior.
5. **Fresh disassembly — LAST.** Only when tiers 1–4 don't contain the fact. Use the **pre-analyzed** Ghidra project (§2); never cold-analyze. For ABI/arg-count facts specifically, the abi_walker (§3) is the verified method, not prologue-shape reading.

If a higher tier *partially* answers (e.g. seed prose has args but not the return type — the exact cap-21/cap-22 IConsole case), you only descend for the *missing* piece, and you cite the tier that already had the rest.

---

## 2. Fresh Ghidra — only after the ladder (per `reverse-engineering.md`)

- Pre-analyzed project: `third-party-ghidra/ghidra_project/KCD2.gpr`; install at `third-party-ghidra/ghidra_12.1_PUBLIC/`; binary `third-party-ghidra/WHGame.dll`. **Never cold-analyze** (hours) — query the existing `.rep`.
- Ghidra 12.1 dropped Jython → write a **Java** post-script. Examples in `third-party-ghidra/ghidra_scripts/` (`FindIsInCombatSlot.java`, `DumpIsInCombatWrappers.java`, the `FindVtable*.java` family).
- Headless: `analyzeHeadless.bat "<project_dir>" KCD2 -process WHGame.dll -postScript <Script> -noanalysis -readOnly`.
- Navigate by `image base + RVA`. Read the decompiled body AND cross-check callers.
- The scripted `pefile + capstone` approach (`_research/phase7-recon/disasm_*.py`) is an equally valid fresh-disassembly path and is more reproducible — prior recon used it; reuse those scripts where they fit.
- **x64dbg is unreliable** against this build — static (Ghidra/capstone) is the verification path; x64dbg is fallback only.

---

## 3. ABI / signature facts — the verified method, never a guess (AP2)

- **Prologue-shape reading is forbidden as the basis for an ABI claim** (a wrong arg count silently corrupts state). Use `_research/phase6-save-load/phase6_abi_walker.py` (capstone body-wide stack-arg analyzer) on any new hook target.
- **Return type:** read the decompiled return path — does the function set `eax`/`rax` to a meaningful value before returning, or is it never set / ignored (→ `void`)? **Cross-check the callers**: does any caller test the return in `al`/`eax`? Caller-ignores-it corroborates `void`; caller-branches-on-it corroborates `bool`/`i32`/`ptr`.
- **Don't infer from the name.** A function called `RemoveCommand` is *likely* `void`, but read the binary — the name is a hypothesis, the bytes are the fact.
- **The binary wins over canonical headers (AP3).** KCD2's vtable order and ABI can differ from the canonical CryEngine `IConsole` interface (precedent: `AddCommand` is slot 33, not 32). Cross-reference a canonical header for a *lead*, never as the answer.

---

## 4. Honest-uncertainty is a valid answer — never invent (AP2)

If a fact is genuinely ambiguous from the binary, the answer is **"ambiguous — leave unverified,"** not a best guess. An empty `signature`/return field is correct; an invented one is an AP2 violation that ships a wrong ABI. The Address Library only carries facts structured from verified evidence. Say what you could and couldn't determine, and why.

---

## 5. Deliverable

For each fact requested:

- **The fact** — stated as the kcdx signature-DSL primitive where applicable (`void` / `bool` / `i32` / `ptr` / a full `ret (args)` shape per `hook_signature.h`), or "ambiguous — leave unverified."
- **The tier it came from** — "already in seed row 2003 prose," "captured in `_research/phase7-recon/_abi_<addr>.txt`," or "fresh Ghidra, script `<name>.java`."
- **The evidence** — the decompiled return-path snippet / the abi_walker output / whether `eax` is set / whether callers consume the return. Enough that it can go into the seed row's notes as verified provenance (prose convention: a `-> <type>` arrow, like row 2003 `GetCVar ... -> ICVar*`).
- **A new `_research/` artifact** if you ran a fresh disassembly — drop the script + its raw output under the relevant `_research/<phase>/` so the NEXT agent finds it at ladder tier 2 instead of re-disassembling.

## 6. Handoff — verifying ≠ recording

This skill VERIFIES the fact and produces the provenance. Writing it into `data/address-library/seed.csv` + `src/address_library.cpp::kEntries[]` is a code edit — the working flow (`/execute`, or the caller that requested the research). State the exact prose + field value ready to paste; let the recording happen as its own reviewed change. (Append-only IDs; edit both seed and the `kEntries[]` mirror — `address-library.md`.)
