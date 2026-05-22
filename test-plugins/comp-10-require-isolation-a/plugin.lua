-- COMP-10 plugin A — cross-plugin require-cache isolation + within-plugin
-- cache hit. See kcdx.toml for the full gap description.
--
-- Two assertions, both decided at plugin load (boot-only):
--   (1) ISOLATION: require("helper") resolves to THIS plugin's helper.lua
--       (marker "A"), not plugin B's (marker "B"). If the shared-_LOADED
--       collision were live, whichever plugin loaded first would poison the
--       bare "helper" cache key and the other would see the wrong marker.
--   (2) WITHIN-PLUGIN CACHE HIT: a second require("helper") in the SAME
--       plugin returns the SAME table (same owner+modname -> same kcdx
--       cache key), proving the namespaced cache caches per plugin instead
--       of recompiling.

local h1 = require("helper")
local h2 = require("helper")   -- second require -> must be the SAME table

local marker_ok = type(h1) == "table" and h1.marker == "A"
local cache_ok  = (h1 == h2)   -- table identity == within-plugin cache hit

kcdx.test.report("COMP-10-require-isolation-a", marker_ok and cache_ok,
    (marker_ok and cache_ok)
        and ("require('helper') resolved to THIS plugin's helper (marker='A') "
             .. "AND a second require('helper') returned the SAME table "
             .. "(within-plugin cache hit) — cross-plugin isolation + "
             .. "per-plugin cache both hold")
        or  ("isolation/cache FAIL: marker="
             .. tostring(type(h1) == "table" and h1.marker or h1)
             .. " (expected 'A'; a wrong marker means plugin B's helper "
             .. "leaked through the shared bare-name cache), same_table="
             .. tostring(cache_ok) .. " (expected true — within-plugin "
             .. "require should hit the kcdx cache)"))

kcdx.log.info("COMP10",
    "plugin A: require('helper').marker=" .. tostring(h1.marker)
    .. ", second-require-same-table=" .. tostring(cache_ok))
