# Phase 3 — C++ DLL API parity (additive) + ergonomic wrapper

**Status: DONE** (all three subs). Detail: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 3" + [`../../phase-3-sub-1-extended.md`](../../phase-3-sub-1-extended.md).

The C++ surface mirrors the Lua verbs at full parity: `kcdxHookInterface`,
`kcdxBytesInterface`, `kcdxTrampolineInterface`, with the `Kcdx.h` empowered
wrapper and the sig-mismatch gate.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| sub-1 — kcdxHookInterface v1 + Kcdx.h wrapper + sig-mismatch gate | DONE | cdd5e7a / b5e548a / d5c3314 |
| sub-2 — kcdxBytesInterface (C++ kcdx.bytes mirror) | DONE | 2b2e6f5 |
| sub-3 — kcdxTrampolineInterface v2 (raw pool + Allocate/Export, the kcdx.code C++ mirror) | DONE | 38f9dd5 |
