# cvar-existence probe archive — which candidate KCD2 CVars exist on the live build

**Verdict:** RESOLVED. Of 39 candidate CryEngine/KCD2 console-variable names
queried against build `release_1_5_1164953_841`, **23 EXIST** (resolve to a
value) and **16 MISS** (not present on this build). The 6 entries selected for
the P3 s2 shipped catalog roster are all in the resolved set, each readable via
`kcdx.cvar.get_int`.

**Root cause / method:** the existence question is checkable read-only and
theory-independent — `kcdx.cvar.get_int(name)` (and `get_float` as a fallback)
returns a number on a hit and `nil` on a miss, with no value predicted. The
`nil`-vs-number outcome IS the existence signal, one variable per name. No game
state is written (every query is a read). The console/CVar read surface is armed
at `input_loaded`, so the probe queries from a `kcdx.on("input_loaded", ...)`
handler.

**Backlink:** Phase 9.5 P3 s2 (the shipped behavior-catalog entries) — every
catalog entry must target a CVar proven to exist (the §14 "applies against the
live binary" bar). The 6 chosen entries' live values set each entry's `default`.

## Outcome map (the probe committed this before running)

Per candidate name:

- `get_int`/`get_float` returns a NUMBER  ->  RESOLVE name=<value>  ->  the CVar
  EXISTS on this build (a roster candidate).
- both return `nil`  ->  MISS name  ->  not present on this build (drop it).

## Results — 23 RESOLVED (with live values), 16 MISSED

Live values observed on build `release_1_5_1164953_841` (a 4K windowed/vsync
dev session; display-size CVars reflect that session, not a universal default):

| CVar | Live value | | CVar | Live value |
|------|-----------:|-|------|-----------:|
| `r_TexturesStreaming` | 2 | | `cl_fov` | 63 |
| `r_DisplayInfo` | 0 | | `cl_bob` | 1 |
| `r_Fullscreen` | 1 | | `g_godMode` | 0 |
| `r_VSync` | 1 | | `g_skipIntro` | 0 |
| `r_MotionBlur` | 0 | | `sys_maxfps` | 120 |
| `r_DepthOfField` | 2 | | `sys_MaxFPS` | 120 |
| `r_ssdo` | 1 | | `sys_spec` | 0 |
| `r_Sharpening` | 0 | | `sys_languages` | 0 |
| `r_ChromaticAberration` | 0 | | `wh_ui_showCompass` | 1 |
| `r_AntialiasingMode` | 3 | | `ai_DebugDraw` | -1 |
| `r_Width` | 3840 | | `con_restricted` | 0 |
| `r_Height` | 2160 | | | |

**MISSED (16, not present on this build):** `r_HDRBloomRatio`, `r_FilmGrain`,
`r_Vegetation`, `r_Beams`, `cl_hud`, `cl_crosshair`, `g_showHUD`,
`g_difficulty`, `g_timeOfDay`, `g_immortal`, `g_unlimitedStamina`,
`wh_pl_showFirstPersonBody`, `wh_dlg_showText`, `wh_pl_ShowFireDamageVignette`,
`wh_time_scale`, `p_speed_scale`.

**Selected for the v1 catalog roster (6, all from the resolved set):**
`r_MotionBlur` (0), `r_DepthOfField` (2), `r_DisplayInfo` (0),
`r_ChromaticAberration` (0), `wh_ui_showCompass` (1), `g_skipIntro` (0). Each is
backed by a console-driven `kcdx.behavior.*` catalog entry; the live value sets
each entry's declared `default`.

## Reusable probe wiring (reconstruct from here, never from source)

A throwaway suite-gated Lua plugin (`test-plugins/cvar-probe-existence/`,
removed from source after this capture). To re-run an existence sweep for any
candidate set: drop the wiring below into a suite-gated plugin's `plugin.lua`,
extend `CANDIDATES`, build/deploy, launch, grep `CVAR_PROBE`.

```lua
-- Candidate set: read-only queries only. get_int first; fall back to
-- get_float so a float-typed CVar still registers as RESOLVE (existence is the
-- question, not the type). A behavior that SETS one of these is the roster's
-- job, NOT this probe — this only proves existence.
local CANDIDATES = {
    "r_MotionBlur", "r_DepthOfField", "r_DisplayInfo", "r_ChromaticAberration",
    "wh_ui_showCompass", "g_skipIntro", -- … extend with the next sweep's names
}

local function probe()
    local resolved, missed = {}, {}
    for _, name in ipairs(CANDIDATES) do
        local vi = kcdx.cvar.get_int(name)
        if vi ~= nil then
            resolved[#resolved + 1] = name
            kcdx.log.info("CVAR_PROBE", "RESOLVE " .. name .. "=" .. tostring(vi) .. " (int)")
        else
            local vf = kcdx.cvar.get_float(name)
            if vf ~= nil then
                resolved[#resolved + 1] = name
                kcdx.log.info("CVAR_PROBE", "RESOLVE " .. name .. "=" .. tostring(vf) .. " (float)")
            else
                missed[#missed + 1] = name
                kcdx.log.info("CVAR_PROBE", "MISS " .. name)
            end
        end
    end
    kcdx.log.info("CVAR_PROBE",
        "SUMMARY resolved=" .. #resolved .. "/" .. #CANDIDATES
        .. " names=[" .. table.concat(resolved, ",") .. "]")
    kcdx.log.info("CVAR_PROBE",
        "SUMMARY missed=" .. #missed .. " names=[" .. table.concat(missed, ",") .. "]")
end

-- The console surface (and thus the CVar readers) is armed by input_loaded.
kcdx.on("input_loaded", function()
    if type(kcdx.cvar) ~= "table" or type(kcdx.cvar.get_int) ~= "function" then
        kcdx.log.error("CVAR_PROBE", "kcdx.cvar.get_int is not available — cannot probe")
        return
    end
    probe()
end)
```

The plugin's manifest was a standard suite-gated `kcdx.toml` (`[kcdx]
test_suite_only = true`, author `ts`, `[entrypoints] lua = "plugin.lua"`), with
no `test_names` (the probe only logs `CVAR_PROBE` lines; it reports no rows).

## Read recipe

`grep CVAR_PROBE kcdx-dev_<ts>.log` — one `RESOLVE <name>=<value>` or
`MISS <name>` line per candidate, then two `SUMMARY` lines (resolved count +
names, missed count + names). The discriminator: a number = the CVar exists; a
`nil` (logged as `MISS`) = it is absent on this build.

---

## Follow-up: writability probe (P3 s2 correction)

**Why.** The existence probe proved cvars are READABLE — but the catalog bar is
that an entry actually CHANGES the game (runtime-WRITABLE). On the first P3 s2
launch, `skip_intro_logos`->`g_skipIntro` FAILED: `g_skipIntro` is readable but
its `console.execute` set no-op'd (a write-restricted/const cvar). Readable does
NOT imply writable. A second probe tested writability to pick a confirmed
replacement.

**Method (one variable per candidate — did the write take?).** For each
candidate: read `before`; `console.execute("<name> <changed-value>")`; read
`after`. `after == target` -> WRITABLE; `after == before` -> RESTRICTED (set
no-op'd); else PARTIAL. Restore `before` on every path.

**Results (build release_1_5_1164953_841, launch 2026-06-12 18:54):**

| CVar | before -> after | verdict |
|------|----------------|---------|
| `r_ssdo` | 1 -> 0 | WRITABLE |
| `r_Sharpening` | 0 -> 1 | WRITABLE |
| `r_VSync` | 1 -> 0 | WRITABLE |
| `cl_bob` | 1 -> 0 | WRITABLE |
| `r_TexturesStreaming` | 2 -> 0 | WRITABLE |
| `r_ssao` | — | SKIP (not readable on this build) |

Plus the 5 already-shipped entries proved writable in the first launch
(`r_MotionBlur`, `r_DepthOfField`, `r_DisplayInfo`, `r_ChromaticAberration`,
`wh_ui_showCompass` all changed). Selected `r_ssdo` (ambient occlusion) as the
`skip_intro_logos` replacement — a clean, recognizable graphics toggle, confirmed
readable AND writable.

**Lesson for future catalog entries:** a console-driven behavior entry needs BOTH
proofs — readable (existence) AND writable (the set takes). A cvar can be one
without the other (`g_skipIntro` is read-only at runtime). Probe writability, not
just existence, before shipping a console-toggle entry.

**Read recipe:** `grep CVAR_WRITE kcdx-dev_<ts>.log` — one
`WRITABLE/RESTRICTED/PARTIAL/SKIP <name>` line per candidate + a `SUMMARY
writable=` line. The reusable wiring is the same read-set-read-restore shape as
the existence probe, with the write attempt added between the reads.
