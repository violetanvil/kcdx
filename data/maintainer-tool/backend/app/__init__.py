"""app -- the kcdx maintainer-tool FastAPI backend (design D14).

A thin shell over the headless data-core (seeds_shared): it imports the data-core
in-process and holds NO validation/SQL/export rule logic (D13/R3). Modules:

  config.py     -- the configured checkout path (D18) + derived read locations.
  data_core.py  -- the data-core import seam (the ONE place seeds_shared is reached).
  adapter.py    -- the version-tag -> data-core-params adapter (no DLL server-side).
  main.py       -- the FastAPI app + the health/load endpoint (US-1 / S7).
"""
