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

**D-004 — Deterministic generation, save only deltas** · Accepted (M0 review) · 2026-11-11
Seeded hashing everywhere; fields, patches, weather, shapes are recomputed.
Why: small saves, reproducible bugs, "same forest every time".

**D-005 — Single runtime module organized by domain folders** · Accepted (M0 review) · 2026-11-11
Editor module added only when Python cannot do a task. Why: low overhead
for a solo project; split later if compile times grow.

**D-006 — Gameplay data in JSON imported to DataTables via Python** · Accepted (M0 review) · 2026-11-11
JSON supports nested structs/arrays (CSV does not), diffs well, is editable
without recompiling; DataTables give typed access and asset references.

**D-007 — Weather as a random-access deterministic function of time** · Accepted (M0 review) · 2026-11-11
Layered time noise on a seasonal climate profile + debug overrides. Why: rain
history for the spawn model and for forests not currently loaded comes for
free; nothing to simulate or save except overrides.

**D-008 — Procedural mushrooms: FDynamicMesh3 → UStaticMesh variants + ISM** · Accepted (M0 review) · 2026-11-11
Not ProceduralMeshComponent (no instancing/LOD). Variant pool per species ×
stage, unique mesh only for the inspected specimen.

**D-009 — PCG for vegetation, custom spawner for mushrooms** · Accepted (M0 review) · 2026-11-11
PCG for static scatter with a custom C++ node exposing the forest field;
mushrooms change daily and carry state, so they get their own subsystem.

**D-010 — Fab/Megascans assets are not committed** · Accepted · 2026-11-11
`Content/Fab/` and `Content/Megascans/` are git-ignored: the repository is
**public**, the Fab license very likely forbids redistributing the assets,
and they would exhaust the GitHub LFS quota. README lists the assets to
download. Re-check the Fab license text when the first assets are imported.

**D-011 — Time defaults** · Accepted · 2026-11-11
Day = 600 s; seasons Spring 6 / Summer 5 / Autumn 8 / Winter 5 game days
(year = 24 days ≈ 4 h); `BiologicalDaysPerGameDay = 4`. Autumn is longest
because it is the main mushroom season.

**D-012 — Low-RAM build settings** · Accepted (to be measured) · 2026-11-11
UBT `-MaxParallelActions=6`; shader workers limited through
`[DevOptions.Shaders] PercentageUnusedShaderCompilingThreads=65` (≈ 7 workers
on 20 threads instead of 10), applied in `Config/DefaultEngine.ini`. To be
measured during the first big shader compile and adjusted.

**D-013 — Error consequences** · Accepted · 2026-11-11
Selling a toxic specimen: option B (fine, reputation crash, temporary buyer
ban). Eating one (with cooking, later): option C (real symptom latency,
hospital, lost days and money). Option D (permadeath for deadly species) is
an opt-in difficulty setting. See `Docs/Progression.md` §6.

**D-014 — In-game text: English source, localization-ready** · Accepted · 2026-11-11
All player-facing strings are `FText` in English; a French localization is
planned (Localization Dashboard). French vernacular species names are data.

**D-015 — Starting knowledge** · Accepted · 2026-11-11
The base poster teaches Chanterelle and Fly agaric; the Cep is learned from a
torn guide page found at the hunting hut (M1).

**D-016 — Template variants removed** · Accepted · 2026-11-11
Horror and Shooter variants (code, content, maps) and `Content/Weapons` are
deleted, together with the AI/StateTree module dependencies and plugins only
they needed. Kept: `FirstPerson` (character, controller, test map),
`Characters` (first-person arms), `Input`, `LevelPrototyping` (grey-box meshes
used by the test map).

**D-017 — MIT license for original work** · Accepted · 2026-11-11
Original code, data, tools and docs are MIT (`LICENSE`). Template-derived
files (Epic copyright header) and engine content stay under the Unreal Engine
EULA; third-party assets are excluded (D-010). New source files carry no Epic
header.

**D-018 — Weather calibration and debug HUD** · Accepted · 2026-11-11
Default lowland climate tuned by the `ClimateCalibration` test: ~14 % rainy
hours, ~2.7 mm/day (~1 000 mm/year), rare natural heavy rain (≥ 3 mm/h,
mostly storms). Debug HUD is an on-screen message list (no widget asset) so
it works without editor setup.

**D-019 — Environment director preserves the authored level** · Accepted · 2026-11-11
The director auto-finds sky actors and uses their authored intensities/fog as
the "clear day" reference. The editor preview only rotates the sun and moon,
never intensities, so previewing cannot alter the saved level.

## Open questions (to the developer)

- None at the moment.
