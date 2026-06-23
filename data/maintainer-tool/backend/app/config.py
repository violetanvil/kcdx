"""app.config -- the backend's configured checkout path.

The backend reads the reference DB + the curated CSVs from a git checkout on a
mounted volume at a CONFIGURED PATH. The image carries only app code; the
operator provides the checkout volume. This module owns ONLY where that path
comes from + the derived sub-paths the backend reads -- no rule logic: every
validation/SQL/export rule is the data-core's.

The path source, in priority order:
  1. the KCDX_CHECKOUT env var (the operator wires it to the mounted volume);
  2. an explicit override passed to load_config (tests point it at a fixture);
  3. a documented dev default -- a dev default lets the app boot + run locally:
     the repo root inferred from this file's location, so a developer running
     the backend from inside the repo boots without setting the env var.

A configured path that does NOT resolve a DB/CSVs is NOT an error here -- the
health/load endpoint reports the empty/error state. load_config never raises on
a missing checkout; it records what it found so the endpoint can name where it
looked.
"""
import os
from dataclasses import dataclass

# The env var the operator wires to the mounted-volume checkout.
CHECKOUT_ENV_VAR = "KCDX_CHECKOUT"

# The env vars the operator wires to the configured maintainer IDENTITY: a single
# name + email used for BOTH the GUI's verified_by default (the audit-trio author the
# maintainer is signing rows off as) AND the git commit author. A non-secret PUBLIC author
# identity (the same shape as a git author line) -- safe to surface to the frontend, unlike
# the push CREDENTIAL (KCDX_PUSH_TOKEN, git_commit.py), which is a SECRET and is NEVER
# exposed (secrets never enter a readable/exposed surface).
# Unset/empty -> the documented dev defaults below, so the app boots + commits + defaults
# verified_by locally without the operator's env (the same boot-without-config posture as
# the checkout / CORS dev defaults).
MAINTAINER_NAME_ENV_VAR = "KCDX_MAINTAINER_NAME"
MAINTAINER_EMAIL_ENV_VAR = "KCDX_MAINTAINER_EMAIL"

# The documented dev-default maintainer identity (the boot-without-config seam). A real,
# non-secret author identity so the app + the verified_by default + the git author all work
# locally with no env wired. The operator overrides both via the env vars above.
DEV_DEFAULT_MAINTAINER_NAME = "VioletAnvil"
DEV_DEFAULT_MAINTAINER_EMAIL = "maintainer-tool@kcdx.local"

# The env var the operator wires to the built frontend's static-assets dir (the SPA's
# `dist/`). The single-image design has one uvicorn process serve the API AND the built
# frontend same-origin; this names WHERE that built `dist/` lives. The image's stage-2 COPY
# places the multi-stage-built `dist/` at the dev default below, so the container needs no
# env. An operator (or a dev serving a locally-built bundle) overrides it. A missing dir is
# NOT an error -- the app boots + serves the API regardless (the static_serving degraded
# contract), the same boot-without-config posture as the checkout / CORS dev defaults.
STATIC_DIR_ENV_VAR = "KCDX_STATIC_DIR"

# The env var the operator wires to the real frontend origin(s) in production -- the
# operator-wired seam, alongside KCDX_CHECKOUT / KCDX_PUSH_TOKEN. A comma-separated
# allowlist of browser ORIGINS the served frontend calls the backend from. Unset/empty ->
# the localhost dev default below, so the app boots + runs locally without the operator's
# env (the same boot-without-config posture as the checkout dev default).
CORS_ORIGINS_ENV_VAR = "KCDX_CORS_ORIGINS"

# The localhost dev default: the vite `preview` port (4173) + the vite `dev` port
# (5173), in BOTH the localhost and 127.0.0.1 spellings -- a browser treats localhost and
# 127.0.0.1 as DISTINCT origins, so a dev hitting either reaches the backend without wiring
# KCDX_CORS_ORIGINS. NOT a wildcard origin: the maintainer tool writes + commits the
# Address Library, so a tight allowlist is the security-correct default.
_DEV_DEFAULT_CORS_ORIGINS = (
    "http://localhost:4173",
    "http://localhost:5173",
    "http://127.0.0.1:4173",
    "http://127.0.0.1:5173",
)

# Sub-paths of a checkout the data-core reads. These mirror the data-core's own
# layout constants -- the backend names WHERE the checkout puts them; the data-core
# owns what they contain.
# WHY data/db-export/ for BOTH: the legacy seeds dir (retired) is no longer read by
# anything. The genesis role moved to the tracked CSV export: run_rebuild now rebuilds
# both DBs from the curated CSVs at data/db-export/ (+ the bulk under Git LFS), not the
# legacy seeds. So the bootstrap `seed_dir` the load endpoint checks for presence now
# resolves data/db-export/ -- the SAME curated CSVs the DB's deterministic per-save
# export writes there. seed_dir and db_export_dir collapse to one location: the curated
# CSVs are both the rebuild genesis AND the git-tracked diff record (the DB stays the
# originator, out of git; only the GENESIS source moved off the legacy seeds).
_SEED_SUBDIR = os.path.join("data", "db-export")
_SEED_FILES = ("module_seed.csv", "address_names_seed.csv",
               "address_versions_seed.csv")
_DB_EXPORT_SUBDIR = os.path.join("data", "db-export")
# The reference DBs the data-core write path amends (out_dir holds both).
USER_DB_NAME = "reference.sqlite"
DEV_DB_NAME = "reference-dev.sqlite"


def _repo_root_from_here():
    """The repo root inferred from this file: backend/app/config.py ->
    backend/app -> up 4 = the repo root. The documented dev
    default so the app boots without the operator's env var when run from inside
    the repo."""
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.normpath(os.path.join(here, "..", "..", "..", ".."))


def _default_static_dir():
    """The dev-default static-assets dir: <app>/static (alongside this module). The
    image's stage-2 COPY places the built frontend `dist/` HERE, so a container with no
    KCDX_STATIC_DIR resolves it (single image -- the backend serves the built SPA).
    Absent on a bare dev checkout (no FE build yet) -- that is the static_serving degraded
    contract, not a config error, so this never has to exist."""
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(here, "static")


@dataclass(frozen=True)
class Config:
    """The resolved backend configuration. `checkout_path` is the directory the
    backend reads the reference DB + curated CSVs from; the rest are derived
    read locations the load endpoint resolves against."""
    checkout_path: str
    checkout_source: str          # "env" / "override" / "dev-default" (audit trail)

    @property
    def seed_dir(self):
        """The bootstrap-genesis dir the load endpoint checks for presence --
        data/db-export/ (the curated CSVs run_rebuild now rebuilds from). The
        legacy seeds dir is retired; this resolves the SAME location as
        db_export_dir."""
        return os.path.join(self.checkout_path, _SEED_SUBDIR)

    @property
    def seed_files(self):
        return tuple(os.path.join(self.seed_dir, f) for f in _SEED_FILES)

    @property
    def db_export_dir(self):
        """The derived-export dir Confirm writes the three CSVs to --
        data/db-export/. Since the legacy seeds dir is retired, this is also the
        rebuild genesis (seed_dir resolves the same path). The git commit stages
        the DB's export -- these three CSVs by exact path (the DB itself is not
        committed)."""
        return os.path.join(self.checkout_path, _DB_EXPORT_SUBDIR)

    @property
    def db_export_files(self):
        """The three derived-export CSV paths under data/db-export/ -- the files
        Confirm exports the committed DB to + stages + integrity-checks."""
        return tuple(os.path.join(self.db_export_dir, f) for f in _SEED_FILES)

    @property
    def out_dir(self):
        """The directory holding the two reference DBs the data-core amends
        (the data-core's `out_dir` param). The DBs live at
        <checkout>/data/{reference,reference-dev}.sqlite -- under data/, NOT
        data/db-export/ (which holds the curated CSV export: the rebuild genesis +
        the git-tracked diff record, no .sqlite). run_rebuild writes
        <out_dir>/reference.sqlite and the release ships it at data/reference.sqlite,
        so out_dir is <checkout>/data. The load endpoint reports which DBs resolve."""
        return os.path.join(self.checkout_path, "data")

    @property
    def user_db(self):
        return os.path.join(self.out_dir, USER_DB_NAME)

    @property
    def dev_db(self):
        return os.path.join(self.out_dir, DEV_DB_NAME)


def load_config(checkout_override=None):
    """Resolve the configured checkout path. Priority: explicit override >
    KCDX_CHECKOUT env > the dev default (the repo root). Never raises on a missing
    checkout -- a path that resolves nothing is the load endpoint's empty/error
    state, not a config error."""
    if checkout_override is not None:
        return Config(checkout_path=os.path.abspath(checkout_override),
                      checkout_source="override")
    env = os.environ.get(CHECKOUT_ENV_VAR)
    if env:
        return Config(checkout_path=os.path.abspath(env), checkout_source="env")
    return Config(checkout_path=_repo_root_from_here(),
                  checkout_source="dev-default")


def cors_origins():
    """The allowed browser ORIGINS the served frontend calls the backend from --
    the operator-wired CORS seam. KCDX_CORS_ORIGINS (a comma-separated list the operator
    wires to the real production origin), else the localhost dev default. Never a wildcard
    -- a mutating API on a wildcard CORS is a security finding; the allowlist is the
    security-correct default. Read fresh each call (mirrors load_config), so a test can
    set the env and re-construct the middleware deterministically."""
    env = os.environ.get(CORS_ORIGINS_ENV_VAR)
    if env:
        origins = [o.strip() for o in env.split(",") if o.strip()]
        if origins:
            return origins
    return list(_DEV_DEFAULT_CORS_ORIGINS)


def static_dir():
    """The built frontend's static-assets dir (the SPA `dist/`) the backend serves
    (single image). KCDX_STATIC_DIR if the operator/dev wired it, else the dev default
    (<app>/static, where the image's stage-2 COPY lands `dist/`). Read fresh each call
    (mirrors cors_origins / load_config) so a test can point it at a fixture dir
    deterministically. The path is NOT required to exist -- static_serving degrades
    gracefully when it is absent (the API still serves); resolving the path is not the
    same as the dir being present."""
    env = (os.environ.get(STATIC_DIR_ENV_VAR) or "").strip()
    if env:
        return os.path.abspath(env)
    return _default_static_dir()


def maintainer_identity():
    """The configured maintainer IDENTITY: (name, email) from
    KCDX_MAINTAINER_NAME / KCDX_MAINTAINER_EMAIL, else the documented dev defaults. Used for
    BOTH the GUI's verified_by default (surfaced via /health) AND the git commit author's
    fallback (routes_confirm._resolve_author). Each half resolves independently so a partial
    env still yields a complete identity. A NON-SECRET public author identity -- it is safe
    to surface to the frontend (the health body); the push CREDENTIAL (KCDX_PUSH_TOKEN) is a
    SECRET and is NEVER read here or exposed. Read fresh each call (mirrors cors_origins /
    load_config) so a test can set the env deterministically.

    Returns {"name": <str>, "email": <str>} -- the shape surfaced in the health body."""
    name = (os.environ.get(MAINTAINER_NAME_ENV_VAR) or "").strip() \
        or DEV_DEFAULT_MAINTAINER_NAME
    email = (os.environ.get(MAINTAINER_EMAIL_ENV_VAR) or "").strip() \
        or DEV_DEFAULT_MAINTAINER_EMAIL
    return {"name": name, "email": email}
