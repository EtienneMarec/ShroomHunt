# Roadmap

Each milestone ends with: compiling code, editor steps
(`Docs/Milestones/Mx.md`), a test procedure, README/CLAUDE.md updates and a
short "UE concepts learned" section. Sub-steps are individually demoable.

## M0 — Setup & design ✅ (done 2026-11-11)
Git + LFS, CLAUDE.md, design docs (architecture, species schema, spawn model,
world generation, progression, roadmap, decisions).

## M1 — Vertical slice (lowland oak–beech, 1 km²) ✅ (done 2026-11-23)
| Step | Deliverable | Demo |
|---|---|---|
| M1a Time & weather core ✅ code (editor setup: `Docs/Milestones/M1a.md`) | `UTimeSettings`, `UCalendarSubsystem`, `FWeatherModel`, `UWeatherSubsystem`, `AEnvironmentDirector` (sun/moon, fog, clouds, rain FX), debug HUD, `sh.Time.*` / `sh.Weather.*` | template map: 10-min day/night, rain arriving and clearing, forced storm |
| M1b Species data ✅ (`Docs/Milestones/M1b.md`) | `FMushroomSpeciesRow`, `import_data.py`, `DT_Species` with 3 species, `USpeciesDatabaseSubsystem` | DataTable visible in editor, reimport after a JSON edit |
| M1c Procedural mushrooms ✅ (`Docs/Milestones/M1c.md`) | `FMushroomMorphology`, `FMushroomMeshBuilder`, `UMushroomMeshCache`, debug "specimen garden" actor | grid of 3 species × stages × variants |
| M1d-1 Forest field & terrain ✅ (`Docs/Milestones/M1d.md`) | `FForestField`, heightmap/weightmap bake, debug maps, landscape import steps, landscape material | walk on the 1 km² terrain |
| M1d-2 Forest vegetation ✅ (`Docs/Milestones/M1d.md`, part 2) | placeholder procedural trees (beech, oak, hornbeam, birch, alder) + understory baked to Nanite meshes, PCG `Forest Scatter` node | walk in a 1 km² forest |
| M1e-1 Path network ✅ (`Docs/Milestones/M1e.md`) | roads (A*, MST + loop), rides on parcel boundaries, footpaths, terrain grading, gravel/dirt layers, vegetation clearance | walk the roads and rides |
| M1e-2 Landmarks & water ✅ | named crossroads and signposts, parcel stones, base house, hunting hut, bridge, calvary, pond and stream water surfaces | follow a ride to a named crossroads |
| M1f Spawning ✅ (`Docs/Milestones/M1f.md`) | suitability, patches, daily flushes, ISM streaming, debug maps | rain → mushrooms 2–3 days later in predictable spots |
| M1g-1 Foraging ✅ (`Docs/Milestones/M1g.md`) | aim, examine with observable criteria, pick, basket, patch exhaustion | find, examine, pick |
| M1g-2 Knowledge ✅ (`Docs/Milestones/M1g.md`, part 2) | poster + torn page, names of known species, minimal encyclopedia | identify |
| M1h Rain polish ✅ (`Docs/Milestones/M1h.md`) | wind heading, rain slanted by the wind, streak material, ground splashes (CPU collision events), less rain and dripping leaves under the canopy, rain sound, wet materials (`MF_Wetness`) | walk from a clearing into a dense stand in the rain |

## V1 — Visual pass (brought forward from M6, `Docs/Milestones/V1.md`)
Semi-realistic look for the demo before M2 (D-033): Megascans trees mapped onto
the vegetation palette (beech, hornbeam, alder first), forest floor materials,
PCG ground debris (leaves, twigs, small plants, stones), understory assets,
lighting pass, Sequencer camera path, README screenshots and demo video.
Third-party assets stay out of the repository (D-010); placeholders remain the
fallback.

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
**Forest build automation**: an editor tool (`ShroomHuntEditor` module) builds a
forest map from its JSON in one command — bake, Landscape creation with its
material and layers, forest actors, PCG volume and generation — replacing the
manual steps of M1d/M1e (D-023).

## M6 — Polish
Audio ambience (birds by time/season, rain, wind, footsteps per surface),
post-process & lighting pass, UI polish, performance pass,
README screenshots/video, packaged build.

## Later
Cleaning, cooking & tasting; paper map & compass; forests 3–6; photo mode.
