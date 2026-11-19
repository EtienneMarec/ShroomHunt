# M1g — Interaction and knowledge

M1g is split in two:
- **M1g-1 (this part):** aim at a mushroom, examine it up close with its
  observable criteria, pick it into the basket; picking exhausts the
  mycelium patch for a while.
- **M1g-2 (next):** knowledge — the starting poster (chanterelle, fly agaric),
  the torn guide page at the hunting hut (cep), real names on known species,
  a minimal encyclopedia.

## What the code provides

| Piece | Role |
|---|---|
| `UForagerComponent` | added to the player's pawn by the Mushroom Spawner at play: aiming, examine, pick, its own input actions and HUD |
| `UForagingWidget` | prompt under the crosshair, basket summary, examine panel (UMG built in C++, no widget Blueprint) |
| `ObservableCriteria` | what a player can see without knowing the species: cap shape / size / color / surface / warts, underside (gills, pores, ridges), stem, ring, volva, age; a descriptive label ("Unknown brown mushroom with pores"); estimated weight |
| `UForagingSubsystem` | the basket, the picked individuals (they do not come back), the picks per patch |
| Spawn model hooks | exhaustion: each pick adds 0.15 to its patch, decaying over ~3 game days, and lowers the flush probability of the next days; picked mushrooms are skipped |
| `UForagingSettings` | *Project Settings > ShroomHunt > Foraging*: reach (2.5 m), keys (E, F), exhaustion rules |

The examined specimen is the **exact individual at its exact age**, built in
full detail just for the inspection: the same seed and age as the one drawn
in the forest.

> **UE concept — Enhanced Input at runtime.** Input actions and mapping
> contexts are usually assets; here the forager creates them in C++
> (`UInputAction`, `UInputMappingContext::MapKey`) and adds the context with a
> higher priority than the template's, so the keys work without editing the
> character Blueprint.
>
> **UE concept — UMG from C++.** A `UUserWidget` can build its widget tree in
> `RebuildWidget()` (canvas, text blocks, border) instead of a widget
> Blueprint: handy for debug or simple HUDs; a designer-made Blueprint can
> replace it later.

## Editor steps

None: the C++ is built and the forager is added automatically. Just **Play**
in `Lvl_F01_LowlandOakBeech`.

## Test checklist
- [ ] `sh.Spawn.Goto`: looking at the mushroom from close (≤ 2.5 m) shows its
      descriptive label and `[E] Examine  [F] Pick`; looking away hides it.
- [ ] **E**: the mushroom appears in front of you in full detail, the mouse
      turns it, the panel lists cap, underside, stem (ring, volva for the fly
      agaric; network for the cep) and age; movement is blocked. **E** again
      puts it back.
- [ ] **F** (looking at it, or while examining): it disappears from the forest,
      the basket counter (top right) increases with its weight.
- [ ] A picked mushroom never comes back; a patch picked clean gives fewer
      mushrooms in the next flushes (`sh.Weather.Force Rain 24`,
      `sh.Time.SkipDays 3`).
- [ ] Automation: 26 tests green (`ShroomHunt.Player.Foraging` included).

## UE concepts learned
- Actor components added at runtime (`NewObject` + `RegisterComponent`).
- Enhanced Input created in C++; ignoring move/look input while examining.
- UMG widget built in C++; `FText::Format` for player-facing text.
- Player view point (`GetPlayerViewPoint`) and analytic ray picking.
