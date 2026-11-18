# M1e — Paths and landmarks (F01)

M1e is split in two:
- **M1e-1 (this part):** the path network — forest roads, rides along the
  parcel boundaries, footpaths — generated from the forest field, graded into
  the terrain, painted on the ground and kept clear of vegetation.
- **M1e-2 (next):** landmarks and orientation — named crossroads, signposts,
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
2. In **Target Layers**, `Gravel` and `Dirt` now appear without a layer info.
   For each: click the **+** icon on its row > *Weight-Blended Layer
   (normal)* > save in `Content/ShroomHunt/Forests/F01/LayerInfo`.

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
