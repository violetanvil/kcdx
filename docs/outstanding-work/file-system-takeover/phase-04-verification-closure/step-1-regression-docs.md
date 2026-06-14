# Step 4.1 — regression plugin(s) + matrix rows + subsystem doc

**What.** Ship the permanent regression coverage for the takeover and the
file-system subsystem reference doc. The regression rows exercise the three
load-bearing user-facing outcomes (design §1 success criteria, §7 contract): (a) a
vanilla-pak asset read by kcdx's own reader serves correctly, (b) a loose override
wins over the vanilla asset, (c) a stock Nexus/Workshop pak mod loads unchanged.
The subsystem doc describes the new file-system unit (its responsibility, the
vtable-swap seating, the unified index, the pak reader, the per-slot table) per the
docs-mirror-code rule.

**Scope.** A permanent `test-plugins/<cap-NN>-fs-takeover/` plugin (or sub-tests in
an existing asset plugin) with the three falsifiable matrix rows; the file-system
subsystem reference doc under the repo's reference-docs tree
(`.claude/rules/structure-by-responsibility.md` §6). One commit. This verifies the
author-facing contract is unchanged (E24) — the rows ARE the contract check.

**Test bar.** Each matrix row is falsifiable: (a) FAILS if the vanilla asset does
not render / loads wrong bytes; (b) FAILS if the vanilla asset serves instead of
the override; (c) FAILS if the stock pak mod's asset does not win where its
load-order says. A launch confirms `suite: X/Y passing` with all three PASS
(agent-read, `kcdx-dev.log`). The subsystem doc exists + mirrors the built units
(`.claude/rules/docs-discipline.md`).

**Dependencies.** Phase 3 (the full takeover is live — the slots, index, and
reader all exist to exercise). The author-facing contract (E24) is verified here
because the rows exercise it end-to-end.

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §1 (success criteria),
§7 (the author-facing contract); `.claude/rules/test-suite.md`,
`.claude/rules/structure-by-responsibility.md`, `.claude/rules/docs-discipline.md`.

**Disassembler-test / author-burden.** The regression plugin is authored on the
existing author-facing surface (the `assets/` folder + sidecars, unchanged) — no
new hex/ABI burden; the contract is the same one asset-replacement already
established (design §7).
