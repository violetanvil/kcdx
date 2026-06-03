---
name: research-disassembly
description: Use this skill to verify a game-function fact about WHGame.dll from the binary — an address/RVA, ABI signature, return type, argument count, vtable slot index, or calling convention. Runs the reuse-first evidence ladder (existing seed prose → prior _research/ dumps → predecessor sigs → analyzed Ghidra project → fresh disassembly LAST), enforces AP1/AP2/AP3, lands findings as verified provenance. NOT for a runtime bug (crash/hang/wrong-output) — that's /debug. NOT for editing the seed CSVs under data/seeds/ — that's the working flow once the fact is verified.
---

# Research disassembly — verify a game-function fact, reuse-first

For any "what is this WHGame.dll function's X?" question where X is a static fact of the binary: address, signature/ABI, return type, arg count, vtable slot, calling convention. The deliverable is a **verified fact + its evidence**, ready to become Address Library provenance.

A runtime question (does it crash? what's on the stack at fault?) is `/debug`, not this skill.

Fresh disassembly is the LAST tier, not the first.

---

## 1. The reuse-first evidence ladder — walk IN ORDER, stop at the first tier that answers

A task prompt saying "read the decompilation" or "use Ghidra" named the *last* tier. Run the ladder from the top regardless. State which tier you're at and what you found before descending.

1. **Existing Address Library row.** Read `data/seeds/address_names_seed.csv` (id, name, notes) and `data/seeds/address_versions_seed.csv` (rva, signature, verification audit) for the function (by id, name, or RVA) — these are the reference-DB source. The `notes` prose and `signature` field may already carry the fact (address, ABI, return). If the fact you need is already there and verified → cite it, done. (Per `address-library.md`.)
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

## 3.5 Every claim is read in the body that OWNS it — call-edges + cross-front synthesis (AP2, AP19)

A static fact about ONE function is read in that function (§3). The same bar binds two facts the single-function framing missed — the seam where an inference ships as ground truth:

- **A call-edge claim ("A calls B") is grounded by reading A's body for the call to B** — never inferred. "B is reached by other consumers," "B is a by-name slot the pattern says everyone calls," "A is the kind of function that would call B" are NOT evidence A calls B. Read A's decompiled body; the call to B is there (cite the call site — `A at 0x… → call 0x<B>`) or the edge does not exist. A call-graph arrow you did not read in the caller's body is an AP19 inference, not a fact.
- **The DEFAULT for an unread edge is "no edge," not "probably an edge."** Honest-uncertainty (§4) applies to edges: if A's body is not read this turn, the answer is "A→B unverified — A's body not read," never an asserted arrow.
- **Synthesis re-grounds; it does not assemble.** When findings come from multiple fronts/sources, the synthesizer treats each front's output as a CLAIM to re-verify, not a fact to stitch into a narrative. Every LOAD-BEARING claim the synthesis rests on — especially any cross-front call-edge — is read in the owning body BEFORE it ships. A synthesis whose elegance ("one universal chokepoint") outruns its reads is the failure this section exists to stop: the clean story is a hypothesis, the bodies are the fact.

> **Self-check before asserting any "A calls B" / "everything routes through X":** *Did I read A's body (X's callers' bodies) and SEE the call this turn?* If no, it is an inference — mark it unverified or read the body. A runtime "does the path actually reach X?" question is a `/debug` probe, not a static edge claim (the intro's runtime-vs-static line) — the static fact is whether the call exists in the body; whether execution reaches it at runtime is `/debug`'s.

---

## 4. Honest-uncertainty is a valid answer — never invent (AP2)

If a fact is genuinely ambiguous from the binary, the answer is **"ambiguous — leave unverified,"** not a best guess. An empty `signature`/return field is correct; an invented one is an AP2 violation that ships a wrong ABI. The Address Library only carries facts structured from verified evidence. Say what you could and couldn't determine, and why.

---

## 4.5 Fanning out parallel fronts — the synthesizer re-grounds, a gate confirms before authority

A big target gets split across parallel disassembly fronts (one per vtable region / subsystem / call cluster), then a synthesizer assembles the answer. Govern BOTH halves — the fronts and the synthesizer are where an inference enters:

- **Each front returns CLAIMS + evidence, never assembled conclusions.** A front's deliverable is `<claim> — <evidence>`: the body snippet, the dump line, the call site read (`0x… → call 0x…`), the slot decompile. NOT "X is the chokepoint" / "everything routes through Y" — those are conclusions the front did not earn from its own slice. A front states only what it read in the bodies it was given.
- **The synthesizer's job is to RE-GROUND, not to stitch.** It does NOT weave front outputs into the cleanest narrative; it takes each LOAD-BEARING claim the answer rests on — every cross-front call-edge above all (§3.5) — and reads the owning body to confirm it before the synthesis asserts it. An edge that spans two fronts (front-1 says "A is a by-name slot," front-3 says "B resolves paths" → "∴ A calls B") is the exact unread inference AP19 forbids: the synthesizer reads A's body or marks the edge unverified.
- **A claim that will become DESIGN AUTHORITY or a seed row is GATED before it ships** (`_shared/verification-contract.md`). A self-asserted disassembly finding is the AP8 "self-reported gate" shape — "I read the body, trust me" is not evidence. Before a load-bearing claim becomes the basis for a design decision or a `data/seeds/` row, dispatch a gated body-read verifier: a fresh subagent (per the verification-contract dispatch discipline — `Agent` tool, WITHHELD = the synthesizer's leaning, independence citation, gated verdict) re-reads the owning body for that specific claim and returns PROCEED (the call/fact is in the body, cite the site) or HALT (not in the body / contradicts it / unread). A HALT blocks the claim from shipping as authority — it returns to "unverified" or to a fresh read, never ships on the synthesizer's word. This is the disassembly counterpart to `step-review` (gates a commit) and `root-cause-verifier` (gates a Resolution); a load-bearing fact has the same independent-read gate.

A single-fact, single-front lookup (the common case) does not fan out and needs no synthesizer gate — its §3/§3.5 body-read IS the grounding. The gate fires for the multi-front synthesis whose claim becomes authority — the case that shipped an unread edge.

---

## 5. Deliverable

For each fact requested:

- **The fact** — stated as the kcdx signature-DSL primitive where applicable (`void` / `bool` / `i32` / `ptr` / a full `ret (args)` shape per `hook_signature.h`), or "ambiguous — leave unverified."
- **The tier it came from** — "already in seed row 2003 prose," "captured in `_research/phase7-recon/_abi_<addr>.txt`," or "fresh Ghidra, script `<name>.java`."
- **The evidence** — the decompiled return-path snippet / the abi_walker output / whether `eax` is set / whether callers consume the return. Enough that it can go into the seed row's notes as verified provenance (prose convention: a `-> <type>` arrow, like row 2003 `GetCVar ... -> ICVar*`).
- **A new `_research/` artifact** if you ran a fresh disassembly — drop the script + its raw output into one `<task-slug>-recon/` dir per the `_research/` layout in `reverse-engineering.md` (one investigation = one dir; `FINDINGS.md` + `<verb>_<noun>.py` scripts + `_abi_<addr>.txt` dumps), so the NEXT agent finds it at ladder tier 2 instead of re-disassembling.

## 6. Handoff — verifying ≠ recording

This skill VERIFIES the fact and produces the provenance. Writing it into the `data/seeds/` files (entity row in `address_names_seed.csv`, per-version fact in `address_versions_seed.csv`) is a separate edit — the working flow (`/execute`, or the caller that requested the research). The reference DB regenerates from the seeds; there is no in-source table to also edit. State the exact prose + field value ready to paste; let the recording happen as its own reviewed change. (Append-only IDs — `address-library.md`.)

## 7. Close — milestone-commit the `_research/` artifact

If a fresh disassembly produced a new `<task-slug>-recon/` dir (per §5), that's a durable artifact regardless of whether the recording into the seeds has happened yet — the next agent finds it at ladder tier 2 instead of re-disassembling. Invoke `/commit` on the `_research/` files this turn produced before stopping, per CLAUDE.md "Commit at coherent milestones." Verified-fact-only turns (no new `_research/` files written — the fact came from existing prose or a prior dump) commit nothing from this skill; the recording commit is the caller's.
