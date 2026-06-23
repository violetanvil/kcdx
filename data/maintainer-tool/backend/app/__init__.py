"""app -- the kcdx maintainer-tool FastAPI backend (single-image: one process serves the API + the built SPA).

A thin shell over the headless data-core (seeds_shared): it imports the data-core
in-process and holds NO validation/SQL/export rule logic (the data-core owns all of it). Modules:

  config.py     -- the configured checkout path + derived read locations.
  data_core.py  -- the data-core import seam (the ONE place seeds_shared is reached).
  adapter.py    -- the version-tag -> data-core-params adapter (no DLL server-side).
  main.py       -- the FastAPI app + the health/load endpoint.
"""
