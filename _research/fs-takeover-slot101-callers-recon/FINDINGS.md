# slot-101 (FindFirst) callers + iterator recon — FINDINGS

**Question.** Does keeping CCryPak slot 101 (`FindFirst` / the `CCryPakFindData`
iterator factory) THUNK, while slot 14 (`ForEachFile`) is a KCDX impl that walks
the unified set (disk + kcdx index pak vpaths), create an inconsistency — a
caller using FindFirst/FindNext seeing a disk-only listing while a parallel
ForEachFile caller sees the unified listing?

**Verdict: Outcome A — keeping slot 101 THUNK is CLEAN.** No new slot-14-vs-101
inconsistency for player-visible assets. The slot-101 iterator reimplementation
is a separable later step (owed only when the takeover extends enumeration to
kcdx's index-only paks).

Evidence tier: fresh Ghidra 12.1 (tiers 1–4 lacked the iterator body + the
caller picture). Reused the tier-2 102-slot map
(`_research/phase8.5-pak-resolver/front1-full-vtable-surface.md`) to confirm the
slot binding + factory role. Game `release_1_5_1164953_841`, image base
0x180000000.

## The load-bearing fact — the CCryPakFindData iterator walks the pak dir ITSELF

Slot 101 `FUN_180973294` (vtable +0x328; binding CONFIRMED
`*(0x183A95FA8+0x328) == 0x180973294`) is a pure FACTORY: allocs 0x20, stamps
`CCryPakFindData::vftable` (0x183a646b8), inits a find-handle node, returns an
EMPTY iterator (no `AdjustFileName`, no `_findfirst64`, no path arg).

The iterator's scan/advance is its vftable slot 2 = `FUN_180973220`, which reads
`sys_pakPriority` and drives BOTH arms:

- **Pak arm `FUN_180462af8`** — walks the engine LOADED-PAK vector
  `[this+0x120..+0x128]` (the same vector GetPakInfo/slot 33 enumerates) and each
  matching pak's zip directory (`pak[6]+0x990`, via FUN_180463d04 / FUN_1804635e8).
  **Pak-directory enumeration, NOT disk.**
- **Disk arm `FUN_1809b249c`** — `_wfindfirst64` over the resolved path +
  `CreateFileW` / `GetFinalPathNameByHandleW` per entry.

So FindFirst already enumerates the UNION of engine-pak-dir + disk,
pakPriority-ordered — it is NOT disk-only. Contrast: the engine-original slot 14
`ForEachFile` (`FUN_18241d2e8`) is `_findfirst64`/`_findnext64` DISK-only; kcdx's
KCDX-14 impl is what adds the unified index on top.

## Why THUNK-101 is therefore clean

Because the engine iterator already walks the pak dir, leaving slot 101 THUNK
introduces no NEW slot-14-vs-101 inconsistency for player-visible assets. Both
the THUNK FindFirst and a vanilla ForEachFile see the engine's pak dir + disk.

**The one residual (known scope, not a defect of step 3.3):** a THUNK FindFirst
— like every THUNK file-op — cannot see kcdx's INDEX-ONLY pak vpaths (paks kcdx
mounted into its own unified index but NOT into the engine's `[+0x120]`
loaded-pak vector). That is the read-slot takeover's known scope, owned by the
read-slot cutover, not introduced by flipping the existence/enum slots.
Reimplement the `CCryPakFindData` iterator (a KCDX slot 101) later, when/if the
takeover extends enumeration to index-only paks.

## Call-site picture (slot 101 is reached only virtually)

`FUN_180973294`'s 3 reverse-refs are all `type=DATA` — vtable-slot bindings (the
CryPak vtable +0x328 at 0x183a962d0, plus 0x1847e3678 / 0x1856f17d4 binding the
same factory). There is NO direct `call 0x973294` edge: it is reached only as
`call qword ptr [pCryPak+0x328]`. Read as "no static call edge," not inferred.

## AP19 — unverified, NOT asserted

An exact count of genuine `gEnv->pCryPak->FindFirst` asset-enumeration call sites
is UNVERIFIED. A raw indirect-`+0x328` scan found 382 sites, but +0x328 is shared
by many unrelated game-object/scene vtables — reading bodies showed clear false
positives (`FUN_180423b18` calls +0x328 on a non-CryPak object; `FUN_180b7728c`'s
+0x328 result returns float transforms — a render/scene interface;
`FUN_180c54620`/`FUN_180c54c7c` call +0x40..+0x90 on the returned object, which the
tiny CCryPakFindData vftable does not have). A real count needs per-site
`this`-provenance typing (trace the receiver back to `*(gEnv+0x50)`), not done
this pass — and NOT load-bearing for the verdict, since the iterator-walks-the-
pak-dir fact settles it regardless of how many consumers reach it.

## Artifacts (this dir)

Six worker scripts + raw dumps: `dump_slot101` / `finddata` / `realvt` /
`consumers` / `scanpair` / `realconsumers` (`.java` + matching `_*_dump.txt`).
