// PROBE (Phase 1 / step 1.1 of maintainer-tool-fix-divergence-diff) — THROWAWAY.
// Captured to _research/maintainer-tool-fix-divergence-probe/, then removed from src/ (zero residue).
//
// Settles the two D45 `unverified, probe-before-building` claims against a DIVERGENT-DLL fixture:
//   E1 — does the existing runVerdictCheck return a defined, non-throwing verdict for EACH kind when
//        the DLL is the DIVERGENT build (recorded survival datum no longer matches the on-disk bytes)?
//   E2 — for a `function` divergent fixture, can the ONE row-level verdict be ATTRIBUTED to the
//        specific diverged field (signature vs rva), or does the worker need per-field derivation?
//
// OUTCOME→MEANING MAP (committed BEFORE running — results-driven §2):
//   E1, per kind:
//     A) returns a defined Changed/Ambiguous/CannotCheck, NO throw → the existing check is
//        divergent-DLL-safe for that kind; the worker can call it directly.
//     B) throws → the worker must guard/extend that kind before the render can trust it.
//     C) returns a spurious Unchanged on genuinely-divergent bytes → the check is BLIND to that
//        kind's divergence; the worker cannot rely on it (a different, worse failure than B).
//   E2 (function):
//     A) the row-level verdict + the existing extracted inputs already distinguish "signature diverged"
//        from "rva diverged" → attribution is a thin mapping.
//     B) one body-hash verdict cannot split the two → the worker needs explicit per-field derivation;
//        record exactly what each field needs.

import { describe, expect, it } from "vitest";
import type { VersionRow } from "../../api/types";
import { runVerdictCheck } from "../verdictCheck";
import { savedSeedRow } from "../fieldModel";
import { parsePe } from "../../dll-resolver/peSections";
import {
  IMAGE_BASE,
  MS_TEXT,
  MS_RDATA,
  makeAgreeingPE,
  makeMultiSectionPE,
} from "../../dll-resolver/makeFakePE";
import { blake3 } from "@noble/hashes/blake3.js";
import { bytesToHex } from "@noble/hashes/utils.js";

// A baseline VersionRow with every column null; per-kind tests fill only the columns that kind reads.
function baseRow(kind: string): VersionRow {
  return {
    kcdx_id: 1, kind, module_id: null, rva: null, length: null, value: null, signature: null,
    observed_arg_slots: null, caller_reg_arg_count: null, caller_arg_agreement: null, offset: null,
    vtable_slot: null, struct_offset: null, aob: null, anchor_string: null, rule: null,
    slot_count: null, expect_unique: null, derives_from: null, last_verified_at_version: null,
    verified_by: null, verified_date: null, evidence_kind: null, content_hash: null,
    valid_from: null, valid_through: null, valid_from_version: "1.5.1164953",
    valid_through_version: null, status: "active",
  };
}

// parsePe takes a DataView, not a Uint8Array — wrap the buffer.
function pe(buf: Uint8Array) {
  return parsePe(new DataView(buf.buffer, buf.byteOffset, buf.byteLength));
}

// Capture every probe observation here; printed as the finding at the end.
const observations: string[] = [];
function record(line: string) {
  observations.push(line);
}

describe("PROBE: divergent-DLL behavior + per-field attribution", () => {
  // ── E1: FUNCTION — recorded content_hash of the ORIGINAL body, but the on-disk body MUTATED ──
  it("E1 function — divergent body bytes", () => {
    const FN_RVA = MS_TEXT.rva + 0x10; // inside .text
    const FN_LEN = 0x20;
    // Build a matching DLL: plant body bytes at FN_RVA, record their hash. Then build a DIVERGENT
    // DLL with DIFFERENT bytes at the same RVA — recorded hash no longer matches on-disk.
    const originalBody = Array.from({ length: FN_LEN }, (_, i) => (i * 7 + 3) & 0xff);
    const recordedHash = bytesToHex(blake3(Uint8Array.from(originalBody)));
    const divergentBody = Array.from({ length: FN_LEN }, (_, i) => (i * 11 + 99) & 0xff);
    const divergentPe = makeMultiSectionPE({ rva: FN_RVA, bytes: divergentBody });
    const buf = new Uint8Array(divergentPe);
    const dpe = pe(buf);
    const row = { ...baseRow("function"), rva: FN_RVA, length: FN_LEN, content_hash: recordedHash,
      signature: "void __fastcall f(int)" };
    let verdict: string, threw = false, reason = "";
    try {
      const r = runVerdictCheck(row, savedSeedRow(row), dpe, buf);
      verdict = r.result.verdict; reason = r.result.reason;
    } catch (e) { threw = true; verdict = "THREW"; reason = String(e); }
    record(`E1 function: verdict=${verdict} threw=${threw} reason="${reason}"`);
    // E2 sub-probe: does the verdict / extracted inputs distinguish signature-diverged vs rva-diverged?
    // Re-run with rva WRONG (relocated) instead of body-mutated — same matching DLL, but row.rva off.
    const matchingPe = makeMultiSectionPE({ rva: FN_RVA, bytes: originalBody });
    const mbuf = new Uint8Array(matchingPe); const mpe = pe(mbuf);
    const rowRvaWrong = { ...baseRow("function"), rva: FN_RVA + 0x4, length: FN_LEN, content_hash: recordedHash,
      signature: "void __fastcall f(int)" };
    const rRvaWrong = runVerdictCheck(rowRvaWrong, savedSeedRow(rowRvaWrong), mpe, mbuf);
    // And with signature WRONG but body+rva correct (matching DLL).
    const rowSigWrong = { ...baseRow("function"), rva: FN_RVA, length: FN_LEN, content_hash: recordedHash,
      signature: "TOTALLY DIFFERENT SIG" };
    const rSigWrong = runVerdictCheck(rowSigWrong, savedSeedRow(rowSigWrong), mpe, mbuf);
    record(`E2 function: rva-wrong→${rRvaWrong.result.verdict} (relocated span hashes differently); ` +
      `sig-wrong-but-body-ok→${rSigWrong.result.verdict} (signature is NEVER hashed — body-only)`);
    expect(threw, `threw — E1 Outcome B for this kind: ${reason}`).toBe(false);
  });

  // ── E1: CALLSITE — recorded AOB, on-disk .text MUTATED so the pattern no longer matches ──
  it("E1 callsite — divergent .text bytes", () => {
    // The matching fixture plants no specific AOB target; a recorded AOB that does not appear in the
    // zero-filled .text of a divergent build → no match. Use a concrete AOB unlikely to appear.
    const buf = new Uint8Array(makeMultiSectionPE());
    const row = { ...baseRow("callsite"), rva: MS_TEXT.rva, aob: "DE AD BE EF CA FE", expect_unique: true };
    let verdict: string, threw = false, reason = "";
    try {
      const r = runVerdictCheck(row, savedSeedRow(row), pe(buf), buf);
      verdict = r.result.verdict; reason = r.result.reason;
    } catch (e) { threw = true; verdict = "THREW"; reason = String(e); }
    record(`E1 callsite: verdict=${verdict} threw=${threw} reason="${reason}"`);
    expect(threw, `threw — E1 Outcome B for this kind: ${reason}`).toBe(false);
  });

  // ── E1: STRING_ANCHOR — recorded literal absent from the divergent .rdata ──
  it("E1 string_anchor — divergent .rdata (literal removed)", () => {
    const buf = new Uint8Array(makeAgreeingPE()); // .rdata holds release_* interns, NOT our literal
    const row = { ...baseRow("string_anchor"), rva: MS_RDATA.rva,
      anchor_string: "a literal that the build no longer contains", expect_unique: true };
    let verdict: string, threw = false, reason = "";
    try {
      const r = runVerdictCheck(row, savedSeedRow(row), pe(buf), buf);
      verdict = r.result.verdict; reason = r.result.reason;
    } catch (e) { threw = true; verdict = "THREW"; reason = String(e); }
    record(`E1 string_anchor: verdict=${verdict} threw=${threw} reason="${reason}"`);
    expect(threw, `threw — E1 Outcome B for this kind: ${reason}`).toBe(false);
  });

  // ── E1: VTABLE_BASE — recorded slot_count, on-disk qwords point OUTSIDE .text (divergent) ──
  it("E1 vtable_base — divergent qword table", () => {
    // makeMultiSectionPE zero-fills raw data → qwords at .rdata read as 0, which is NOT in the .text
    // window → Changed (a divergent table). Tests the kind returns a defined verdict, not a throw.
    const buf = new Uint8Array(makeMultiSectionPE());
    const row = { ...baseRow("vtable_base"), rva: MS_RDATA.rva, slot_count: 3 };
    let verdict: string, threw = false, reason = "";
    try {
      const r = runVerdictCheck(row, savedSeedRow(row), pe(buf), buf);
      verdict = r.result.verdict; reason = r.result.reason;
    } catch (e) { threw = true; verdict = "THREW"; reason = String(e); }
    record(`E1 vtable_base: verdict=${verdict} threw=${threw} reason="${reason}"`);
    expect(threw, `threw — E1 Outcome B for this kind: ${reason}`).toBe(false);
  });

  // ── E1: VTABLE_INDEX — always CannotCheck (deferred), divergence is moot ──
  it("E1 vtable_index — always CannotCheck", () => {
    const buf = new Uint8Array(makeMultiSectionPE());
    const row = { ...baseRow("vtable_index"), vtable_slot: 5 };
    let verdict: string, threw = false, reason = "";
    try {
      const r = runVerdictCheck(row, savedSeedRow(row), pe(buf), buf);
      verdict = r.result.verdict; reason = r.result.reason;
    } catch (e) { threw = true; verdict = "THREW"; reason = String(e); }
    record(`E1 vtable_index: verdict=${verdict} threw=${threw} reason="${reason}"`);
    expect(threw, `threw — E1 Outcome B for this kind: ${reason}`).toBe(false);
  });

  // ── E1: a function row with NO recorded content_hash (the honest CannotCheck case) ──
  it("E1 function (no content_hash) — honest CannotCheck", () => {
    const FN_RVA = MS_TEXT.rva + 0x10;
    const buf = new Uint8Array(makeMultiSectionPE({ rva: FN_RVA, bytes: [1, 2, 3, 4] }));
    const row = { ...baseRow("function"), rva: FN_RVA, length: 0x10, content_hash: null };
    const r = runVerdictCheck(row, savedSeedRow(row), pe(buf), buf);
    record(`E1 function(no-hash): verdict=${r.result.verdict} reason="${r.result.reason}"`);
    expect(r.result.verdict).toBe("CannotCheck");
  });

  it("ZZ print finding", () => {
    record(`IMAGE_BASE=0x${IMAGE_BASE.toString(16)} (fixture base; unused by verdicts, sanity only)`);
    // eslint-disable-next-line no-console
    console.log("\n===== PROBE FINDING =====\n" + observations.join("\n") + "\n=========================\n");
    expect(observations.length).toBeGreaterThan(0);
  });
});
