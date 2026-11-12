# Roadmap

Each milestone ends with: compiling code, editor steps
(`Docs/Milestones/Mx.md`), a test procedure, README/CLAUDE.md updates and a
short "UE concepts learned" section. Sub-steps are individually demoable.

## M0 — Setup & design ✅ (done 2026-11-11)
Git + LFS, CLAUDE.md, design docs (architecture, species schema, spawn model,
world generation, progression, roadmap, decisions).

## M1 — Vertical slice (lowland oak–beech, 1 km²)
| Step | Deliverable | Demo |
|---|---|---|
| M1a Time & weather core ✅ code (editor setup: `Docs/Milestones/M1a.md`) | `UTimeSettings`, `UCalendarSubsystem`, `FWeatherModel`, `UWeatherSubsystem`, `AEnvironmentDirector` (sun/moon, fog, clouds, rain FX), debug HUD, `sh.Time.*` / `sh.Weather.*` | template map: 10-min day/night, rain arriving and clearing, forced storm |
| M1b Species data | `FMushroomSpeciesRow`, `import_data.py`, `DT_Species` with 3 species, `USpeciesDatabaseSubsystem` | DataTable visible in editor, reimport after a JSON edit |
| M1c Procedural mushrooms | `FMushroomMorphology`, `FMushroomMeshBuilder`, `UMushroomMeshCache`, debug "specimen garden" actor | grid of 3 species × stages × variants |
| M1d Forest terrain | `FForestField`, heightmap/weightmap bake, landscape import steps, landscape material, PCG trees (oak, beech, hornbeam, birch) + understory with custom field node | walk in a 1 km² forest |
| M1e Paths & landmarks | parcels, roads/rides/footpaths, splines, 3 landmarks (hunting hut, pond with bridge, calvary/crossroads) | follow a ride to a named crossroads |
| M1f Spawning | suitability, patches, daily flushes, ISM streaming, debug maps | rain → mushrooms 2–3 days later in predictable spots |
| M1g Interaction & knowledge | interaction/inspection components, basket, poster + torn page, minimal encyclopedia, observable criteria | find, examine, identify, pick |

**After M1d — rain polish** (needs the real forest): wind-driven slant
(wind speed from the weather), streak texture, ground splashes via particle
depth-buffer collision, less direct rain and dripping leaves under dense
canopy, rain sounds, wet materials through `MPC_Environment.Wetness`.

## M2 — Seasons
Season-driven temperature/weather, foliage color & leaf fall (`MPC_Season`),
litter/moss/snow layers, phenology fully active, winter species
(e.g. wood blewit, oyster mushroom), spring morels & St George's mushroom.

## M3 — Base & economy
Forester's house (spawn, storage, encyclopedia desk), selling with
quality/freshness, shop (books, kits), identification keys UI, pharmacist
check, chosen error consequences.

## M4 — Save & content
Complete save/load (all systems, versioned), 12–15 species including the
key dangerous confusions (death cap / green russulas, jack-o'-lantern /
chanterelle, bitter bolete / cep), more landmarks.

## M5 — Second forest
Mountain beech–fir forest (Vosges/Jura): second `ForestDefinition`, climate,
altitude-driven biomes, forest unlock & travel, PCG runtime generation for
bigger maps, HLOD.

## M6 — Polish
Audio ambience (birds by time/season, rain, wind, footsteps per surface),
post-process & lighting pass, UI polish, performance pass,
README screenshots/video, packaged build.

## Later
Cleaning, cooking & tasting; paper map & compass; forests 3–6; photo mode.
