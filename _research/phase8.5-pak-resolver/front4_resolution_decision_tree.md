# FRONT 4 — the COMPLETE vpath→bytes resolution decision tree (assembled)

Captured 2026-06-02. ASSEMBLY of the already-decompiled pieces into ONE authoritative
model of the engine's `CCryPak` asset resolution. Trust level: PRIMARY EVIDENCE — every
node cites a decompile/disasm line in the prior tier-2 dumps; NO fresh Ghidra was needed
(the reuse ladder answered every node at tier 2). The one structural fact this front
ADDS — the alias-entry layout — is read directly off the decompiled `FUN_180462664` body
(`_subresolver_leaves.txt` 961-1019), not inferred (AP2). Slots binary-read (AP3).

Sources assembled (do NOT re-walk):
- `subresolver-decompiled-mechanism.md` + `_subresolver_decomp.txt` (slot-1 resolver, leaf opener, pak-membership)
- `searchpath-registrar-mechanism2.md` (the per-entry pakPriority existence-test table, AddMod registrar)
- `_subresolver_leaves.txt` (disk leaf `FUN_1819c9cb4`, mode-3 gate `FUN_18241ad60`, alias walker `FUN_180462664`)
- `_subresolver_findings.md` (flag-bit map), `FINDINGS.md` (FOpen/gEnv/vtable)

---

## The node map (RVA + role, all VERIFIED-decompiled)

| Node | RVA | vtable slot | Role |
|---|---|---|---|
| `CCryPak::FOpen` | 0x4614A0 | 36 (+0x120) | Open-by-path entry. Parses mode→flags, calls slot-1 to get the resolved concrete path, then opens it. (kcdx_id 131) |
| `CCryPak::AdjustFileName` resolver `FUN_18046205c` | 0x6205C | 1 (+0x8) | **The decision root.** Search-path precedence loop + pakPriority gating; returns the resolved path. |
| Leaf normalizer `FUN_1804621bc` | 0x621BC | — | `AdjustFileName` core: root-prefix logic + alias substitution + `%user%` branch. Builds the candidate path. |
| Alias walker `FUN_180462664` | 0x462664 | — | Prefix-substitution table walk over `[this+0x1b0..0x1b8]`. |
| PAK-membership `FUN_1804631f0` | 0x631F0 | — | Binary-searches the loaded-pak directory index (`[this+0x120..0x128]`); thread-locked. Returns pak entry or 0. NEVER touches loose disk. |
| DISK existence `FUN_1819c9cb4` | 0x9C9CB4 | — | `(*(*pCryPak+0x228))(pCryPak,path) != -1` — OS file-attribute query. |
| Mode-3 gate `FUN_18241ad60` | 0x241AD60 | — | Builds `"mods/"` + `"c3level/"` prefixes, `_strnicmp`s the path. The `sys_pakPriority==3` special case. |
| Search-path registrar `CCryPak::AddMod` `FUN_1819AF1A8` | 0x19AF1A8 | 19 (+0x98) | push_back a root onto the `[this+0x198..0x1a0]` vector (the loop iterates it). RemoveMod=slot20, GetMod=slot21. |

Struct field anchors on `this` (CCryPak): `+0x188` = game DATA root CryStringT (also `this[0x31]`); `this[0x4a]` = second recognized root; `+0x198/+0x1a0/+0x1a8` = search-path vector begin/end/cap; `+0x1b0/+0x1b8` = alias-table begin/end; `+0x228` = the object whose `+0x20` int is `sys_pakPriority`'s iValue, and whose vtable `+0x228` is the disk-attr query.

---

## The flag-bit decoder (cited `_subresolver_findings.md` §flag tests)

| Bit | Mask | Tested in | Meaning |
|---|---|---|---|
| 16 | 0x10000 | resolver + leaf | **Loose/search-path arm enabler.** Set per-entry in the loop (`BTS R9D,0x10` @0x180462123). In the leaf: if set, the data-root re-prefix is SKIPPED and a leading `./`,`.\` is stripped (path treated as already-data-root-relative). |
| 23 | 0x800000 | resolver + leaf | User-dir / "skip search-path" combined gate. In resolver `TEST EBP,0x810000` skips the loop. In leaf, set → the `%user%\` prefix branch. |
| 21 | 0x200000 | leaf | "Already fully adjusted" — skip ALL root logic, copy path verbatim. |
| 18 | 0x40000 | leaf | Append trailing `\` (directory form). |
| 28 | 0x10000000 | resolver | Per-entry: force the DISK arm regardless of pakPriority (`BT EBP,0x1c` @0x180462143). |
| 24 | 0x1000000 | resolver | OR'd in when global `DAT_184927274 > 0` (tools/editor present; =0 in this build). |

The resolver tests NEITHER 0x4 NOR 0x2 — those live in FOpen's own arm-selection, upstream.

---

## THE DECISION TREE — given (vpath `pName`, `nFlags`, current `sys_pakPriority` mode)

### Stage 0 — FOpen (slot 36, 0x4614A0)
1. `strlen(pName)`; if `len > 0x7FF` → return 0 (the 2048-byte CryEngine path cap).
2. Parse `szMode` chars → set open-flag bits (`a`,`+` → 0x8000/0x800000…).
3. `path = slot1_resolver(this, pName, outBuf, computedFlags)`.  ← Stage 1
4. Open `path` (as a pak handle or an OS handle, per what the resolver returned).

### Stage 1 — slot-1 resolver `FUN_18046205c` (0x6205C) — the precedence root
```
if (DAT_184927274 > 0) nFlags |= 0x1000000;          // editor-present (=0 here)

// GATE: run the search-path loop only if ALL hold:
if ( (nFlags & 0x810000) == 0                          // neither 0x10000 nor 0x800000 preset
     && searchPathVector nonempty ([+0x198] != [+0x1a0])
     && pName[0] != '%' ) {

  // Iterate search-path entries BACKWARDS (last-registered wins):
  for (e = [+0x1a0]; e != [+0x198]; e -= 8) {
     built = CryStringT(*(e-8)) + "/" + pName;          // <entry>/<vpath>
     cand  = FUN_1804621bc(this, built, outBuf, nFlags | 0x10000);   // Stage 2 (loose arm)

     // mode-3 pre-gate: at pakPriority 3, skip entry unless mods/-prefix gate passes
     if ( mode==3 && !FUN_18241ad60(cand) ) { continue; }

     if ( (nFlags & 0x10000000) == 0 ) {                // normal (not force-disk)
        switch (mode = *(*(this+0x228)+0x20)) {          // sys_pakPriority
          case 0:  if (FUN_1819c9cb4(cand)) return cand;  // DISK first
                   else if (FUN_1804631f0(cand)) return cand;   // then PAK
                   break;
          case 1:  if (FUN_1804631f0(cand)) return cand;  // PAK first
                   else if (FUN_1819c9cb4(cand)) return cand;   // then DISK
                   break;
          case 2:  if (FUN_1804631f0(cand)) return cand;  // PAK ONLY — no disk fallback
                   break;
          case 3:  if (FUN_1819c9cb4(cand)) return cand;  // (post mods/-gate) DISK
                   break;
        }
     } else {                                            // force-disk (bit 28)
        if (FUN_1819c9cb4(cand)) return cand;
     }
     // miss on this entry → free + next entry
  }
}

// FALLTHROUGH (loop skipped, or no entry hit):
return FUN_1804621bc(this, pName, outBuf, nFlags);        // Stage 2 with ORIGINAL flags
                                                          // (data-root prefixing, NO search-path)
```

**The per-entry existence-test table (the load-bearing precedence answer, cited `searchpath-registrar-mechanism2.md` + `_subresolver_decomp.txt` disasm 0x18046213d-…):**

| `sys_pakPriority` | per-entry test order | does a LOOSE file in a registered dir resolve? |
|---|---|---|
| 0 | DISK then PAK | YES |
| 1 | PAK then DISK | YES (after pak miss) |
| **2 (published default)** | **PAK ONLY**, no disk fallback in the loop | **NO** |
| 3 | (mods/-prefix gate) then DISK | only mods/-prefixed |

Caveat that survives the table: bit 28 (`0x10000000`) on the open forces DISK for an entry at ANY mode; static caller scan did not show common asset opens setting it (NEEDS-LIVE-CONFIRM which asset classes, if any, do).

### Stage 2 — leaf normalizer `FUN_1804621bc` (0x621BC) — build the candidate path
Operates on a 2048-byte local buffer `buf`; returns the normalized concrete path in `outBuf`.
```
copy pName→buf (cap 0x7FE);

if ( (nFlags & 0x800000) == 0 ) {                  // NOT the user-dir branch
   FUN_1804625d0(buf, ~(nFlags>>24)&1);  FUN_1804627a0(buf);   // case-normalize / cleanup
} else {                                           // 0x800000 user-dir branch
   if (this[0x231]==0 && pName[0]!='%' && pName[0]!=0 && pName[1]!=':')
        buf = "%user%\\" + pName;                  // cosave / user path — NOT assets
   FUN_1804625d0(buf, ...);
}

aliasHit = FUN_180462664(this, buf);               // Stage 2a — alias substitution (in place)

if ( (nFlags & 0x200000) == 0 ) {                  // NOT "already adjusted"
   existsFlag = (*(*this+0x198))(this, buf);        // a quick membership pre-check (vtable +0x198)

   if ( (nFlags & 0x10000) && (buf starts "./" || ".\\") )
        strip the leading "./";                     // already-data-root-relative

   // THE ROOT-PREFIX DECISION:
   if ( existsFlag==0 && (nFlags & 0x10000)==0 && aliasHit==0 ) {
      if ( buf starts "languages\\"                  // recognized roots → leave as-is
           || startsWith(buf, this[0x31] /*data root*/)
           || buf starts ".\\" || "..\\" || "editor\\" || "mods\\"
           || startsWith(buf, this[0x4a]) || buf starts "engine\\" ) {
          // already rooted: (the ".\\" case strips "./", else leave)
      } else {
          // UNROOTED + loose-arm-not-forced → PREPEND THE DATA ROOT this[0x31]:
          memmove(buf up by rootLen); memcpy(buf, dataRoot, rootLen);   // buf = "<Data>/" + buf
      }
   }
   copy buf→outBuf;
   if ( (nFlags & 0x40000) && outBuf nonempty && last!='\\' ) append '\\';
} else {
   copy buf→outBuf verbatim;                         // 0x200000: no root logic at all
}
return outBuf;
```

### Stage 2a — alias walker `FUN_180462664` (0x462664) — prefix substitution (NEW structural fact)
Walks the alias-table vector `[this+0x1b0 .. this+0x1b8]`. **Each element is a pointer to an
alias struct** with this layout (read off the decompiled body, `_subresolver_leaves.txt` 978-1012):

```
struct AliasEntry {            // *puVar8 points here (puVar3 in decomp)
   char* from;     // +0x00  the prefix to match (decomp: *puVar3, (char*)*puVar3)
   int   fromLen;  // +0x08  length of `from` (decomp: *(int*)(puVar3+1))
   char* to;       // +0x10  the replacement prefix (decomp: puVar3[2])
   int   toLen;    // +0x18  length of `to`     (decomp: *(int*)(puVar3+3))
};
```
Logic: for each entry, if `path[0]==from[0]` and `path` starts with `from` (case-sensitive char compare, `fromLen` chars) AND the char after the matched prefix is `'\\'`, then: copy `to` (toLen bytes) into a temp buffer, append the remainder of `path` after the matched `from`, `memcpy` the result (0x800 bytes) back over `path`, and return 1 (hit). Else return 0. **It is a prefix-SUBSTITUTION table (`from\` → `to\`), NOT an additive search path** — confirms `searchpath-registrar-mechanism2.md`'s claim. First matching entry wins; substitution mutates the path buffer in place.

### Stage leaves — the two existence primitives
- **PAK** `FUN_1804631f0` (0x631F0): acquires the pak SRW lock (`+0x100`, recursive via thread-id `+0x10c`/count `+0x108`), walks the loaded-pak array `[this+0x128]` down to `[this+0x120]` (stride 0x38), per pak does a `memcmp` of the pak's bound-root against the path then a binary search of the pak's directory index (the `(>>1)`-bisection loops). Returns the pak file-entry ptr (and writes the owning pak to `*param_4`) or 0. Pure pak; never the OS filesystem.
- **DISK** `FUN_1819c9cb4` (0x9C9CB4, 35 B): `return (*(*pCryPak+0x228))(pCryPak, path) != -1;` — the OS attribute query (`pCryPak = DAT_18492b850 = gEnv+0x50`). Loose-file-exists.

---

## Where each path RETURNS (the "which bytes" answer)

- **Search-path entry hit** (loop) → resolver returns `<entry>/<vpath>` resolved either to a PAK entry (modes 1/2/3, or 0 after disk-miss) or to a LOOSE disk path (mode 0, or bit-28-forced). FOpen opens that.
- **No entry / loop skipped** → resolver returns the FALLTHROUGH candidate: `pName` normalized + data-root-prefixed (Stage 2 with original flags, NO `0x10000`). At pakPriority 2 this is then opened pak-first by FOpen's own logic.
- **Alias hit** rewrites the prefix BEFORE either of the above (affects what gets pak/disk-checked).
- **`0x200000`** → verbatim path, no rooting (used when the caller already adjusted).
- **`0x800000`** → `%user%\`-rooted (cosave/user, not assets).

---

## Contribution to the 4 synthesis questions (Front 4 = the reference design)

**(1) Salvageable / reusable.** The whole `this`-struct shape is reusable as kcdx's resolver model: the search-path vector (`+0x198`), the alias table (`+0x1b0`), the loaded-pak array (`+0x120`), the data root (`+0x188`), the pakPriority cvar slot (`+0x228 → +0x20`). The PAK-membership leaf (`FUN_1804631f0`) and DISK leaf (`FUN_1819c9cb4`) are clean, single-purpose primitives kcdx's own resolver can mirror (binary-searched pak dir-index; OS attr query). The alias table is a ready-made prefix-redirect mechanism. AddMod (slot 19) is a reusable "register a root" entry IF kcdx keeps the search-path vector.

**(2) Full-replace vs partial.** The decision tree is small and fully mapped — a kcdx-owned resolver can REPLACE `FUN_18046205c` (slot-1) wholesale: kcdx implements the loop + mode gating + fallthrough to its own spec, while still consulting the engine's pak array + disk leaf for stock content. Full-replace of slot-1 is the clean seam (it is the single chokepoint every FOpen routes through, and it returns a path string, not a handle — so replacing it does not touch handle lifetime). FOpen (slot 36) need NOT be replaced — hooking slot-1 alone captures all resolution. Partial (keep engine slot-1, hook around it) is the mechanism-1 per-open redirect already proven for `.dds`; full-replace is the path to OWN resolution to kcdx's spec.

**(3) Exactly what to hook vs replace.** REPLACE: slot-1 `FUN_18046205c` (the decision root) — this is where "vpath → which bytes" is decided. Within a kcdx slot-1, REUSE (call through to) the engine's `FUN_1804631f0` (pak membership) and `FUN_1819c9cb4` (disk exists) so stock paks/loose still resolve. Do NOT need to hook FOpen, FWrite, FClose, AddMod, or the alias walker — they are all downstream of or orthogonal to the decision. The data-root/recognized-root prefix logic in `FUN_1804621bc` is the normalization kcdx's resolver must reproduce (or call the engine's leaf for) so stock vpaths still root correctly.

**(4) How a kcdx resolver still loads a STOCK Nexus/Workshop pak mod unchanged.** A stock pak mod is loaded into the engine's pak array (`[this+0x120..0x128]`) by the engine's mod-loading path (or kcdx's init-cycle takeover, which already walks Workshop+mods). A kcdx-owned slot-1 that, on a miss in kcdx's own overlay map, FALLS THROUGH to call the engine's `FUN_1804631f0` (pak membership over that same array) resolves every stock-pak asset exactly as the engine does today — the kcdx resolver is a SUPERSET: it checks kcdx overlays first, then defers to the engine's pak array for everything else. Because stock pak mods register their content INTO the pak array (not via loose files), and `FUN_1804631f0` is mode-agnostic about WHERE a pak came from, a kcdx resolver that consults that array is automatically compatible with Nexus/Workshop paks. The `sys_pakPriority 2` default (pak-first) means kcdx must inject its overlay check ABOVE the pak membership test to win, which is exactly the slot-1 replace.

---

## Confidence map

VERIFIED (decompiled, this build, all reuse — no fresh Ghidra):
- Full slot-1 decision tree (loop gate `0x810000`, backward iteration, per-mode existence-test table, fallthrough).
- Leaf root-prefix logic (recognized roots: `languages\`,`.\`,`..\`,`editor\`,`mods\`,`engine\`, data-root `this[0x31]`/`+0x188`, second root `this[0x4a]`; data-root prepend when unrooted + `0x10000` clear).
- Alias-entry struct layout `{from, fromLen, to, toLen}` — read off `FUN_180462664` body; prefix-substitution, first-match wins.
- PAK leaf (lock + array walk + dir-index bisection) and DISK leaf (vtable +0x228 attr query) shapes.
- Mode-3 gate builds `mods/` + `c3level/` prefixes (string literals resolved in `_subresolver_strings.txt`: m,o,d,s,/ and c,3,/,l,e,v,e,l,s,/).
- The flag-bit decoder (16/18/21/23/24/28).

NEEDS-LIVE-CONFIRM (runtime facts, not in any body):
- Which asset classes (if any) open with bit 28 (`0x10000000`) set — the only path that reaches loose disk for a search-path entry at the default mode 2.
- That a kcdx slot-1 replacement that calls through to `FUN_1804631f0`/`FUN_1819c9cb4` preserves stock-pak resolution end-to-end (HIGH expected; a live readback over a known stock asset closes it).
- The alias table's actual CONTENTS at runtime (the struct layout is verified; the populated entries are runtime state — a live GetMod-style readback of `[this+0x1b0..0x1b8]` would enumerate them).

## Seed-row candidates (AP18 — FLAGGED, NOT written; need user sign-off)

This front introduces NO new candidates beyond those already flagged by the prior fronts; it
RE-AFFIRMS the resolver root as the load-bearing one to seed if kcdx replaces slot-1:
- **`CCryPak::AdjustFileName` resolver `FUN_18046205c`** — RVA 0x6205C, slot 1 (+0x8). The decision root kcdx replaces. (Already flagged in `subresolver-decompiled-mechanism.md`.)
- Secondary (only if a kcdx resolver calls them directly): pak leaf `FUN_1804631f0` (0x631F0), disk leaf `FUN_1819c9cb4` (0x9C9CB4), alias walker `FUN_180462664` (0x462664), mode-3 gate `FUN_18241ad60` (0x241AD60).
- Registrar (only for mechanism 2 / a PAKED-mod-root feature): `CCryPak::AddMod` `FUN_1819AF1A8` (0x19AF1A8, slot 19). (Already flagged in `searchpath-registrar-mechanism2.md`.)
Already seeded: FOpen (kcdx_id 131), gEnv_pCryPak (kcdx_id 132).
