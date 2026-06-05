# Probe — does a pre-commit file snapshot give a robust rollback (incl. sqlite_sequence/PK)?

**Context.** maintainer-tool Phase 2 step 5 (Confirm, direct-write rework). The user
required a ROBUST rollback: "on failure nothing lands" must hold literally for EVERY
failure step, including a git failure AFTER the irreversible DB commit. The deferred-commit
`rollback(handle)` (4a/4c) resets sqlite_sequence/PK but works ONLY while the txn is HELD
(`import_to_sqlite.commit()` is irreversible — COMMITs both DBs + closes both connections,
single-use). Export/integrity/git MUST run post-commit (a fresh `export_seeds` connection
cannot see the held uncommitted txn). So a post-commit failure is past the deferred
rollback's reach. The chosen mechanism: a BACKEND file snapshot (`app/restore_point.py`)
of the two reference DB files + the data/db-export/ CSVs, captured just before commit,
restored on any post-commit-onward failure.

**The checkable unknown.** Does a file-byte snapshot of `reference.sqlite` taken AFTER the
deferred write but BEFORE `commit()` capture the *pristine pre-commit* committed state
(including sqlite_sequence / PK auto-increment), so restoring it undoes a committed change?
The risk: if the held uncommitted txn durably mutates the main `.sqlite` file's committed
pages (e.g. under `journal_mode=OFF`), the snapshot would be DIRTY and the rollback wrong.

**Outcome → meaning map (set up-front, theory-independent):**
- snapshot reads the POST-edit rows / bumped sequence → DIRTY snapshot → restore-point is
  WRONG; must capture before the write opens the connection (or the journal mode defeats it).
- snapshot reads the PRE-edit rows / original sequence → pristine; capture-after-write is
  correct, and restore resets PK too.

**Verdict: PRISTINE.** `_open_rw` (the deferred path's connection opener) is a plain
`sqlite3.connect` with NO `journal_mode=OFF` (that PRAGMA is set only in `write_db`, the
rebuild's fresh-build connection). So the deferred `BEGIN ... INSERT` uses SQLite's default
rollback journal: the main DB file's committed pages stay intact until `COMMIT`. A snapshot
taken mid-held-txn captured `[(1,'a')], seq=1` while the live DB after commit held
`[(1,'a'),(2,'b')], seq=2`; restoring the snapshot returned the live DB to `[(1,'a')],
seq=1` — sqlite_sequence reset included. The robust rollback holds.

**Consequence for the build.** `restore_point.capture()` is called AFTER the direct write
succeeds and BEFORE `data_core.commit()` — correct, and a pre-commit validation failure
pays no snapshot cost. The cost is a per-Confirm copy of the DB files (~1.3GB DEV in
production) on confirms that reach commit; accepted to honor the literal "nothing lands"
requirement (UX cornerstone > perf).

**Reusable probe wiring (reconstruct, don't re-derive):**
```python
import sqlite3, os, shutil, tempfile
d = tempfile.mkdtemp(); db = os.path.join(d, "t.sqlite")
con = sqlite3.connect(db)
con.execute("CREATE TABLE t(id INTEGER PRIMARY KEY AUTOINCREMENT, v TEXT)")
con.execute("INSERT INTO t(v) VALUES('a')"); con.commit(); con.close()
def state(p):
    c = sqlite3.connect(p)
    r = c.execute("SELECT id,v FROM t ORDER BY id").fetchall()
    s = c.execute("SELECT seq FROM sqlite_sequence WHERE name='t'").fetchone()
    c.close(); return r, s
con = sqlite3.connect(db); con.execute("BEGIN")
con.execute("INSERT INTO t(v) VALUES('b')")          # held, uncommitted
snap = os.path.join(d, "snap.sqlite"); shutil.copy2(db, snap)   # capture mid-txn
con.execute("COMMIT"); con.close()
print("snap mid-txn:", state(snap))                  # pristine: [(1,'a')], seq=1
shutil.copy2(snap, db); print("restored:", state(db))  # [(1,'a')], seq=1
```
Trust level: PRIMARY EVIDENCE (a fresh sqlite3 run, this session). Not a hypothesis.
