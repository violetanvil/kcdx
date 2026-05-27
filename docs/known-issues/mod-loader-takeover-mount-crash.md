# Mod-loader takeover: native MOUNT CryFatalErrors (~2.7GB alloc) over kcdx's synthesized records

## Symptom

After step 4 (commit `bdc5f43`) wired the production SELECT-detour takeover, the
game crashes on launch. The takeover itself runs clean — kcdx rebuilds the
enabled list (71 records: 7 vanilla pak mods + 64 plugins) and repoints the
native vector. The crash comes AFTER, during the native MOUNT / a downstream
pass walking kcdx's list: a deliberate CryEngine fatal-error abort
(`RaiseException` code `0xDE`) triggered by a failed ~2.7 GB memory allocation.

## Facts

- The takeover is structurally correct: `enabled_list_built count=71 vanilla=7
  plugins=64 dropped=0`; `takeover_repoint orig_count=15 new_count=71`. No crash
  at the repoint. (kcdx-dev_2026-05-27_11-49-55.log)
- Native SELECT-side validation ACCEPTED all 71 records — kcd.log shows every
  record's `[Mod] '<path>' is not limited to any game version, it will be
  enabled` line. So a from-scratch kcdx record passes the native validation pass.
- Then kcd.log: `*** Memory allocation for 2879947683 bytes failed, free 0MB ...
  *ERROR ... <CrySystem> Last System Error: Access is denied.` → CryFatalError →
  `RaiseException(0xDE)`. `2879947683 = 0xABA883A3` — a value computed from
  garbage (close to but NOT the MSVC fill 0xABABABAB), used as an alloc size.
- **ZERO `[Mod] Opening paks in ...` lines in kcd.log** — MOUNT crashed BEFORE
  opening a single pak, i.e. in the MOUNT driver's setup or a downstream
  per-record pass that runs before the OpenPacks loop, NOT in a per-record pak
  open. So the cause is up-front over the whole list / a record field read as a
  size, not a per-record path issue. (kcd.log, 11:49 boot)
- Faulting-thread stack (.ecxr, tid 38288): `KERNELBASE!RaiseException` ←
  `WHGame!NVSDK_NGX_UpdateFeature+0x871b5a` (the CryFatalError raiser) ←
  `WHGame!CreateGameStartup+0xda687` (carries args `0x81C`=2076 AND
  `0xABA883A3`) ← `WHGame+0x4f7db5` ← `ffxFsr2ResourceIsNull+0x2cec` (receives
  our list range `new_begin=0x27fcd209100`, `new_end=0x27fcd209338`, `0x07`).
  So `0xABA883A3` is computed in the `WHGame+0x4f7db5` frame while walking
  kcdx's list. (dump kcdx_2026-05-27_11-49-55.dmp)
- The minidump captured the I_Mod* ARRAY intact (71 valid pointers at
  0x27fcd209100) but NOT the record BODIES they point at (`????` — kcdx's
  record_synth deque/boxed heap is outside the dump's captured ranges). So the
  record contents can't be read from this dump — needs a live read-only probe.
- PROBE U.8 mounted ONE kcdx record cleanly — but it COPIED string pointers from
  a REAL native record. Step 4 SYNTHESIZES strings (the U.8 OPEN) and builds 71
  records including 64 plugins with often-EMPTY string fields (version/createdDate
  are `.clear()`'d; many plugins have empty description/author).
- U.6.3 doc note: the real record's string fields are "raw char*/CryString-char
  buffers (chars at +0)" — i.e. deref'ing a field pointer landed on chars
  directly. So the FIELD is a char*-to-chars; the "field is a {ptr,len,cap}
  CryString object" hypothesis for the field itself is weakened (but the
  per-record VALIDATION that read it differs from the MOUNT-setup pass that
  crashed — the crashing read may interpret a DIFFERENT field).

## Trail

| Probe | Action | Result |
|---|---|---|
| (dump) | Read fault context + stack + our list/records from the minidump | code 0xDE via RaiseException; `0xABA883A3` computed in WHGame+0x4f7db5 walking our list; records not in dump (`????`) → need a live read-only probe. |
| A | Side-by-side 0x70 dump native[0] vs kcdx[0], read-only (no repoint) | Vtables + the 8 heap-ptr fields SAME shape; only diff was native[0] +0x50 = `\x00ap_36_c` inline vs kcdx zeroed → suspected +0x50 a real field. |
| A.2 | Dump +0x50..0x6F across ALL 15 native records, read-only | 14/15 native records ZERO at +0x50 (idx6 had stray ptr bytes). +0x50 is NOT a structural field — native zeros it too; the A `ap_36_c` was stale heap garbage. THEORY KILLED. |
| A.3 | Deref all 8 string-field targets, native[0] vs kcdx[0], dump 24 bytes at each | BOTH have valid NUL-terminated ASCII at every field (kcdx FastLaunch: path/id/name/desc/author/version all readable; +0x48 date empty="" → \0, native had a date). Record layout + string content BOTH fine. STRING-CONTENT THEORY KILLED. |
| B mode1 | Repoint kcdx's array holding the 15 GENUINE native record ptrs (count=15), let MOUNT run | CLEAN boot, 15 paks mounted (`Opening paks` ×15, no alloc-fail). kcdx's ARRAY BUFFER + repoint mechanism INNOCENT. Bug needs kcdx RECORDS or count>15. |
| B mode2 | kcdx array, 71 entries = native record ptrs cycled (count=71, all native records) | CLEAN boot, 71 paks mounted. COUNT INNOCENT — 71 native records mount fine. Bug is kcdx RECORD CONTENT specifically. |
| B mode3 | Repoint at EXACTLY ONE kcdx synthesized record (count=1, the FastLaunch pak-mod) | CRASHED, alloc size 0xEFE8160C (DIFFERENT from the orig 0xABA883A3) → size VARIES run-to-run → MOUNT-setup reads UNINITIALIZED memory. ONE kcdx record is the minimal repro. |
| B.4 | Read-only: native record-array stride + native bytes +0x70..+0xFF | stride_bytes=0x70 (rec1=rec0+0x70 — native records CONTIGUOUS at 0x70 stride); +0x70.. is the NEXT record (vtable at +0x70 like +0x00). Object IS 0x70; native records live in ONE contiguous block. kcdx's are SCATTERED unique_ptr allocs → contiguity-mismatch was the live hypothesis (later killed by C). |
| B.5 | Repoint at kcdx records COPIED into one contiguous 0x70-stride block | CRASHED — but a DIFFERENT crash: ACCESS_VIOLATION in VCRUNTIME140 (a memcpy), no alloc-fail line. Contiguity changed the failure mode but didn't fix it → not (purely) contiguity. The memcpy-AV pointed at a string-COPY reading a bad length. |
| C | Read-only: dump 16 bytes BEFORE each string-field char ptr (CryString header region), native[0] vs kcdx[0] | **DECISIVE — root cause.** Native fields are CryStringT: a `{nRefs=1, nLength=<exact len>, nAllocSize>=len}` header sits immediately before the char data (e.g. +0x08 path: nLength=62 = the 62-char path; +0x40 version: nLength=5 = "1.0.0"; every field's nLength == its actual string length). kcdx wrote a bare `std::string::c_str()` — the 12 bytes before it are unrelated heap garbage, so the engine reads nLength as ~1.9e9 → the variable ~2.7-4GB alloc / memcpy AV. |

## Resolution

ROOT CAUSE (PROBE C, decisive — no inference): the I_Mod string members are
**`CryStringT`**, not bare `char*`. A CryStringT field stores a pointer to the
CHAR DATA of a ref-counted buffer laid out
`[int nRefs][int nLength][int nAllocSize][char data... \0]`; the engine reads
`nLength` (at data-8) to size string copies during MOUNT. Native records have a
valid header (nRefs=1, nLength=exact length); kcdx's `record_synth::BuildRecord`
wrote a bare `std::string::c_str()` into the field, so the 12 bytes preceding the
chars are unrelated heap bytes → the engine reads a garbage multi-GB nLength →
CryFatalError on the failed alloc (or a memcpy AV). This is why U.8 worked (it
COPIED native field pointers, which pointed at real CryString headers) and
PROBE A.3 missed it (it dumped the chars AT the pointer — valid — but never the
header BEFORE it). 

FIX (landed `ad21fa4`): `record_synth::InternCryString` synthesizes a real
CryStringT buffer — `[int32 pad=0][int32 nRefs=1][int32 nLength=len][int32
nAllocSize=len][chars][\0]` (16-byte header so the chars are aligned and
data-12/-8/-4 hold the header), stores the pointer to `chars`. The 8 BuildRecord
string fields call it; boxed in `unique_ptr<vector<uint8_t>>` for process-lifetime
address stability (replaced the `std::deque<std::string>` store).

VERIFIED (live, 2026-05-27 12:46 boot): the full production takeover ran — 71
records (7 vanilla + 64 plugins) rebuilt in kcdx order, every kcdx `str_header`
`nLength` == the exact string length (matching native: path=73, id `FastLaunch`=10,
name=11, version `1.0`=3, empty date=0), all 71 mounted (`[Mod] Opening paks` ×71),
ZERO alloc-fail, reached menu. The keystone takeover works.

Probe instrumentation reverted (select_detour.cpp back to the clean production
detour; the takeover install in dllmain re-enabled). The PROBE A/B/C diagnostic
edits were never committed.

## Tracked follow-up (observability gap this hunt exposed)

kcdx had no instrumentation on what the native engine READS from a kcdx-supplied
record — the crash surfaced as an opaque native multi-GB abort, not a kcdx
diagnostic. A takeover SELF-VALIDATION pass (kcdx walks its own rebuilt records
before repointing and asserts each against the known native invariants — incl.
the CryString header shape: nRefs==1, nLength==strlen, nAllocSize>=nLength —
logging/failing loud per record) would catch a malformed synthesized record at
build time, named, instead of as a native fatal-error. Fold into the absorb
feature's hardening (the planned step-6 + a cap-NN regression that asserts the
synthesized record's CryString header, so this exact bug can never silently
return). Also worth a cap-48/52 sub-assertion on the header nLength once the
parallel public-boundary scrub of record_synth_selftest.cpp settles.

## Reframe 2026-05-27: the synthesized RECORD is not the problem

Three theories killed (field-shape, +0x50 tail, string-content) — all confirmed
the kcdx record is byte-for-byte structurally equivalent to a native record:
matching vtables, 8 valid char* string pointers to NUL-terminated ASCII, zeroed
+0x50 tail. **The records are fine.** So the ~0xABA883A3 alloc crash is NOT
caused by a malformed record. Frame lost (theory-hopped 3×) → fresh-frame
subagent dispatched to design the most direct ground-truth observation of where
0xABA883A3 actually comes from, assuming no prior theory.

Candidate axes NOT yet observed (for the fresh frame, not pre-judged):
- The LIST itself: 71 records where native had 15. Does a MOUNT-setup pass
  allocate/compute something from the COUNT (71) or a per-record product?
  (0xABA883A3 vs 71 — no obvious factor, but unobserved.)
- The ORDER/CONTENT mix: native[0..14] were all mods/ pak mods; kcdx's list
  interleaves 7 pak mods + 64 PLUGINS whose paths are under
  Bin\...\kcdx-plugins\ — a path the native MOUNT-setup may handle differently
  (e.g. compute a buffer from path length, or a pak-scan that mis-sizes).
- The crash is in WHGame+0x4f7db5 ← ffxFsr2ResourceIsNull+0x2cec walking the
  list BEFORE any "Opening paks" — a per-list setup/alloc, not per-record mount.
  The actual computed value's derivation has NOT been instrumented.

## Open questions

- **The crashing MOUNT-setup pass reads a record field (or a per-list scratch)
  as a size, and kcdx's synthesized value is wrong** — Probe: a read-only
  side-by-side dump, in the SELECT detour BEFORE the repoint, of (a) a REAL
  native record's full 0x70 bytes and (b) kcdx's first synthesized record's full
  0x70 bytes. Compare field-by-field — which field differs in SHAPE (a real
  record has a non-char-pointer where kcdx wrote a char*, or a non-zero scalar in
  +0x50..0x6F kcdx zeroed). Theory-independent: dumps both raw, no assumption
  about which field.

## Active diagnostic instrumentation

| File | Change | Keep/revert |
|---|---|---|
| `src/dllmain.cpp:174` | takeover `InstallSelectDetour()` commented out (DISARMED) — restores a working boot during the investigation | revert (re-enable) once fixed |

## Decision

(pending)
