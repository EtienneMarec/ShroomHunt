# M1h — Rain polish

Closes M1: rain that feels like rain in a forest. Slanted by the wind, thinner
under dense trees, big drops falling from the wet leaves (also after the
shower), splashes on the ground, wet darker and shinier surfaces, an optional
rain sound.

## What the code provides

| Piece | Role |
|---|---|
| `FWeatherSample::WindHeadingDeg` | the wind now has a direction: westerlies (toward +X of the forest) veering slowly over a day or two, deterministic like the rest of the weather |
| `LocalRain::Compute` | pure: rain intensity, ground wetness and canopy cover → direct rain, drops from leaves, wind factor under the trees |
| `AEnvironmentDirector` | samples the forest canopy under the camera (`Data/Forests/<ForestName>.json`), feeds the Niagara system three user parameters (`RainIntensity` = direct rain, `DripIntensity`, `Wind` in cm/s), moves the emitter upwind so the slanted rain lands around you, drives the `RainAudio` component |
| `SkyMood::Compute` + *WeatherPostProcess* component | bad weather changes the light (D-032): under a closed sky the direct sun fades and its shadows blur and fade (source angle, shadow amount), leaving diffuse sky light; rain and storms then darken the sky atmosphere, sky light and fog, with exposure compensation and desaturation; tunable in the director's *Gloom* category |
| Debug HUD | line 2: wind heading, and `here: rain / drips / canopy` at the camera, `gloom` and its exposure compensation |

> **UE concept — Niagara user parameters.** Values exposed by a Niagara system
> (*User Exposed* section) that code can set at runtime
> (`SetVariableFloat`, `SetVariableVec3`). Modules read them through *Link Inputs > User*.
>
> **UE concept — Material Function.** A reusable piece of material graph with
> inputs and outputs (`MF_` prefix). One `MF_Wetness` makes every material react
> to `MPC_Environment.Wetness` the same way.

## Editor steps

Close nothing: these are asset edits only (the C++ is already built). Save
often (*File > Save All*). Shader compilation after material edits can take
a few minutes and a lot of RAM: let it finish (counter at the bottom right)
before the next material.

If a menu or module name below differs in your editor, stop at that step and
tell me what you see.

### 1. Streak material for the drops — `M_RainDrop`
1. In `Content/ShroomHunt/Environment`: right-click > **Material**, name it
   `M_RainDrop`, double-click it.
2. Click the big result node (`M_RainDrop`). In **Details**:
   - **Material Domain** *Surface*, **Blend Mode** *Translucent - Greyscale
     Transmittance* (the project uses Substrate: plain *Translucent* is this one;
     *Colored Transmittance* would tint what is behind the drop);
   - with Substrate the result node has a single **Front Material** pin and no
     *Shading Model* setting: add a **Substrate Shading Models** node, set its
     **Shading Model** to *Unlit* in **Details**, and wire its output into
     **Front Material**. Steps 3–5 then wire into this node's pins;
   - section **Usage** (search `niagara`): tick **Used with Niagara Sprites**.
3. Right-click in the graph, search **Particle Color**, add it. Right-click,
   search **RadialGradientExponential**, add it (keep its default inputs).
4. Add a **Multiply** node (hold **M** and click). Wire the **A** output of
   *Particle Color* (the 5th pin, alpha) into Multiply **A**, the output of
   *RadialGradientExponential* into Multiply **B**.
5. Wire *Particle Color* first output (white pin, RGB) into **Emissive Color**,
   the Multiply output into **Opacity**.
6. **Apply**, **Save**. The sprite of a drop is 1.5 × 70 cm, so this round
   gradient becomes a soft streak.

### 2. Wind and the new material in `NS_Rain`
1. Double-click `NS_Rain`. Left panel **Parameters** > **User Exposed** > **+**:
   - *Float* named `DripIntensity`, value `0`;
   - *Vector* named `Wind`, value (0, 0, 0).
   (`RainIntensity` exists since M1a.)
2. In **System Overview**, double-click the emitter's name and rename it
   `Rain`. Click it to show its modules in **Selection**.
3. *Particle Spawn > Add Velocity* > **Velocity**: click the small arrow at
   its right > *Dynamic Inputs* > search **Add Vector** (vector addition).
   Set its **A**: arrow > *Link Inputs > User > Wind*; set **B** to
   (0, 0, -1100). The sprites are velocity-aligned, so they tilt with the wind.
4. *Render > Sprite Renderer* > **Material**: `M_RainDrop`.
5. *Particle Spawn > Initialize Particle* > **Color**: alpha `0.5` (the
   gradient makes the streaks thinner, they need a bit more opacity).
6. **Compile**, **Save**.

### 3. Drops falling from the leaves — emitter `Drips`
1. In **System Overview**, right-click the `Rain` emitter > **Duplicate**.
   Rename the copy `Drips`.
2. *Emitter Update > Spawn Rate*: set the Multiply **A** to `500`; for **B**:
   arrow > *Link Inputs > User > DripIntensity*.
3. *Particle Spawn > Initialize Particle*: **Lifetime** `1.6`, **Sprite Size**
   X `2.5`, Y `28`, color alpha `0.6`.
4. *Shape Location* > **Box Size** (2500, 2500, 100).
5. *Add Velocity* > the Add Vector **B**: (0, 0, -650) (heavier, slower drops,
   they come from the leaves just above you).
6. **Compile**, **Save**.

### 4. Splashes on the ground — emitters `SplashDrops` and `Splash`
Drops near the player hit the ground (CPU collision) and each hit spawns a
tiny splash, through a Niagara *event*.

1. Duplicate `Rain` again, rename it `SplashDrops`. Select it:
   - *Emitter Spawn > Emitter Properties*: **Sim Target** *CPUSim*;
     tick **Requires Persistent IDs**;
   - *Spawn Rate*: Multiply **A** `700` (B stays *User.RainIntensity*);
   - *Shape Location*: **Box Size** (2400, 2400, 100);
   - *Initialize Particle*: **Lifetime** `2.5` (time to reach the ground in
     a valley);
   - *Particle Update* > **+** > **Collision**: **Collision Type** (or
     *Collision Query Type*) *CPU Ray Traces* (not *Scene Depth*); keep the
     *Bounce* response with **Restitution** `0` and **Friction** `1`: the drop
     stops at its first contact and, with no gravity in this emitter, never
     collides again (one event, one splash). If searching `kill` in the
     Selection panel shows a *Kill Particles On Collision* option, tick it too;
   - *Particle Update* > **+** > **Generate Collision Event**;
   - *Render*: untick the **Sprite Renderer** checkbox (these drops are
     invisible, the `Rain` emitter already draws the rain).
2. Right-click an empty area of **System Overview** > **Add Emitter** >
   template **Empty** (or *Minimal*), rename it `Splash`. Select it:
   - *Emitter Properties*: **Sim Target** *CPUSim*;
   - in the emitter's **Properties** (or *Emitter Summary* > *Event Handlers*)
     click **+** next to **Event Handlers**: **Source** `SplashDrops` →
     *CollisionEvent*, **Execution Mode** *Spawned Particles*,
     **Spawn Number** `3`;
   - in the new **Event Handler** stage > **+** > **Receive Collision Event**
     (it places the splash where the drop hit);
   - *Particle Spawn* > *Initialize Particle*: **Lifetime** `0.25`,
     **Sprite Size** `3`, color white alpha `0.5`;
   - *Particle Spawn* > **+** > **Add Velocity in Cone**: **Cone Axis**
     (0, 0, 1), **Cone Angle** `60`, **Velocity Strength** `150`;
   - *Particle Update* > **+** > **Gravity Force**;
   - *Render > Sprite Renderer* (add one with **+** if absent): **Material**
     `M_RainDrop`.
3. **Compile** (no red error in the *Niagara Log*), **Save**, close.

### 5. Wet surfaces — `MF_Wetness`
1. In `Content/ShroomHunt/Environment`: right-click > *Materials* >
   **Material Function**, name it `MF_Wetness`, double-click it.
2. Add three **Function Input** nodes (right-click, search `function input`);
   in **Details** of each: **Input Name** and **Input Type**:
   - `BaseColor`, *Function Input Vector 3*;
   - `Roughness`, *Function Input Scalar*;
   - `Porosity`, *Function Input Scalar*, **Preview Value** `1`, tick
     **Use Preview Value As Default**.
3. Add a **Collection Parameter** node: **Collection** `MPC_Environment`,
   **Parameter Name** `Wetness`. **Multiply** it by *Porosity*: this is `W`.
4. Color: a **Lerp** node (the basic *LinearInterpolate* of *Math*: hold **L**
   and click; not the *Lerp_...* functions) with **A** = `1` (constant, hold **1** and click),
   **B** = `0.55`, **Alpha** = `W`; then a second **Multiply** with **A** =
   *BaseColor* and **B** = this Lerp's output, wired into the *BaseColor*
   output. The Lerp is a darkening factor (1 dry, 0.55 soaked): *BaseColor*
   must not go into the Lerp, or every material turns white.
5. Roughness: a **Lerp** with **A** = *Roughness*, **B** = constant `0.15`,
   **Alpha** = `W` (the same `W` wire, not a typed value).
6. Two **Function Output** nodes named `BaseColor` and `Roughness`, wired from
   steps 4 and 5. **Save**.
7. In each material below, drag `MF_Wetness` from the Content Browser into
   the graph, wire what currently goes into **Base Color** (of the result node,
   or of the *Substrate* node plugged into *Front Material*)
   into its *BaseColor* input and what goes into **Roughness** into its
   *Roughness* input (no roughness wired: a constant `0.8`), then its two
   outputs into **Base Color** and **Roughness**. Set **Porosity** with a
   constant:
   - `M_Landscape_Forest` (`Environment/Landscape`): porosity `1`;
   - `M_Vegetation` (`Environment/Vegetation`): porosity `0.6`;
   - `M_Mushroom` (`Mushrooms`): porosity `0.5`.
   **Apply** and **Save** each one, waiting for the shaders between them.

### 6. Rain sound (optional)
1. Find a free looping rain sound under a CC0 license (for example on
   freesound.org, search `rain forest loop`, filter *License: Creative Commons 0*),
   as `.wav`.
2. Create the folder `Content/ShroomHunt/Environment/Audio`, drag the file
   into it; double-click the imported sound wave, tick **Looping**, save.
   Name it `SW_RainLoop`.
3. Open `BP_EnvironmentDirector`, click the **RainAudio** component, in
   **Details** set **Sound** to `SW_RainLoop`. **Compile**, **Save**.
   The volume follows the rain (louder under the trees, where it hits the leaves).

### 7. Clouds that follow the weather — `M_VolumetricCloud`
The code already darkens the sky in bad weather (sky atmosphere luminance,
sky light, grey fog, exposure compensation and saturation through the
director's *WeatherPostProcess* component: nothing to set up). The clouds
themselves keep the engine material's fixed coverage; this step makes them
close in with the weather. The material reads `MPC_Environment.CloudCover`
(no dynamic material instance, which crashed the editor in M1e).

The cloud actor uses a **material instance**, `m_SimpleVolumetricCloud_Inst`
(its parameter values make the clouds), of the material
`m_SimpleVolumetricCloud` (the graph, whose default values alone give no
visible clouds). We copy both, edit the graph copy, and point the instance
copy at it.

> **UE concept — Material Instance.** A material instance (`MI_`) has no graph:
> it reuses a parent material and overrides some of its parameters (ticked in
> its Details panel). Double-clicking one opens a 3D preview with the
> parameter list, not a graph.

1. In the **Outliner**, select the **VolumetricCloud** actor. In **Details**,
   **Material** shows `m_SimpleVolumetricCloud_Inst`: click the
   folder-with-arrow icon (*Browse to asset*). If the Content Browser cannot
   show it, click **Settings** (top right of the Content Browser) > tick
   **Show Engine Content**, then browse again.
2. Drag `m_SimpleVolumetricCloud_Inst` onto the folder `ShroomHunt/Environment`
   (left tree of the Content Browser) > **Copy Here**; rename the copy
   `MI_VolumetricCloud`. Do the same with its parent `m_SimpleVolumetricCloud`
   (same engine folder), renamed `M_VolumetricCloud`.
3. Double-click `MI_VolumetricCloud`. In its **Details**, find
   **Cloud_GlobalCoverage** (search `coverage`): if it is ticked, note its value
   (`C0`); if not, `C0` = `-0.2` (the parent's default). Close it.
4. Double-click `M_VolumetricCloud` (graph). Press **Ctrl+F**, search
   `Coverage`: the **Scalar Parameter** `Cloud_GlobalCoverage`. Add a
   **Collection Parameter** node (**Collection** `MPC_Environment`,
   **Parameter Name** `CloudCover`) and a **Lerp** (hold **L** + click):
   **A** = constant `C0`, **B** = constant (see step 7), **Alpha** = *CloudCover*.
   Drag from the Lerp output to every pin the parameter was wired to
   (Alt+click the old wires to remove them), then delete the parameter node.
   **Apply**, **Save**.
5. Open `MI_VolumetricCloud` again: **Details** > **General** > **Parent** =
   `M_VolumetricCloud`. **Save**.
6. Select the **VolumetricCloud** actor: **Details** > **Material** =
   `MI_VolumetricCloud`. Wait for the shaders: the clouds look as before.
7. Overcast value for **B**: outside Play the viewport uses the MPC default
   values. In `MPC_Environment` set the default of `CloudCover` to `1` (do not
   save), then in `M_VolumetricCloud` try **B** values (**Apply** each time)
   until the sky is overcast; set `CloudCover` back to `0`, **Save** both.
   *File > Save All*.

If you cannot find a coverage parameter in step 3, take a screenshot of the
graph around the result node and send it to me.

### 8. Clouds that block the sun
1. In the **Outliner**, select the sun (the **DirectionalLight** with
   *Atmosphere Sun Light Index* 0). In **Details**, search `cloud`: tick
   **Cast Cloud Shadows** (section *Atmosphere and Cloud*). The volumetric
   clouds now shade the ground: passing cloud shadows on a cloudy day.
2. Select the **SkyLight**: search `real time`, tick **Real Time Capture** (if
   it is not already). The ambient light then takes the real sky color: grey
   under clouds instead of blue.
3. *File > Save All*.

## Test checklist
- [ ] `sh.Weather.Force Rain 3` in a clearing: streaks (not rectangles) fall
      slanted with the wind; the HUD line 2 shows `here: rain ~0.3, canopy < 0.3`.
- [ ] Walk into a dense stand: fewer streaks, big drops from the leaves
      (`drips` near 1), the slant decreases.
- [ ] `sh.Weather.Force Storm 2`: strong slant, heavy rain.
- [ ] `sh.Weather.Force Overcast 2`: no "eclipse light": the sharp sun
      shadows blur and fade, the light becomes flat and diffuse (not just dimmer).
- [ ] The sky darkens with the weather: overcast a little, rain more, a storm
      much more (HUD `gloom` 0 → ~0.6 → 1, exposure down to -0.7 EV); the fog
      turns grey, colors desaturate; clouds close in (step 7) and their shadows
      pass on the ground (step 8). Back to normal after the forced weather.
- [ ] Small splashes on the ground around you while it rains.
- [ ] After the forced rain: drops keep falling under the trees for a while
      (`drips` > 0 while `ground wetness` is high), none in the clearings.
- [ ] Ground, trees and mushrooms look darker and shinier when wet and dry out
      over a few game hours.
- [ ] (Optional) the rain sound fades in and out with the rain.
- [ ] Automation: 29 tests green (`ShroomHunt.Environment.LocalRain` and `SkyMood` included).

## UE concepts learned
- Niagara: user parameters set from C++, dynamic inputs (*Add Vector*),
  duplicated emitters, CPU collisions and event handlers.
- Translucent unlit particle material (`Particle Color`, gradient as opacity).
- Material Functions and Material Parameter Collections shared by all materials.
- An `UAudioComponent` faded in and out from C++.
