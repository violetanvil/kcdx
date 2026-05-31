"""seeds_shared.schema -- the canonical declaration of the reference-DB shape.

The single source of truth for every table, every column + SQL type, the USER
column allowlist, the per-(table,col) dict-encoded columns, the table sets per
db, and the two enums that constrain seed values (address kinds + evidence
kinds).

Moved verbatim out of import_to_sqlite.py (db-updator Phase 1, step 1). Both the
rebuild path and the future incremental `apply` path import from here so they
cannot drift. This module holds DATA ONLY -- no I/O, no validation logic (that
is validators.py, which imports from here), no row construction (row_builder.py).

The baseline game-version constants (GAME_VERSION_TAG / _ORDINAL / _ID) and the
meta constants (SCHEMA_VERSION, ABI_CONFIDENCE) stay in import_to_sqlite.py:
they parameterize the single-version baseline REBUILD orchestration (which
game_versions/meta rows to emit), not the schema shape. The row-builder takes
the version id as an argument rather than reaching for a module constant, so it
stays version-agnostic and shareable by `apply`.
"""

# Dict-encoded columns, keyed by the SCHEMA table+column name (post-transform).
# `address_names.source` REMOVED 2026-05-28 (carried no information; "verified"
# duplicated status, which is now derived). `address_versions.status` REMOVED
# 2026-05-28 (status is derived from verification audit columns + entity
# flags). `address_versions.evidence_kind` ADDED 2026-05-28.
DICT_COLS = {
    "address_versions":      ["kind", "caller_arg_agreement",
                              "decompile_quality", "evidence_kind"],
    "statements":            ["kind"],
    "referenced_vars":       ["storage_kind", "data_type"],
}

# USER db column allowlists (per table). Tables NOT listed here are DEV-only.
# A column omitted from the list is dropped from the USER CREATE TABLE + insert.
# Under the FLATTENED schema (2026-05-28) there is no entities/entity_versions
# split: address_names is the (kcdx_id, name) registry, address_versions carries
# every per-version resolve fact for any curated address (the kcdx_id is the
# stable handle plugins reference). For the USER (production) DB, only address-
# names whose kcdx_id has at least one row in address_versions ship; bulk
# discovery functions live in DEV-only tables under their own bulk id-space.
USER_COLUMNS = {
    "modules":          ["id", "name", "path"],
    "game_versions":    ["id", "tag", "ordinal", "released"],
    "address_names":    ["id", "name",
                         "superseded_by", "superseded_at_version",
                         "is_deprecated", "deprecated_at_version",
                         "deprecation_replacement", "notes"],
    # `id` IS the kcdx_id (the stable cross-version handle); no separate kcdx_id
    # column. address_versions.kcdx_id references address_names.id.
    # excludes notes (DEV-only).
    # The verification audit columns ship to USER -- the engine needs them to
    # derive status at resolve time (a plugin author resolving target="X" at
    # current game_version V needs to see "is this row verified at V?", which
    # is exactly the (valid_from <= V <= last_verified_at_version) test).
    "address_versions": ["id", "kcdx_id", "kind", "module_id", "rva", "length",
                         "content_hash", "value", "signature",
                         "observed_arg_slots", "caller_reg_arg_count",
                         "caller_arg_agreement", "offset", "vtable_slot",
                         "last_verified_at_version", "verified_by",
                         "verified_date", "evidence_kind",
                         "valid_from", "valid_through", "struct_offset"],
    # excludes auto_name, decompile_quality (DEV-only discovery labels)
    "meta":             ["id", "schema_version", "abi_confidence"],
    # survival ships to USER: it is curated-entity data the (future) engine
    # survival pass reads at the user tier. Every survival column is in the USER
    # projection (the whole table is curated -- there are no bulk survival rows).
    "survival":         ["id", "address_version_id", "kind_form", "derives_from",
                         "aob", "anchor_string", "rule", "slot_count",
                         "expect_unique", "content_hash", "length"],
}

# Full schema (DEV): every table, every column, with its SQL column type.
# BLOB = content_hash; INTEGER = dict/int/fk; TEXT = the rest.
SCHEMA = {
    # modules.id and address_names.id are NOT autoincrement -- both come from
    # the seed files (module_seed.csv.id, address_names_seed.csv.id). The importer
    # fails loud on null or duplicate id in either file. address_versions.id
    # stays autoincrement (internal row id; hypothetically could change between
    # rebuilds and that's fine as long as resolution still walks correctly).
    "modules": [
        ("id", "INTEGER PRIMARY KEY"),            # from module_seed.csv.id
        ("name", "TEXT"),
        ("path", "TEXT"),                         # install-relative path
    ],
    "game_versions": [
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("tag", "TEXT"),
        ("ordinal", "INTEGER"),
        ("released", "TEXT"),
    ],
    # address_names: ONE ROW PER CURATED ENTITY. `id` is the canonical kcdx_id
    # supplied by address_names_seed.csv (NOT autoincrement; the importer
    # fails loud on null or duplicate id in either seed file). The id is
    # stable across rebuilds for the same address_names_seed.csv row.
    #
    # Two distinct entity-level events, each with its own version anchor:
    #
    # (1) SUPERSESSION (cosmetic rename, version-gated, engine AUTO-FOLLOWS):
    #     `superseded_by` -> address_names.id of the direct successor (NOT the
    #     terminal -- the engine walks the chain at query time, applying the
    #     version filter at each hop). `superseded_at_version` -> game_versions.id;
    #     the supersession edge becomes active at that version inclusive. Both
    #     paired (both NULL or both NOT NULL). Resolution: at any game version V,
    #     resolve(name) walks the chain, following each edge IFF
    #     edge.superseded_at_version.ordinal <= V; stops on the first row whose
    #     active edge is NULL.
    #
    # (2) DEPRECATION (entity behavior changed; engine WARNS but does not
    #     redirect): `is_deprecated` 0/1 paired with `deprecated_at_version`.
    #     The optional `deprecation_replacement` -> address_names.id is an
    #     advisory pointer surfaced in the warning ("X is deprecated; consider
    #     switching to Y") -- the engine does NOT auto-follow it. The two flags
    #     are orthogonal: a rename (1) shares an address with its successor; a
    #     deprecation (2) does not, and the replacement (when given) is a
    #     DIFFERENT entity with different functionality.
    "address_names": [
        ("id", "INTEGER PRIMARY KEY"),                 # IS the kcdx_id; from address_names_seed.csv.id
        ("name", "TEXT"),
        # supersession pair (cosmetic rename; engine auto-follows, version-gated)
        ("superseded_by", "INTEGER"),                  # FK to address_names.id (direct successor)
        ("superseded_at_version", "INTEGER"),          # FK to game_versions.id; supersession applies >= this
        # deprecation pair (behavior changed; engine warns, does not redirect)
        ("is_deprecated", "INTEGER"),                  # 0/1
        ("deprecated_at_version", "INTEGER"),          # FK to game_versions.id; deprecation applies >= this
        ("deprecation_replacement", "INTEGER"),        # nullable FK to address_names.id; "consider switching to" advisory
        # prose (DEV only)
        ("notes", "TEXT"),                             # DEV-ONLY (entity-level prose)
    ],
    # address_versions: per-(entity, version-interval) resolve facts. kcdx_id
    # is NULLABLE -- set when the row is a curated entity (FK to address_names.id),
    # NULL when the row is a bulk-only DEV function (no curated name). Partial
    # UNIQUE (kcdx_id) WHERE kcdx_id IS NOT NULL AND valid_through IS NULL
    # enforces "at most one current form per CURATED entity." (Bulk rows are
    # currently 1:1 with their function but not enforced as such.)
    "address_versions": [
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("kcdx_id", "INTEGER"),                # NULLABLE FK to address_names.id (curated only)
        ("kind", "INTEGER"),                   # dict: function | callsite | vtable_base | etc.
        ("module_id", "INTEGER"),              # FK to modules.id
        ("rva", "INTEGER"),
        ("length", "INTEGER"),
        ("content_hash", "BLOB"),
        ("value", "INTEGER"),                  # AUTHORED per-kind integer datum (its own seed column)
        ("signature", "TEXT"),                 # verified ABI; for un-curated bulk this is the abi_walker floor
        ("observed_arg_slots", "INTEGER"),
        ("caller_reg_arg_count", "INTEGER"),
        ("caller_arg_agreement", "INTEGER"),   # dict
        ("offset", "INTEGER"),                 # AUTHORED consumer offset (callsite / data_slot; its own seed column)
        ("vtable_slot", "INTEGER"),            # AUTHORED vtable_index slot integer (its own seed column)
        # Verification audit trail (added 2026-05-28). The `status` column was
        # REMOVED -- status is derived from
        #   (current_version, valid_from, last_verified_at_version) PLUS
        # entity-level supersession + deprecation flags on address_names.
        # See policy.md.
        ("last_verified_at_version", "INTEGER"), # nullable FK to game_versions.id; NULL = never verified
        ("verified_by", "TEXT"),                 # nullable person identifier
        ("verified_date", "TEXT"),               # nullable ISO YYYY-MM-DD
        ("evidence_kind", "INTEGER"),            # nullable dict (EVIDENCE_KIND_ENUM)
        ("auto_name", "TEXT"),                 # DEV-ONLY (FUN_<rva> for bulk discovery)
        ("decompile_quality", "INTEGER"),      # dict, DEV-ONLY
        ("valid_from", "INTEGER"),             # FK to game_versions.id (== valid_from_version)
        ("valid_through", "INTEGER"),          # NULL = current (open interval)
        # AUTHORED vtable/struct byte offset (e.g. IConsole vtable +0xB8). Its
        # own seed column; appended at the END so existing positional column
        # expectations elsewhere (engine SELECT, oracle) are not disturbed.
        ("struct_offset", "INTEGER"),
    ],
    "meta": [
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("schema_version", "INTEGER"),
        ("abi_confidence", "TEXT"),
    ],
    # DEV-only tables. Each row carries TWO FK columns to its owning function:
    #   address_version_id -- FK to address_versions.id; ALWAYS SET. The
    #     universal "which function row" pointer (works for both curated +
    #     bulk; what kcdx.find walks).
    #   kcdx_id            -- FK to address_names.id; NULLABLE, non-unique.
    #     Set only when the owning function is curated. Ergonomic shortcut for
    #     curated-subset joins; redundant with address_version_id otherwise.
    "statements": [   # DEV-ONLY
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("address_version_id", "INTEGER"),     # always set; -> address_versions.id
        ("kcdx_id", "INTEGER"),                # nullable, non-unique; -> address_names.id when curated
        ("idx", "INTEGER"),
        ("kind", "INTEGER"),                    # dict
        ("pseudo_text", "TEXT"),
        ("byte_range_start", "INTEGER"),
        ("byte_range_len", "INTEGER"),
        ("content_hash", "BLOB"),
        ("callee", "TEXT"),
        ("string_ref", "TEXT"),
    ],
    "referenced_vars": [   # DEV-ONLY
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("address_version_id", "INTEGER"),     # always set; -> address_versions.id
        ("kcdx_id", "INTEGER"),                # nullable, non-unique
        ("statement_idx", "INTEGER"),
        ("var_name", "TEXT"),
        ("storage_kind", "INTEGER"),            # dict
        ("storage_detail", "TEXT"),
        ("size_bytes", "INTEGER"),
        ("data_type", "INTEGER"),               # dict
    ],
    "call_edges": [   # DEV-ONLY
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("caller_address_version_id", "INTEGER"),   # always set
        ("callee_address_version_id", "INTEGER"),   # always set (call must resolve to land here)
        ("caller_kcdx_id", "INTEGER"),              # nullable, non-unique; curated caller only
        ("callee_kcdx_id", "INTEGER"),              # nullable, non-unique; curated callee only
        ("callsite_rva", "INTEGER"),
    ],
    # survival: the per-kind survival datum, a SIBLING table 1:1 with a CURATED
    # address_versions row (one survival row per curated entity-version; bulk
    # uncurated functions get none). The future engine survival pass reads it,
    # dispatching on `kind_form`; the hot resolve path never touches it (hot/cold
    # separation, same grain as the other sibling tables). Ships to USER + DEV
    # (curated-entity data the engine consumer needs at the user tier).
    #
    # Decided (db-updator step 5.1): a kind-discriminated payload + a first-class
    # `derives_from` FK (the cross-row dependency DAG: data_slot ->
    # instruction_anchor -> string_anchor; vtable_index -> vtable_base) so the
    # survival check can walk in dependency order. Payload columns are typed +
    # mutually-exclusive by kind_form (only the column(s) a kind uses are set;
    # the rest NULL) -- the seed is hand-authored (step 5.2), and typed columns
    # are far more authorable than a packed blob.
    #
    # See data/maintainer-tool/fingerprint-per-kind.md (the per-kind datum + this
    # table shape) and data/seeds/policy.md (the seed columns step 5.2 fills).
    "survival": [
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("address_version_id", "INTEGER"),     # FK -> address_versions.id (1:1; the entity this survives)
        ("kind_form", "TEXT"),                 # function_hash | aob | literal | derivation | table_shape | slot_target
        ("derives_from", "INTEGER"),           # nullable FK -> address_versions.id (the DAG edge); resolved from survival_derives_from kcdx_id
        # Kind-typed payload columns. Each kind_form uses a subset; the rest NULL.
        ("aob", "TEXT"),                       # aob form (callsite/instruction_anchor): pattern bytes + folded ? wildcard mask
        ("anchor_string", "TEXT"),             # literal form (string_anchor): the literal bytes
        ("rule", "TEXT"),                      # derivation form (data_slot): the derivation rule (e.g. disp32@<kid> / <kid>-0xA8)
        ("slot_count", "INTEGER"),             # table_shape form (vtable_base): expected slot count
        ("expect_unique", "INTEGER"),          # nullable 0/1: aob (callsite/instruction_anchor) AOB-unique + literal (string_anchor) unique-xref assertion
        # function_hash form (function kinds): carry the body fingerprint already
        # on the address_versions row (no seed authoring -- reused). slot_target
        # (vtable_index) would also carry a target body hash, but its population
        # is DEFERRED (gated on the runtime-vtable path), so these stay NULL there.
        ("content_hash", "BLOB"),              # function_hash: BLAKE3 of the body span (from the av row)
        ("length", "INTEGER"),                 # function_hash: the span length the hash covers (from the av row)
    ],
}

# Table sets per db. `survival` ships to BOTH (curated survival datum the engine
# consumer reads at the user tier), so it is in USER_TABLES as well as DEV_TABLES.
DEV_TABLES = ["modules", "game_versions", "address_names", "address_versions",
              "meta", "statements", "referenced_vars", "call_edges", "survival"]
USER_TABLES = ["modules", "game_versions", "address_names", "address_versions",
               "meta", "survival"]

# The 9 kinds for address_versions.kind (covering every curated row type seen
# in the address seeds + the bulk function default).
ADDRESS_KINDS = ("function", "function_variadic", "function_no_sig", "callsite",
                 "vtable_index", "vtable_base", "data_slot", "string_anchor",
                 "instruction_anchor")

# The function kind-classes — the subset of ADDRESS_KINDS whose identity IS a
# contiguous code body, so a body-hash fingerprint (content_hash + length) is a
# meaningful survival check. Only these PROMOTE a matched bulk row (keeping its
# fingerprint); every other kind mints with the fingerprint columns NULL,
# regardless of whether its rva coincides with a bulk function entry. This is
# the SINGLE definition both the rebuild promote-gate (build_rows) and the
# incremental apply path consume, so the two writers cannot drift on which kinds
# carry a fingerprint. See data/maintainer-tool/fingerprint-per-kind.md.
FUNCTION_KINDS = ("function", "function_no_sig", "function_variadic")

# The legal survival.kind_form values (the survival check's dispatch tag). One
# per ADDRESS_KIND class:
#   function_hash -> function / function_no_sig / function_variadic (body hash)
#   aob           -> callsite / instruction_anchor (AOB re-match)
#   literal       -> string_anchor (literal presence)
#   derivation    -> data_slot (re-run the derivation rule)
#   table_shape   -> vtable_base (slot-count + pointer-table shape)
#   slot_target   -> vtable_index (resolve base + index -> target body hash; the
#                    payload population is DEFERRED on the runtime-vtable path)
# The single shared definition both the rebuild and apply survival builders use.
SURVIVAL_KIND_FORMS = (
    "function_hash",
    "aob",
    "literal",
    "derivation",
    "table_shape",
    "slot_target",
)

# Legal evidence_kind enum values. Order is the quality ranking (live-tier
# strongest; pattern-scan-only weakest). The importer fails loud on any other
# value when last_verified_at_version is set.
EVIDENCE_KIND_ENUM = (
    "live_production",
    "live_test_plugin",
    "maintainer_ghidra",
    "predecessor_sig",
    "pattern_scan",
)
