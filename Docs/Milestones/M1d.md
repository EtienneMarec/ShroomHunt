# M1d — Forest terrain (F01, lowland oak–beech, 1 km²)

M1d is split in two:
- **M1d-1 (part 1):** the deterministic *forest field* and the terrain —
  heightmap, ground layers, water network, soils, parcels and stand map — and
  the editor steps to build the forest map with a Landscape.
- **M1d-2 (part 2, end of this note):** vegetation (trees, understory, dead
  wood) placed from the field and spawned by PCG.

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
   right-click an empty spot of the graph, type `Constant3Vector` and pick it
   (a color constant appears; shortcut: hold `3` + left-click, which needs
   `Shift` on an AZERTY keyboard). Double-click its color swatch or set
   *Details > Constant* R, G, B, then wire it to the matching input:
   - Litter: R 0.20, G 0.12, B 0.06 (brown leaves)
   - Moss: R 0.10, G 0.19, B 0.05
   - Grass: R 0.17, G 0.25, B 0.07
   - Mud: R 0.09, G 0.07, B 0.05
6. Wire the Layer Blend output to **Base Color**. Right-click > `Constant`
   (shortcut `1` + click) with value `0.85` → **Roughness**.
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
   (UE centers a new landscape on this location: the Landscape actor ends up
   at -50400, -50400, its corner.)
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

---

# Part 2 — M1d-2 Forest vegetation

![Placeholder vegetation](../Media/placeholder_vegetation.png)

*Rows: alder, birch, hornbeam, beech, oak (2 saplings, 2 poles, 3 mature,
2 old, at a common scale per row), then bracken, brambles, logs and stumps
(each at its own scale). Offline preview render.*

The trees are **procedural placeholders** (D-024): stylized, but with the
right size, crown shape, bark and green for each genus. Fab/Megascans trees
can replace them later without code changes
(*Project Settings > ShroomHunt > Vegetation > Mesh Overrides*).

## What the code provides

| Piece | Role |
|---|---|
| `Data/Vegetation/Vegetation.json` | tree archetypes (size, crown, bark, foliage), forms, densities per stand age class, understory habitat ranges |
| `VegetationMeshBuilder` | builds every tree / understory mesh (pure C++, deterministic) |
| `VegetationScatter` | places trees and understory from the forest field (pure C++, unit-tested) |
| `sh.Vegetation.BakeMeshes` | saves the 55 meshes as **Nanite** static meshes `SM_<Genus>_<Form>_<n>`, `SM_Bracken_<n>`… |
| PCG node **Forest Scatter** | outputs the placed instances as PCG points with `Mesh`, `Kind` and `Genus` attributes |
| `sh.Vegetation.Stats`, `sh.Vegetation.ExportObj` | counts per kind and genus; OBJ export for the offline preview |

How placement works:
1. **Trees.** Density per stand age class (pole stands ~600 stems/ha, mature
   ~220, old ~110, a few seed trees in clear-cuts), reduced in natural canopy
   gaps. Each tree picks its **genus from the stand composition** of its 4 m
   cell (so alders line the streams and birches colonize young parcels) and
   its **form** (pole / mature / old) from the parcel's age class.
2. **Understory.** Saplings (regeneration parcels and gaps), bracken (acidic,
   light), brambles (light, richer soils), logs (old stands), stumps
   (clear-cuts): density × habitat response (moisture, pH, light = 1 − canopy).
3. **Spacing.** A global grid of candidates, each kept with probability
   density × cell area, then thinned: a candidate is removed when a stronger
   neighbor is closer than the spacing radius. Everything depends only on the
   seed and the position, so each PCG partition computes its own part and the
   result is the same as for the whole forest (tested).
4. Nothing grows in water; only alders stand right on the bank.

F01 result: ~19,600 trees (13k poles, 5.7k mature, 0.7k old) and ~26,700
understory instances on 102 ha.

> **UE concept — PCG graph.** A graph asset executed by a *PCG component*
> (here on a PCG Volume). Nodes pass **point data** (transform + attributes);
> a *Static Mesh Spawner* turns points into instanced static meshes. Our C++
> node `Forest Scatter` creates the points; the spawner reads the mesh to use
> from the `Mesh` attribute (*By Attribute* mesh selector).
>
> **UE concept — Partitioned generation.** With *Is Partitioned*, PCG splits
> the volume into grid cells (256 m by default) and generates each one in its
> own partition actor, which World Partition streams like any other actor.
>
> **UE concept — Nanite.** Virtualized geometry: the GPU streams and draws only
> the triangles visible at screen resolution, so thousands of 10k-triangle
> trees need no hand-made LODs.

## Editor steps

The C++ is already built; the **PCG** plugin is now enabled in
`ShroomHunt.uproject`. The editor stays open for all steps below.

### 1. Create the vegetation material
1. Content Browser: create `Content/ShroomHunt/Environment/Vegetation`.
2. In it: right-click > **Material**, name `M_Vegetation`, open it.
3. Right-click an empty spot > `Vertex Color` > drag its white **RGB**
   output pin (the top one) onto **Base Color**.
4. Right-click > `Constant`, value `0.85` → **Roughness**.
5. **Apply**, **Save**.

### 2. Bake the meshes
1. *Window > Output Log*, command box in **Cmd** mode:
   `sh.Vegetation.BakeMeshes`, Enter.
2. Expected line (about 30 s): `OK: 55 meshes baked (... triangles, material
   M_Vegetation) in ... s`. If it says `VertexColorMaterial`, the material was
   not found: check its name and folder, then run the command again.
3. The meshes appear in `Content/ShroomHunt/Environment/Vegetation/Generated`.
   Double-click `SM_Quercus_Mature_0`: the mesh editor shows *Nanite* enabled
   and, with **Show > Simple Collision**, the trunk capsule.

Running the command again after a change of `Vegetation.json` updates the
assets in place (levels and graphs keep their references).

### 3. Create the PCG graph
1. Content Browser, `Content/ShroomHunt/Forests/F01`: right-click > **PCG** >
   **PCG Graph** (empty graph). Name `PCG_ForestVegetation`, open it.
2. Right-click the graph, search **Forest Scatter**, add it. *Details*:
   **Layer** = *Trees*. Leave **Forest Name** = `F01_LowlandOakBeech` and
   **Align To Landscape** checked: the node places the forest on the level's
   Landscape, wherever it is.
3. Add a second **Forest Scatter** below it with **Layer** = *Understory*.
4. Right-click > **Static Mesh Spawner**, twice. Wire each Forest Scatter's
   **Out** pin to the **In** pin of one spawner.
5. Select the **trees** spawner. *Details*:
   - **Mesh Selector Type** = `PCGMeshSelectorByAttribute`;
   - under *Mesh Selector Parameters*: **Attribute Name** = `Mesh`;
   - in **Template Descriptor** > *Collision*: **Collision Presets** = `BlockAll`.
6. Select the **understory** spawner: same **Mesh Selector Type** and
   **Attribute Name**, then in **Template Descriptor**:
   - **Collision Presets** = `NoCollision`;
   - **Instance Start Cull Distance** = `12000` and **Instance End Cull
     Distance** = `15000` (cm: bracken and brambles fade out beyond ~150 m).
7. **Save**.

### 4. Place it in the forest
1. Open `Lvl_F01_LowlandOakBeech`.
2. **Quick Add** (cube with +) > search `PCG Volume` > add it.
3. *Details* > *Transform*: **Location** `0, 0, 0` (the landscape's
   center), **Scale** `504, 504, 40` (a PCG Volume is 200 units wide at
   scale 1, so it now covers the whole 1008 m forest and ±40 m of height).
4. Select its **PCG Component** (component list at the top of Details):
   - **Graph** = `PCG_ForestVegetation`;
   - **Is Partitioned** = ✔;
   - **Generation Trigger** = *Generate On Load*.
5. Click **Generate** (PCG Component details). The first run takes a few
   seconds (the forest field is generated once, then cached). Partition
   actors `PCGPartitionActor_...` appear in the Outliner.
6. *File > Save All*, then **Play**.

If trees float or are sunk, check that both Forest Scatter nodes have
**Align To Landscape** checked and that the landscape was imported with
Scale 100, 100, 100.

**Regenerating after a change** (moved volume, new graph settings, edited
`Vegetation.json`): PCG Component > **Cleanup**, then **Generate**.

## Test checklist
- [ ] `sh.Vegetation.BakeMeshes` reports 55 meshes with `M_Vegetation`.
- [ ] Old beech and oak stands: big trees, open understory with bracken; pole
      stands: dense thin trunks; clear-cuts: brambles, stumps, a few seed
      trees; regeneration parcels: many saplings.
- [ ] Alders along the stream, birches in young parcels; no tree in the
      ponds or the stream bed (compare with `debug_maps.png`, north = +X).
- [ ] The player is blocked by trunks, logs and stumps, walks through bracken.
- [ ] Viewport *View Mode > Nanite Visualization > Triangles* shows the trees
      as Nanite.
- [ ] `stat fps` / `stat unit`: note the frame time in the densest pole stand.
- [ ] `sh.Vegetation.Stats` prints ~19,600 trees and ~26,700 understory instances.
- [ ] Automation: 18 tests green (`ShroomHunt.Vegetation.*` included).

## UE concepts learned
- **PCG**: graphs, PCG Volume / component, point data and attributes, Static
  Mesh Spawner with the *By Attribute* mesh selector, partitioned generation.
- **Custom PCG node in C++**: `UPCGSettings` + `IPCGElement::ExecuteInternal`,
  point data (`UPCGBasePointData`), metadata attributes; nodes run on worker
  threads; caching (`IsCacheable`).
- **Creating assets from C++** (editor only): `CreatePackage`, mesh
  description, `UStaticMesh::Build`, Nanite settings, simple collision
  (`UBodySetup`, capsule), `UPackage::SavePackage`.
- **Nanite** meshes and instanced rendering.
