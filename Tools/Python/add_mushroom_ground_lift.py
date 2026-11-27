"""Lift the forest mushrooms onto the displaced leaf litter in M_Mushroom.

The landscape is displaced by Nanite tessellation (V1-2b, D-043): leaves pile up a couple of
centimeters above the mean ground, so mushrooms placed on the mean ground looked buried. This adds
a World Position Offset to M_Mushroom:

    Z = CustomPrimitiveData[0] x GroundLiftCm

Custom Primitive Data 0 is set to 1 by the Mushroom Spawner on its mushrooms only: the examined
specimen and the Mushroom Garden (default 0) do not move.

Snow does not lift the mushrooms any more (they rose and sank with the snow pack, which looked odd
during a thaw): the landscape lowers its snow layer around the mushrooms nearest to the camera
instead (Tools/Python/add_snow_hollows.py).

Idempotent: nodes it adds are tagged "ShroomGroundLift" in their description and are rebuilt
on every run (the snow nodes of the first version are removed too). Run in the editor
(Tools > Execute Python Script...) or headless (editor closed):
    UnrealEditor-Cmd.exe ShroomHunt.uproject -run=pythonscript
        -script="<project>/Tools/Python/add_mushroom_ground_lift.py [material path]"
"""

import sys

import unreal

MATERIAL = sys.argv[1] if len(sys.argv) > 1 else "/Game/ShroomHunt/Mushrooms/M_Mushroom"
TAG = "ShroomGroundLift"
# Top of the leaf litter above the mean ground (GroundTextures.json: litter relief x ReliefScale).
GROUND_LIFT_CM = 1.5

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


def binary(material, cls, a, b, x, y, a_out="", b_out=""):
    expression = node(material, cls, x, y)
    connect(a, a_out, expression, "A")
    connect(b, b_out, expression, "B")
    return expression


def main():
    material = unreal.EditorAssetLibrary.load_asset(MATERIAL)
    if material is None:
        raise RuntimeError(f"missing {MATERIAL}")

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
    lift = node(material, unreal.MaterialExpressionScalarParameter, x + 1400, y + 350)
    lift.set_editor_property("parameter_name", "GroundLiftCm")
    lift.set_editor_property("default_value", GROUND_LIFT_CM)

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
    log(f"OK: ground lift ({GROUND_LIFT_CM} cm) added to {MATERIAL}")


main()
