# KI-0019 inventory-open AV — dump read + mechanism (2026-06-13)

Durable recon for [`docs/known-issues/KI-0019-ccrypak-fopen-reentrancy-av-on-inventory-open.md`](../../docs/known-issues/KI-0019-ccrypak-fopen-reentrancy-av-on-inventory-open.md).
The reusable wiring: how to read a kcdx crash dump, and the verified fault mechanism.

## The cdb recipe (reuse this on the next crash — read the dump FIRST)

```
CDB="/c/Program Files (x86)/Windows Kits/10/Debuggers/x64/cdb.exe"
DMP="<game-bin>/kcdx-engine/logs/kcdx_<ts>.dmp"
"$CDB" -z "$DMP" -c ".ecxr; !analyze -v; q"      # fault instruction + bucket
"$CDB" -z "$DMP" -c ".ecxr; k 40; q"             # symbolized faulting stack
"$CDB" -z "$DMP" -c "lmDvm kcdx; lmDvm ucrtbase*; q"   # CRT-boundary modules
```
WHGame.dll has no PDB → frames show as `WHGame+0xRVA` / `WHGame!<nearest-export>+off`;
`ucrtbase` symbolizes fully (the CRT frames name the mechanism).

## Verified fault (run kcdx_2026-06-13_11-09-56.dmp)

- **Fault:** `WHGame!CreateGameStartup+0x97932: mov dword ptr [rax],0Dh` with `rax=0`.
  `!analyze`: `AV.Fault: Write`, `AV.Dereference: NullPtr`,
  `Failure.Bucket: NULL_POINTER_WRITE_c0000005_WHGame.dll`. **Not** a re-entrancy spiral
  (the text-log `FAULTED_FIRE` seq stack is the GUARD's kcdx-hook inventory, not the
  fault stack — a misleading artifact; see KI-0019).
- **`0x0D` = 13 = EACCES.** Faulting frames: `fseek` → `common_fseek` → `lseeki64_nolock`
  → `get_osfhandle+0x55` → `invalid_parameter_noinfo` → `invalid_parameter` → AV.
  `ucrtbase`'s `get_osfhandle` got an INVALID fd → invalid-parameter handler → null
  errno=EACCES write.
- **Deepest non-CRT caller:** `fseek()` on a `FILE*` inside WHGame's pak code
  (`WHGame+0x46200a/0x461ba0/0x46088c` — the `0x...FE1xxx-FE2xxx` neighborhood = the
  logged `engine.ccrypak_fopen` hook va `0x7FF955FE14A0`).
- **Outer frames:** `WHGame!ffxFsr2ResourceIsNull+…` → `WHGame!NVSDK_NGX_UpdateFeature+…`
  (FSR2/DLSS init — the same subsystem as KI-0012).
- **Modules:** `kcdx.dll` loaded (own static CRT, built Jun 12 14:57:52); `ucrtbase.dll`
  is WHGame's CRT.

## Mechanism (source-confirmed, src/asset_overlay.cpp FOpenLooseOverlay)

HOOK 2 (the CCryPak::FOpen Around cFn) on a HIT mints a `FILE*` via kcdx's own CRT
`_wfopen_s` (asset_overlay.cpp L339-342) and returns it as FOpen's result (L379) —
a **kcdx-CRT `FILE*` handed to the engine**. The design's cross-CRT safety proof
(L259-261) is scoped to **`FRead`** only ("FRead routes any real heap FILE* to its OS
arm; handle−1 ≫ pak-count; gate-verified `_research/asset-fopen-handle-recon/`"). It
does NOT cover **`fseek`/`get_osfhandle`**, which validates the fd in the **engine's**
CRT (`ucrtbase`), where kcdx's fd is invalid. FSR2/DLSS init `fseek`s the returned
`FILE*` → invalid-param → null EACCES write → AV. This is the cross-CRT `FILE*` hazard
KI-0006 named, surfacing as a cross-CRT `fseek` (KI-0006 found the cross-CRT FREE).

## Honest residual (NOT yet confirmed)

The crash fires only on a HOOK 2 **HIT** (overlay-map hit). Whether the FSR2-init file
was a HIT (kcdx FILE* served) or a MISS (call_original; engine's own FILE*, crash is
engine-side) is unconfirmed — the dump doesn't carry the crash-time overlay map. A
targeted probe (log the vpath + HIT/MISS + returned-handle origin per FOpen on the
FSR2-init path) would settle it. The mechanism (cross-CRT fseek hazard) is confirmed;
that THIS crash is the HIT-path instance is the open edge.

## Why non-deterministic

FSR2/DLSS init touching that specific `FILE*` is GPU/driver/timing-dependent → the
identical config crashes only sometimes (explains why 4 repro launches with various
probe subsets all came back "no crash"). Live-launch bisection is the WRONG tool for a
non-deterministic crash with a dump on disk; the dump is deterministic.

## The probes are a red herring
cap-105/106/107 installed ZERO hooks (raw-VA `address=` form rejected at register;
`kcdx.hook` returned nil) — never on the faulting stack. Their presence correlated with
the one crash only by non-determinism. Separate defect: the raw-VA entry-hook
registration rejection (own /execute fix).
