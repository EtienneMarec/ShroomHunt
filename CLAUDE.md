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
│  │  ├─ Environment/      sky, weather, seasons (MPC_Season), materials,
│  │  │                    Vegetation/Generated (baked placeholder meshes, D-024)
│  │  ├─ Forests/<ForestId>/   map, PCG graphs, landmarks for one forest
│  │  ├─ Mushrooms/        mushroom materials, mesh cache settings
│  │  ├─ Data/             DataTables / DataAssets generated from /Data
│  │  ├─ UI/               WBP_* widgets
│  │  └─ Debug/            debug materials and widgets
│  ├─ Fab/                 third-party Fab/Megascans assets (NOT versioned, see D-010)
│  └─ (template folders)   FirstPerson, Characters, Input, LevelPrototyping (kept, D-016)
├─ Data/                   source-of-truth gameplay data (JSON), imported by Tools/
│  ├─ Species/             one JSON file per species
│  ├─ Forests/             one JSON file per forest definition
│  └─ Vegetation/          tree archetypes, densities, understory rules
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
  `Core/ Time/ Weather/ Environment/ Forest/ Vegetation/ Mycology/ Mushroom/
  Player/ Knowledge/ Economy/ Save/ UI/ Debug/`, tests in `<Domain>/Tests/`. Template code stays where it is until cleaned up.
  New folders must be added to `PublicIncludePaths` in `ShroomHunt.Build.cs`
  only if needed; prefer includes relative to the module root
  (`#include "Weather/WeatherSubsystem.h"`).
- UObject pointer members: `UPROPERTY() TObjectPtr<UFoo>` (GC safety). Soft
  references (`TSoftObjectPtr`) for assets referenced from data.
- `UPROPERTY` categories: `"ShroomHunt|<Domain>"`.
- One log category per domain: `LogShroomTime`, `LogShroomWeather`,
  `LogShroomForest`, `LogShroomVegetation`, `LogShroomSpawn`, `LogShroomMesh`, `LogShroomKnowledge`,
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

**Editor open or closed?** Tell the developer at the start of each step:
- docs, JSON, Python, `.ini` only → the editor can stay open;
- `.cpp` changes only → the developer may apply them with Live Coding
  (`Ctrl+Alt+F11`), but Claude cannot build/test from the command line meanwhile;
- any header change (`UPROPERTY`, `UFUNCTION`, new class) → the editor must be
  closed (Save All first).
Headless tests/exports start a second editor process (~2 GB): with the editor
open, free RAM is often too low (16 GB machine). Batch work that needs a build.

Run UBT from **PowerShell** (calling `Build.bat` from Git Bash breaks on the
space in `Program Files`, see §11):

```powershell
# Build editor target (Development) — about 1 min incremental
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" ShroomHuntEditor Win64 Development "-Project=$PWD\ShroomHunt.uproject" -WaitMutex -MaxParallelActions=6
```

```powershell
# Regenerate Visual Studio project files (also: right-click .uproject > Generate Visual Studio project files)
& "C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" -projectfiles "-project=$PWD\ShroomHunt.uproject" -game -engine
```

```powershell
# Run automation tests headless (filter "ShroomHunt.")
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\ShroomHunt.uproject" "-ExecCmds=Automation RunTests ShroomHunt.;Quit" -unattended -nopause -nullrhi -nosplash -log
```

Always check the log for `Result: Succeeded`, not only the exit code.

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
  Parsing/validation is C++ (`Mycology/SpeciesJsonParser`, D-021); Python only
  creates the asset and calls `unreal.SpeciesDataLibrary`. After editing a
  JSON, re-run the import (the `DataTableInSync` test fails otherwise).
- Claude can run the import headless (`-run=pythonscript`, see
  `Docs/Milestones/M1b.md`) and commit the regenerated DataTable as a
  `content(data):` commit.
- Python scripts must be idempotent (safe to re-run) and log what they change.
- **Visual check without the editor**: `sh.Mesh.ExportObj` (headless, see
  `Docs/Milestones/M1c.md`) + `Tools/Python/render_mushroom_preview.py`
  (stdlib-only renderer) produce a PNG Claude can look at. Use it after any
  change to the mesh builder or to morphology data. Vegetation:
  `sh.Vegetation.ExportObj` + the same script with `--grid`.
- Vegetation meshes are assets baked from data: after changing
  `Data/Vegetation/Vegetation.json` or the builder, run `sh.Vegetation.BakeMeshes`
  (editor or headless) and commit the regenerated `SM_*` as `content:`.

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
- **No `Co-Authored-By` trailer** (nor any AI attribution line) in commit
  messages or PR descriptions: the developer is the sole author.
- Ask before pushing, force-pushing or rewriting published history.

## 10. Architecture cheat sheet (details in `Docs/Architecture.md`)

- **Calendar** (`UCalendarSubsystem`, GameInstance): single source of time
  (total game seconds) → time of day, day, season, year. Pure math in
  `CalendarMath` (also sun/moon positions).
- **Environment** (`AEnvironmentDirector`, one `BP_` per map): applies time +
  weather to lights, fog, clouds, rain FX and `MPC_Environment`.
- **Debug**: `sh.*` console commands (`Debug/ShroomConsoleCommands.cpp`) and
  the on-screen HUD (`UShroomDebugSubsystem`, `sh.Debug.HUD`).
- **Weather** (`FWeatherModel` pure + `UWeatherSubsystem`): deterministic,
  random-access function of (forest seed, climate, time) + debug overrides.
  Rain history is computed, not stored.
- **Forest field** (`FForestField`, from `Data/Forests/<Id>.json`): terrain
  (1 m), hydrology/moisture/soil/pH/parcels/stands/canopy (4 m cells).
  `sh.Forest.Bake` writes heightmap + weightmaps + debug maps to
  `Saved/ForestBake/<Id>/` (landscape imported by hand, `Docs/Milestones/M1d.md`).
  Check `debug_maps.png` visually after any change to the field.
- **Mycology**: species rows (`FMushroomSpeciesRow` in `DT_Species`, loaded by
  `USpeciesDatabaseSubsystem`) → static suitability → persistent mycelium
  patches → daily flush roll from lagged weather → fruit bodies.
- **Mushroom mesh**: species ranges + seed → `FMushroomMorphology` →
  `MushroomMeshBuilder` (`FDynamicMesh3`, revolve + deform, 2 LODs) →
  `UStaticMesh` cached per species x stage x variant in `UMushroomMeshSubsystem`
  (engine subsystem: works in the editor too); the inspected specimen gets a
  unique mesh. `AMushroomGarden` previews them.
- **Vegetation** (D-024): `Data/Vegetation/Vegetation.json` →
  `VegetationMeshBuilder` (placeholder trees/understory, baked to Nanite assets by
  `sh.Vegetation.BakeMeshes`) and `VegetationScatter` (deterministic placement from
  the field, partition-independent) → PCG node *Forest Scatter* → Static Mesh
  Spawner. Field and vegetation data shared through `ForestFieldCache`.
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
- Calling `Build.bat` from Git Bash fails ("'C:\Program' n'est pas reconnu…")
  **with exit code 0**: use PowerShell for UBT and check `Result: Succeeded`.
- Before deleting assets outside the editor, grep the remaining `.uasset`
  files for `/Game/<Folder>/` and `/Script/ShroomHunt.<Class>` (package paths
  are stored as plain strings). Matches in import metadata (original source
  file paths) are harmless.
- A `UFUNCTION(BlueprintImplementableEvent)` parameter must not share a name
  with a class member: the generated `.gen.cpp` fails with C4458 ("masque le
  membre de classe").
- Headless test logs contain ~20 `LogAutomationTest: Error: Condition failed`
  lines at frame 0: they come from an engine self-test (`UnifiedErrorTest`),
  not from our tests. Read the `Test Completed. Result={...}` lines.
- `TestEqual` has no overload for enums / `uint32`: use `TestTrue(A == B)`.
- **Unity builds** merge several `.cpp` into one translation unit: two files
  defining the same helper in an anonymous namespace fail to compile (C2084)
  as soon as UBT groups them. Put shared helpers in `Core/` (`ShroomMath.h`)
  and give file-local helpers specific names.
- `FJsonObject::Values.Find` does not compile on a `const FJsonObject`; use
  `TryGetField`.
- `-ExecCmds=` separates commands with **commas**, and `Quit` does not close
  `UnrealEditor-Cmd`: use `"-ExecCmds=sh.Some.Command,QUIT_EDITOR"`
  (`Automation RunTests X;Quit` is special: the automation command parses `;Quit`).
- **Triangle winding is left-handed in Unreal**: GeometryCore's
  `VectorUtil::Normal(A, B, C)` is `(C - A) x (B - A)` and that side is the
  rendered front face. A mesh built with the usual right-handed rule renders
  inside out (back faces culled: we saw mushroom gills through the caps).
  `FMeshAccumulator::AddTriangle` swaps B/C; tests check the volume with the
  engine formula, and the preview renderer culls back faces like UE.
- Using `FDynamicMesh3::Attributes()` needs `DynamicMesh/DynamicMeshAttributeSet.h`
  (forward-declared in `DynamicMesh3.h`).
- `UStaticMesh::BuildFromMeshDescriptions` matches sections to materials by
  **slot name**: set `PolygonGroupMaterialSlotNames` to the `FStaticMaterial`
  slot name, or sections get no material.
- `-run=pythonscript -script=` path: use forward slashes (`\t` in
  `C:\Users\tytym` is read as a tab).
- Bash heredocs in this environment sometimes fail on large multi-line
  content ("unexpected EOF"): write helper scripts to the scratchpad with the
  Write tool and run them with `python -I <script> <args>`.
- Landscape heightmaps are 16-bit PNG, big-endian in the file (ImageWrapper
  handles it): 32768 = height 0, 128 units per meter at Z scale 100. Verified by
  decoding the PNG.
- **PCG** runs node elements on worker threads: only read immutable shared data
  (`ForestFieldCache` returns `TSharedPtr<const ...>`). PCG cannot see our
  `Data/` JSON files, so nodes reading them return `IsCacheable() = false`.
- A PCG Static Mesh Spawner references meshes by asset path: runtime-built
  transient meshes cannot be spawned by PCG, hence the baked vegetation assets.
- Player-facing strings are `FText` (localization-ready, D-014); never build
  UI text from `FString` concatenation.
