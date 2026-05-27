# reference.sqlite — the function reference database

`reference.sqlite` is the static reference database the kcdx engine consults at
launch. It carries, per WHGame.dll function, the data the engine needs to keep
installed plugins working across game updates and to install callback hooks.

## Not in this repo — it ships as a release asset

The database is a **generated binary artifact**, not a tracked file. It is built
once per KCD2 version and **distributed with the kcdx release** (bundled in the
release archive). After a user unpacks a kcdx release, the engine finds
`reference.sqlite` in its install directory and opens it directly — there is
nothing for the user to download separately, configure, or decompress.

- **Download size:** ~22 MB (the release archive compresses it).
- **Installed size:** ~50 MB on disk.
- **Launch cost:** negligible. The engine memory-maps the file and reads only the
  rows for the functions a user's installed plugins actually touch — a handful of
  milliseconds even for a large modlist. The file size does not affect launch
  speed.

## What it contains (the user-facing tables)

| Table | Purpose |
|---|---|
| `functions` | per-function `rva`, `auto_name`, the byte `content_hash` (the cross-version survival check), the inferred `signature` (the ABI a callback hook needs to marshal arguments), and `decompile_quality` |
| `signatures` | the per-function argument-width signature floor (merged over `functions`) |
| `caller_reg_args` | a caller-side register-argument estimate — a non-authoritative tighter bound on argument count |

These are the only tables a mod **user** needs:

1. **Cross-version survival.** When KCD2 updates, the engine compares the current
   on-disk hash of each function a plugin touched against the hash the plugin was
   authored against. Unchanged → the plugin keeps working silently. Genuinely
   changed → a clear warning naming the function, and the entry proceeds (or skips,
   if the author marked it safety-critical). Plugins do not break wholesale on
   every game update — only when a function they specifically target actually
   changed.
2. **Hook installation.** A callback hook (`kcdx.hook`) needs the target
   function's argument shape to marshal arguments into the plugin's callback;
   compiled code carries no runtime-queryable signature, so the engine reads it
   here.

The discovery/inspection tables (per-statement metadata, the call graph) that back
the authoring tools are **not** in this user database — a separate, larger dataset
serves those, fetched by mod authors when building plugins.

## Encoding

The file is a stock SQLite database, losslessly encoded for size: byte hashes are
stored as 32-byte blobs (not hex text), repetitive low-cardinality columns are
dictionary-encoded into integer keys with `_dict_*` lookup tables, and address /
count columns are integers. No special SQLite build or extension is required to
read it.

## Per-version refresh

A new `reference.sqlite` is built for each KCD2 version and shipped with the kcdx
release that supports it. A function keeps a stable identity across versions, so a
plugin that targets a function by name or id continues to resolve it even when the
game updates and the function moves.
