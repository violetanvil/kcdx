---
id: KI-0026
opened: 2026-06-16
status: open
commit_at_filing: ba391e0
---

# 0xC8 CryFatalError in NGX/FSR2 graphics-init (C_Game::CreateInstance) — first boot with the fs-takeover metadata/enum slots live

**Status:** open

A boot crash on the first launch carrying the file-system-takeover step-3.3
existence/metadata + enumeration slots live (engine build deployed from the
working tree at `247d295`+`ba391e0`, plus the uncommitted PROBE M mount-slot
instrumentation). The engine raises a `0xC8` CryFatalError (decimal 200) inside
its NGX/FSR2 graphics-init path during `C_Game::CreateInstance` — the same
graphics-init signature as KI-0012 (the mod-mount-list-walk fatal) and the
KI-0019/KI-0006 cross-CRT family. No kcdx frame appears in the fault stack; the
crash is the engine fatal-erroring on data/state, not a direct kcdx code fault.

**Repro:** launch KCD2 (RTX 3080 Ti — NVIDIA, so the NGX/DLSS path is active);
crash ~6s into boot, before the main menu, during graphics init. The launch was
the PROBE M acceptance run (intended to reach gameplay to answer the pak-mount
lifecycle question); it never got past init.

## Trail

| Date       | Action | Result |
|------------|--------|--------|
| 2026-06-16 | Launched the PROBE M build (step-3.3 metadata/enum slots live + 5 mount-slot logging stubs) | Crash ~6s in, during graphics init; minidump + crash bundle written (`crash_2026-06-16_15-10-22.zip`, dmp `kcdx_2026-06-16_15-10-22.dmp`) |
| 2026-06-16 | Read the dump (`cdb -z … -c ".ecxr;!analyze -v;k 40"`) | Exception `0xC8` via `KERNELBASE!RaiseException`; 14-frame stack all WHGame→KingdomCome.exe: `RaiseException` ← `NVSDK_NGX_UpdateFeature+0x871b5a` ← `CreateGameStartup` ← `ffxFsr2ResourceIsNull` (×3) ← `C_Game::CreateInstance` ← KingdomCome.exe thread start. Bucket `APPLICATION_FAULT_c8_WHGame.dll`. No kcdx frame. |
| 2026-06-16 | Checked PROBE M (the new mount-slot stubs) | EXONERATED — all 5 stubs installed (`mount_slot_wrapped` slot=6/7/9/10/100 at 15:10:25) but ZERO `pak_lifecycle_event` fires before the crash; the trampoline never ran. Not the cause. |
| 2026-06-16 | Read the dev-log tail for last kcdx activity before the fatal | `ctor_bracket_complete enabled_n=15` (kcdx replaced ModManager_ctor, MOUNT iterates kcdx's order) → `swap_live_first_open` slot 36 FOpen dispatched into kcdx (`./system.cfg`) → `kcdx_open_first` (kcdx handle minted) → **`loose_open_failed slot=FOpen vpath="engine/config/engine_core.thread_config" errno=2`** → ~430ms later the `0xC8` fatal. |

## Facts

- Exception code is `0xC8` / decimal 200 (`code=200` in the kcdx GUARD log, `0xC8`
  in the dump — same value), raised via `KERNELBASE!RaiseException`, NOT an access
  violation. `rdi=0xC8`, `r14=0xC8` in the fault context.
- The fault stack is entirely engine: `NVSDK_NGX_UpdateFeature` →
  `CreateGameStartup` → `ffxFsr2ResourceIsNull` (×3) → `C_Game::CreateInstance`.
  Graphics init (NGX/FSR2). No kcdx frame.
- This is the FIRST launch with the step-3.3 metadata/enum slots (13/45/67/68/69/
  70/92/93 + 14) live and dispatching during boot. The open+read cutover (3.2,
  `842e5d5`) was accepted live earlier and was clean (KI-0019 clean) — but that
  was before the metadata slots answered engine queries during graphics init.
- The 8 metadata-slot originals were captured at swap time
  (`metadata_originals_captured` at 15:10:25) — the Decision-C miss-thunk wiring
  is present, so an index MISS *should* fall through to the engine original.
- One resolution anomaly immediately precedes the fatal: `loose_open_failed` on
  `engine/config/engine_core.thread_config` (errno=2 / not-found) through kcdx's
  FOpen. Whether this is causal (an engine config/asset graphics-init needs,
  mis-resolved by kcdx) or benign (the engine probing an optional path) is
  UNVERIFIED — a probe target, not asserted.
- PROBE M is in-tree but uncommitted (the mount-slot logging stubs +
  `src/fs_takeover/probe_m_pak_lifecycle.{h,cpp}` + the vtable_swap wiring). It
  did not cause the crash but is part of the deployed build.
- Hardware: RTX 3080 Ti (NVIDIA) — the NGX/DLSS path is active (bugsplat
  `Attributes` GPU Info), consistent with the `NVSDK_NGX_UpdateFeature` frame.

## Open questions

- Is the crash caused by THIS build's kcdx resolution (the metadata/open slots
  mis-serving an engine file graphics-init reads), or does it pre-date the
  step-3.3 metadata slots? (A revert-to-`842e5d5`-DLL relaunch, or a `/debug`
  probe, isolates it.)
- Does graphics init (NGX/FSR2 / `C_Game::CreateInstance`) query a kcdx
  metadata/open slot (IsFileExist / GetFileSize / GetFileAttributes / FOpen) for
  a file that lives in an engine-mounted pak the kcdx index does NOT carry — and
  does kcdx's answer (or the Decision-C miss-thunk fall-through) return the wrong
  result (not-found / size 0) that the engine then fatal-errors on? (The §6 /
  Decision-C pak-long-tail gap — the suspected mechanism, unproven.)
- Is `engine_core.thread_config`'s `loose_open_failed` causal or incidental?
- The `0xC8` is the engine's own fatal-error code — what assertion/condition
  inside the NGX/FSR2 init raises it? (The dump's WHGame frames are unsymbolized;
  the RVA `WHGame+0x871b5a` past `NVSDK_NGX_UpdateFeature` is the raise site.)

## Evidence

- Minidump: `<game-bin>/kcdx-engine/logs/kcdx_2026-06-16_15-10-22.dmp` (60 MB).
- Crash bundle: `<game-bin>/kcdx-engine/logs/crash/crash_2026-06-16_15-10-22.zip`
  (dev log + per-plugin logs + dmp + `bugsplat_C92T3LA2.log` + `game/kcd.log`).
- Dev log: `<game-bin>/kcdx-engine/logs/kcdx-dev_2026-06-16_15-10-22.log`.
- Static recon context: `_research/fs-takeover-pak-mount-recon/FINDINGS.md`
  (the pak-mgmt slot model), KI-0012 (the mod-mount-list graphics-init fatal),
  KI-0019 (the cross-CRT FSR2/DLSS-init crash family).
