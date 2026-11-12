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

### 0. Editor vocabulary (read once)

| Panel | Where | Used for |
|---|---|---|
| **Viewport** | big 3D view in the middle | look at the level; hold the right mouse button + WASD to fly |
| **Outliner** | top right (*Window > Outliner > Outliner 1* if closed) | list of all actors in the level; click one to select it |
| **Details** | under the Outliner (*Window > Details > Details 1* if closed) | properties of the selected actor; has a **search box** at its top |
| **Content Browser** | bottom (*Window > Content Browser > Content Browser 1*, or `Ctrl+Space` for the drawer) | project files (assets) |
| **Quick Add** | toolbar button with a cube and a green **+**, top left | add actors to the level |

Tip: in the **Details** panel, typing part of a property name in its search box
(e.g. `mobility`) filters the list. It is the fastest way to find every
property mentioned below.

### 1. Check the settings
1. Menu *Edit > Project Settings…*
2. In the left column, scroll down to the section **ShroomHunt**: it contains
   **Time** and **Weather** (they exist because of our C++ `UDeveloperSettings`).
3. Click **Time**: day length 600 s, seasons 6/5/8/5, start Autumn day 0 at
   7.5 h, latitude 47. Leave the defaults; close the window.

### 2. Prepare the sky actors in `Lvl_FirstPerson`

The template level already contains every sky actor we need:
`DirectionalLight` (the sun), `SkyLight`, `SkyAtmosphere`, `VolumetricCloud`
and `ExponentialHeightFog`. The sun and the sky light are already **Movable**.
We only check two settings, then add a moon and an exposure volume.

**2.1 Open the level**
1. In the **Content Browser**, left column, open the folders
   *Content > FirstPerson*.
2. Double-click **Lvl_FirstPerson** (map icon). If asked to save the current
   level, answer *Don't Save*.

**2.2 Check the sun**
1. In the **Outliner**, type `Directional` in its search box (top of the
   Outliner) and click **DirectionalLight**.
2. In **Details**, search box: type `mobility`. *Mobility* must show
   **Movable** highlighted (it should already be). If not, click *Movable*.
3. Clear the search box, type `atmosphere sun`:
   - **Atmosphere Sun Light** must be checked (click the checkbox if not).
   - **Atmosphere Sun Light Index** must be **0**.

**2.3 Check the sky light**
1. In the **Outliner** search, type `SkyLight`, click **SkyLight**.
2. **Details** search `real time`: **Real Time Capture** must be checked
   (already the case in this template).

**2.4 Add a moon** (a second directional light)
1. Select **DirectionalLight** again in the Outliner.
2. Press `Ctrl+D` (or menu *Edit > Duplicate*): a copy named
   **DirectionalLight2** appears in the Outliner, already selected.
3. Rename it: press `F2` in the Outliner (or click its name at the very top of
   the Details panel), type `MoonLight`, press Enter.
4. In **Details** (use the search box each time):
   - `intensity` → **Intensity**: `0.5` (unit: lux).
   - `light color` → click the color swatch of **Light Color**, set
     *R* 190, *G* 205, *B* 255 (pale blue) or *Hex sRGB* `BECDFFFF`, click *OK*.
   - `atmosphere sun` → **Atmosphere Sun Light** checked, **Atmosphere Sun
     Light Index**: `1`.
   - `cast shadows` → **Cast Shadows** checked.

   The sky looks identical for now: the director moves the moon at play time.

**2.5 Add an exposure volume** (otherwise the camera brightens the night)

> **UE concept — Auto exposure.** Like a real camera, UE adapts brightness to
> the scene. Without limits, a night scene is brightened back to a grey day.
> A *Post Process Volume* sets the allowed exposure range.

1. Toolbar **Quick Add** (cube with green +) > *Volumes* > **Post Process
   Volume**. It appears in front of the camera and is selected.
2. **Details** search `unbound` → check **Infinite Extent (Unbound)** (the
   volume then affects the whole level).
3. **Details** search `exposure`. In the *Exposure* group every property has a
   small checkbox on its left: **a property is applied only when its checkbox
   is ticked**. Tick and set:
   - **Metering Mode**: *Auto Exposure Histogram*
   - **Min EV100**: `-2`
   - **Max EV100**: `14`
4. Save everything: *File > Save All* (`Ctrl+Shift+S`). If a dialog lists
   files, click *Save Selected*.

### 3. Create the director Blueprint

> **UE concept — Blueprint subclass of a C++ class.** The C++ class holds the
> logic; the Blueprint is a child class where you set assets and values in the
> editor (and can add visual-only events). We place the Blueprint in the
> level, not the raw C++ class, so assets can be assigned without code.

1. **Content Browser**, left column: right-click the top folder **Content** >
   *New Folder*, name it `ShroomHunt`. Right-click `ShroomHunt` > *New Folder*,
   name it `Environment`. Click `Environment` to open it (it is empty).
2. Right-click in the empty right-hand area > **Blueprint Class**.
3. The window *Pick Parent Class* opens. At its bottom, expand **All Classes**,
   type `Environment Director` in its search box, click **Environment
   Director**, then the **Select** button.
4. A new asset appears with its name in edit mode: type
   `BP_EnvironmentDirector`, Enter.
5. Drag `BP_EnvironmentDirector` from the Content Browser into the
   **Viewport** (drop it anywhere, its position does not matter).
6. With it selected, **Details** > section *ShroomHunt | Environment |
   References*: leave **Auto Find Actors** checked and the slots empty. At
   play time it finds the sun (index 0), the moon (index 1), the sky light,
   the fog and the clouds by itself.
7. Optional preview: section *Editor Preview*, tick **Preview In Editor**,
   then change **Preview Hour** (try 7, 12, 19) and **Preview Season**: the
   sun and moon rotate in the viewport. Untick it when done.
8. *File > Save All*.

### 4. Material Parameter Collection (for later materials: wet ground…)

> **UE concept — Material Parameter Collection (MPC).** A global set of values
> that any material can read. C++ writes them every frame; materials (ground,
> leaves, mushrooms) will later use `Wetness` to look wet after rain.

1. In `Content/ShroomHunt/Environment`, right-click the empty area >
   *Materials* > **Material Parameter Collection**. Name it `MPC_Environment`.
2. Double-click it. Next to **Scalar Parameters**, click **+** nine times.
3. Expand each new entry and set its **Parameter Name** exactly (case
   matters), default value 0: `TimeOfDay`, `SunElevation`, `CloudCover`,
   `RainIntensity`, `Wetness`, `FogDensity`, `WindSpeed`, `TemperatureC`,
   `SeasonProgress`. Save (`Ctrl+S`), close.
4. Select `BP_EnvironmentDirector` in the level (Outliner), **Details** search
   `environment parameters` → pick `MPC_Environment` in the dropdown.
5. *File > Save All*.

### 5. Rain particles (Niagara) — optional, can be done later

Fog, clouds and light already follow the weather without this step. If a
menu name below differs in your editor, skip this step and tell me what you see.

1. In `Content/ShroomHunt/Environment`: right-click > *FX* > **Niagara
   System**. In the wizard choose **New system from selected emitter(s)**,
   click *Next*, select the template emitter **Hanging Particulates**, click
   the green **+** to add it to the list, then *Finish*. Name it `NS_Rain`,
   double-click to open it.
2. Left panel **Parameters** > section **User Exposed** > **+** > *Float*.
   Name it `RainIntensity`, set its value to `1`.
3. In the middle **System Overview**, click the emitter: its modules appear
   in the **Selection** panel on the right. Edit:
   - *Emitter Spawn > Emitter Properties*: **Local Space** unchecked;
     **Calculate Bounds Mode** *Fixed*; **Fixed Bounds** Min
     (-3000, -3000, -2000) Max (3000, 3000, 500).
   - *Emitter Update > Spawn Rate*: click the small arrow at the right of
     **Spawn Rate** > *Dynamic Inputs* > **Multiply Float**; set *A* = `6000`;
     for *B* click its arrow > *Link Inputs > User > RainIntensity*.
   - *Particle Spawn > Initialize Particle*: **Lifetime** `1.2`;
     **Sprite Size Mode** *Non-Uniform*; **Sprite Size** X `1.5`, Y `70`;
     **Color** white with A `0.35`.
   - *Particle Spawn > Shape Location* (add it with **+** if absent):
     **Shape Primitive** *Box/Plane*, **Box Size** (4000, 4000, 200).
   - *Particle Spawn* > **+** > search **Add Velocity**: **Velocity**
     (0, 0, -1100).
   - *Particle Update*: if modules like *Curl Noise Force* or *Drag* exist,
     right-click > *Delete*.
   - *Render > Sprite Renderer*: **Facing Mode** *Velocity Aligned*,
     **Alignment** *Velocity Aligned*.
4. Toolbar **Compile**, then **Save**. Close.
5. Double-click `BP_EnvironmentDirector` to open the Blueprint editor. In
   **Components** (top left) click **RainParticles**; in **Details** set
   **Niagara System Asset** (search `asset`) to `NS_Rain`. **Compile**, **Save**.

### 6. Play
Toolbar green **Play** button (or Alt+P); Esc stops. The yellow debug HUD shows the date, the time scale
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
