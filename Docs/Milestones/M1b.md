# M1b — Species data pipeline

Goal: species are described in readable JSON files, validated, imported into a
DataTable without recompiling, and available in game through a subsystem.
Three species: cep (*Boletus edulis*), chanterelle (*Cantharellus cibarius*),
fly agaric (*Amanita muscaria*).

```mermaid
flowchart LR
  J[Data/Species/*.json<br/>source of truth] --> P[SpeciesJson parser C++<br/>validation, errors with file + field]
  P --> T[Automation tests]
  P --> L[USpeciesDataLibrary]
  PY[Tools/Python/import_data.py] --> L
  L --> DT[(DT_Species<br/>DataTable asset)]
  DT --> S[USpeciesDatabaseSubsystem<br/>in game]
  J -. dev: sh.Species.Reload .-> S
```

## What the code provides

| Piece | Role |
|---|---|
| `FMushroomSpeciesRow` (+ enums and small structs) | row structure of `DT_Species`: identity, ecology, phenology, spawning, morphology, knowledge |
| `FTrapezoid` | habitat response curve, `Evaluate` and `EvaluatePeriodic` (months) |
| `SpeciesJson` parser | compact JSON → row; precise errors, warnings for unknown keys (typos) |
| `USpeciesDataLibrary` | Python-callable import/validate functions (`unreal.SpeciesDataLibrary`) |
| `UMycologySettings` | *Project Settings > ShroomHunt > Mycology*: which DataTable to use |
| `USpeciesDatabaseSubsystem` | lookups in game (`FindSpecies`, `GetSpeciesInGuideGroup`…) |
| `sh.Species.*` commands | list, show, reload, validate |
| `Tools/Python/import_data.py` | creates/updates `DT_Species` |

> **UE concept — DataTable.** A table asset whose rows share one C++ struct
> (here `FMushroomSpeciesRow`). Designers see it as a spreadsheet in the
> editor; code looks rows up by name (`BoletusEdulis`).

> **UE concept — Editor Python.** The *Python Editor Script Plugin* exposes
> the editor and our C++ (`UFUNCTION`s, `USTRUCT(BlueprintType)`) to Python:
> `FMushroomSpeciesRow` becomes `unreal.MushroomSpeciesRow`. Ideal for
> repetitive asset work that Claude cannot do in `.uasset` files.

## Editor steps

`DT_Species` was already generated once by running the script headless, so
the data is in place. These steps show the day-to-day workflow.

### 1. Look at the DataTable
1. **Content Browser** > *Content > ShroomHunt > Data*, double-click
   **DT_Species**.
2. The top list shows the three rows; click one to see all its fields in the
   *Row Editor* below (Cap, Stem, Hosts…). Do not edit here: the JSON files
   are the source of truth and the next import overwrites the table.
3. *Edit > Project Settings > ShroomHunt > Mycology*: **Species Table** points
   to `DT_Species`.

### 2. Change a value and re-import
1. Open `Data/Species/BoletusEdulis.json` in any text editor (VS Code,
   Visual Studio…). Change `"PricePerKg": 30.0` to `32.0`, save.
2. In Unreal: menu **Tools > Execute Python Script…**, select
   `Tools/Python/import_data.py` in the project folder, *Open*.
   (Alternative: *Window > Output Log*, command box at the bottom in *Cmd*
   mode, type `py "<project folder>/Tools/Python/import_data.py"` with the
   full path and forward slashes, press Enter.)
3. *Window > Output Log*: lines `[import_data] ...` then
   `OK: 3 species, 0 error(s), 0 warning(s)` and `... saved.`
4. Re-open `DT_Species`: the price is 32. Put it back to 30 and re-import.

### 3. See what an error looks like
1. In `BoletusEdulis.json`, replace `"Type": "Pores"` with `"Type": "Porez"`, save.
2. Run the script again. The Output Log shows in red:
   `ERROR BoletusEdulis.json: Underside.Type: unknown value 'Porez' (expected one of: Gills, Pores, Ridges, Teeth)`
   and `Species import FAILED: DataTable left unchanged.`
3. Fix the typo, re-run: OK again.
4. Without playing, the console command `sh.Species.Validate` (Output Log,
   *Cmd* mode) checks the JSON files the same way without touching the table.

### 4. In game
**Play**, open the console (`²` / `` ` ``):

| Command | Effect |
|---|---|
| `sh.Species.List` | the loaded species and their source (DataTable) |
| `sh.Species.Show BoletusEdulis` | hosts, pH, phenology, morphology summary |
| `sh.Species.Reload` | re-reads the JSON files into the running game (fast tuning; the DataTable is not changed) |

Results appear in the Output Log (filter `LogShroomDebug`).

## Test checklist
- [ ] `DT_Species` shows 3 rows with filled fields.
- [ ] A JSON change + script run updates the table; a typo is reported with
      file and field and leaves the table unchanged.
- [ ] `sh.Species.List` in PIE lists 3 species from `DataTable /Game/ShroomHunt/Data/DT_Species`.
- [ ] Automation (*Tools > Session Frontend > Automation*, filter `ShroomHunt`):
      11 tests green. `DataTableInSync` fails if a JSON was edited but not re-imported.

## Headless import (for scripts and CI)
```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\ShroomHunt.uproject" -run=pythonscript "-script=$(("$PWD\Tools\Python\import_data.py") -replace '\\','/')" -unattended -nullrhi
```
Use forward slashes in `-script=`: a backslash before `t` (as in `\tytym`)
is read as a tab by the Python commandlet.

## UE concepts learned
- **DataTables** and row structs (`FTableRowBase`), `FindRow`, `ForeachRow`.
- **USTRUCT/UENUM reflection**: enums parsed by name with `StaticEnum<T>()`;
  `BlueprintType` structs are visible to Blueprints and Python.
- **Editor Python** and the `pythonscript` **commandlet** (headless editor tasks).
- **Soft object pointers** (`TSoftObjectPtr`) in settings, loaded on demand.
- **Localizable FText** with stable keys (`ShroomHunt.Species / BoletusEdulis.Description`).
- **Unity builds**: UE merges `.cpp` files, so two files with the same helper
  in an anonymous namespace clash — shared helpers live in `Core/`.
