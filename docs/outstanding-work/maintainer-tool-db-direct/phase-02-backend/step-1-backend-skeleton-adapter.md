# Step 1 — backend skeleton + data-core import seam + version-tag→params adapter

**What.** Stand up the Python backend package in `data/maintainer-tool/backend/` (FastAPI or
Flask — the data-core is Python, so the backend imports `seeds_shared` in-process, no
subprocess; D14). Build: the app entry point + config (the **configured checkout path** the
backend reads the reference DB / `data/seeds/` from — D18), the **data-core import seam**
(the backend calls `seeds_shared`'s public surface, holds no rule logic — D13/§5), and the
**version-tag→data-core-params adapter** — the data-core write functions take a `dll_path`
(a desktop assumption); the backend has no DLL server-side, so the adapter maps a
maintainer-chosen/resolved **version tag** to the parameters the data-core expects (the
backend never reads a DLL). Plus a health/load endpoint that reports the checkout resolved
(or the empty/error state for s01).

**Scope.** The backend package skeleton + config + the import seam + the adapter + the
health/load endpoint. No read/save/commit endpoints yet (steps 2–5). No frontend, no Docker.

**Test bar.** A backend test (`pytest`) on the mini-dump fixture: the app boots; the
health/load endpoint reports the checkout resolved (and the empty/error states when the
configured path has no DB/seeds — feeds s01's empty/error states); the adapter maps a known
version tag to the data-core params correctly (assert the params a known job would receive).
Runnable now (the data-core + the fixture exist; no later step needed to exercise it).

**Dependencies.** Phase 1 (the data-core `seeds_shared` it imports — landed). This is the
backend foundation every later backend step builds on; it lands first in Phase 2.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§5 (the backend unit — "the Python API over the data-core … a thin adapter maps a chosen
version tag → the data-core's params (no DLL server-side)") + §10 D13 (the applier path) +
D14 (Python backend) + D18 (the configured checkout path). `requirements.md` R2 (no Ghidra/
DLL prerequisite to run the tool).

**Disassembler-test / author-burden.** N/A — backend plumbing; no author-facing
game-function input. The version-tag adapter REMOVES the desktop DLL-path assumption from
the server (the client resolves a DLL, P4 step 11; the server takes only a tag).
