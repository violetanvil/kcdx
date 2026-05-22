-- CAP-23 plugin.lua — DELIBERATE runtime error. This is NOT a bug.
--
-- THE ERROR ON THE LINE BELOW IS THE TEST. Do not "fix" it, do not guard
-- it with `if`, do not pcall it. The whole point of this fixture is to make
-- plugin.lua raise a RUNTIME error every boot so the loader
-- (src/lua_plugin_loader.cpp) can capture the error text and assert that it
-- carries:
--   1. a file:line marker  (e.g. "plugin.lua:18:")  -- piece 1, storedebug
--   2. a "stack traceback:"                          -- piece 2, errfunc
-- The loader reports the verdict under "cap-23-lua-error-lineinfo".
--
-- It MUST be a RUNTIME error (not a syntax/compile error): a compile error
-- has no live stack and thus no traceback, which would not exercise piece 2.
-- Indexing a field on a nil global is a clean runtime error that produces
-- both the line marker and the traceback.

-- cap23_deliberate_nil is never defined -> it is nil -> indexing .field on
-- nil raises "attempt to index global 'cap23_deliberate_nil' (a nil value)"
-- at this line, with a traceback. INTENTIONAL.
local x = cap23_deliberate_nil.field
