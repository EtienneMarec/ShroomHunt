# M1a — Time & weather core

Goal: a 10-minute day/night cycle with a moving sun and moon, deterministic
weather (clouds, fog, rain, storms) driving the sky, and debug tools to
accelerate time and force weather. Demo map: the template's `Lvl_FirstPerson`.

## What the code provides

| Class | Role |
|---|---|
| `UTimeSettings`, `UWeatherSettings` | tunables in *Project Settings > ShroomHunt* (saved to `Config/DefaultGame.ini`) |
| `CalendarMath` | pure date/season/month mapping, sun and moon positions |
| `UCalendarSubsystem` | game clock (Game Instance), events `OnHourChanged`, `OnDayStarted`, `OnSeasonChanged` |
| `FWeatherModel` | deterministic random-access weather (seed + climate + time) |
| `UWeatherSubsystem` | current weather of the world, debounced state, wetness, rain history, forced weather, lightning |
| `AEnvironmentDirector` | applies time + weather to sun, moon, sky light, fog, clouds, rain particles, MPC |
| `UShroomDebugSubsystem` | on-screen debug HUD |
| `sh.*` console commands | time and weather debug |

## Editor steps

Close the editor before building from the command line (see `CLAUDE.md` §6),
or build from Visual Studio (*Development Editor | Win64*), then open
`ShroomHunt.uproject`.

### 1. Check the settings
1. *Edit > Project Settings*, scroll the left column to the **ShroomHunt**
   section: **Time** and **Weather**.
2. Time: day length 600 s, seasons 6/5/8/5, start Autumn day 0 at 7.5 h,
   latitude 47. Leave the defaults for now.

### 2. Prepare the sky actors in `Lvl_FirstPerson`
1. Open `Content/FirstPerson/Lvl_FirstPerson`.
2. *Window > Env. Light Mixer*. The panel lists the sky actors of the level;
   click the buttons for any missing one: **Create Sky Light**, **Create
   Atmospheric Light 0** (sun), **Create Sky Atmosphere**, **Create Volumetric
   Cloud**, **Create Height Fog**.
3. Select the sun (Directional Light) in the *Outliner*. In *Details*:
   - *Transform > Mobility*: **Movable**.
   - *Light > Atmosphere Sun Light*: checked, *Atmosphere Sun Light Index*: **0**.
4. Select the **Sky Light**: *Mobility* **Movable**, *Light > Real Time
   Capture*: checked (the sky light then follows the sky automatically).
5. **Moon** (optional but recommended): select the sun, `Ctrl+D` to duplicate,
   rename it `MoonLight` (F2). In *Details*:
   - *Intensity*: **0.5** lux, *Light Color*: pale blue (R 190, G 205, B 255).
   - *Atmosphere Sun Light Index*: **1**.
   - *Cast Shadows*: checked.
6. Exposure: select the *PostProcessVolume* (add one with *Quick Add (+) >
   Volumes > Post Process Volume* if missing), check *Infinite Extent
   (Unbound)*, then *Exposure*: *Metering Mode* **Auto Exposure Histogram**,
   *Min EV100* **-2**, *Max EV100* **14**. Without a low *Min EV100* the
   camera brightens nights back to daylight.

### 3. Create the director Blueprint
1. Content Browser: create folders `Content/ShroomHunt/Environment`.
2. Right-click in it > *Blueprint Class* > expand *All Classes*, search
   **Environment Director**, select it, name it `BP_EnvironmentDirector`.
3. Drag `BP_EnvironmentDirector` into the level (any position).
4. In *Details > ShroomHunt|Environment|References*, keep *Auto Find Actors*
   checked: sun (index 0), moon (index 1), sky light, fog and clouds are
   found at play time. You can also assign them explicitly.
5. Preview: check *Editor Preview > Preview In Editor*, change *Preview Hour*
   and *Preview Season*: the sun and moon rotate in the viewport. Uncheck it
   afterwards if you prefer the level's original sun angle.

> **UE concept — Blueprint subclass of a C++ class.** The C++ class holds the
> logic; the Blueprint is a child class where you set assets and values in the
> editor (and can add visual-only events). We always place the Blueprint, not
> the raw C++ class, so assets can be assigned without code.

### 4. Material Parameter Collection (for later materials: wet ground…)
1. In `Content/ShroomHunt/Environment`: right-click > *Materials >
   Material Parameter Collection*, name it `MPC_Environment`.
2. Open it, add **Scalar Parameters** with exactly these names (default 0):
   `TimeOfDay`, `SunElevation`, `CloudCover`, `RainIntensity`, `Wetness`,
   `FogDensity`, `WindSpeed`, `TemperatureC`, `SeasonProgress`.
3. Save. Open `BP_EnvironmentDirector` (or select it in the level) and set
   *Environment Parameters* to `MPC_Environment`.

> **UE concept — Material Parameter Collection (MPC).** A global set of values
> that any material can read. C++ writes them once per frame; materials
> (ground, leaves, mushrooms) will use `Wetness` to look wet after rain.

### 5. Rain particles (Niagara)
1. In `Content/ShroomHunt/Environment`: right-click > *FX > Niagara System* >
   **New system from selected emitter(s)** > choose the template
   **Hanging Particulates** > *Finish*. Name it `NS_Rain` and open it.
2. In the *Parameters* panel (left), under **User Exposed**, click **+** >
   *Float*, name it `RainIntensity`, default **1**.
3. Select the emitter and edit its modules (*System Overview* / stack on the right):
   - *Emitter Spawn / Emitter Properties*: *Local Space* **unchecked**.
   - *Emitter Update > Spawn Rate*: click the down arrow next to *Spawn Rate*
     > *Dynamic Inputs > Multiply Float*; set *A* = **6000**, *B* = link to
     `User.RainIntensity` (drag it from the Parameters panel onto *B*).
   - *Particle Spawn > Initialize Particle*: *Lifetime* **1.2**, *Sprite Size
     Mode* **Non-Uniform**, *Sprite Size* **(1.5, 70)**, *Color* white with
     alpha **0.35**.
   - *Particle Spawn > Shape Location*: *Shape Primitive* **Box/Plane**,
     *Box Size* **(4000, 4000, 200)**.
   - *Particle Spawn*: **+** > *Add Velocity*: *Velocity* **(0, 0, -1100)**.
   - Remove noise/drag modules from *Particle Update* if present (select >
     *Delete*).
   - *Render > Sprite Renderer*: *Facing Mode* **Velocity Aligned**,
     *Alignment* **Velocity Aligned**.
   - *Emitter Properties*: *Calculate Bounds Mode* **Fixed**, *Fixed Bounds*
     min (-3000,-3000,-2000) max (3000,3000,500).
4. Save, compile (top-left button). In `BP_EnvironmentDirector`: select the
   **RainParticles** component, *Niagara System Asset* = `NS_Rain`. Compile, save.

Rain is optional for the demo: fog, clouds and light already follow the weather.

### 6. Play
Press **Play** (Alt+P). The yellow debug HUD shows the date, the time scale
and all weather variables. Open the console with **`** (or **²** on an AZERTY
keyboard) and try:

| Command | Effect |
|---|---|
| `sh.Time.Scale 20` | time 20x faster (a day in 30 s) |
| `sh.Time.Set 21` | jump to 21:00 |
| `sh.Time.SkipDays 3` | skip three days |
| `sh.Time.Pause` | freeze/unfreeze the clock |
| `sh.Weather.Force Rain 4` | rain for 4 game hours |
| `sh.Weather.Force Fog 3` / `Storm 2` / `Clear 6` | other states |
| `sh.Weather.Clear` | back to natural weather |
| `sh.Debug.HUD 0` | hide the HUD |

## Test checklist
- [ ] HUD visible; the clock advances ~1 game hour every 25 real seconds.
- [ ] `sh.Time.Scale 20`: the sun rises in the east, culminates south, sets
      west; nights are dark (moon visible when up), dawn/dusk are colored.
- [ ] `sh.Time.SkipDays 6` moves to winter: lower noon sun, shorter days.
- [ ] `sh.Weather.Force Fog 3` at dawn: fog thickens smoothly, then clears.
- [ ] `sh.Weather.Force Rain 4`: clouds close, light dims, rain particles
      (if NS_Rain is set), ground wetness rises on the HUD; after the override
      wetness decays over a few game hours.
- [ ] `sh.Weather.Force Storm 2`: lightning flashes (sky light pulses).
- [ ] Output Log (*Window > Output Log*), filter `LogShroom`: references found
      by the director, cloud coverage parameter found, weather transitions.
- [ ] Automation: *Tools > Session Frontend > Automation*, filter
      `ShroomHunt`, run all: 7 tests green.

If the clouds do not react, the log says which material parameter was not
found: set *Clouds > Cloud Coverage Parameter* on the director to the right
name (open the cloud material instance to see its parameters) and tune
*Cloud Coverage Clear/Overcast*.

## UE concepts learned
- **Subsystems** (`UGameInstanceSubsystem` vs `UWorldSubsystem`) and
  **ticking** without actors (`FTickableGameObject`, `UTickableWorldSubsystem`).
- **Developer Settings**: C++ properties editable in Project Settings, stored
  in `.ini` text files.
- **Directional light + Sky Atmosphere**: the atmosphere colors the sky from
  the light direction; index 0 = sun, 1 = moon. Mobility must be *Movable*.
- **Sky Light real-time capture**, **Exponential Height Fog**, **Volumetric
  Cloud** materials with **material layers** (parameters addressed by layer).
- **Material Parameter Collections** for global shader values.
- **Niagara user parameters** set from C++ (`SetVariableFloat`).
- **Console variables/commands** (`IConsoleManager`), on-screen debug messages.
- **Automation tests** (`IMPLEMENT_SIMPLE_AUTOMATION_TEST`) run headless.
