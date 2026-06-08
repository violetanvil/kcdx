# Step 10 — comp-NN N-hook batch fixture

**What.** The permanent regression test for the batch-install path: a `comp-NN`
fixture that installs an N-hook set through ONE thread-suspend window and confirms
all N fire, proving the scale win (one stop-the-world cycle, not N) is real and
durable. This is the test-suite row the batch capability ships with — both the
batch mechanism (step 9) and this fixture land the feature
(`test-suite.md` — every capability ships its permanent regression plugin).
Covers E26 (`../context.md`).

**Scope (commit-grain).**
- A `comp-NN` `test-plugins/` fixture (`test-plugins/README.md` matrix row added)
  that declares an N-hook install set (N large enough to be a meaningful batch —
  the design's "TC/multiplayer-scale install set") and installs it through the
  batch path (step 9).
- The fixture self-reports via the test harness (the canonical `ACCEPT-*` signal
  into `kcdx-dev.log`, `acceptance-signal.md`): all N hooks fire → PASS; any hook
  in the set that does not fire → FAIL with the missing row named.
- The assertion is the SCALE property, not just correctness: the batch set installs
  through one suspend window (the step-9 mechanism), and all N fire — the fixture
  is the durable proof the batch path stayed wired and correct as the engine
  evolves. (If U7 fell back to per-hook at step 9, this fixture still asserts all N
  fire on the fallback path — correctness holds; the row notes the unbatched
  fallback.)

**Test bar.** This step IS the test. The `comp-NN` row passes live: N hooks
installed as a batch, all N fire, `kcdx-dev.log` `suite: X/Y passing` reflects the
new row. A FALSIFIABLE claim: any hook in the batch set that does not fire is a
FAIL. Agent builds + deploys + enables dev mode, user launches, agent reads the
log (`agent-builds-and-deploys.md`).

**Dependencies.** Step 9 (the batch mechanism must exist to test). The fixture is
the last step of the phase — it exercises the mechanism step 9 built.

**Design authority.** [`hook-backend-marriage.md §4.5, §1`](../../../design/hook-backend-marriage.md)
— the batch-install success criterion ("a fixture installing N function-entry
hooks at boot completes through a single thread-suspend window") is the §1 v1
criterion this fixture proves.

**Disassembler-test / author-burden note.** None — a test fixture; no author
surface, no game-address resolution.

**Reference.** [`../context.md`](../context.md) E26 + the §1 batch-install success
criterion.
