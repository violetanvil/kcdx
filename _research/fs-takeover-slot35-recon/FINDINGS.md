# FINDINGS — CCryPak open+read slot→FUN→role→ABI map, reconciled

Captured 2026-06-15. Trust: **PRIMARY EVIDENCE** for every body-read fact below —
fresh Ghidra 12.1 decompile of WHGame.dll `release_1_5_1164953_841` (image base
0x180000000) for slot 35 (this dir's `_slot35_dump.txt`), plus body-read
reconciliation of the existing `_research/phase8.5-pak-resolver/` dumps
(`_readpath_decomp.txt`, `_readpath_leaves.txt`) for slots 38/39/40 and the
front docs for the rest. The slot→`+offset`→`FUN_` bindings are read off the live
CCryPak vtable @ VA 0x183A95FA8 (RTTI `.?AVCCryPak@@`).

## Why this investigation ran

file-system-takeover step 3.2/3.3 flips specific CCryPak vtable slots from THUNK
to KCDX and implements their bodies. Two prior recon fronts CONFLICTED on the
role labels for the read/open family — front1's vtable-surface map vs front3's
read-path decompile assigned different roles to the same slot numbers (e.g.
`FUN_18051cd00` = "slot 40 FGetCachedFileData" in front1 but "slot 40 FRead" in
front3; `FUN_180461304` = "FOpen-by-pak-index" in front1 but "FSeek" in front3).
A wrong slot→function mapping installs the wrong impl into a slot the engine
calls for something else (AP2, silent corruption a green build never catches), so
the map was resolved authoritatively against the binary BEFORE the build. The
reconciliation was confirmed by an independent gated body-read verifier (WITHHELD
lean), and the one genuinely-unverified slot (35, whose body was in no prior
dump) was freshly dumped here.

## The verdict — front1 (and design §4.5) is authoritative; front3 mislabeled the read-family slot ROLES

The `+offset`→`FUN_` binding was never in dispute (front1, front3, and the
readpath dump all read it identically off the binary). The conflict was purely
the ROLE each function performs, settled here by reading each body:

- **front3 took the handle-tag dispatch SHAPE it correctly identified as common
  across the read family and mis-assigned the canonical libc names** (FRead /
  FSeek / FEof) to slots 38/39/40. The bodies show all three of 38/39/40 are
  **read-family** functions; the real FSeek/FTell/FEof live at slots 53/54/56.
- **front3's read-path MECHANISM finding is correct and not in dispute** — the
  handle is a tagged union (small index+1 = pak entry in `[this+0x40]`; else a
  real FILE*), and the loose-vs-pak decision bites at FOpen-time, not per-read.
  Only its slot ROLE labels for 38/39 were wrong.

## The authoritative slot → FUN → role → ABI table (open+read family)

`this` (rcx) = CCryPak* throughout; all are `__fastcall` member functions. RVA =
VA − 0x180000000.

| slot | +off | FUN RVA | role (body evidence) | verified ABI | seed |
|---|---|---|---|---|---|
| 1 | +0x8 | 0x46205c | **AdjustFileName** — resolve a vpath to a path STRING (search-path walk + pakPriority gate) | seed id 152 | **id 152** |
| 35 | +0x118 | 0x182418de4 | **FOpenRaw** — open-into-caller-buffer: calls slot 1 (`*this+8`) to resolve, copies the resolved name into the caller buffer, opens via the `_wfopen` primitive `FUN_1809b2b28`, registers the handle via `[*this+0x268]` (cleanup `FUN_18241bc8c` on fail) | **5-arg** (below) | **none — NEW (AP18)** |
| 36 | +0x120 | 0x4614a0 | **FOpen** — engine-wide open-by-path | seed id 131: `FILE*-like(this, const char* pName, const char* szMode, u32 nFlags)` | **id 131** |
| 37 | +0x128 | 0x2418ec8 | release/reopen pair of slot 35 — thin `void(this,p2,p3){ FUN_1809b2b28(p2,p3); }` | `void(this, name, mode)` 3-arg | none |
| 38 | +0x130 | 0x461304 | **FReadRaw-by-pak-index** — NOT an open, NOT FSeek: dispatches `param_5-1 < pakEntryCount` over `[this+0x40]`; pak arm → `FUN_1804618b4` (read-raw leaf, str `"FRead did not read expected number of byte"`); OS arm → `FUN_1804d7ab4` (CRT `fread`) | 5-arg `(this, buf, size, count, taggedHandle/index)` | none — NEW (AP18) if resolved by name |
| 39 | +0x138 | 0x51e1f8 | **FReadRaw / cached-read** — handle-tag dispatch; OS arm `fseek(h,0,0)` then `FUN_1804d7ab4` (CRT `fread`). A READ, not FEof/FTell | 4-arg `(this, buf, size, FILE* handle)` | none |
| 40 | +0x140 | 0x51cd00 | **FGetCachedFileData** — **dispositive string** `"!Cannot have more then 1 FGetCachedFileData at the same time"` | 3-arg `(this, FILE* handle, longlong* outSizeDst)` | none |
| 41 | +0x148 | 0xa700c8 | **FWrite** (`fwrite`) | per id 131 prose | covered by id 131 prose |
| 53 | +0x1a8 | 0x46068c | **FSeek** (`fseek`) | front1 fingerprint | none |
| 54 | +0x1b0 | 0x460cdc | **FTell** (`_ftelli64`) | front1 fingerprint | none |
| 55 | +0x1b8 | 0x4609d0 | **FClose** | per id 131 prose | covered by id 131 prose |
| 56 | +0x1c0 | 0x961d48 | **FEof** | front1 fingerprint | none |

## Slot 35 — the freshly-dumped ABI (the one gap the prior dumps left)

`_slot35_dump.txt`, `FUN_182418de4` decompiled body (binding re-confirmed live:
slot 35 @ vtable+0x118 = 0x182418de4):

```c
longlong FUN_182418de4(longlong* this /*rcx*/, undefined8 pName /*rdx*/,
                       undefined8 szMode /*r8*/, undefined8 outBuf /*r9*/,
                       int bufCap /*stack, param_5*/) {
    FUN_1804613d0(lock, this);                          // pak-mgr scope lock
    resolved = (**(this + 8))(this, pName, scratch, 0); // slot 1 AdjustFileName(flag 0)
    cap = (bufCap < 0x801) ? bufCap : 0x800;            // clamp to 2048
    FUN_180ab5db4(outBuf, cap, resolved, -1);           // copy resolved name into caller buffer
    h = FUN_1809b2b28(resolved, szMode);                // _wfopen-backed open
    (**(this + 0x2c8))(this, pName, szMode);            // post-open vtable hook
    if (h == 0) FUN_18241bc8c(this, pName);             // open failed → cleanup
    else (**(this + 0x268))(this, h, pName);            // register the handle
    FUN_1804613fc(lock);
    return h;                                            // FILE* (or 0)
}
```

**Slot 35 FOpenRaw — verified ABI: 5-arg `__fastcall` member**
`FILE*-like (CCryPak* this /*rcx*/, const char* pName /*rdx*/, const char* szMode /*r8*/, char* outResolvedBuf /*r9*/, int bufCap /*stack*/)`.
- Resolves `pName` via slot 1 (so a kcdx slot-1 owns slot 35's resolution for free).
- Writes the resolved path string into `outResolvedBuf` (clamped to ≤2048).
- Opens through the **`_wfopen`-backed primitive `FUN_1809b2b28`** — confirmed body:
  builds two wide strings and calls `_wfopen(path, mode)` → `FILE*`.
- Returns the `FILE*` handle (registered into the engine's handle table).

This confirms front1's role label exactly, now with the full verified ABI. Slot
35 IS a true open slot.

## The §4.5 wording defect to correct (surfaced, not fixed here)

`docs/design/file-system-takeover.md` §4.5 lists slot 38 under **"Open: slot 36
FOpen, slot 35 FOpenRaw, slot 38 FOpen-by-pak-index."** The body (above) shows
slot 38 leafs into the read-raw leaf `FUN_1804618b4` and CRT `fread`, with NO
open/`fopen` call — it is a **read-raw-by-pak-index**, not an open. front1's
"FOpen-by-pak-index" label and §4.5's grouping of 38 under "Open" are both
body-unsupported. **Consequence for the build: slot 38 belongs to the READ family
(step 3.3), not the open family (step 3.2).** §4.5's read-family assignments
(39/40/41/53/54/55/56) all match the bodies; only the slot-38 grouping is the
wording defect. (The fix to §4.5 is a separate edit, surfaced for the user, not
made in this RE turn.)

## Seed-row status (AP18 — FLAGGED, NOT written)

- **Slot 1 (AdjustFileName) — seeded id 152.** Resolve by name; no new row.
- **Slot 36 (FOpen) — seeded id 131.** Resolve by name; no new row.
- **Slots 41 (FWrite) / 55 (FClose) — covered by id 131 prose**, not separate
  entities; a NEW AP18 entity each only if kcdx resolves them by name independently.
- **Slot 35 (FOpenRaw) — NO seed row; a NEW AP18-gated entity is needed** to
  resolve it by name. Its ABI is now body-verified here (5-arg, above) and
  seed-ready. RVA 0x2418DE4; the `_wfopen` open primitive `FUN_1809b2b28` (RVA
  0x9B2B28) is its leaf.
- **Slots 38/39/40/53/54/56 — NO seed rows;** each a NEW AP18-gated entity if
  kcdx resolves it by name. Slot 38's ABI is body-verified (5-arg); the others
  per their bodies/fingerprints above.

No seed row is authored here — every addition is the user's per-entity call (AP18).

## Producers (this dir)

- `third-party-ghidra/ghidra_scripts/Slot35FOpenRawDump.java` — the slot-35 dump
  script (decompile + disasm of slot 35 + the `_wfopen` primitive + the slot-37
  pair; re-confirms the 35/36/37 vtable bindings live).
- `_slot35_dump.txt` — its raw headless output (the slot-35 body + ABI evidence).

## Reuse pointers (the bodies this reconciliation rests on)

- Slots 38/39/40 bodies + the read leaves: `_research/phase8.5-pak-resolver/_readpath_decomp.txt`, `_readpath_leaves.txt`.
- The full 102-slot surface: `_research/phase8.5-pak-resolver/front1-full-vtable-surface.md` (authoritative on roles where its rows are body-read **V**).
- The read-path mechanism (handle-tag union, decision-at-FOpen): `_research/phase8.5-pak-resolver/front3-handle-consume-read-path.md` (mechanism correct; its slot-38/39 role labels superseded by this doc).
