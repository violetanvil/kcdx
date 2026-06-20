# KI-0026 — the 0xC8 raise site is CSystem::FatalError, not an NGX/FSR2 assertion (2026-06-19)

Static recon (no launch) of the `0xC8` raise site for
[`KI-0026`](../../docs/known-issues/KI-0026-fs-takeover-metadata-slots-graphics-init-fatal.md).
Reuses the `pefile + capstone` pattern from `ki0012-modmanager-size-recon/`.
The reusable wiring: derive the real raise-site RVA from the dump (not the GUARD
decimal), then read the raising function from the binary.

## Deriving the real RVA from the dump (the method — reuse this)

The GUARD log's `WHGame.DLL rva=38115914` (=`0x245F2CA`) is NOT the raise site —
disassembling there shows a destructor/teardown sweep. The raise site comes from
the **dump stack frames + module base**, not the GUARD decimal:

```
cdb -z <dmp> -c ".ecxr; kn 12; lmDvm WHGame; q"
  WHGame base                  = 0x7fffc7850000
  frame 01 return (after the   = 0x7fffc9ca9a4a  -> RVA 0x2459a4a
    call RaiseException)         (the `call r9` raising instr is just before)
  frame 02 return              = 0x7fffc9305387  -> RVA 0x1ab5387 (= CreateGameStartup+0xda687, a cookie epilogue)
```

WHGame has no PDB → cdb labels every address by the **nearest export**. The
`ffxFsr2ResourceIsNull+…` and `NVSDK_NGX_UpdateFeature+…` frame names are
nearest-export NOISE across a large span of unrelated CrySystem code — NOT
evidence the fault is in NGX/FSR2.

## The raising function = CSystem::FatalError (CryFatalError), RVA 0x2459810

`find_func_start` (int3-padding boundary) → function start `0x2459810`. Read
start→raise. It is CryEngine's fatal-error reporter:

- **`rsi` = the `CSystem`/`ISystem` object** (`[rsi+0x5a8]` = a report-once latch
  set to 1 on entry; `[rsi+0x498]`, `[rsi+0x170]` getters/dispatch called).
- **`edx` (saved to `ebx`, then `[rip+0x3022225]`) = the error id.**
- Builds the message via repeated `FUN_1804d4510(rcx=r14, edx=<msg-id>,
  r8=<string ptr>, …)` for ids `0xf6b 0xf6c 0xf6d 0xf6f 0xf74 0xf78`.
- `r14d = 0xC8` (`mov r14d,0xc8` at `0x2459967`) threaded as the code; the raise
  is `0x2459a47 call r9` where `r9 = [[handler]+0x58]` (the fatal-handler vtable
  method), which internally calls `KERNELBASE!RaiseException(0xC8)` (frame 01).

`0xC8` (=200) is **CryEngine's CryFatalError exception code**, not an NGX code.

## The message strings name a CONFIG / SYSTEM-INIT failure (read from .rdata)

The string pointers the reporter emits resolve to the CrySystem fatal/config
family — `read_assert_strings.py`:

- category/file: `d:\…\CryEngine\CrySystem\System.cpp`
- `<CrySystem> Last System Error: %s`
- neighbors in the same table: `*ERROR`, `- Fatal Error`,
  **`Config file '%s' not found!`**, **`Couldn't get length for Config file '%s'`**,
  `'%s' -> invalid configuration`, `Loading config file '%s' (%s)`,
  `ISystem` / `CrySystem` / `GameDir: %s`.

This is the **config-file-load / system-init error family** — exactly the
operation the dev log shows failing right before the fatal
(`loose_open_failed vpath="engine/config/engine_core.thread_config" errno=2`,
plus the `system.cfg` / `pak.cfg` reads).

## What this changes for KI-0026

- The `0xC8` is **CSystem::FatalError aborting on a system-init/config failure**,
  reached from `C_Game::CreateInstance` — NOT an FSR2/NGX-internal null-resource
  abort. The earlier "deliberate NGX null-resource abort" reading was built on
  the nearest-export frame names; the binary overturns it.
- This is consistent with PROBE G (the swap-WRITE is the trigger) and PROBE N
  (kcdx's open produces byte-identical object state): the swap-write leaves the
  engine in a state where a **later config/system-init step fails**, and
  CSystem::FatalError raises `0xC8` on that failure.
- **P-live is now SAFE and kcdx-shaped:** hook `CSystem::FatalError` (RVA
  0x2459810) — a CryEngine function, not an NGX RVA — at the boot window, capture
  `edx` (error id) + `rsi` + the formatted message (or the resolved args). The
  message NAMES the failing resource directly. No NGX-internal hook, no guessing.

## Files

- `disasm_ngx_raise_site.py` — derives the func start, disassembles start→raise.
- `read_assert_strings.py` — reads the .rdata message strings.
- `_raise_site.txt`, `_raise_tail.txt`, `_assert_strings.txt` — raw output.
