# ShroomHunt

*A contemplative first-person mushroom-foraging game in procedurally generated
French forests — Unreal Engine 5.8, C++.*

> **Status: pre-alpha — design phase (milestone M0).** Screenshots will be
> added as soon as the vertical slice (M1) is playable.

<!-- Screenshots / GIF go here: Docs/Media/*.png -->

## Pitch

Walk through vast French forests that change with the hour, the weather and
the seasons. Learn where and when mushrooms grow — after the rain, under the
right trees, on the right soil — and learn to tell a prized cep from a
bitter bolete, or a chanterelle from a toxic jack-o'-lantern. Sell your
harvest, buy field guides and test kits, fill your encyclopedia and unlock
new forests, from lowland oak woods to subalpine larches.

## Features (planned)

- **Deterministic procedural forests** inspired by French regions, with local
  biomes from noise fields (altitude, moisture, pH, soil, exposure), parcels,
  a hierarchical path network and believable landmarks.
- **Mycologically grounded spawning**: static habitat suitability (host
  trees, pH, moisture, light) creates persistent good spots; recent rain,
  temperature and season trigger flushes after a growth delay.
- **Procedural mushroom meshes**: each species is a set of parameter ranges,
  each individual a deterministic draw (cap shape, gills/pores, stem, ring,
  volva, colors, age stage).
- **Day/night cycle, nuanced weather and short seasons.**
- **Progressive identification**: unknown mushrooms only show observable
  criteria; books, test kits and keys let you identify them; dangerous
  lookalikes matter.
- Debug tools: suitability/probability heat maps, time acceleration, forced
  weather.

## Architecture

C++ core with thin Blueprint subclasses for presentation; pure-C++
deterministic models (weather, forest field, spawn model, mesh builder)
wrapped by UE subsystems; gameplay data in JSON imported into DataTables;
UE5 PCG for vegetation scatter, World Partition streaming.

Design documents:
[Architecture](Docs/Architecture.md) ·
[Species data](Docs/SpeciesData.md) ·
[Spawn model](Docs/SpawnModel.md) ·
[World generation](Docs/WorldGeneration.md) ·
[Progression](Docs/Progression.md) ·
[Roadmap](Docs/Roadmap.md) ·
[Decision log](Docs/Decisions.md)

## Building

Requirements: Windows 10/11, Unreal Engine **5.8**, Visual Studio 2022 with
the "Game development with C++" workload (see `.vsconfig`), Git LFS.

```bash
git lfs install
git clone https://github.com/EtienneMarec/ShroomHunt.git
```

1. Right-click `ShroomHunt.uproject` → *Generate Visual Studio project files*.
2. Open `ShroomHunt.sln`, select *Development Editor | Win64*, build.
3. Open `ShroomHunt.uproject`.

Third-party environment assets (Fab / Megascans) are not included in the
repository; the list of required free assets will be documented here.

## License

Original code, data, tools and docs: [MIT](LICENSE). Files derived from
Unreal Engine templates and Unreal Engine content fall under the Unreal
Engine EULA; third-party assets are not included and keep their own licenses.
