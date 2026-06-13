# Pilot — perk_unlocked, pure-static fire-site walk

Front 3 left the perk fire-site "one caller-hop unread". This front walked that
hop via a pure static read (no game launch). Three theory-independent bridges to
"the function that grants/unlocks a perk", each READ in the body, none assumed.

**VERDICT: NEEDS-LIVE-CORRELATION** (lead remains STATIC, materially narrowed).
The static read FOUND the C++ perk-collection insert + its change-notify, and
captured its address + ABI — but it cannot statically prove that THIS path is the
one a `kcdx.on("perk_unlocked")` hook must target, because the grant is reached
through a **vtable-dispatched, RTTR-effect-driven** layer whose runtime binding is
not statically pinned. The bridge that would settle it (the `C_AddPerkEffect` /
`storm::addPerk` effect → this insert) is RTTR-reflected, runtime-bound; the
interned event-key strings have ZERO static fire-site consumer. One game-trigger
correlation closes it.

Artifacts (this dir): `WalkPerkGrant.java` (the walk script), `_walk_perk_grant.txt`
(raw Ghidra output), `_abi_182cee04c.txt` + `_abi_18046b704.txt` (abi_walker raw).

---

## BRIDGE A — the `AddPerk` Lua-command C++ impl (the hop front 3 left unread)

front 3 read `FUN_181675cdc` registering `FUN_180a52c20(param_1,"AddPerk","perk_id",param_1,&local_res8)`
with `local_res8 := FUN_182cee04c` set immediately before (event-anchors-recon.txt
line 523-525). So **`FUN_182cee04c` is the C++ implementation bound to the Lua
`AddPerk` command** — the hop. Read its body (`_walk_perk_grant.txt` L54-87):

- `lVar2 = FUN_18041ebc4(param_2)` — resolves the RPG-state object from arg2.
  — call site `0x182cee073 → call 0x18041ebc4`, result in `rsi`.
- `cVar1 = FUN_1807207ac(param_3, &local_28)` — parses arg3 as a 16-byte **GUID**:
  its body runs `sscanf(...,"%8x-%4hx-%4hx-%2hhx%2hhx-%2hhx%2hhx%2hhx%2hhx%2hhx%2hhx",...)`
  and returns `iVar6 == 0xb` (11 fields). — `_walk_perk_grant.txt` L165-170. So the
  Lua `AddPerk("perk_id")` arg is a perk GUID string.
- **the grant call:** `FUN_18046b704(lVar2 + 0x758, &parsedGUID, 0)`
  — `0x182cee0a1: lea rcx,[rsi+0x758]` ; `0x182cee0b1: lea rdx,[rsp+0x20]` (the GUID) ;
  `0x182cee0a8: xor r8d,r8d` ; `0x182cee0b6: call 0x18046b704` (`_abi_182cee04c.txt`).
- then `call [*param_2 + 0x58]` — the Lua-command return path (vtable slot 0x58),
  taken on BOTH the success and the early-out branch.

**`FUN_182cee04c` ABI (abi_walker + body):** `__fastcall void FUN_182cee04c(void* luaCtx /*rcx, unused*/, void* rpgStateObj /*rdx*/, const char* perkGuidStr /*r8*/)`. arg-home accesses: arg2(rdx)+arg3(r8) used; arg1(rcx) unused; returns via `[rpgStateObj+0x58]` vtable. NOT a hookable "perk granted" notify — it is the *Lua command entry*, one of (likely several) callers of the real insert below.

### The real grant: `FUN_18046b704` — perk-collection insert + change-notify

Read its body (`_walk_perk_grant.txt` L181-240; disasm `_abi_18046b704.txt`):
`undefined1 FUN_18046b704(longlong collection /*rcx = rpgState+0x758*/, GUID* key /*rdx*/, undefined8 param_3 /*r8*/)`:

- `FUN_181fca870(&local_58, collection+0x28, key)` — looks the GUID up in the set.
- `if (local_58 == *(collection+0x30))` — **the "not already present" branch**:
  allocate a node (`FUN_18046b4ec`), store the GUID (`plVar5[2]=lo; plVar5[3]=hi`),
  insert it. i.e. **add-perk-if-absent**, GUID-keyed. — L201-219.
- **the notify fire** (L228-234, disasm `0x18046b7de`-`0x18046b804`):
  ```
  test byte ptr [rsi+0x60], 2     ; only if flag bit 1 is CLEAR
  jne  skip
  rax = [rsi+0x48]; rcx = [rax-8] ; resolve a delegate/listener object
  call [ [rcx] + 0x18 ]           ; predicate "should notify?"
  test al,al; je skip
  rdx = r15 (param_3) ; call [ [rcx] + 8 ]   ; THE notify: vtable slot +8
  ```
  This is the perk-collection's **on-changed virtual callback** — slot `+8` on the
  delegate at `[[rpgState+0x758]+0x48]-8`, fired once on a genuinely-new perk.

So a real C++ "perk added" notify EXISTS and is statically located:
`FUN_18046b704 @ RVA 0x46b704` (module-relative; file VA `0x18046b704`, image base
`0x180000000`), the notify being its `call [vtable+8]` on the delegate. But the
notify TARGET is a vtable slot on a runtime-bound delegate object — not a fixed fn.

---

## BRIDGE B — RTTR `storm::addPerk` / `*PerkEffect` are STRINGS, not functions

Symbol scan over 1,945,262 symbols (`_walk_perk_grant.txt` L263-267): every
perk-grant-shaped name is a **`s_` RTTI/RTTR string DATA symbol, ZERO are functions**:
- `s_wh::rpgmodule::storm::addPerk_183b78df8` @ `183b78df8` — (no fn)
- `s_AddPerk_183d38828`, `s_LearnPerk_18472c4f8` — (no fn)
- `s_AlcoholPerkBacchusHangoverEffect_...` — (no fn)

Confirms front 3: `storm::addPerk`, `C_AddPerkEffect`, `C_LearnPerkEffect` are
RTTR effect TYPE-NAME strings (reflection metadata), **not a statically-named
grant function**. There is no `storm::addPerk` symbol to resolve to an address.
The effect→insert binding is RTTR-reflected and runtime-dispatched.

## BRIDGE C — interned event-key constants have NO static fire-site consumer

The interned `std::string` keys returned by the getters
(`&DAT_1855e3888`=AddPerk, `&DAT_1855e3890`=LearnPerk, `&DAT_1855e38a0`=PerkUsed)
are referenced statically ONLY by (a) their own getter stub and (b) an `atexit`
destructor thunk — NOTHING else (`_walk_perk_grant.txt` L272-285):
```
DAT AddPerk-key  @1855e3888: from 1808fd0b7/0df (getter FUN_1808fd090), 18217ed50 (atexit, no fn)
DAT LearnPerk-key@1855e3890: from 182cac09f/0c3 (getter FUN_182cac05c), 18217edd0 (atexit, no fn)
DAT PerkUsed-key @1855e38a0: from 181585273/29b (getter FUN_18158524c), 18217ee00 (atexit, no fn)
```
So the fire-site does NOT reference the key DAT directly — a consumer obtains the
key by **calling the getter at runtime** (`FUN_182cac05c()` returns the LearnPerk
string), then matches/dispatches on it. Walking the constant statically does not
reach the fire-site; the consumer binding is runtime. This is exactly why the
constant-walk "one more hop" front 3 deferred does not statically terminate.

---

## Why NEEDS-LIVE-CORRELATION (the static bridge's exact failure)

The static read DID find a real C++ perk-add + change-notify (`FUN_18046b704`),
narrowing the lead hard. It is NEEDS-LIVE, not STATIC-FINDABLE, because two static
gaps remain and neither is closable without a running observation:

1. **The hook target is a vtable-dispatched notify on a runtime-bound delegate**
   (`call [ [[rpgState+0x758]+0x48]-8 ][+8]`), not a flat C-ABI grant function. A
   `kcdx.on("perk_unlocked")` C++ hook needs a fixed fire fn / slot; the delegate
   object + its vtable are bound at runtime, unpinned statically.
2. **`FUN_182cee04c` is the LUA-COMMAND path only.** Whether a perk granted by the
   game's own progression (quest reward, level-up, the RTTR `C_AddPerkEffect`
   effect) flows through `FUN_18046b704` — vs a different insert path — is NOT
   statically provable: the effect→insert binding is RTTR-reflected (bridge B). A
   hook on `FUN_18046b704` might catch only Lua-driven adds, or all adds; only a
   live trigger (unlock a perk via normal play, watch whether `FUN_18046b704`
   fires) tells which.

The minimal closing probe is a one-launch correlation: hook `FUN_18046b704` entry,
log on fire, trigger a perk unlock in-game (Lua `AddPerk` AND a natural
progression unlock), observe whether both reach it. That promotes this to
STATIC-FINDABLE with `FUN_18046b704` as the confirmed anchor.

## AP18 seed-row candidates (NOT yet proposed — gated on the live correlation)

No seed row proposed this pass (NEEDS-LIVE, per the no-invented-address bar). When
the live correlation confirms the path, the candidate is:
- `perk_grant_insert -> bool FUN_18046b704(void* perkCollection /*rpgState+0x758*/, GUID* perkId, void* ctx)` @ RVA `0x46b704`, the perk-add-if-absent + on-changed notify. ABI captured (`_abi_18046b704.txt`): `__fastcall`, arg1=rcx collection, arg2=rdx GUID*, arg3=r8 ctx, returns bool (added?).
- Supporting: `FUN_182cee04c` @ RVA `0x2cee04c` = the Lua `AddPerk(guidStr)` command impl (`__fastcall(luaCtx, rpgStateObj, perkGuidStr)`), if a Lua-command anchor is also wanted.

These are RVAs read from the binary, ABI from `abi_walker` — but the anchor SELECTION (which is the right `perk_unlocked` fire-site) is the unproven part the live correlation settles. Recording either as a seed row before that correlation would assert an unverified anchor (AP2/AP18).
