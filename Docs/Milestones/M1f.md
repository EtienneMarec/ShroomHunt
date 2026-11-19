# M1f — Mushroom spawning

Mushrooms now grow in the forest following `Docs/SpawnModel.md`: the
mycelium lives in **persistent good spots**, and fruit bodies appear **2–3
game days after enough rain**, in season, then age (young, mature, old) and
rot away.

![Cep suitability on F01](../Media/spawn_cep_suitability.png)

*Cep (Boletus edulis) habitat suitability on F01 (black 0 → yellow 1, water in
blue, north up) with its mycelium patches (red circles). Fresh soils under
beech and oak; dry ridges, wet hollows and limestone stay empty.*

## What the code provides

| Piece | Role |
|---|---|
| `SpawnModel::Suitability` | static suitability S of a species at a point: host trees x pH x altitude x moisture x canopy x soil x edge x patchiness noise; 0 on water, paths and landmark clearances |
| `SpawnModel::GeneratePatches` | deterministic mycelium patches: candidates at `PatchDensityPerHa`, kept with probability S^1.5 |
| `SpawnModel::Trigger` | weather trigger F: phenology (month) x rain over the lagged window x mean temperature |
| `SpawnModel::RawRoll`, `FruitBodiesAt` | daily flush roll at 04:00 (P = (1 − e^(−k·S·F))), Poisson number of mushrooms, cooldown, placement by clustering pattern (fairy ring, troop, scattered...), lifetime and stages |
| **Mushroom Spawner** actor (`AMushroomSpawner`) | builds the patches at play, re-evaluates every game hour, draws mushrooms with instanced meshes (culled beyond 90 m) |
| `sh.Spawn.*` commands | stats, teleport to a mushroom, maps, offline simulation |

**Nothing is stored**: the mushrooms standing at a given time are a function of
the forest, the weather history and the time. The same weather always gives
the same mushrooms in the same places, and returning to a forest after a few
days simply evaluates that day. Cooldowns are stateless too (a flush needs no
*raw* flush in the previous cooldown days). Picking (M1g) will add the only
saved state: exhausted patches and picked mushrooms.

F01 (default weather, 2 simulated years): 23 cep patches, 13 chanterelle
patches, 7 fly agaric patches. Mushrooms appear in summer and early autumn,
none in winter and spring; peaks of ~45 ceps and ~85 chanterelles after a wet
spell. Late autumn is too cold for the cep (November ~7.5 °C, below its 8 °C
minimum). Densities will be tuned in play tests (`PatchDensityPerHa`,
`FlushRate`, `FruitsPerFlush` in `Data/Species/*.json`).

> **UE concept — Instanced Static Mesh component.** Draws many copies of one
> mesh in a single draw call, each with its own transform. The spawner keeps
> one per species x stage x shape variant and refills it when mushrooms are
> born, change stage or rot.

## Editor steps

The C++ is built. The fly agaric data changed (more presence under beech and
oak); `DT_Species` was re-imported (`content(data)` commit).

### 1. Place the spawner
1. Open `Lvl_F01_LowlandOakBeech`.
2. **Quick Add** > search `Mushroom Spawner` > add it (anywhere: it aligns on
   the landscape by itself).
3. *Details* > *World Partition*: untick **Is Spatially Loaded**.
4. *File > Save All*.

### 2. See mushrooms
1. **Play**. The game starts on the first day of autumn.
2. Console (`` ` `` or `²`): `sh.Spawn.Stats` lists, per species, the patches,
   today's weather trigger (0–1) and the mushrooms standing now.
3. `sh.Spawn.Goto` teleports you next to the nearest mushroom
   (`sh.Spawn.Goto CantharellusCibarius` for a species).
4. No mushroom (dry weather)? Make it rain and wait:
   `sh.Weather.Force Rain 24`, then `sh.Time.SkipDays 3`, then `sh.Spawn.Stats`.

### 3. Optional: maps and simulation
- `sh.Spawn.Map` writes `Saved/SpawnDebug/<Species>.png` (suitability and
  patches) for every species.
- `sh.Spawn.Simulate 2` runs two years offline and prints the mushrooms per
  species and season.

### 4. More light under the canopy (after the first play test)
The placeholder crowns are opaque, and the sky light was set to 0.5 lux in
M1a for the bare test level: the forest was too dark. The stands are now a
little thinner (`Data/Vegetation/Vegetation.json`: pole 460, mature 175, old
95 stems/ha). In `Lvl_F01_LowlandOakBeech`:
1. Select the **Sky Light** (Outliner) > *Details* > *Light* > **Intensity
   Scale**: `1.5` (the Environment Director scales it for night and clouds from
   this value). Leave *Indirect Lighting Intensity* and *Volumetric Scattering
   Intensity* at `1`.
2. Select the **Post Process Volume** > *Details*, search `leaking`: tick
   **Skylight Leaking** and set it to `0.05` (Lumen lets a little sky light into
   occluded places, like light filtering through leaves).
3. Same volume, search `compensation`: tick **Exposure Compensation** and set
   it to `0.5` (the camera adapts a little brighter in the woods).
4. Regenerate the vegetation on the whole map (M1e part 2, step 4: load the
   region, PCG **Cleanup** then **Generate**, *Save All*).

> **UE concept — Lumen sky light leaking.** Lumen computes how much of the sky
> each point sees; under dense crowns almost none, so it is nearly black.
> *Skylight Leaking* adds back a fraction of the sky light everywhere, a cheap
> way to fake light passing through foliage.

Mushrooms, stumps, logs and saplings no longer grow inside tree trunks:
understory instances are tested against the trunks around them, mushrooms
against trunks, logs and stumps (`FVegetationObstacleGrid`, test
`ShroomHunt.Vegetation.Obstacles`).

### 5. Continuous growth and fixes (second play test)
![Mushroom growth](../Media/mushroom_growth.png)

*One individual per species from button to rot (fly agaric, cep, chanterelle).*

- **Growth.** A mushroom is now one individual for its whole life: its seed
  fixes its final size, proportions, tint and the cap shapes it goes through;
  its age (0–1) moves it continuously from a closed button through the young,
  mature and old shapes of the species data to a rotting cap (sagging margin,
  browned colors, leaning stem). Meshes are cached at 10 age steps per variant
  (*Project Settings > ShroomHunt > Mushroom Meshes > Age Steps*) and the size
  grows continuously between steps. `sh.Mesh.ExportGrowth` exports the growth
  for the offline preview.
- **White / grey mushrooms.** Mushrooms are drawn by one static mesh component
  each instead of instanced meshes: the vertex colors (their albedo) did not
  show on instanced meshes. Untick *Used with Instanced Static Meshes* on
  `M_Mushroom` again if you ticked it (not needed).
- **Frozen sun after `sh.Spawn.Goto`.** A long teleport made World Partition
  unload the cell of the Environment Director. The director, *Forest Water*,
  *Forest Landmarks* and the *Mushroom Spawner* are now always loaded (set in
  C++; check *World Partition > Is Spatially Loaded* is unticked on the
  director in the level).

## Test checklist
- [ ] `sh.Spawn.Stats` lists the three species with their patch counts.
- [ ] After rain and 3 skipped days, ceps / chanterelles / fly agarics stand in
      groups (fly agarics in rings) in the woods, never on paths or in water.
- [ ] The same spot keeps its mushrooms when you come back (they age:
      young → mature → old) and they disappear after a few game days.
- [ ] In dry weather no new mushrooms appear; after a forced rain they come 2–3
      game days later.
- [ ] Mushrooms disappear beyond ~90 m (culling), no frame-rate drop.
- [ ] The forest floor is lit (shade, not darkness) at midday; nights stay dark.
- [ ] No mushroom, stump, log or sapling inside a tree trunk.
- [ ] Mushrooms show their colors; watching one spot with `sh.Time.Scale 20`,
      they grow from small buttons, open, then fade and collapse.
- [ ] The sun keeps moving after `sh.Spawn.Goto`.
- [ ] Automation: 25 tests green (`ShroomHunt.Mycology.Spawn.*` and
      `ShroomHunt.Vegetation.Obstacles` included).

## UE concepts learned
- **Instanced Static Mesh** components; actor **Tick interval**.
- Stateless simulation: deterministic hashing replaces stored random state.
- Console commands bound to the current world (`FAutoConsoleCommandWithWorld`).
