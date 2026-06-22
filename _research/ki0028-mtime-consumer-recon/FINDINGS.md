# KI-0028 — slot-66 (FGetModificationTime / pak mtime=0) consumer recon — DIVERGENCE D

**Date:** 2026-06-22 · **Trust:** primary evidence (binary xref + body reads).
**Verdict: DIVERGENCE D (pak mtime = 0) is FALSIFIED as a DIRECT no-present-wedge driver.**

## Question

KI-0028 slot-diff found kcdx's slot 66 (FGetModificationTime, vtable +0x210) returns **0** (epoch)
for a pak-resident asset, where the engine original returns the pak ENTRY's DOS timestamp. The
candidate fear: a boot CACHE-FRESHNESS check comparing a source mtime against a cached mtime mis-fires
on mtime=0 (treats the cache as always-stale -> a rebuild that never settles -> the PROBE-M
"handshake that never completes" / no-present wedge). Does such a consumer exist?

## Method (reuse-first)

Reused the proven drift-free instrument from `ki0028-metadata-consumer-recon/` (linear capstone DRIFTS
on WHGame - not used). Byte-scanned the **680** `mov r64,[rip]` loads of the pCryPak global
**0x18492B850** (= gEnv 0x18492B800 + 0x50), then correlated each to `mov vt,[reg]; call [vt+0x210]`
(or a direct `call [reg+0x210]`) - call edge AND receiver==pCryPak both read, AP19-clean. Then, because
mtime could be reached NOT through the vtable, byte-scanned the whole `.text` for DIRECT references
(rel32 `call`/`jmp`, address-taken `mov`/`lea [rip->body]`) to the **engine FGetModificationTime body**.
Scripts here: `correlate_mtime_slot.py`, `dump_func.py`, `read_vtable_slot.py`, `find_direct_callers.py`.

- CCryPak vtable VA `0x183A95FA8` (RTTI `.?AVCCryPak@@`, from `fs-takeover-pak-mount-recon`); slot 66 = +0x210.
- Engine FGetModificationTime body (read from `*(vtable+0x210)`): **VA 0x18241a3bc (RVA 0x241a3bc)**.

## Result — consumer count: 3 (all provenance-verified). Direct (non-vtable) callers of the body: 0.

### Engine original's return — CONFIRMED (no longer inferred)

Body `0x241a3bc` (read at `_engine_body.txt`): indexes the pak entry (`jae 0x241a432` is the
NOT-FOUND-in-pak arm). The **found-in-pak arm** (`0x241a410`) loads the entry record
(`[rcx+rax*8+8]` -> `[rcx+0x28]`, the entry's stored timestamp field), passes it through a conversion
(`call 0x180eb72b0`), and returns it in `rax`. The not-found arm (`0x241a432`+) runs the OS file-time
path (`call [rip+...]` x3 = GetFileAttributesEx -> FileTimeToDosDateTime-style, writing a DOS
date/time pair onto the stack at `[rsp+0x48/0x4c]`) and returns that. **So the engine returns the pak
entry's DOS timestamp for a pak-resident asset - divergence D's premise is confirmed: kcdx returns 0
where the engine returns the entry DOS time.**

### The 3 consumers — each body-read; NONE compares the mtime or gates on it

1. **`0x9a2074` -> call `0x9a20ec` (`direct-pcrypak`).** A boot subsystem-pointer **cache-assembly**
   function: a straight SEQUENCE of vtable getter calls on pCryPak (slots 0x220, **0x210**, 0x2d0,
   0x258, 0x268, ...), each stashing its return into adjacent file-scope globals. The slot-66 return at
   `0x9a20ec` is stored to the global at `0x9a2120` (`mov [rip+...],rax`, global VA `0x185497880`).
   **No `cmp`, no `sub`, no branch on the value** - it is cached, never tested. A whole-`.text` scan for
   any read of that stash global found **0** consumers (`_consumer1_freshness_check.txt`): the value is
   written and (within this evidence) never read back as a freshness input. NOT a cache-freshness gate.

2. **`0x14d5580` -> call `0x14d5619` (`vt<-pcrypak`).** The slot-66 return `rax` is immediately used as
   an **object `this`** for the very next virtual call (`mov rcx,[rax]; mov r10,[rcx]; call r10` at
   `0x14d563d`). The return is dereferenced as an interface pointer, not compared as a timestamp -
   i.e. at this site the `[+0x210]` slot is being used as an object-returning method, not a mtime
   compare. **No freshness gate, no timestamp branch.**

3. **`0x235d7e4` -> call `0x235d8df` (`vt<-pcrypak`).** The slot-66 return `rax` -> `rbx`, then passed
   **forward as an argument** (`r8`) to the next call `[rdx+0x1b8]` (`0x235d8f8`). The return is a
   handle/pointer threaded into a subsequent call. **No compare, no branch, no cache gate.**

### No window / swapchain / present / display-mode consumer

None of the 3 consumers references a swapchain / `r_Fullscreen` / DXGI / present / display-mode loop or
the window-mgr singleton cluster `0x492b8xx` (where the PROBE-M loop `0x869c39` lives). Consumer 1 is a
boot interface-pointer cache; consumers 2/3 use the slot return as an object/handle, not a mtime.
**No cache-freshness check and no boot/render/present consumer reads slot 66.**

## Consequence

The pak-mtime=0 divergence cannot DIRECTLY drive the no-present wedge: there is no consumer that
compares the returned mtime against another timestamp, branches on it, or uses it as a
cache-freshness/rebuild gate - and no window/present consumer reads it at all. The feared
"mtime=0 -> cache always stale -> never-settling rebuild" shape **does not exist in WHGame's consumer set**.
Divergence D joins A (existence-timing) and B (find-handle straddle) as FALSIFIED direct drivers.

**Residual (NOT statically traceable, low-likelihood):** all three consumers consume the slot-66 return
as a value/pointer they store or forward; if any DOWNSTREAM holder later diffs that stored value against
a fresh stat (multi-hop, in a function the value is passed into), a 0 could read as "older than any real
file." Nothing in the 3 direct consumers does this, and there are 0 non-vtable callers, so the surface
for it is small. Closing it would need a live swap-on/off probe watching the consumer-1 cache globals -
not more static work. Static evidence points to FALSIFIED.

## Reuse

- pCryPak global `0x18492B850` (gEnv `0x18492B800` + 0x50); 680 loads. CCryPak vtable `0x183A95FA8`.
- Slot 66 FGetModificationTime: vtable +0x210; **engine body RVA 0x241a3bc** (returns pak-entry DOS
  time on the in-pak arm, OS file-time on the miss arm). kcdx's swap returns 0 on the in-pak arm.
- The byte-scan correlate + direct-caller scripts here are the reusable slot-66 xref instrument.
