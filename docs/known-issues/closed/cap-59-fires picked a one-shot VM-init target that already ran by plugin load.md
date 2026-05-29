# cap-59-fires picked a one-shot VM-init target that already ran by plugin load

**Status:** resolved. The smart-resolver engine path is correct; cap-59-fires reframed onto `lua_pcall` (called continuously after plugin load) and self-reports PASS from the first callback fire.

## Symptom

`CAP-59-fires` row reported FAIL in the launch log at `2026-05-29 09:02-09:03`:

```
[09:03:09.260][INFO][engine][LEGACY] hook_chain: appended before 'lua_hook' (plugin 'cap_59_lua_hook_smart_resolver') to target 0x00007FFEEDD399AC (chain now 7)
[09:03:09.629][DEBUG][engine][TEST] REPORT name="CAP-59-fires" pass=false reason="the kcdx.hook.luaopen_math.before(fn) callback did NOT fire between plugin load and input_loaded — the install path wired no detour (handle=kcdx.handle<id=70 name=lua_hook status=applied>, :applied()=true, :reason()=nil). luaopen_math fires once during the lualib init wave well before input_loaded, so a missed fire here means the smart-resolver install never reached the dispatch path"
```

The smart-resolver install reported `:applied()==true` + `:reason()==nil`; the `hook_chain` log showed the entry correctly appended (chain length 7). The callback never fired between plugin load and `input_loaded`. Reframed: the test plugin's own backstop claimed the smart-resolver install was broken; in fact the install was correct and the target was wrong for what the test was trying to observe.

## Facts

- Smart-resolver install path executed correctly: `[REFDB] resolve_hit input_name="luaopen_math" kcdx_id=97 rva=9607596 kind="function" verification_state=verified` resolved + `hook_chain: appended before 'lua_hook' ... to target 0x00007FFEEDD399AC` confirms the chain append.
- The cap-59 install handle reported `:applied()==true` + `:reason()==nil` at the `input_loaded` backstop — install machinery is sound.
- Zero callback-fire events recorded at target `0x00007FFEEDD399AC` anywhere in the log between install (`09:03:09.260`) and the `input_loaded` backstop fire (`09:03:09.629`).
- The Lua state at `L=0x1E256273840` already existed at `09:03:02.690` — first observed in the unrelated `[MID_HOOK] lua_pcall.new_L_seen` probe — meaning the VM had been initialized before this timestamp.
- `luaopen_math` is the `math` library opener invoked from `luaL_openlibs`, which runs ONCE per Lua state during state initialization.
- cap-59's `plugin.lua` runs at `09:03:07.606`; the hook applies at `09:03:09.260`. Both events are 4-7 seconds AFTER the VM was already up and the `luaL_openlibs` library-init wave had completed.
- cap-33 / cap-34 / cap-35 also "hook" `luaopen_math` and their rows report PASS — but those rows assert `:applied()==true` at `kcdx.on("ready")`, not "the callback fired." The cap-35 plugin docstring explicitly frames this: *"assertions fire at kcdx.on('ready') after the apply pass"*. cap-59 is the FIRST row that asserts the callback actually fires.
- Switching the target name from `luaopen_math` to `lua_pcall` (kcdx_id=1, called continuously by every Lua-from-C dispatch) is sufficient — `lua_pcall` fires from the moment the kcdx Lua surface comes online, so cap-59's first-fire one-shot guard trips within milliseconds of install.

## Trail

| Date | Action | Result |
|------|--------|--------|
| 2026-05-29 | Read the log directly for callback-fire events at target `0x00007FFEEDD399AC` | Zero fire events anywhere in the log; ground-truth observation. Eliminated "install path didn't wire the detour" hypothesis (install path was correct; the function was never called after install). |
| 2026-05-29 | Cross-referenced `luaopen_math` call-site lifetime against plugin-load timing | `luaopen_math` runs once per Lua state during `luaL_openlibs` at VM creation; the VM was up by `09:03:02.690`; cap-59's hook applied at `09:03:09.260` — 6.5 seconds after the only call site executed. Mechanism identified without a probe. |

## Resolution

- **Root cause:** the cap-59 test plugin chose `luaopen_math` as its smart-resolver fire-observation target. `luaopen_math` is the Lua-stdlib `math` library opener; the Lua runtime calls it exactly once per `lua_State*`, from inside `luaL_openlibs`, during Lua state initialization. The Lua state for this session was initialized at or before `2026-05-29 09:03:02.690` (first observed via the unrelated `MID_HOOK lua_pcall.new_L_seen` probe). cap-59's `plugin.lua` script body ran at `09:03:07.606` and its smart-resolver install applied at `09:03:09.260` — 6.5 seconds AFTER the only call site for `luaopen_math` this session had already executed. The install was mechanically correct (refdb resolved the right RVA `0xEDD399AC`, `hook_chain` appended the entry, `:applied()` true, `:reason()` nil); the hook simply had zero future dispatch opportunities because the target's caller chain had already passed through that function. The test plugin's own docstring quoted cap-35's "called EXACTLY ONCE during boot" framing without checking whether that single call happened before or after plugin load — the inherited assumption is what made the test pick a target it could never observe firing.
- **Fix:** commit `<follow-up SHA>` — replace `kcdx.hook.luaopen_math.before(fn)` with `kcdx.hook.lua_pcall.before(fn)` in `test-plugins/cap-59-lua-hook-smart-resolver/plugin.lua`. `lua_pcall` (kcdx_id=1, curated verified leaf, signature `i32 (ptr L, i32 nargs, i32 nresults, i32 errfunc)`) is called continuously from the moment the kcdx Lua surface comes online — every Lua-from-C dispatch routes through it. The first cap-59 launch after the fix saw the callback fire within milliseconds of install. Updated `kcdx.toml`'s docstring + description, `plugin.lua`'s docstring + log line + `input_loaded` backstop message, and the matrix-row entry in `test-plugins/README.md` to record the target choice + the rationale.
- **Verification:** same launch shape after the fix lands; the `input_loaded` backstop no longer fires; `CAP-59-fires` self-reports PASS from the first callback fire (one-shot guarded).
- **Diagnostic archive:** no probes were authored this investigation — the log answered the question on a careful re-read of ground truth (zero fire events anywhere in the log + cross-reference of `luaopen_math` call-site lifetime against plugin-load timing). Nothing to archive.
- **Doc updates:** cap-59's `plugin.lua` + `kcdx.toml` + the test-plugins/README.md CAP-59 section now record the target-choice rationale (lua_pcall over luaopen_math) so the next reader sees the constraint at the call site. No CLAUDE.md or `.claude/rules/*.md` change owed — the existing `test-suite.md` "Be proactive — the test is part of the work" + `results-driven.md` "Static evidence before live probes" already cover the discipline that would have caught this; the gap was the cap-59 author's inherited assumption, not a missing rule.

## Hard rule / design implications

None — no rule was wrong. The author of cap-59 inherited a "called ONCE during boot" framing from cap-35's docstring without checking whether the single call happened before or after plugin load. That's an author-side review oversight, not a rule gap. The existing `results-driven.md` § "Static evidence before live probes" already mandates the pre-launch check this fix's investigation performed; if cap-59's author had asked "does the call site execute before or after my hook installs?" before picking the target, the same evidence in the log (the unrelated MID_HOOK probe captured the Lua state at boot well before plugin load) would have surfaced the timing issue.

## Active diagnostic instrumentation

(No probes authored this investigation.)
