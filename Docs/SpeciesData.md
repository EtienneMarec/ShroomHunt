# Species data schema

Status: **accepted at M0 review (2026-11-11)** — living document.

Each species is one JSON file in `Data/Species/<SpeciesId>.json`. The editor
script `Tools/Python/import_data.py` merges them into the DataTable
`Content/ShroomHunt/Data/DT_Species` (row struct `FMushroomSpeciesRow`).
Edit JSON → re-run the script → no recompilation.

> **UE concept — DataTable.** A table asset whose rows all share one C++
> struct (`FTableRowBase` subclass). Rows are looked up by name (`FName`).
> UE can fill a DataTable from JSON, including nested structs and arrays,
> which CSV handles poorly — hence JSON.

## Conventions
- Ecology values are **real-world** values (days, mm, °C, months). The game
  converts them with `BiologicalDaysPerGameDay` and the calendar mapping
  (see `Docs/SpawnModel.md`), so data stays mycologically meaningful.
- Ranges use **trapezoids** `[min, optLow, optHigh, max]`: response is 0
  outside `[min, max]`, 1 inside `[optLow, optHigh]`, linear in between.
- Morphology ranges are `[min, max]`; an individual draws inside them with a
  seed and a stage-dependent bias.
- Colors are sRGB hex; a range is two colors, the individual lerps between
  them (in Oklab) plus small noise.
- The JSON uses a **compact authoring form** (e.g. a trapezoid is
  `[min, optLow, optHigh, max]`). The import script normalizes it into the
  exact struct layout UE expects (`{"Min":…, "OptLow":…}`), validates
  ranges/enums and fails loudly with the file name on errors.
- Texts are English source strings (`FText`, localization-ready). French
  vernacular names are data, shown in the encyclopedia.

## Schema (`FMushroomSpeciesRow`)

| Field | Type | Notes |
|---|---|---|
| `Name` | row name | stable id, PascalCase scientific name: `BoletusEdulis` |
| `ScientificName` | string | |
| `CommonNames` | `{en, fr}` | vernacular names |
| `Genus`, `Family` | string | |
| `GuideGroup` | enum | which book teaches it: `Boletes`, `Amanitas`, `Chanterelles`, `Russulas`, `Lepiotas`, `Morels`, `Tricholomas`, `Polypores`, `Others` |
| `Edibility` | enum | `Excellent`, `Edible`, `EdibleWithCare`, `Inedible`, `Toxic`, `Deadly` |
| `ToxicSyndrome` | enum | `None`, `Gastrointestinal`, `Phalloidian`, `Pantherinic`, `Muscarinic`, `Orellanian`, … |
| `PricePerKg` | float € | base buying price at the market; 0 = not bought |
| **Ecology** | | |
| `Trophic` | enum | `Mycorrhizal`, `Saprotrophic`, `Parasitic`, `Grassland` |
| `Substrates` | enum[] | `Soil`, `Litter`, `Moss`, `DeadWood`, `Stump`, `LivingTrunk`, `Grass`, `Burnt` |
| `Hosts` | `{genus, affinity}[]` | tree genera with affinity 0–1 (mycorrhizal & parasitic) |
| `pH` | trapezoid | soil pH |
| `AltitudeM` | trapezoid | meters |
| `Moisture` | trapezoid | 0 dry – 1 waterlogged (topographic wetness index, normalized) |
| `Canopy` | trapezoid | 0 open – 1 closed canopy (light preference) |
| `EdgeAffinity` | float −1…1 | preference for forest edges/clearings (+) or interior (−) |
| `Soils` | `{type, weight}[]` | `Sandy`, `Loam`, `Clay`, `Limestone`, `Peat` |
| **Phenology & trigger** | | |
| `FruitingMonths` | trapezoid (month, 1–12.99, circular) | e.g. `[6, 8, 10.5, 11.5]` |
| `RainWindowDays` | float | accumulation window (real days) |
| `RainMm` | `[min, opt]` | rain needed over the window |
| `TempC` | trapezoid | mean temperature over the window |
| `LagDays` | `[min, max]` | delay from trigger to fruiting (real days) |
| `FlushRate` | float | `k` in the flush probability (see spawn model) |
| `FlushCooldownDays` | float | real days before the same patch can flush again |
| **Spawning** | | |
| `PatchDensityPerHa` | float | candidate mycelium patches per hectare |
| `PatchRadiusM` | `[min, max]` | |
| `Clustering` | enum | `Solitary`, `Scattered`, `Caespitose` (tufts), `FairyRing`, `Troop` |
| `FruitsPerFlush` | float | mean fruit bodies per flush at full suitability |
| `LifetimeDays` | float | real days from emergence to collapse |
| `StageSplit` | `[young, mature]` | fractions of lifetime: young < a, mature < b, old after |
| `WormyChance` | float | probability an old specimen is wormy |
| **Morphology** | | |
| `Cap` | struct | `ShapesByStage {young[], mature[], old[]}` of `{shape, weight}`; `DiameterCm`; `MarginType` (`Even`, `Inrolled`, `Wavy`, `Striate`, `Appendiculate`); `Surface` (`Smooth`, `Velvety`, `Scaly`, `Warty`, `Viscid`, `Fibrillose`); `Colors` (range); `Warts` {color, density, washOffByRain} |
| `Underside` | struct | `Type` (`Gills`, `Pores`, `Ridges`, `Teeth`); `Attachment` (`Free`, `Adnate`, `Decurrent`, `Sinuate`); `ColorsByStage` |
| `Stem` | struct | `LengthCm`, `ThicknessCm`, `Shape` (`Cylindrical`, `Clavate`, `Bulbous`, `Tapering`, `Rooting`), `Colors`, `Reticulation` {present, color, extent}, `Ring` {type: `None`, `Skirt`, `Fragile`, `Cortina`}, `Volva` {type: `None`, `Sac`, `Friable`, `Bulb`} |
| `Flesh` | struct | `Color`, `ColorChange` (`None`, `Blue`, `Red`, `Yellow`…), `Odor`, `Taste` (`Mild`, `Bitter`, `Acrid`, `Untested`) |
| `SporePrint` | color | hidden criterion (needs spore-print test) |
| **Knowledge** | | |
| `Lookalikes` | `{species, danger, keyDifferences[]}[]` | species may be absent from the game at first (string id) |
| `Description`, `HabitatText` | text | encyclopedia |
| `Illustration` | soft ref | encyclopedia image (later) |

Cap shapes: `Hemispherical`, `Campanulate` (bell), `Conical`, `Convex`,
`Umbonate` (with nipple/mamelon), `Flat`, `Depressed`, `Infundibuliform`
(funnel). Each shape is a parametric 2D profile in the mesh builder.

Observability of criteria (drives identification, `Docs/Progression.md`):
- **visible** at a glance: cap shape/color/surface, stem color, size, habitat;
- **close look** (examine mode, turn the specimen): underside type & color,
  attachment, ring, volva (only if the specimen was dug out, not cut),
  reticulation, odor;
- **tests** (kits): flesh color change (cut test), spore print, taste
  (risky), chemical reagents, lab analysis.

## Example 1 — Cep (Boletus edulis)

```json
{
  "Name": "BoletusEdulis",
  "ScientificName": "Boletus edulis",
  "CommonNames": { "en": "Penny bun / Cep", "fr": "Cèpe de Bordeaux" },
  "Genus": "Boletus", "Family": "Boletaceae", "GuideGroup": "Boletes",
  "Edibility": "Excellent", "ToxicSyndrome": "None", "PricePerKg": 30.0,

  "Trophic": "Mycorrhizal",
  "Substrates": ["Soil", "Litter", "Moss"],
  "Hosts": [
    { "genus": "Fagus", "affinity": 1.0 }, { "genus": "Quercus", "affinity": 1.0 },
    { "genus": "Castanea", "affinity": 0.9 }, { "genus": "Picea", "affinity": 0.9 },
    { "genus": "Pinus", "affinity": 0.7 }, { "genus": "Betula", "affinity": 0.5 },
    { "genus": "Carpinus", "affinity": 0.3 }
  ],
  "pH": [3.8, 4.5, 6.0, 7.2],
  "AltitudeM": [0, 0, 1600, 2100],
  "Moisture": [0.25, 0.40, 0.70, 0.85],
  "Canopy": [0.30, 0.50, 0.80, 0.95],
  "EdgeAffinity": 0.2,
  "Soils": [ { "type": "Sandy", "weight": 1.0 }, { "type": "Loam", "weight": 1.0 },
             { "type": "Clay", "weight": 0.5 }, { "type": "Limestone", "weight": 0.4 } ],

  "FruitingMonths": [6.0, 8.0, 10.5, 11.8],
  "RainWindowDays": 15, "RainMm": [20, 40],
  "TempC": [8, 12, 20, 24],
  "LagDays": [7, 12],
  "FlushRate": 1.2, "FlushCooldownDays": 6,

  "PatchDensityPerHa": 1.5, "PatchRadiusM": [3, 8],
  "Clustering": "Scattered", "FruitsPerFlush": 5,
  "LifetimeDays": 8, "StageSplit": [0.25, 0.7], "WormyChance": 0.45,

  "Cap": {
    "ShapesByStage": {
      "young":  [ { "shape": "Hemispherical", "weight": 1.0 } ],
      "mature": [ { "shape": "Convex", "weight": 0.8 }, { "shape": "Flat", "weight": 0.2 } ],
      "old":    [ { "shape": "Flat", "weight": 0.7 }, { "shape": "Convex", "weight": 0.3 } ]
    },
    "DiameterCm": [6, 22], "MarginType": "Even", "Surface": "Smooth",
    "Colors": ["#C9A47A", "#6B3F22"],
    "Warts": null
  },
  "Underside": {
    "Type": "Pores", "Attachment": "Adnate",
    "ColorsByStage": { "young": "#F2EEE4", "mature": "#E3D27A", "old": "#9A9A45" }
  },
  "Stem": {
    "LengthCm": [6, 18], "ThicknessCm": [3, 7], "Shape": "Clavate",
    "Colors": ["#F0E6D4", "#D8C3A0"],
    "Reticulation": { "present": true, "color": "#FFFFFF", "extent": 0.5 },
    "Ring": "None", "Volva": "None"
  },
  "Flesh": { "Color": "#FFFFFF", "ColorChange": "None", "Odor": "Pleasant, nutty", "Taste": "Mild" },
  "SporePrint": "#6E5A2E",

  "Lookalikes": [
    { "species": "TylopilusFelleus", "danger": "Inedible",
      "keyDifferences": ["Pores turn pink", "Dark brown network on stem", "Very bitter taste"] },
    { "species": "RubroboletusSatanas", "danger": "Toxic",
      "keyDifferences": ["Red pores", "Red stem network", "Flesh turns blue", "Calcareous soils"] }
  ],
  "Description": "Stocky bolete with a bun-like brown cap, white then olive pores and a pale club-shaped stem covered by a fine white network.",
  "HabitatText": "Broadleaf and conifer woods, often on acidic soils, from late summer to autumn after warm rains followed by cooler nights."
}
```

## Example 2 — Chanterelle (Cantharellus cibarius)

```json
{
  "Name": "CantharellusCibarius",
  "ScientificName": "Cantharellus cibarius",
  "CommonNames": { "en": "Golden chanterelle", "fr": "Girolle" },
  "Genus": "Cantharellus", "Family": "Cantharellaceae", "GuideGroup": "Chanterelles",
  "Edibility": "Excellent", "ToxicSyndrome": "None", "PricePerKg": 25.0,

  "Trophic": "Mycorrhizal",
  "Substrates": ["Soil", "Moss"],
  "Hosts": [
    { "genus": "Quercus", "affinity": 1.0 }, { "genus": "Fagus", "affinity": 1.0 },
    { "genus": "Castanea", "affinity": 0.9 }, { "genus": "Carpinus", "affinity": 0.6 },
    { "genus": "Picea", "affinity": 0.8 }, { "genus": "Pinus", "affinity": 0.6 },
    { "genus": "Betula", "affinity": 0.6 }
  ],
  "pH": [3.5, 4.0, 5.8, 6.8],
  "AltitudeM": [0, 0, 1500, 1900],
  "Moisture": [0.30, 0.45, 0.75, 0.90],
  "Canopy": [0.35, 0.55, 0.90, 1.00],
  "EdgeAffinity": -0.1,
  "Soils": [ { "type": "Sandy", "weight": 1.0 }, { "type": "Loam", "weight": 0.9 },
             { "type": "Clay", "weight": 0.4 }, { "type": "Limestone", "weight": 0.1 } ],

  "FruitingMonths": [5.5, 7.0, 9.5, 11.0],
  "RainWindowDays": 10, "RainMm": [15, 30],
  "TempC": [10, 15, 24, 28],
  "LagDays": [5, 10],
  "FlushRate": 0.9, "FlushCooldownDays": 10,

  "PatchDensityPerHa": 1.0, "PatchRadiusM": [4, 10],
  "Clustering": "Troop", "FruitsPerFlush": 9,
  "LifetimeDays": 18, "StageSplit": [0.2, 0.75], "WormyChance": 0.05,

  "Cap": {
    "ShapesByStage": {
      "young":  [ { "shape": "Convex", "weight": 1.0 } ],
      "mature": [ { "shape": "Depressed", "weight": 0.6 }, { "shape": "Infundibuliform", "weight": 0.4 } ],
      "old":    [ { "shape": "Infundibuliform", "weight": 1.0 } ]
    },
    "DiameterCm": [2, 9], "MarginType": "Wavy", "Surface": "Smooth",
    "Colors": ["#F6D36B", "#E5A93A"],
    "Warts": null
  },
  "Underside": {
    "Type": "Ridges", "Attachment": "Decurrent",
    "ColorsByStage": { "young": "#F3D57A", "mature": "#EDC560", "old": "#D9AE4E" }
  },
  "Stem": {
    "LengthCm": [2, 7], "ThicknessCm": [0.8, 2], "Shape": "Tapering",
    "Colors": ["#F4D77E", "#E2B550"],
    "Reticulation": { "present": false },
    "Ring": "None", "Volva": "None"
  },
  "Flesh": { "Color": "#FFF6DD", "ColorChange": "None", "Odor": "Fruity, apricot", "Taste": "Mild, slightly peppery" },
  "SporePrint": "#F2E3B3",

  "Lookalikes": [
    { "species": "OmphalotusOlearius", "danger": "Toxic",
      "keyDifferences": ["True thin gills", "Grows in dense clusters on wood or roots", "Brighter orange", "No fruity smell"] },
    { "species": "HygrophoropsisAurantiaca", "danger": "Inedible",
      "keyDifferences": ["True forked gills", "Orange, darker centre", "On conifer debris"] }
  ],
  "Description": "Egg-yolk yellow, irregular funnel-shaped cap with a wavy margin; underside of blunt, forked ridges running down the stem; fruity smell.",
  "HabitatText": "Mossy acidic broadleaf and mixed woods; appears from early summer after storms and lasts long."
}
```

## Example 3 — Fly agaric (Amanita muscaria)

```json
{
  "Name": "AmanitaMuscaria",
  "ScientificName": "Amanita muscaria",
  "CommonNames": { "en": "Fly agaric", "fr": "Amanite tue-mouches" },
  "Genus": "Amanita", "Family": "Amanitaceae", "GuideGroup": "Amanitas",
  "Edibility": "Toxic", "ToxicSyndrome": "Pantherinic", "PricePerKg": 0.0,

  "Trophic": "Mycorrhizal",
  "Substrates": ["Soil", "Litter", "Moss"],
  "Hosts": [
    { "genus": "Betula", "affinity": 1.0 }, { "genus": "Picea", "affinity": 0.9 },
    { "genus": "Pinus", "affinity": 0.8 }, { "genus": "Fagus", "affinity": 0.35 },
    { "genus": "Quercus", "affinity": 0.35 }
  ],
  "pH": [3.5, 4.0, 5.5, 6.5],
  "AltitudeM": [0, 0, 1800, 2300],
  "Moisture": [0.20, 0.35, 0.70, 0.85],
  "Canopy": [0.10, 0.30, 0.70, 0.90],
  "EdgeAffinity": 0.5,
  "Soils": [ { "type": "Sandy", "weight": 1.0 }, { "type": "Loam", "weight": 0.7 },
             { "type": "Peat", "weight": 0.5 }, { "type": "Limestone", "weight": 0.05 } ],

  "FruitingMonths": [7.5, 9.0, 10.8, 12.0],
  "RainWindowDays": 14, "RainMm": [20, 45],
  "TempC": [5, 9, 17, 22],
  "LagDays": [6, 11],
  "FlushRate": 1.0, "FlushCooldownDays": 7,

  "PatchDensityPerHa": 0.8, "PatchRadiusM": [5, 12],
  "Clustering": "FairyRing", "FruitsPerFlush": 7,
  "LifetimeDays": 9, "StageSplit": [0.25, 0.65], "WormyChance": 0.2,

  "Cap": {
    "ShapesByStage": {
      "young":  [ { "shape": "Hemispherical", "weight": 1.0 } ],
      "mature": [ { "shape": "Convex", "weight": 1.0 } ],
      "old":    [ { "shape": "Flat", "weight": 0.8 }, { "shape": "Depressed", "weight": 0.2 } ]
    },
    "DiameterCm": [8, 20], "MarginType": "Striate", "Surface": "Warty",
    "Colors": ["#E8341C", "#F07A1E"],
    "Warts": { "color": "#FAF7EE", "density": 0.7, "washOffByRain": true }
  },
  "Underside": {
    "Type": "Gills", "Attachment": "Free",
    "ColorsByStage": { "young": "#FFFFFF", "mature": "#FDFBF3", "old": "#F1EAD2" }
  },
  "Stem": {
    "LengthCm": [8, 22], "ThicknessCm": [1.5, 3], "Shape": "Bulbous",
    "Colors": ["#FFFFFF", "#F4F0E2"],
    "Reticulation": { "present": false },
    "Ring": "Skirt", "Volva": "Friable"
  },
  "Flesh": { "Color": "#FFFFFF", "ColorChange": "None", "Odor": "Faint", "Taste": "Untested" },
  "SporePrint": "#FFFFFF",

  "Lookalikes": [
    { "species": "AmanitaCaesarea", "danger": "Edible",
      "keyDifferences": ["Yellow gills and stem", "Large white sac-like volva", "Orange cap usually without warts", "Southern, warm oak woods"] },
    { "species": "AmanitaPantherina", "danger": "Toxic",
      "keyDifferences": ["Brown cap with white warts", "Rimmed volva at base"] }
  ],
  "Description": "Iconic red cap dotted with white warts, white free gills, white stem with a hanging skirt and a bulb ringed by warty volva remnants.",
  "HabitatText": "Mostly with birch and spruce on acidic soils, often at wood edges and clearings; late summer to autumn."
}
```

> Note: in the oak–beech starting forest, fly agarics will concentrate where
> birches grow (clearings, edges, acidic patches) — a good teaching example of
> host-driven distribution.
