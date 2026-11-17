# M1d — Forest terrain (F01, lowland oak–beech, 1 km²)

M1d is split in two:
- **M1d-1 (this note):** the deterministic *forest field* and the terrain —
  heightmap, ground layers, water network, soils, parcels and stand map — and
  the editor steps to build the forest map with a Landscape.
- **M1d-2 (next):** PCG vegetation (trees, understory, dead wood) driven by the field.

![F01 debug maps](../Media/forest_f01_maps.png)

*F01 (north up). Top: terrain with streams and ponds, moisture (wetness index),
soils (sand, loam, clay, limestone, peat). Bottom: pH, dominant tree genus with
parcel borders (beech green, oak olive, hornbeam teal, birch pale, alder blue),
canopy (pale = clear-cut / regeneration).*

## What the code provides

| Piece | Role |
|---|---|
| `Data/Forests/F01_LowlandOakBeech.json` | the forest's parameters (seed, relief, water, soils, parcels, tree palette) |
| `FForestDefinition` / `ForestData::LoadForest` | JSON → struct (UE's `FJsonObjectConverter`) + validation |
| `FForestField` | pure C++ generator and sampler of every layer |
| `ForestBake` | heightmap (16-bit PNG), weightmaps, debug maps, report |
| `UForestGenerationLibrary::BakeForest`, `sh.Forest.Bake` | run the bake from Python or the console |

How the field is built (all from the seed):
1. **Terrain** — domain-warped gradient-noise fBm for the hills + a valley network
   where low-frequency noise crosses zero (1 m grid).
2. **Hydrology** (4 m grid) — *priority-flood* depression filling, D8 flow
   directions, flow accumulation. Cells draining more than 5 ha become streams
   and are carved into the terrain; the 3 deepest reasonable depressions become
   ponds, other hollows stay damp.
3. **Moisture** — topographic wetness index `ln(upslope area / tan slope)`,
   smoothed, normalized by percentiles, boosted near water, reduced on south slopes.
4. **Soils and pH** — limestone and sand patches from noise (exact area shares
   via percentiles), clay in wet ground, peat around ponds and bogs.
5. **Parcels** — ~8 Voronoi cells of ~13 ha with an age class (clear-cut …
   old stand) and a dominant genus, like French state forest parcels.
6. **Stands** — per 4 m cell, weights of beech/oak/hornbeam/birch/alder from
   their moisture and pH ranges, the parcel's main species, pioneers in young
   stands and alders along the water.
7. **Canopy and edges** — from the age class, minus natural gaps.

> **UE concept — Landscape.** UE's terrain: a heightfield split into
> components with automatic LOD. A *Landscape Material* blends *paint layers*
> (weightmaps) such as litter, moss, grass and mud. A heightmap is a 16-bit
> grayscale image: value 32768 is height 0; with a Z scale of 100, one unit is
> 1/128 m.

## Editor steps

The editor can stay open for all of M1d-1 (no C++ change to load).

### 1. Bake the forest
The files are already baked on this machine; to regenerate them (after
editing the JSON): *Window > Output Log*, command box in **Cmd** mode, type
`sh.Forest.Bake` and press Enter. Output:
`<project>/Saved/ForestBake/F01_LowlandOakBeech/` — `heightmap.png`,
`weight_Litter.png`, `weight_Moss.png`, `weight_Grass.png`, `weight_Mud.png`,
`debug_maps.png`, `bake_report.txt`.

### 2. Create the landscape material (before importing, so layers exist)
1. Content Browser: create `Content/ShroomHunt/Environment/Landscape`.
2. In it: right-click > **Material**, name `M_Landscape_Forest`, open it.
3. Right-click the graph, search **Landscape Layer Blend**, add it.
4. Select the node. In *Details > Layers*, click **+** four times. For each
   entry set **Layer Name** and keep **Blend Type** = *LB Weight Blend*:
   `Litter`, `Moss`, `Grass`, `Mud` (exact names: they must match the files).
5. The node now has four inputs *Layer Litter*, *Layer Moss*… For each one:
   hold `3` and left-click in the graph (a color constant appears), set its
   value in *Details > Constant*, and wire it to the matching input:
   - Litter: R 0.20, G 0.12, B 0.06 (brown leaves)
   - Moss: R 0.10, G 0.19, B 0.05
   - Grass: R 0.17, G 0.25, B 0.07
   - Mud: R 0.09, G 0.07, B 0.05
6. Wire the Layer Blend output to **Base Color**. Hold `1` + click for a
   constant `0.85` → **Roughness**.
7. **Apply**, **Save**. (Plain colors for now; real textures come with the
   vegetation assets.)

### 3. Create the forest map
1. *File > New Level…* > **Empty Open World** > *Create*.
2. *File > Save Current Level As…*: create the folders
   `Content/ShroomHunt/Forests/F01`, name the level `Lvl_F01_LowlandOakBeech`, *Save*.

> **UE concept — World Partition.** "Open World" levels split actors into
> grid cells streamed around the player; each actor is saved as its own file
> under `__ExternalActors__`. Essential later for larger forests.

### 4. Import the landscape
1. Switch to Landscape mode: top-left toolbar dropdown (currently **Selection
   Mode**) > **Landscape** (or `Shift+2`).
2. In the Landscape panel: tab **Manage** > **New** > choose **Import from File**.
3. **Heightmap File**: click **...** and pick
   `Saved/ForestBake/F01_LowlandOakBeech/heightmap.png`.
4. **Material**: pick `M_Landscape_Forest`. The **Layers** list shows Litter,
   Moss, Grass, Mud.
5. For each layer row:
   - click the icon to create its **Layer Info** > *Weight-Blended Layer
     (normal)*, save it in `Content/ShroomHunt/Forests/F01/LayerInfo`;
   - in its file field pick the matching `weight_<Layer>.png`.
6. Transform: **Location** 0, 0, 0 — **Rotation** 0 — **Scale** 100, 100, 100.
7. Size (usually auto-filled from the file): **Section Size** *63x63 Quads*,
   **Sections Per Component** *2x2*, **Number of Components** 8 x 8,
   **Overall Resolution** 1009 x 1009.
8. Click **Import**. Back to *Selection Mode* (`Shift+1`). *File > Save All*.

If a field name differs in your editor version, tell me what you see: the
values above are the important part.

### 5. Sky, player and environment
1. *Window > Env. Light Mixer*: create **Sky Light**, **Atmospheric Light 0**,
   **Sky Atmosphere**, **Volumetric Cloud**, **Height Fog**. Check the sun and
   the sky light are **Movable** and Real Time Capture is on (as in M1a).
2. Add the moon and a Post Process Volume exactly as in M1a steps 2.4 – 2.5.
3. Drag `BP_EnvironmentDirector` into the level.
4. **Quick Add > Basic > Player Start**; move it above the terrain near the
   center and press `End` to drop it on the ground.
5. *Window > World Settings* > **GameMode Override**: `BP_FirstPersonGameMode`.
6. *Edit > Project Settings > Maps & Modes*: **Editor Startup Map** and **Game
   Default Map** = `Lvl_F01_LowlandOakBeech`.
7. *File > Save All*, then **Play**.

## Test checklist
- [ ] The landscape is ~1 km wide, ~40 m of relief, gentle hills and a valley.
- [ ] A stream bed runs down the valley; three hollows will hold ponds (water
      surfaces come in M1e).
- [ ] Ground colors: brown litter under the stands, green moss in damp shade,
      grass in clearings / clear-cuts, dark mud along the stream.
- [ ] Day/night and weather work in the new map (director + HUD).
- [ ] Compare with `debug_maps.png` (north = +X): ponds and stream match.
- [ ] Automation: 15 tests green (`ShroomHunt.Forest.*` included).

## UE concepts learned
- **Landscape**: heightmap import, components/sections, Z scale encoding.
- **Landscape materials and layers** (Layer Blend, Layer Info, weightmaps).
- **World Partition** levels.
- **FJsonObjectConverter**: JSON ↔ USTRUCT by reflection.
- **ImageWrapper**: writing PNG files (16-bit) from C++.
- **ParallelFor** for heavy per-pixel generation.
