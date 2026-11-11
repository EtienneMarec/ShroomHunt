# Decision log

Format: **D-xxx — title** · status (Proposed / Accepted / Superseded) · date.
Add new entries at the bottom; never rewrite accepted ones, supersede them.

## Decisions

**D-001 — Unreal Engine 5.8, C++ first** · Accepted · 2026-11-11
Core gameplay in C++ (Claude cannot edit `.uasset`); Blueprint subclasses for
visual setup. Project created from the First Person C++ template.

**D-002 — Git + Git LFS on GitHub** · Accepted · 2026-11-11
`.uasset/.umap` and binary art in LFS; generated folders ignored.

**D-003 — Repo language is English** · Accepted · 2026-11-11
Code, docs, commits in English; conversation in French.

**D-004 — Deterministic generation, save only deltas** · Proposed
Seeded hashing everywhere; fields, patches, weather, shapes are recomputed.
Why: small saves, reproducible bugs, "same forest every time".

**D-005 — Single runtime module organized by domain folders** · Proposed
Editor module added only when Python cannot do a task. Why: low overhead
for a solo project; split later if compile times grow.

**D-006 — Gameplay data in JSON imported to DataTables via Python** · Proposed
JSON supports nested structs/arrays (CSV does not), diffs well, is editable
without recompiling; DataTables give typed access and asset references.

**D-007 — Weather as a random-access deterministic function of time** · Proposed
Layered time noise on a seasonal climate profile + debug overrides. Why: rain
history for the spawn model and for forests not currently loaded comes for
free; nothing to simulate or save except overrides.

**D-008 — Procedural mushrooms: FDynamicMesh3 → UStaticMesh variants + ISM** · Proposed
Not ProceduralMeshComponent (no instancing/LOD). Variant pool per species ×
stage, unique mesh only for the inspected specimen.

**D-009 — PCG for vegetation, custom spawner for mushrooms** · Proposed
PCG for static scatter with a custom C++ node exposing the forest field;
mushrooms change daily and carry state, so they get their own subsystem.

**D-010 — Fab/Megascans assets are not committed** · Proposed
`Content/Fab/` and `Content/Megascans/` are git-ignored: their license very
likely forbids redistribution in a public repository, and they would exhaust
the GitHub LFS quota. README lists the assets to download. To verify against
the current Fab license before the repo goes public.

**D-011 — Time defaults** · Proposed
Day = 600 s; seasons Spring 6 / Summer 5 / Autumn 8 / Winter 5 game days
(year = 24 days ≈ 4 h); `BiologicalDaysPerGameDay = 4`. Autumn is longest
because it is the main mushroom season.

**D-012 — Low-RAM build settings** · Proposed (shader setting applied in `Config/DefaultEngine.ini`)
UBT `-MaxParallelActions=6`; shader workers limited through
`[DevOptions.Shaders] PercentageUnusedShaderCompilingThreads=65` (≈ 7 workers
on 20 threads instead of 10). To be measured during the first big shader
compile and adjusted.

## Open questions (to the developer)

1. Error consequences: options A–D in `Docs/Progression.md` §6.
2. In-game UI language: English source with French localization later, or
   French first?
3. Remove the template's Horror/Shooter variants and weapons (code + content)?
4. Starting knowledge: poster = Chanterelle + Fly agaric, Cep via a torn page?
5. Is the GitHub repository public (affects D-010)?
