# kcdx maintainer-tool — backend

The Python (FastAPI) API over the headless Address Library **data-core**
(`seeds_shared`), per `data/maintainer-tool/design.md` §5 + §10 D14. A **thin
shell**: it imports the data-core in-process (the data-core is Python — a direct
import, no subprocess; D14) and holds **NO validation / SQL / export / authoring
rule logic** — every rule is the data-core's (D13 / R3 / §5 law). All of
`data/maintainer-tool/` is private (the publish-public carve-out, R10).

## Package shape

```
backend/
  app/
    __init__.py
    config.py     — the configured checkout path (D18) + derived read locations
    data_core.py  — the data-core import seam (the ONE place seeds_shared is reached)
    adapter.py    — the version-tag → data-core-params adapter (no DLL server-side)
    main.py       — the FastAPI app + the /health load endpoint (US-1 / §7)
  tests/
    test_backend_skeleton.py
  requirements.txt        — runtime deps (pinned)
  requirements-dev.txt    — + test deps (pinned)
  README.md               — this file (the license manifest below)
```

What ships in step 1: the skeleton + config + import seam + version-tag adapter +
the `/health` load endpoint. **No read/save/commit endpoints yet** (steps 2–5); no
frontend, no Docker.

## Configured checkout (D18)

The backend reads the reference DB + seeds from a git checkout at a **configured
path** (the operator mounts it on a volume; the image carries only app code). The
path source, in priority order: the `KCDX_CHECKOUT` env var → an explicit override
→ a documented **dev default** (the repo root inferred from the package location,
so a developer boots without setting the env var; D17). A path that resolves no
DB/seeds is **not** an error — the `/health` endpoint reports the empty/error
state and names where it looked (US-1 / §7).

## The `/health` load endpoint

`GET /health` reports `{ state, detail, checkout_path, checkout_source, seed_dir,
out_dir, artifacts{user_db, dev_db, seed_files}, known_version_tags }`, where
`state` is `resolved` (DB + all three seeds present) / `empty` (missing DB or
seeds — `detail` names the missing artifacts) / `error` (the data-core read
failed). This feeds s01's empty/error states.

## The version-tag → data-core-params adapter

The data-core's write functions take `(out_dir, dll_path, …)`; `dll_path` is a
desktop assumption — the data-core resolves the target game version by scanning a
linked DLL's `.rdata` (`resolve_version(dll_path) → (tag, ordinal)`), used for
nothing else. The backend has **no DLL server-side**, so the adapter maps a
maintainer-chosen **version tag** → the resolved `VersionContext(tag, ordinal)`
the data-core needs, validated against the known game versions the server holds
(the built DB's `game_versions` rows, with the data-core baseline constant as the
floor). **How that resolved context threads into the data-core's `dll_path`-shaped
write call is an open integration decision** surfaced to the user — not needed for
step 1 (no write endpoint yet); `adapter.data_core_dll_param` raises a clear
`NotImplementedError` naming the fork rather than fabricating a `dll_path`.

## Dependencies

License-checked before adding (`.claude/rules/dependencies.md`); all permissive,
consistent with this private sub-repo's distribution model. Pinned to stable
ranges — not floated to latest.

| Name | Version (pinned) | License | Purpose |
|------|------------------|---------|---------|
| fastapi | `>=0.136,<0.137` | MIT | the web framework — the API over the data-core (D14); provides the `/health` route + `TestClient` |
| uvicorn | `>=0.48,<0.49` | BSD-3-Clause | the ASGI server that runs the FastAPI app in production |
| pytest | `>=8.3,<10` (dev) | MIT | the test runner for the backend test suite |
| httpx | `>=0.27,<0.29` (dev) | BSD-3-Clause | required by FastAPI's `TestClient` to drive the real app in-process under test |

Versions verified against PyPI (fastapi 0.136.3, uvicorn 0.48.0, pytest 9.0.3,
httpx 0.28.1 current at authoring); licenses verified on the package registry +
each project's source repository.

## Install + run

```
# runtime only
python -m pip install -r requirements.txt
# + the test deps
python -m pip install -r requirements-dev.txt

# the test suite (from the repo root)
python -m pytest data/maintainer-tool/backend/tests/ -q

# the dev server (the documented dev default boots without an env var)
python -m uvicorn app.main:app --reload   # from inside backend/
```
