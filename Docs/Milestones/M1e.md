# M1e — Paths and landmarks (F01)

M1e is split in two:
- **M1e-1 (this part):** the path network — forest roads, rides along the
  parcel boundaries, footpaths — generated from the forest field, graded into
  the terrain, painted on the ground and kept clear of vegetation.
- **M1e-2 (part 2, end of this note):** landmarks and orientation — named crossroads, signposts,
  parcel stones, the forester's house at the base, a hunting hut, a bridge, a
  calvary — and water surfaces for the ponds and the stream.

![F01 debug maps with paths](../Media/forest_f01_maps.png)

*Top-left tile: roads in white, rides in yellow, footpaths in red (north up).*

## What the code provides

| Piece | Role |
|---|---|
| `Data/Forests/F01_LowlandOakBeech.json` | new *Paths* parameters: entrances, hubs, loops, grade cost, widths, footpaths |
| `ForestPaths` (pure helpers) | A* on a grid, simplification, Chaikin smoothing, resampling, exact Voronoi edges |
| `FForestField::GeneratePaths` | builds the network after the parcels, then grades the terrain (`RasterizePaths`) |
| `FForestSample::PathDistanceM / PathRank` | distance to the nearest path, used by the vegetation and later by mushrooms (edge species) |
| `ForestBake` | heightmap with graded paths, two new weightmaps `weight_Gravel.png` and `weight_Dirt.png`, paths on `debug_maps.png` |

How the network is built (all from the seed):
1. **Terminals.** Two **entrances** on gentle border cells (where public roads
   arrive), the **base** (future forester's house) 50–100 m inside the first
   entrance, and four **hubs** on flat, dry ground at least 220 m apart.
2. **Forest roads.** A* on the 4 m grid with cost per meter
   `1 + 800 × grade² + noise`, ponds forbidden (with a 4 m margin), streams
   allowed at a cost (culverts). The roads form the **minimum spanning tree**
   of the terminals plus **one loop** (the connection that saves the longest
   detour). A road that reaches an existing road follows it at a third of the
   cost, so roads merge into trunk roads with **junctions**.
3. **Rides.** French state forests are cut by straight grassy *lignes* along
   parcel boundaries. The parcels are Voronoi cells, so the rides are the
   **exact Voronoi edges**, meeting at **star crossroads** (Voronoi vertices),
   cut where they would enter a pond.
4. **Footpaths.** Winding paths (strong noise in the cost) from the network
   to the pond shores, then to old stands.
5. **Smoothing and grading.** Grid routes are simplified, rounded (Chaikin)
   and resampled every 2 m; each path gets a smoothed height profile. The
   terrain is blended to that profile across the path and its shoulders;
   roads get a slight camber and side ditches.
6. **Surfaces.** Gravel on roads with dirt shoulders, grass with two dirt
   wheel ruts on rides, dirt on footpaths. Roads and rides open the canopy
   (more light, *edge* conditions along them); trees keep 1.5 m away from
   path surfaces, understory 0.3 m.

F01: 6 road sections (2.5 km), 12 rides (3.8 km), 3 short footpaths, 5 star
crossroads; steepest road stretch 14 % over 10 m.

> **UE concept — Landscape edit: Import tool.** *Landscape mode > Manage >
> Import* replaces the heights and paint layers of an existing landscape from
> image files, without recreating the actor (its components, the map and the
> PCG setup stay).

## Editor steps

The C++ is built and the bake files are already in
`Saved/ForestBake/F01_LowlandOakBeech/` (regenerate with `sh.Forest.Bake`).

### 1. Add the path layers to the landscape material
1. Open `M_Landscape_Forest`, select the **Landscape Layer Blend** node.
2. *Details > Layers*: **+** twice, **Layer Name** `Gravel` then `Dirt`
   (exact names), Blend Type *LB Weight Blend*.
3. Wire a color constant (right-click > `Constant3Vector`) to each new input:
   - Gravel: R 0.42, G 0.40, B 0.36 (pale grey gravel)
   - Dirt: R 0.26, G 0.19, B 0.12 (bare earth)
4. **Apply**, **Save**.

### 2. Create the layer infos
1. Open `Lvl_F01_LowlandOakBeech`, switch to **Landscape** mode (`Shift+2`),
   tab **Paint**.
2. UE 5.8 does not add new material layers to the landscape by itself. On the
   **Target Layers** header row, click the **sync** icon (two arrows, tooltip
   *Create Layers From Assigned Materials*): `Gravel` and `Dirt` appear. If
   the icon is greyed out (*All Material Layers Created*), the material does
   not have the layers yet: check their exact names, **Apply** and **Save**.
3. For each new row: click its **+** icon (*Create Layer Info*) >
   *Weight-Blended Layer (normal)* > save in
   `Content/ShroomHunt/Forests/F01/LayerInfo`.

### 3. Re-import heights and layers
1. Landscape mode, tab **Manage**, tool **Import**.
2. Keep **Import** selected (not Export). **Heightmap File**: `...` >
   `Saved/ForestBake/F01_LowlandOakBeech/heightmap.png`.
3. In the layer list, set each layer's file to the matching
   `weight_<Layer>.png` (6 layers: Litter, Moss, Grass, Mud, Gravel, Dirt).
4. **Import Mode**: *All*. Click **Import**, then back to *Selection Mode*
   (`Shift+1`).

If a field name differs, tell me what you see.

### 4. Regenerate the vegetation
1. Select the **PCG Volume** > its **PCG Component**: **Cleanup**, then
   **Generate** (trees now leave the paths free).
2. *File > Save All*, **Play**.

## Test checklist
- [ ] Gravel roads from the two entrances (north border) wind through the
      forest, merge at junctions and form one loop; no road through a pond.
- [ ] Straight grassy rides with two dirt ruts follow the parcel boundaries
      and meet at star crossroads; they stop at the ponds.
- [ ] Short dirt footpaths lead to the pond shores.
- [ ] Paths are graded: flat across, smooth along, roads with ditches; no
      tree on a path, light and brambles along roads and rides.
- [ ] Compare with `debug_maps.png` (north = +X: the top of the image is +X).
- [ ] Automation: 20 tests green (`ShroomHunt.Forest.Path*` included).

## UE concepts learned
- Landscape **Import** tool (re-import heights and weightmaps in place) and
  new **paint layers / layer infos** on an existing landscape.
- Path finding (A*) and graph algorithms (minimum spanning tree, Voronoi) in
  plain C++ feeding the terrain bake.

---

# Part 2 — M1e-2 Landmarks and water

![Placeholder landmarks](../Media/placeholder_landmarks.png)

*Bridge, calvary, forester's house, hunting hut, hunting tower (mirador), log
pile, parcel stone, signpost arrow, signpost (offline preview, each at its
own scale).*

## What the code provides

| Piece | Role |
|---|---|
| `FForestField::GenerateLandmarks` | places the landmarks after the paths; vegetation keeps clear of them |
| `Data/Names/PlaceNames.json` + `ForestPlaceNames` | French place names chosen from the surroundings (pond, young parcel, tree genus), unique per forest |
| `LandmarkMeshBuilder` | placeholder meshes with box collisions |
| **Forest Landmarks** actor (`AForestLandmarkSet`) | builds every landmark, the signpost texts and arrows, the parcel numbers |
| **Forest Water** actor (`AForestWater`) | one surface for the ponds (flat at their spill level) and the streams (just above their bed) |
| `sh.Forest.ExportLandmarks` | OBJ export of the landmark meshes for the offline preview |

Placement rules (all from the seed):
- **Forester's house** beyond the end of the road from the first entrance,
  door toward the road, in a 28 m clearing.
- **Hunting hut** near the hub closest to a clear-cut or a regeneration, in a
  small clearing; **2 hunting towers** at stand edges facing an open parcel.
- **Calvary** at a road junction.
- **Signposts** at the star crossroads, hubs, road junctions and entrances,
  with the place name and arrow boards toward the neighboring named places
  (name and distance along the path).
- **Parcel stones** at the star crossroads, painted with the numbers of the
  surrounding parcels (as in French state forests).
- **Bridges** wherever a road, ride or footpath crosses a stream (stone for
  roads, wooden plank bridges otherwise); **4 log piles** along the roads
  next to mature stands.

F01: 34 landmarks (14 signposts, 5 parcel stones, 2 towers, 6 bridges,
4 log piles...) and 15 named places such as *Barrière du Vergne*.

> **UE concept — Text Render component.** Draws text in the world (signs,
> labels) with a size in centimeters; it faces its local +X axis.
>
> **UE concept — Is Spatially Loaded.** World Partition streams actors by
> their position. Actors that span the whole map (water, landmark set) must
> be *always loaded*, or they disappear when their cell unloads.

## Editor steps

The C++ is built. The bake in `Saved/ForestBake/F01_LowlandOakBeech/` is up to
date (grass clearing around the house).

### 1. Create the water material
1. Content Browser: create `Content/ShroomHunt/Environment/Water`.
2. In it: right-click > **Material**, name `M_Water`, open it.
3. Right-click > `Constant3Vector` → **Base Color**: R 0.02, G 0.035, B 0.04
   (dark water: the color comes from reflections).
4. Right-click > `Constant`, value `0.05` → **Roughness** (mirror-like).
5. **Apply**, **Save**.

### 2. Re-import the ground layers
The clearings around the house and the hut changed the weightmaps: repeat
part 1 step 3 (*Landscape mode > Manage > Import*, heightmap + 6 weightmaps,
**Import**).

### 3. Place the water and the landmarks
1. Open `Lvl_F01_LowlandOakBeech`.
2. **Quick Add** (cube with +) > search `Forest Water` > add it. It moves
   onto the landscape by itself and shows the ponds and the stream.
3. **Quick Add** > search `Forest Landmarks` > add it. Buildings, signposts,
   stones and bridges appear (a few seconds the first time).
4. For **both** actors: *Details* > *World Partition* > untick **Is Spatially
   Loaded**.
5. If a material is missing (water or buildings show flat debug colors),
   select the actor and set its **Water Material** / **Material** property.

### 4. Clear the vegetation around the landmarks
PCG Volume > PCG Component: **Cleanup**, then **Generate**. *File > Save All*,
then **Play**.

## Test checklist
- [ ] The three ponds and the stream have a water surface; banks hide its
      edges; no water floating above the ground or over the roads.
- [ ] The forester's house stands in a grassy clearing at the end of the road
      from the first entrance, its door facing the road, with
      *Maison forestière* above the door.
- [ ] Signposts at crossroads read e.g. *Carrefour du Gros Chêne*; their
      arrows point along the paths to the next named places with distances.
- [ ] Parcel stones at the star crossroads show the parcel numbers.
- [ ] Bridges cross the stream where paths do, the player can walk on them.
- [ ] The hunting hut, the two towers (facing open parcels), the calvary and
      the log piles are there; the player collides with all of them.
- [ ] No tree grows through a building.
- [ ] Automation: 21 tests green (`ShroomHunt.Forest.Landmarks` included).

## UE concepts learned
- Building transient static meshes with collision at runtime (`UBodySetup`
  boxes) and **construction script components** (rebuilt in the editor).
- **Text Render** components; World Partition **always loaded** actors.
