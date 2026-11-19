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

**D-018b — Cloud coverage not driven for now** · Accepted · 2026-11-18
Driving the engine cloud material's coverage through a dynamic material
instance crashed the editor twice (renderer assertion on a stale uniform
expression cache). `AEnvironmentDirector::bDriveCloudCoverage` is off; light,
fog and rain still follow the weather. To come back with a project cloud
material reading `MPC_Environment.CloudCover` (rain polish milestone).

**D-019 — Environment director preserves the authored level** · Accepted · 2026-11-11
The director auto-finds sky actors and uses their authored intensities/fog as
the "clear day" reference. The editor preview only rotates the sun and moon,
never intensities, so previewing cannot alter the saved level.

**D-020 — Night ambient floor through the moon light** · Accepted · 2026-11-12
Real-time sky light capture returns black at night, so moonless nights were
pitch black. The moon light now carries a minimum "starlight / sky glow"
intensity (`NightAmbientFraction`, 15 % of full moon) from a fixed high
direction, blended towards the real moon direction as the moon rises, so
there is no shadow jump. Gameplay over strict realism: foraging at night
must stay possible.

**D-021 — Species JSON is parsed and validated in C++** · Accepted · 2026-11-12
Refines D-006. The compact JSON is converted by `SpeciesJson` (C++) instead of
Python, so the same validator serves the editor import, the automation tests
(real files + error detection) and a development hot reload
(`sh.Species.Reload`). Python stays a thin asset-creation script. In
development builds the game falls back to the JSON files when `DT_Species` is
missing. Player-facing texts get stable localization keys
(`ShroomHunt.Species`, `<Id>.<Field>`).

**D-022 — Procedural mushroom geometry and preview pipeline** · Accepted · 2026-11-17
Implements D-008. Cap, stem, ring and sac volva are revolved 2D profiles with
one shape function per `ECapShape` / `EStemShape`; gills/ridges are real
double-sided fins on the near LOD only; warts and volva scales are small
pyramids. Albedo in vertex colors (sRGB), surface id in UV1, so one material
covers every species. The mesh cache is an engine subsystem (editor + game).
Shapes are reviewed headless through OBJ export + a stdlib Python renderer, and
an automation test checks winding through the signed volume. (Fixed 2026-11-18: the first version used right-handed winding and rendered
inside out in UE; the renderer now culls back faces with UE's convention and the
tests use the engine's normal formula.)

**D-023 — Forest field implementation and terrain bake** · Accepted · 2026-11-18
Forest parameters in `Data/Forests/<Id>.json`, read with `FJsonObjectConverter`
(no DataTable until runtime needs it). Terrain uses gradient (Perlin-style)
noise: value noise produced grid-aligned streaks. Hydrology: priority-flood +
D8 + flow accumulation; only `MaxPonds` qualifying depressions hold water (the
first version flooded every hollow: 13 ha of ponds). Soil shares are exact via
percentile thresholds. The bake writes PNG files and the landscape is imported
by hand (one-time per forest; automating Landscape creation needs an editor
module). Tree genera are names shared with species hosts (`Fagus`, `Quercus`…).

**D-024 — Placeholder procedural vegetation, PCG scatter node** · Accepted · 2026-11-18
Trees are procedural placeholders first (Megascans later, for the visual polish):
stylized C++ meshes (tapered trunk, branches, lumpy foliage clumps in a
per-genus crown envelope) for 5 genera x 4 forms (sapling, pole, mature, old)
plus bracken, brambles, logs and stumps, from `Data/Vegetation/Vegetation.json`.
They are **baked to Nanite static mesh assets** (`sh.Vegetation.BakeMeshes`,
~16 MB in LFS) because PCG mesh spawners reference assets by path; transient
runtime meshes cannot be. Real assets replace them later through
`UVegetationSettings::MeshOverrides` without code change.
Placement is our own pure C++ (`VegetationScatter`: global jittered candidate
grid, keep probability = density x cell area, Matern hard-core thinning), not
PCG samplers: it is unit-tested, identical for any partition split, and shares
the forest field with mushroom spawning. The PCG node `Forest Scatter` only
outputs those points (Mesh/Kind/Genus attributes); PCG does the instancing,
partitioning and streaming (D-009). The node is not cacheable because it reads
`Data/` files PCG cannot track; the field is cached in `ForestFieldCache`.

**D-025 — Path network generated in the forest field** · Accepted · 2026-11-18
Paths are a stage of `FForestField` (after the parcels), so the terrain bake,
the vegetation and later the mushrooms all see the same network. Roads: A* on
the 4 m grid (cost `1 + k × grade²`, ponds with a 4 m margin forbidden,
streams costly), minimum spanning tree of entrances/base/hubs plus the loop
that saves the longest detour, discount on existing roads so they merge.
Rides: exact Voronoi edges of the parcel sites (straight *lignes* and star
crossroads, as in French state forests), not routed. Footpaths: noisy-cost
Dijkstra to the network. Grid routes are simplified (8 m tolerance) and
Chaikin-smoothed: plain A* routes looked like staircases. The terrain is graded
to a smoothed profile per path (roads: camber and ditches) and two weightmaps
(Gravel, Dirt) are baked; the landscape is re-imported in place with the
editor's Import tool. No splines yet: M1e-2 adds them only if signposts,
bridges or PCG dressing need them.

**D-026 — Landmarks, place names and water from the forest field** · Accepted · 2026-11-18
Landmarks are a field stage (after the paths) so the vegetation can keep clear
of them and later systems (mushrooms, map, quests) share them. They are built
by two always-loaded actors (`AForestLandmarkSet`, `AForestWater`) as transient
placeholder meshes, not saved assets: they are unique, few and cheap to
rebuild, and nothing references them by path (unlike PCG meshes, D-024). Place
names are French proper nouns from `Data/Names/PlaceNames.json`, chosen from
the surroundings and not translated (the game text stays English, D-014).
Water: ponds are flat at their spill height over their whole basin plus two
rings of banks; streams are ribbons along their D8 centerlines just above the
carved bed, 3 m wider than the bed. Edge vertices standing above the ground are
pulled just below it ("skirt"); a test checks that no edge of the surface shows.
Ponds are rebuilt on the 1 m heightmap (8-connected flood below the level, then
bank rings), streams as 5-vertex cross-sections following the real carved bed
(the first versions on the 4 m grid showed steps, saw teeth and dry beds).
Paths do not grade within ~3 m of a stream (the bed stays open under bridges);
bridges rest on the graded banks 6 m beyond the water. UE's Water plugin was not used: experimental,
heavy, and it would duplicate our own hydrology.

**D-027 — Stateless mushroom spawning** · Accepted · 2026-11-18
The spawn model of `Docs/SpawnModel.md` is implemented as pure functions
(`SpawnModel`): live fruit bodies at time t = f(forest, weather history, t).
Cooldowns use *raw* rolls of the previous days instead of stored history, so
any time can be evaluated directly and the answer never changes when the
player comes back later. A `AMushroomSpawner` actor per forest map builds the
patches at play, re-evaluates every game hour and draws instanced meshes; the
weather triggers are cached per (species, day, lag bucket of 0.25 real day).
The fly agaric got more presence under beech and oak (affinity 0.5, 1.6
patches/ha): it is on the starting poster (D-015) and birches are rare in F01.
Play-test tuning knobs: `PatchDensityPerHa`, `FlushRate`, `FruitsPerFlush`.

**D-028 — Continuous mushroom growth, one component per mushroom** · Accepted · 2026-11-19
Morphology is a function of (species, seed, age in [0, 1]): the seed fixes the
individual (its "genome": final size, proportions, tint, irregularities, the
cap shapes it goes through), the age interpolates between five keyframes
(button, young, mature, old, rotten) with blended cap profiles. The stage API
remains as ages of the same individual. Meshes are cached at `AgeSteps` (10)
per variant, scaled continuously between steps. Mushrooms are drawn by pooled
static mesh components: vertex colors did not show on instanced meshes, and
M1g needs one pickable object per mushroom.

**D-029 — Foraging without editor assets** · Accepted · 2026-11-19
The forager (aiming, examine, pick) is a component added at play to the
player's pawn by the Mushroom Spawner; its input actions and mapping context
are created in C++ and its HUD is a UMG widget built in C++. The template
character Blueprint stays untouched and nothing must be authored to play;
widget Blueprints can replace the C++ layout during the UI polish (M6).
Picks are the only foraging state: picked fruit seeds (filtered out of the
stateless spawn model) and pick times per patch (exhaustion), held by a game
instance subsystem until the save system (M4).

**D-030 — Identification in M1: source + one examined specimen** · Accepted · 2026-11-19
A species is *Identified* (real name on its specimens, full encyclopedia page)
once the player owns a source teaching it (poster, guide page, later books)
**and** has examined one specimen, in either order: examining is the
comparison with the source. The dichotomous keys of Docs/Progression.md
replace that comparison when the books arrive (M3); *Mastered* comes with
quality grading. Sources are data (`Data/Knowledge/Sources.json`: id, kind,
host landmark kind, title, intro, species); they hang on the first landmark of
their kind as placeholder sheets built by the *Forest Landmarks* actor and are
found by a visibility trace, so walls hide them. A *Page* is taken when read,
a *Poster* stays.

**D-031 — Rain under the canopy from the forest field** · Accepted · 2026-11-20
The rain felt by the player depends on the canopy cover of the forest field
under the camera (the same field that drives the tree density), not on traces
against the tree meshes (they only have trunk collision, and traces per frame
would cost more). A pure `LocalRain::Compute` turns intensity, wetness and
canopy into direct rain, drops from leaves (lasting while the forest is wet)
and a wind factor; Niagara draws them through user parameters. The wind
heading is part of the deterministic weather model.

## Open questions (to the developer)

- None at the moment.
