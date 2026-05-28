# Reference-data dumps

This directory holds per-game-version Ghidra extractor output. Each subdir is
one full dump from `WHGame.dll`, identified by the short dotted game version:

```
dump/
├── README.md                  (this file; tracked)
├── refdata-1.5.1164953/       (one dump per KCD2 game version; gitignored)
│   ├── functions/             (Ghidra pass output; CSV per RVA shard)
│   ├── statements/            (Ghidra pass output)
│   ├── referenced_vars/       (Ghidra pass output)
│   ├── call_edges/            (Ghidra pass output)
│   ├── signatures/            (Python pass output)
│   ├── caller_reg_args/       (Python pass output)
│   └── _MANIFEST.md           (the Ghidra extractor's run-record)
├── refdata-1.6.xxxxxxx/       (added when KCD2 ships a new build)
└── ...
```

## Naming convention

`refdata-<game-version>/` where `<game-version>` is the dotted form derived
from the engine's branch tag — e.g. `release_1_5_1164953_841` → `1.5.1164953`.
The extractor (`../run-parallel.ps1`) derives this automatically from the
`-VersionTag` argument and writes the dump there.

## What's tracked vs. gitignored

- **Tracked:** this README only.
- **Gitignored:** every `refdata-*/` subdir (each is ~1.3 GB of derived
  binary data that regenerates from the extractor + a copy of `WHGame.dll`).

## How the downstream tools find a dump

- `../python/validate_db_shape.py` and `../sandbox/make_sandbox.py` both
  default to the **highest-version-ordinal** dir under `dump/` when no
  explicit dump_dir argument is passed. Pass an explicit path as the first
  argument to override.
- `../run-parallel.ps1` writes a NEW dir per game version. If a dir for the
  target game version already exists, the extractor stops without
  overwriting — delete the dir manually to re-extract.
