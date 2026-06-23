export const meta = {
  name: 'ki0028-vanilla-init-fs-map',
  description: 'Map vanilla CryEngine init+FS end-to-end from the binary, then diff kcdx against it to surface the deviation that flips the KI-0028 level-load abort',
  whenToUse: 'KI-0028 boot black-screen: full FS-takeover swap aborts the level load; output-level probes exhausted. Build a complete vanilla reference map + structural kcdx diff.',
  phases: [
    { title: 'Inventory', detail: 'one cheap agent reads the _research corpus → coverage map + gap list' },
    { title: 'Fronts', detail: 'gap-scoped parallel disassembly fronts (≤5, only for Phase 0 gaps)' },
    { title: 'VanillaMap', detail: 'synthesize fronts + priors → VANILLA-MAP.md (re-grounded per AP19)' },
    { title: 'KcdxDiff', detail: 'lay kcdx src against the vanilla map → KCDX-DIFF.md, ranked deviations' },
    { title: 'Gate', detail: 'orchestrator gates the diff vs settled facts → top candidate causes' },
  ],
}

const DIR = '_research/ki0028-vanilla-init-fs-map'

// ---- Shared context every agent gets (the settled facts + scope + reuse ladder) ----
const COMMON = `
PROJECT: kcdx — an SKSE-class extender for Kingdom Come: Deliverance 2 (CryEngine-derived, D3D12).
kcdx performs a CCryPak vtable-pointer swap at construct-time to become the engine's filesystem:
the engine calls kcdx for every file op (open/read/metadata/enumerate); other slots THUNK to the
engine original. The bug KI-0028: a FULL swap makes the boot level-load ABORT in C_Game::CreateInstance
(→ MessageBoxA "level can't be loaded") → black screen. A NO-OP swap (all slots thunk) loads the
level fine → menu. So the cause is in kcdx's slot LOGIC, NOT the swap mechanism/timing.

REVERSE-ENGINEERING DISCIPLINE (mandatory):
- WHGame.DLL has NO PDB. Nearest-export labels (e.g. "ffxFsr2ResourceIsNull+0x...") are MISLEADING
  noise — never treat a nearest-export name as the real function. Ground every claim in the BODY.
- AP19: read every call-edge in the OWNING function body; never infer a callee from a name alone.
- Reuse-first ladder: Address Library / prior _research dumps → predecessor sigs → wiki → fresh
  Ghidra LAST. Re-deriving a settled prior finding is a defect. Cite the prior doc.
- Honest uncertainty over invention: a call-edge you cannot verify is marked "unverified", never guessed.
- pCryPak global = 0x18492B850 (gEnv 0x18492B800 + 0x50). Game version release_1_5_1164953_841.

SETTLED FACTS — inputs, NOT open questions. Do NOT re-probe or contradict these:
- The swap MECHANISM is innocent (no-op swap renders; cause is slot LOGIC).
- kcdx serves every byte correctly (zero index-pak opens return result=0).
- kcdx enumerates a strict SUPERSET of vanilla (zero real drops across 190 walks).
- IsFileExist over-report (kcdx=1/vanilla=0) is BENIGN (file then serves fine).
- resourcelist.txt misses are BENIGN (those files exist nowhere; vanilla misses too).
- The abort is a DELIBERATE engine decision (a tested condition → MessageBox), not a crash/failed serve.

APEX QUESTION orienting every phase: "What does the level-load abort TEST, and which kcdx
deviation flips that tested condition?" A finding not serving that question gets ONE line, not pursuit.

SCOPE — IN: boot/init → CSystem::Init → subsystem order → CCryPak construct/seat → pak discovery+mount
→ search-path/pakPriority registration → AdjustFileName/resolve → open → handle lifecycle → the level
load (C_Game::CreateInstance FS-driving inner path) → THE TESTED CONDITION gating the abort →
the CCryPak vtable surface (each slot's vanilla body).
OUT (spend NO tokens): render/PSO/swapchain/present internals; Lua/scripting/save-load/console/hooks;
re-confirming settled facts; any thunked slot not touched on boot→level-load; mod-precedence beyond
what the level-load path exercises.

RAW DUMPS go to disk under ${DIR}/ ; only the DIGEST is returned. Cite prior docs by path.
`

// ============================ Phase 0 — Inventory ============================
phase('Inventory')
const COVERAGE_SCHEMA = {
  type: 'object',
  required: ['regions', 'corpusDocs', 'notes'],
  properties: {
    regions: {
      type: 'array',
      items: {
        type: 'object',
        required: ['region', 'status', 'mappedBy', 'gap'],
        properties: {
          region: { type: 'string', description: 'one of: boot-init-order, ccrypak-seat-construct, pak-discovery-mount, searchpath-pakpriority, resolve-adjust-open, handle-lifecycle, level-load-abort, vtable-slot-surface' },
          status: { type: 'string', enum: ['fully-mapped', 'partial', 'gap'] },
          mappedBy: { type: 'array', items: { type: 'string' }, description: 'prior _research doc paths that already cover this region (with the specific finding)' },
          gap: { type: 'string', description: 'what is NOT yet mapped and needs a fresh front; empty if fully-mapped' },
        },
      },
    },
    corpusDocs: { type: 'array', items: { type: 'string' }, description: 'every _research doc found relevant to the IN-scope FS/init path, by path' },
    notes: { type: 'string', description: 'cross-cutting observations; any region the abort question hinges on that is under-mapped' },
  },
}
const coverage = await agent(
  `${COMMON}

PHASE 0 — INVENTORY (read-only, reuse-first). Read the existing _research corpus and produce a COVERAGE MAP.
For EACH IN-scope region (boot-init-order, ccrypak-seat-construct, pak-discovery-mount, searchpath-pakpriority,
resolve-adjust-open, handle-lifecycle, level-load-abort, vtable-slot-surface): determine whether the prior
corpus ALREADY maps it (cite the doc + the specific finding) or leaves a GAP needing fresh disassembly.

START with these high-value priors (read their digests/markdown, NOT the raw _*.txt dumps unless needed):
- ${DIR}/../phase8.5-pak-resolver/ : front1-full-vtable-surface.md, front2-open-mount-archive.md,
  front3-handle-consume-read-path.md, front4_resolution_decision_tree.md, RESOLUTION-OWNERSHIP-synthesis.md,
  MECHANISM-CONFIRMED-pakpriority-loose.md, subresolver-decompiled-mechanism.md, searchpath-registrar-mechanism2.md
- ${DIR}/../init-cycle-recon/ , ${DIR}/../ccrypak-init-order-recon/
- ${DIR}/../fs-takeover-pak-mount-recon/ , fs-takeover-readslot-abi-recon/ , fs-takeover-slot35-recon/ , fs-takeover-slot101-callers-recon/
- ${DIR}/../ki0027-find-data-abi-recon/ , ki0028-findfirst-straddle-recon/ , ki0028-metadata-consumer-recon/
- ${DIR}/../ki0028-adjustfilename-consumer-recon/ , ki0028-window-exit-gate-recon/
- ${DIR}/../probe-archive/p1-ccrypak-construction-order.md and any p*-*ordering*.md
Also sweep ${DIR}/.. for any OTHER relevant doc you find (glob the dir names).

CRITICAL: the 'level-load-abort' region (what C_Game::CreateInstance tests before the MessageBox) is the
apex. If no prior doc maps the abort's tested condition, that is the #1 GAP — flag it loudly in notes.

Return the COVERAGE_SCHEMA object. Do NOT disassemble anything new — this phase only inventories what exists.`,
  { label: 'inventory', phase: 'Inventory', schema: COVERAGE_SCHEMA, effort: 'medium' }
)

log(`Coverage: ${coverage.regions.filter(r => r.status === 'gap').length} gaps, ${coverage.regions.filter(r => r.status === 'partial').length} partial, ${coverage.regions.filter(r => r.status === 'fully-mapped').length} fully-mapped`)
log(`Notes: ${coverage.notes.slice(0, 240)}`)

// ============================ Phase 1 — Fronts ============================
// Only spawn a front for a region that is 'gap' or 'partial'. Fully-mapped → straight to synthesis.
phase('Fronts')
const FRONT_SCHEMA = {
  type: 'object',
  required: ['region', 'steps', 'unverified'],
  properties: {
    region: { type: 'string' },
    steps: {
      type: 'array',
      description: 'the ordered vanilla steps in this region, each grounded',
      items: {
        type: 'object',
        required: ['order', 'what', 'evidence'],
        properties: {
          order: { type: 'number' },
          what: { type: 'string', description: 'the vanilla engine action/step' },
          evidence: { type: 'string', description: 'body RVA (e.g. 0x180xxxxxx) or prior-doc cite grounding it' },
          fsRole: { type: 'string', description: 'what filesystem participation this step has (open/mount/resolve/enum/metadata/none)' },
        },
      },
    },
    abortCondition: { type: 'string', description: 'ONLY for the level-load-abort front: the exact tested condition that gates the abort, with its body evidence; empty otherwise' },
    unverified: { type: 'array', items: { type: 'string' }, description: 'call-edges / claims this front could NOT verify from a body' },
    rawDumpPath: { type: 'string', description: 'path under the dir where raw decompiles were written' },
  },
}

const gapRegions = coverage.regions.filter(r => r.status === 'gap' || r.status === 'partial')
log(`Spawning ${gapRegions.length} gap-scoped front(s): ${gapRegions.map(r => r.region).join(', ')}`)

const fronts = await parallel(
  gapRegions.map(r => () =>
    agent(
      `${COMMON}

PHASE 1 FRONT — region "${r.region}". Status from inventory: ${r.status}.
Already mapped by (CONSUME these first, cite them, do NOT re-derive): ${(r.mappedBy || []).join(' ; ') || '(none cited)'}.
The GAP to close: ${r.gap || '(fill/confirm the partial mapping)'}.

Map ONLY this region's vanilla behavior to the depth the apex question needs (what flips the level-load abort).
- Consume the cited priors FIRST. Read only the BODIES needed to close the gap, grounded per AP19.
- ${r.region === 'level-load-abort' ? 'THIS IS THE APEX FRONT: find the EXACT tested condition in C_Game::CreateInstance (or its FS-driving callees) that gates the "level cannot be loaded" MessageBox. Trace which file op / resolve / mount-state / handle / enumeration result that condition reads. Put it in abortCondition with body evidence.' : 'Note in one line where this region feeds the level-load-abort path.'}
- Write raw decompiles to ${DIR}/raw-${r.region}.txt (or a _*.txt under ${DIR}). Return only the digest.
- Mark every call-edge you cannot verify from a body as unverified — never guess.

Return the FRONT_SCHEMA object.`,
      { label: `front:${r.region}`, phase: 'Fronts', schema: FRONT_SCHEMA }
    )
  )
).then(rs => rs.filter(Boolean))

log(`Fronts complete: ${fronts.length} returned. Unverified edges: ${fronts.reduce((n, f) => n + (f.unverified?.length || 0), 0)}`)

// ============================ Phase 2 — Vanilla map synthesis ============================
phase('VanillaMap')
const fullyMapped = coverage.regions.filter(r => r.status === 'fully-mapped')
const vanillaMap = await agent(
  `${COMMON}

PHASE 2 — SYNTHESIS. Assemble the single ordered end-to-end VANILLA INIT+FS MAP:
boot → CSystem::Init → subsystem order → CCryPak construct/seat → pak discovery+mount →
searchpath/pakPriority → AdjustFileName/resolve → open → handle lifecycle → level load →
THE ABORT'S TESTED CONDITION.

INPUTS:
- Fronts (fresh-mapped gap regions), as JSON digests:
${JSON.stringify(fronts, null, 1)}
- Fully-mapped regions to pull from their cited priors (read the docs, re-ground the load-bearing steps):
${JSON.stringify(fullyMapped.map(r => ({ region: r.region, mappedBy: r.mappedBy })), null, 1)}
- The full corpus doc list: ${JSON.stringify(coverage.corpusDocs)}

DISCIPLINE: RE-GROUND each load-bearing step and every cross-front call-edge in the OWNING body before
asserting it (AP19 — synthesis re-grounds, it does not blindly stitch). Where a front marked something
unverified, carry that marker forward. The map must culminate in the abort's tested condition stated in
falsifiable terms (what value, read from what FS op, in what state, makes the engine choose to abort).

Write the full map to ${DIR}/VANILLA-MAP.md (ordered sections per stage; each step with its evidence).
Return a SHORT digest: the ordered stage list + the abort condition + any still-unverified load-bearing edge.`,
  { label: 'vanilla-map', phase: 'VanillaMap', effort: 'high' }
)
log(`VANILLA-MAP.md written. Digest head: ${String(vanillaMap).slice(0, 300)}`)

// ============================ Phase 3 — kcdx diff (the deliverable) ============================
phase('KcdxDiff')
const DIFF_SCHEMA = {
  type: 'object',
  required: ['rows', 'rankedDeviations'],
  properties: {
    rows: {
      type: 'array',
      description: 'one row per vanilla step',
      items: {
        type: 'object',
        required: ['vanillaStep', 'kcdxBehavior', 'verdict'],
        properties: {
          vanillaStep: { type: 'string' },
          kcdxBehavior: { type: 'string', description: 'what kcdx does at this step, with src cite (file:line)' },
          verdict: { type: 'string', enum: ['identical', 'thunks-to-original', 'different'] },
          howDifferent: { type: 'string', description: 'for "different": exactly how kcdx deviates' },
        },
      },
    },
    rankedDeviations: {
      type: 'array',
      description: 'every "different" row, ranked by likelihood of flipping the level-load abort (most likely first)',
      items: {
        type: 'object',
        required: ['deviation', 'rank', 'whyItCouldFlipAbort', 'evidence'],
        properties: {
          deviation: { type: 'string' },
          rank: { type: 'number' },
          whyItCouldFlipAbort: { type: 'string', description: 'the mechanism by which this deviation could perturb the abort tested condition' },
          evidence: { type: 'string', description: 'kcdx src cite + vanilla-map step it diverges from' },
          contradictsSettledFact: { type: 'boolean', description: 'true if this "deviation" contradicts an established fact (→ likely a synthesis error, not a real finding)' },
        },
      },
    },
  },
}
const diff = await agent(
  `${COMMON}

PHASE 3 — THE KCDX DIFF (the deliverable). Lay kcdx's implementation against the vanilla map and produce,
for EVERY vanilla step, what kcdx does: IDENTICAL / THUNKS-to-original / DIFFERENT (and exactly how).

VANILLA MAP: read ${DIR}/VANILLA-MAP.md in full (the Phase 2 output).
KCDX SOURCE (read the relevant bodies, cite file:line):
- src/fs_takeover/vtable_table.cpp — the 102-slot table: which slots are Kcdx vs Thunk, per family.
- src/fs_takeover/seating_hook.cpp — the construct-time seat + index build trigger + swap call.
- src/fs_takeover/vtable_swap.cpp/.h — the vtable-pointer swap mechanism (settled innocent; for context).
- src/fs_takeover/asset_index.cpp/.h — the asset index build (IndexPakRoot, NormalizeVPath, alias fold).
- src/fs_takeover/open_slots.cpp — AdjustFileName/FOpen resolve+open (slots 1/35/36).
- src/fs_takeover/read_slots.cpp , file_handle.cpp/.h — handle rep ((id<<1)|1) + read family.
- src/fs_takeover/metadata_slots.cpp — existence/size/stat (slots 13/45/67/68/69/70/92/93).
- src/fs_takeover/enum_slots.cpp , find_slots.cpp — ForEachFile + FindFirst/Next/Close (14/63/64/65).
IGNORE the *_probe.cpp files (drawcall/present/pso/dispatch/reswap/boot_watch — diagnostic scratch, not FS logic).

For each DIFFERENT row, rank by likelihood of flipping the level-load abort. Candidate deviation classes to
weigh: handle-representation mismatch a consumer dereferences; ordering/timing of mount vs index; a mount
the vanilla path performs that kcdx's index build does NOT replicate (search-path/pakPriority registration
state the level load reads); a state mutation or side effect a vanilla slot body had that kcdx's reimpl
dropped; an enumeration/metadata answer shape the abort condition reads differently than a raw byte.

If a "deviation" contradicts a SETTLED FACT, mark contradictsSettledFact=true (it is probably a synthesis
error, deprioritize). Write the full table to ${DIR}/KCDX-DIFF.md. Return the DIFF_SCHEMA object.`,
  { label: 'kcdx-diff', phase: 'KcdxDiff', effort: 'high' }
)

const realDeviations = (diff.rankedDeviations || []).filter(d => !d.contradictsSettledFact).sort((a, b) => a.rank - b.rank)
log(`KCDX-DIFF.md written. ${diff.rows.length} rows; ${realDeviations.length} real deviations (top: ${realDeviations[0]?.deviation?.slice(0, 80) || 'none'})`)

// ============================ Phase 4 — Gate + report ============================
phase('Gate')
return {
  coverageSummary: {
    gaps: coverage.regions.filter(r => r.status === 'gap').map(r => r.region),
    fullyMapped: coverage.regions.filter(r => r.status === 'fully-mapped').map(r => r.region),
  },
  frontsRun: fronts.map(f => f.region),
  abortCondition: fronts.find(f => f.abortCondition)?.abortCondition || '(see VANILLA-MAP.md)',
  topDeviations: realDeviations.slice(0, 6),
  deliverables: [
    `${DIR}/COVERAGE-MAP.md (inventory)`,
    `${DIR}/VANILLA-MAP.md (end-to-end vanilla reference)`,
    `${DIR}/KCDX-DIFF.md (the diff + ranked deviations)`,
  ],
}
