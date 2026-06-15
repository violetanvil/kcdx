# P2/P4 (file-system-takeover seating) — swap seats + holds; KI-0019 persists with the swap active

Captured 2026-06-15. The seating-spike re-verification: a live launch (run
2026-06-15 10:06:34) that PASSED the swap-mechanism gate (P2 + P4) AND
reproduced the KI-0019 inventory-open crash with the swap active. The finding
is the step-4.2 re-verification starting point — KI-0019 resolves only when the
read family is kcdx-owned. Step 1.4 landed `3be161a`.

## Archive header

- **VERDICT:** P2 + P4 **PASS** (the vtable swap seats, the engine dispatches its first file open into kcdx, and the game boots to the world through the 101 thunked slots without crashing). KI-0019 **STILL REPRODUCES** with the swap active — confirmed, expected, and forward-looking (NOT a regression, NOT a Resolution).
- **WHAT IT PROVED:** (1) **P2 — swap accepted live:** the engine read kcdx's swapped vtable pointer and dispatched its FIRST vanilla file open through kcdx's slot 36 (`./system.cfg`). (2) **P4 — thunks sound:** the boot ran a chain of 101 thunked original slot bodies against the swapped (layout-preserved) object and reached the world without crashing. (3) **KI-0019 persists by design:** with the swap LIVE, the inventory-open AV reproduced — the cross-CRT `fseek` null-EACCES write — because the read family (FSeek/FClose/FRead, …) is still `THUNK(original)` on the engine's CRT in the v1 spike; only slot 36 is kcdx-owned. The seating spike was never going to fix KI-0019; it proves the swap mechanism (the prerequisite). KI-0019 resolves at step 4.2 when the read-family slots flip THUNK→KCDX and kcdx owns the read (design §9).
- **KNOWN-ISSUE BACKLINK:** [KI-0019](../../docs/known-issues/KI-0019-ccrypak-fopen-reentrancy-av-on-inventory-open.md) — Trail H / the takeover-seating re-verification. Sibling: [KI-0006](../../docs/known-issues/KI-0006-serve-execute-vehicle-not-found.md) (the cross-CRT `fclose`, same read-family ownership fix). Plan: [`docs/outstanding-work/file-system-takeover/phase-01-seating-spike/`](../../docs/outstanding-work/file-system-takeover/phase-01-seating-spike/) step 1.4; design [`docs/design/file-system-takeover.md`](../../docs/design/file-system-takeover.md) §4.3/§4.4 (101 slots thunk in the spike) + §9 (owning the read family removes the straddle).
- **REVIVAL HINT:** at step 4.2 (read-family slots flipped THUNK→KCDX), repro inventory-open with the swap active and confirm the `engine.ccrypak_fopen` FAULTED_FIRE + the `ucrtbase!fseek → null EACCES write` dump are GONE; that is the KI-0019/KI-0006 closure launch. The FS_TAKEOVER log markers below are the same lines to grep for swap-seated confirmation on any later seating change.

## Trust level

**PRIMARY EVIDENCE** — a live launch (run 2026-06-15 10:06:34) whose result was
read from `kcdx-dev.log`, plus a minidump fault stack read via cdb this session
(`kcdx_2026-06-15_10-06-34.dmp`). Not an agent hypothesis: every line below is a
log read or a dump-frame read. The "resolves at step 4.2" implication is grounded
in design §9 + the verified slot-ownership state (only slot 36 is KCDX in the
spike).

## P2 + P4 PASS — the evidence (from kcdx-dev.log, run 2026-06-15 10:06:34)

```
[10:06:35.356][INFO][engine][FS_TAKEOVER] seating_hook_installed name=CSystem_pCryPak_construct_store
[10:06:35.490] seating_post_publish pCryPak=1676250703664
[10:06:35.491] kcdx_vtable_built slots=102 kcdx_owned=1
[10:06:35.491] vtable_swapped object=1676250703664 original_vtable=140709029371816 kcdx_vtable=140710384946352
[10:06:36.103] swap_live_first_open ... first_vpath=./system.cfg
[10:06:36.103][TEST] RESULT name=cap-108-fs-takeover-seating verdict=PASS
[10:06:50.331][ACCEPT] ACCEPT-SUITE: 1/1 passing
```

- `kcdx_owned=1` of 102 slots — slot 36 (FOpen) is the only kcdx-owned slot; the other 101 are `THUNK(original)`.
- `swap_live_first_open first_vpath=./system.cfg` — P2: the engine dispatched its first vanilla file open into kcdx's slot 36, so the swap took (the engine did not cache or reject the original vtable pointer).
- `cap-108 verdict=PASS` + `ACCEPT-SUITE: 1/1 passing` — P4: the boot reached the world through the 101 thunked slots without crashing.

The PRIOR run (2026-06-15 10:59:03, before this one in wall-clock but a stale
deploy) FAILED to install the seating hook: `seating_install_failed` — the
refdb resolve of the construct-store helper returned 0 because the deployed
`reference.sqlite` was STALE (157 entities, missing curated ids 158/159). The
current DB (159 entities) was redeployed + hash-verified; THIS run (10:06:34)
resolved id 158 by name and armed the swap. (The "why the first launch didn't
arm" so the trail is complete.)

## KI-0019 reproduced with the swap active — the today-dump fault stack (verbatim)

32× `[10:09:00][ERROR][engine][GUARD] FAULTED_FIRE hook=engine.ccrypak_fopen
va=0x7FF95C5314A0`, AFTER the 10:06 swap (so the swap was LIVE when the crash
fired — both timestamps are in the same run's log, a read fact). MiniDump
`kcdx_2026-06-15_10-06-34.dmp`. cdb fault stack (`.ecxr; !analyze -v; k`):

```
ucrtbase!common_fseek_binary_mode_read_only_fast_track_nolock+0x89
ucrtbase!common_fseek_nolock
ucrtbase!common_fseek
ucrtbase!fseek+0x44
WHGame!CreateGameStartup+0x97932    (caller 0x7ff95c53200a)
```

`EXCEPTION_CODE c0000005`, "Attempt to write to address 0",
`FAILURE_BUCKET NULL_POINTER_WRITE_c0000005_WHGame.dll`.

This is the SAME cross-CRT `fseek` mechanism the KI-0019 CORRECTED-mechanism
section already root-caused (`ucrtbase` = the engine's CRT; `fseek` on a fd
invalid in it → `get_osfhandle` invalid-parameter → null EACCES write). The
swap being active did not touch it: FOpen (slot 36) is kcdx's, but FSeek and the
rest of the read family are still thunked engine bodies on the engine's CRT, so
the engine still `fseek`s a kcdx-CRT `FILE*` cross-CRT.

## The finding

**Swap active + read family thunked → KI-0019 persists; it resolves at step 4.2
when the read family is kcdx-owned.** The seating spike (v1) proves only the
swap mechanism — kcdx owns the object the instant it is published and the engine
dispatches into kcdx without crashing the thunked slots. It does NOT, and was
never going to, fix the cross-CRT read-path straddle, because the read slots
still run the engine's original body on the engine's CRT. KI-0019 (and its
sibling KI-0006) close together at the takeover plan's step 4.2, when the
read-family slots flip THUNK→KCDX and every handle the engine receives is one
kcdx's own CRT minted, sought, and closed (design §9 — owning the read family
removes the straddle entirely). This capture is the step-4.2 re-verification
starting point: re-run this exact gesture with the read family owned and confirm
the FAULTED_FIRE + the `ucrtbase!fseek → null EACCES write` dump are gone.

## Backlinks

- **KI-0019** ([`docs/known-issues/KI-0019-ccrypak-fopen-reentrancy-av-on-inventory-open.md`](../../docs/known-issues/KI-0019-ccrypak-fopen-reentrancy-av-on-inventory-open.md)) — Trail H, the takeover-seating re-verification row.
- **KI-0006** ([`docs/known-issues/KI-0006-serve-execute-vehicle-not-found.md`](../../docs/known-issues/KI-0006-serve-execute-vehicle-not-found.md)) — the cross-CRT `fclose` sibling; closes with KI-0019 at step 4.2.
- **File-system-takeover plan** ([`docs/outstanding-work/file-system-takeover/phase-01-seating-spike/`](../../docs/outstanding-work/file-system-takeover/phase-01-seating-spike/)) — step 1.4 (this run); the read-family ownership build is step 4.2.
- **File-system-takeover design** ([`docs/design/file-system-takeover.md`](../../docs/design/file-system-takeover.md)) — §4.3/§4.4 (101 slots thunk in the spike; only slot 36 is kcdx-owned) + §9 (owning the read family removes the cross-CRT straddle).
