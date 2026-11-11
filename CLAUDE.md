# CLAUDE.md — ShroomHunt working agreement

> **Read this file at the start of every session**, then skim `Docs/Decisions.md`
> (decision log + open questions) and `Docs/Roadmap.md` (current milestone).
> Update this file whenever a new convention, method or decision appears.

## 1. Project in one paragraph

ShroomHunt is a contemplative first-person mushroom-foraging game set in
procedurally generated French forests (Unreal Engine 5.8, C++ first). The player
walks deterministic forests with a day/night cycle, weather and seasons, picks
mushrooms whose appearance follows a real mycological model, learns to identify
them, sells them, buys guides and test kits, fills an encyclopedia and unlocks
new forests. Full design: `Docs/`.

## 2. People and communication

- Solo developer, computer-science engineering student, **first Unreal project**:
  comfortable with programming, beginner with UE.
- Conversation with the developer is in **French**. **Everything written in the
  repo is in English** (code, identifiers, comments, commits, docs, this file).
- When introducing a UE concept, explain it briefly. For anything done in the
  editor, give **step-by-step instructions with exact menu and property names**.
- The repo is a **CV showcase**: keep README, docs and history clean.

## 3. Environment

| Item | Value |
|---|---|
| Engine | UE **5.8** (launcher build) at `C:/Program Files/Epic Games/UE_5.8` |
| Project | `ShroomHunt.uproject`, single runtime module `ShroomHunt` (from First Person C++ template) |
| IDE | Visual Studio 2022 (MSVC 14.44, see `.vsconfig`) |
| Machine | i7-13650HX (14C/20T), RTX 4090 Laptop 16 GB VRAM, **16 GB RAM (the bottleneck)** |
| Shells | Git Bash and PowerShell (Windows 11) |

### Memory budget rules (16 GB RAM)
- Compile with a capped number of parallel actions (see §6). Never run a full C++
  build and a shader-compile storm at the same time if avoidable.
- Shader workers: `PercentageUnusedShaderCompilingThreads` raised in
  `Config/DefaultEngine.ini` (see `Docs/Decisions.md` D-012).
- Windows page file must stay system-managed on the SSD (≥ 32 GB effective).
- World Partition cells and PCG generation radius are sized for low RAM
  (`Docs/WorldGeneration.md`). Prefer instancing (ISM/HISM via PCG) over actors.
- Keep the editor viewport at Epic/High scalability while authoring; Cinematic
  only for screenshots.

## 4. Repository layout

```
ShroomHunt/
├─ CLAUDE.md, README.md
├─ Config/                 UE config (text, versioned)
├─ Content/                UE assets (.uasset/.umap via Git LFS)
│  ├─ ShroomHunt/          ALL project-owned assets live under here
│  │  ├─ Core/             game mode, player BPs, input
│  │  ├─ Environment/      sky, weather, seasons (MPC_Season), materials
│  │  ├─ Forests/<ForestId>/   map, PCG graphs, landmarks for one forest
│  │  ├─ Mushrooms/        mushroom materials, mesh cache settings
│  │  ├─ Data/             DataTables / DataAssets generated from /Data
│  │  ├─ UI/               WBP_* widgets
│  │  └─ Debug/            debug materials and widgets
│  ├─ Fab/                 third-party Fab/Megascans assets (NOT versioned, see D-010)
│  └─ (template folders)   FirstPerson, Characters, Input, LevelPrototyping…
├─ Data/                   source-of-truth gameplay data (JSON), imported by Tools/
│  ├─ Species/             one JSON file per species
│  └─ Forests/             one JSON file per forest definition
├─ Docs/                   design docs, decision log, milestone notes
├─ Source/ShroomHunt/      C++ runtime module (see §5)
└─ Tools/Python/           Unreal editor Python scripts (data import, batch setup)
```

## 5. C++ conventions

- Follow the **Epic C++ coding standard**: type prefixes `U` (UObject), `A`
  (Actor), `F` (struct/plain), `E` (enum), `I` (interface), `T` (template);
  booleans `b`; PascalCase everywhere; tabs; braces on their own line.
- **No global project prefix** on class names: domain names (`UCalendarSubsystem`,
  `FMushroomSpeciesRow`). Check for engine name collisions before adding a class.
- Source folders by domain inside `Source/ShroomHunt/`:
  `Core/ Time/ Weather/ Forest/ Mycology/ Mushroom/ Player/ Knowledge/
  Economy/ Save/ UI/ Debug/`. Template code stays where it is until cleaned up.
  New folders must be added to `PublicIncludePaths` in `ShroomHunt.Build.cs`
  only if needed; prefer includes relative to the module root
  (`#include "Weather/WeatherSubsystem.h"`).
- UObject pointer members: `UPROPERTY() TObjectPtr<UFoo>` (GC safety). Soft
  references (`TSoftObjectPtr`) for assets referenced from data.
- `UPROPERTY` categories: `"ShroomHunt|<Domain>"`.
- One log category per domain: `LogShroomTime`, `LogShroomWeather`,
  `LogShroomForest`, `LogShroomSpawn`, `LogShroomMesh`, `LogShroomKnowledge`,
  `LogShroomSave` (declared in `Core/ShroomLog.h`).
- **Pure logic in plain C++ (`F` structs / namespaces), UObject wrappers thin.**
  Weather model, forest field, suitability/spawn math and mesh builder are
  plain C++ so they are unit-testable and deterministic.
- **Determinism**: never use `FMath::Rand*` in generation. Use the hash helpers
  in `Core/ShroomHash.h` (seed + coordinates/ids → value) or a local
  `FRandomStream` seeded from such a hash. Same seed ⇒ same forest, same
  mushroom shapes.
- Prefer subsystems (`UGameInstanceSubsystem`, `UWorldSubsystem`) over
  singletons/actors-with-managers. Avoid per-actor `Tick`; let subsystems drive
  updates at the rate they need.
- Tunable values: `UDeveloperSettings` subclasses (appear in *Project Settings*,
  saved to `Config/Default*.ini` as text) or data files — not magic numbers.
- C++ base classes are `Abstract`/`Blueprintable` where the developer extends
  them in Blueprint (visual setup, asset references). Gameplay logic stays in C++.

## 6. Build, run, test

Close the editor (or at least disable Live Coding) before building from the
command line: UBT refuses to build while Live Coding is active.

```bash
# Build editor target (Development)
"/c/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat" ShroomHuntEditor Win64 Development -Project="$(pwd -W)/ShroomHunt.uproject" -WaitMutex -MaxParallelActions=6
```

```bash
# Regenerate Visual Studio project files (also: right-click .uproject > Generate Visual Studio project files)
"/c/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat" -projectfiles -project="$(pwd -W)/ShroomHunt.uproject" -game -engine
```

```bash
# Run automation tests headless (filter "ShroomHunt.")
"/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$(pwd -W)/ShroomHunt.uproject" -ExecCmds="Automation RunTests ShroomHunt.;Quit" -unattended -nopause -nullrhi -nosplash -log
```

- Automation tests live next to the code in `Source/ShroomHunt/<Domain>/Tests/`
  (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`, names `ShroomHunt.<Domain>.<Test>`).
  Priority: determinism and math of pure-logic modules.
- `-MaxParallelActions=6` keeps compile RAM under control (≈1.5 GB per action).
  A permanent setting can go in `%APPDATA%/Unreal Engine/UnrealBuildTool/BuildConfiguration.xml`.

## 7. Working with assets (Claude cannot edit .uasset)

- Claude writes C++, config `.ini`, JSON data, Python editor scripts and docs.
- The developer creates/edits Blueprints, materials, PCG graphs, levels in the
  editor, following step-by-step instructions written in the milestone notes
  (`Docs/Milestones/Mx.md`).
- Data pipeline: `Data/**/*.json` (source of truth, diffable) →
  `Tools/Python/import_data.py` (run in editor: *Tools > Execute Python Script*)
  → DataTables in `Content/ShroomHunt/Data/`. Never hand-edit those DataTables.
- Python scripts must be idempotent (safe to re-run) and log what they change.

## 8. Asset naming (Content/)

`BP_` Blueprint, `WBP_` widget, `M_` material, `MI_` material instance, `MF_`
material function, `MPC_` material parameter collection, `T_` texture (suffix
`_D` `_N` `_ORM` `_M`), `SM_` static mesh, `SK_` skeletal mesh, `DT_`
DataTable, `DA_` DataAsset, `PCG_` PCG graph, `NS_` Niagara system, `SW_`/`SC_`
sound wave/cue, `IA_`/`IMC_` input, `Lvl_` level (kept from the template).

## 9. Git workflow

- Branch `main`; short-lived feature branches (`feat/weather-core`) for bigger
  work, merged with fast-forward or squash. Remote: `origin` (GitHub).
- **Small, explicit commits**, Conventional Commits style, English, imperative:
  `feat(weather): add deterministic weather model`,
  `fix(spawn): clamp suitability before exponent`, `docs: ...`, `data(species): ...`,
  `test: ...`, `refactor: ...`, `chore: ...`, `content: ...` (asset-only changes).
- Keep C++ changes and asset (`.uasset`) changes in separate commits when possible.
- Binary assets go through **Git LFS** (`.gitattributes`). Before committing,
  check `git status` for generated files and `git lfs status` for binaries not
  in LFS.
- Never commit `Binaries/ Intermediate/ Saved/ DerivedDataCache/ .vs/ *.sln`,
  nor `Content/Fab/` (license + LFS quota, D-010).
- Commit messages end with the Co-Authored-By line when written by Claude.
- Ask before pushing, force-pushing or rewriting published history.

## 10. Architecture cheat sheet (details in `Docs/Architecture.md`)

- **Calendar** (`UCalendarSubsystem`, GameInstance): single source of time
  (total game seconds) → time of day, day, season, year.
- **Weather** (`FWeatherModel` pure + `UWeatherSubsystem`): deterministic,
  random-access function of (forest seed, climate, time) + debug overrides.
  Rain history is computed, not stored.
- **Forest field** (`FForestField`): deterministic sampler (altitude, moisture,
  pH, soil, exposure, stand composition, canopy, parcels) shared by terrain
  bake, PCG custom nodes and mushroom suitability.
- **Mycology**: species rows (DataTable) → static suitability → persistent
  mycelium patches → daily flush roll from lagged weather → fruit bodies.
- **Mushroom mesh**: species ranges + seed → `FMushroomMorphology` →
  `FDynamicMesh3` (revolve + deform) → cached `UStaticMesh` variants rendered
  by ISM; the inspected specimen gets a unique mesh.
- **Knowledge**: per-species knowledge level; unknown specimens shown by
  observable criteria only.

## 11. UE pitfalls met so far (keep this list growing)

- **Live Coding** cannot handle changes to class layout (new `UPROPERTY`,
  `UFUNCTION`, new classes, header changes): close the editor and do a full
  build, otherwise Blueprints can be corrupted on reinstancing.
- `EngineAssociation` in the `.uproject` is a GUID (registry entry for UE_5.8),
  not "5.8": this is normal for this install.
- World Partition stores actors as one file per actor in
  `Content/__ExternalActors__/` — these must be committed with the map.
- Git warns "LF will be replaced by CRLF": harmless, normalized by `.gitattributes`.
