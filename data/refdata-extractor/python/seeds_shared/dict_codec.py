"""seeds_shared.dict_codec -- value-encoding helpers shared by every writer.

Holds the lossless cell-encoding policy the rebuild and `apply` both apply when
turning a seed/dump string into a DB cell:

  - `parse_int`  -- hex/decimal text -> INTEGER (or None)
  - `hash_blob`  -- 64-hex content_hash TEXT -> 32-byte BLOB (or None)
  - `Dicts`      -- per-(table, col) low-cardinality TEXT -> small INTEGER id,
                    materialized into `_dict_<table>_<col>` lookup tables at the
                    end of a db write.

Moved verbatim out of import_to_sqlite.py (db-updator Phase 1, step 1).
`parse_int` / `hash_blob` live here (not in row_builder.py) because they are the
column-VALUE encoding primitives -- the dump readers, the seed validators, and
the row-builder all call them; the row-builder is one of several consumers, not
their owner. Pure functions + one class; no I/O beyond the sqlite connection the
caller hands `Dicts.materialize`.
"""


def parse_int(v):
    if v is None or v == "":
        return None
    try:
        return int(v, 16) if v.startswith("0x") else int(v)
    except (ValueError, AttributeError):
        return None


def hash_blob(v):
    """64-hex TEXT -> 32-byte BLOB; '' -> None."""
    if isinstance(v, str) and len(v) == 64:
        try:
            return bytes.fromhex(v)
        except ValueError:
            return None
    return None


class Dicts:
    """Per-(table, col) value -> small INTEGER id, materialized at the end."""
    def __init__(self):
        self._d = {}   # (table, col) -> { value(str): int_id }

    def encode(self, table, col, value):
        if value is None or value == "":
            return None
        d = self._d.setdefault((table, col), {})
        return d.setdefault(value, len(d) + 1)   # 1-based ids

    def ensure(self, table, col, value):
        """Pre-register a value (so the trigger can look it up) and return id."""
        return self.encode(table, col, value)

    def materialize(self, con):
        n = 0
        for (t, c), d in self._d.items():
            con.execute(f'CREATE TABLE "_dict_{t}_{c}" (id INTEGER PRIMARY KEY, val TEXT)')
            con.executemany(f'INSERT INTO "_dict_{t}_{c}" VALUES (?,?)',
                            [(i, v) for v, i in d.items()])
            n += len(d)
        return n
