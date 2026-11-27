"""Lift the forest mushrooms onto the displaced ground in M_Mushroom (leaf litter relief and snow).

The landscape is displaced by Nanite tessellation (V1-2b, D-043): leaves pile up a couple of
centimeters above the mean ground and snow adds MPC_Season.SnowDepthCm where it lies, so mushrooms
placed on the mean ground looked buried. This adds a World Position Offset to M_Mushroom:

    Z = CustomPrimitiveData[0] x (GroundLiftCm + snow mask x MPC_Season.SnowDepthCm)

- the snow mask is the landscape's own patchy mask (M2 step 3.6), evaluated at the mushroom's
  position: saturate((SnowCover x 1.6 - Noise(ObjectPositionWS x 0.004)) x 3), with the same
  Noise settings (output 0..1, 3 levels), so the mushroom stands on the snow exactly where the
  ground is raised;
- Custom Primitive Data 0 is set to 1 by the Mushroom Spawner on its mushrooms only: the
  examined specimen and the Mushroom Garden (default 0) do not move.

Idempotent: nodes it adds are tagged "ShroomGroundLift" in their description and are rebuilt
on every run. Run in the editor (Tools > Execute Python Script...) or headless (editor closed):
    UnrealEditor-Cmd.exe ShroomHunt.uproject -run=pythonscript
        -script="<project>/Tools/Python/add_mushroom_ground_lift.py [material path]"
"""

import sys

import unreal

MATERIAL = sys.argv[1] if len(sys.argv) > 1 else "/Game/ShroomHunt/Mushrooms/M_Mushroom"
COLLECTION = "/Game/ShroomHunt/Environment/MPC_Season"
TAG = "ShroomGroundLift"
# Top of the leaf litter above the mean ground (GroundTextures.json: litter relief x ReliefScale).
GROUND_LIFT_CM = 1.5
# The landscape's patchy snow (Docs/Milestones/M2.md, step 3.6).
SNOW_COVER_GAIN = 1.6
SNOW_NOISE_SCALE = 0.004
SNOW_SHARPNESS = 3.0

mel = unreal.MaterialEditingLibrary


def log(message):
    unreal.log(f"[mushroom lift] {message}")


def connect(source, output, target, input_name):
    if not mel.connect_material_expressions(source, output, target, input_name):
        raise RuntimeError(f"cannot connect {source.get_name()}.{output or 'out'} -> {target.get_name()}.{input_name or 'in'}")


def node(material, cls, x, y):
    expression = mel.create_material_expression(material, cls, x, y)
    expression.set_editor_property("desc", TAG)
    return expression


def scalar(material, value, x, y):
    constant = node(material, unreal.MaterialExpressionConstant, x, y)
    constant.set_editor_property("r", value)
    return constant


def binary(material, cls, a, b, x, y, a_out="", b_out=""):
    expression = node(material, cls, x, y)
    connect(a, a_out, expression, "A")
    connect(b, b_out, expression, "B")
    return expression


def main():
    material = unreal.EditorAssetLibrary.load_asset(MATERIAL)
    collection = unreal.EditorAssetLibrary.load_asset(COLLECTION)
    if material is None or collection is None:
        raise RuntimeError(f"missing {MATERIAL} or {COLLECTION}")

    # Rebuild: remove our previous nodes, keep anything else already wired to World Position Offset.
    old = [e for e in mel.get_material_expressions(material) if str(e.get_editor_property("desc")) == TAG]
    previous_source = mel.get_material_property_input_node(material, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    previous_output = mel.get_material_property_input_node_output_name(material, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    if previous_source is not None and str(previous_source.get_editor_property("desc")) == TAG:
        previous_source = None
    for expression in old:
        mel.delete_material_expression(material, expression)
    log(f"removed {len(old)} old nodes; other offset: {previous_source.get_name() if previous_source else None}")

    x, y = -1400, 1200
    # Snow mask at the mushroom's position.
    position = node(material, unreal.MaterialExpressionObjectPositionWS, x, y)
    scaled = binary(material, unreal.MaterialExpressionMultiply, position, scalar(material, SNOW_NOISE_SCALE, x, y + 100), x + 200, y)
    noise = node(material, unreal.MaterialExpressionNoise, x + 400, y)
    noise.set_editor_property("output_min", 0.0)
    noise.set_editor_property("output_max", 1.0)
    noise.set_editor_property("levels", 3)
    connect(scaled, "", noise, "")  # first input: Position
    cover = node(material, unreal.MaterialExpressionCollectionParameter, x + 400, y + 200)
    cover.set_editor_property("collection", collection)
    cover.set_editor_property("parameter_name", "SnowCover")
    gained = binary(material, unreal.MaterialExpressionMultiply, cover, scalar(material, SNOW_COVER_GAIN, x + 400, y + 300), x + 600, y + 200)
    patchy = binary(material, unreal.MaterialExpressionSubtract, gained, noise, x + 800, y + 100)
    sharp = binary(material, unreal.MaterialExpressionMultiply, patchy, scalar(material, SNOW_SHARPNESS, x + 800, y + 200), x + 1000, y + 100)
    mask = node(material, unreal.MaterialExpressionSaturate, x + 1200, y + 100)
    connect(sharp, "", mask, "")

    depth = node(material, unreal.MaterialExpressionCollectionParameter, x + 1000, y + 300)
    depth.set_editor_property("collection", collection)
    depth.set_editor_property("parameter_name", "SnowDepthCm")
    snow = binary(material, unreal.MaterialExpressionMultiply, mask, depth, x + 1400, y + 200)
    lift_param = node(material, unreal.MaterialExpressionScalarParameter, x + 1400, y + 350)
    lift_param.set_editor_property("parameter_name", "GroundLiftCm")
    lift_param.set_editor_property("default_value", GROUND_LIFT_CM)
    lift = binary(material, unreal.MaterialExpressionAdd, snow, lift_param, x + 1600, y + 250)

    # Only on the mushrooms of the Mushroom Spawner (Custom Primitive Data 0 = 1; default 0 elsewhere).
    on_ground = node(material, unreal.MaterialExpressionScalarParameter, x + 1600, y + 400)
    on_ground.set_editor_property("parameter_name", "OnForestGround")
    on_ground.set_editor_property("default_value", 0.0)
    on_ground.set_editor_property("use_custom_primitive_data", True)
    on_ground.set_editor_property("primitive_data_index", 0)
    height = binary(material, unreal.MaterialExpressionMultiply, lift, on_ground, x + 1800, y + 300)
    offset = node(material, unreal.MaterialExpressionAppendVector, x + 2200, y + 300)
    zero_xy = node(material, unreal.MaterialExpressionConstant2Vector, x + 2000, y + 200)
    connect(zero_xy, "", offset, "A")
    connect(height, "", offset, "B")
    result = offset
    if previous_source is not None:
        result = binary(material, unreal.MaterialExpressionAdd, offset, previous_source, x + 2400, y + 300, b_out=previous_output)
    if not mel.connect_material_property(result, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET):
        raise RuntimeError("cannot connect World Position Offset")

    mel.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    log(f"OK: ground lift ({GROUND_LIFT_CM} cm + patchy snow depth) added to {MATERIAL}")


main()
