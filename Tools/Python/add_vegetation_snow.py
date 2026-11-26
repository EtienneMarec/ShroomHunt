"""Add snow on the upper side of the vegetation in M_Vegetation (bark, branches, leaves, berries).

Inserts, between the current Base Color / Roughness of the material and its result node, a Lerp toward
snow white with alpha = (world normal pointing up) x MPC_Season.SnowCover. Leaves are double-sided
geometry: only their upper face whitens. Idempotent: nodes it adds are tagged "ShroomSnow" in their
description; a second run does nothing.

Run in the editor (Tools > Execute Python Script...) or headless (editor closed):
    UnrealEditor-Cmd.exe ShroomHunt.uproject -run=pythonscript
        -script="<project>/Tools/Python/add_vegetation_snow.py [material path]"
"""

import sys

import unreal

MATERIAL = sys.argv[1] if len(sys.argv) > 1 else "/Game/ShroomHunt/Environment/Vegetation/M_Vegetation"
COLLECTION = "/Game/ShroomHunt/Environment/MPC_Season"
TAG = "ShroomSnow"
SNOW_COLOR = (0.86, 0.89, 0.93)
SNOW_ROUGHNESS = 0.7
# Normal Z above which surfaces hold snow, and how fast they turn fully white.
UP_START = 0.25
UP_SHARPNESS = 2.5
# Most of the upper side whitens, not all: bark texture and leaf color still show through.
MAX_COVER = 0.85

mel = unreal.MaterialEditingLibrary


def log(message):
    unreal.log(f"[snow] {message}")


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


def main():
    material = unreal.EditorAssetLibrary.load_asset(MATERIAL)
    collection = unreal.EditorAssetLibrary.load_asset(COLLECTION)
    if material is None or collection is None:
        raise RuntimeError(f"missing {MATERIAL} or {COLLECTION}")
    if any(str(e.get_editor_property("desc")) == TAG for e in mel.get_material_expressions(material)):
        log(f"{MATERIAL} already has the snow nodes: nothing to do")
        return

    base_source = mel.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)
    base_output = mel.get_material_property_input_node_output_name(material, unreal.MaterialProperty.MP_BASE_COLOR)
    rough_source = mel.get_material_property_input_node(material, unreal.MaterialProperty.MP_ROUGHNESS)
    rough_output = mel.get_material_property_input_node_output_name(material, unreal.MaterialProperty.MP_ROUGHNESS)
    if base_source is None:
        raise RuntimeError("Base Color is not connected")
    log(f"base color from {base_source.get_name()}.{base_output}, roughness from {rough_source.get_name() if rough_source else None}.{rough_output}")

    x, y = 600, -600
    # Upward mask: saturate((normal.z - UP_START) * UP_SHARPNESS).
    normal = node(material, unreal.MaterialExpressionVertexNormalWS, x - 900, y)
    mask_z = node(material, unreal.MaterialExpressionComponentMask, x - 700, y)
    mask_z.set_editor_property("r", False)
    mask_z.set_editor_property("g", False)
    mask_z.set_editor_property("b", True)
    mask_z.set_editor_property("a", False)
    connect(normal, "", mask_z, "")
    shifted = node(material, unreal.MaterialExpressionSubtract, x - 550, y)
    connect(mask_z, "", shifted, "A")
    connect(scalar(material, UP_START, x - 700, y + 80), "", shifted, "B")
    steep = node(material, unreal.MaterialExpressionMultiply, x - 400, y)
    connect(shifted, "", steep, "A")
    connect(scalar(material, UP_SHARPNESS, x - 550, y + 80), "", steep, "B")
    upward = node(material, unreal.MaterialExpressionSaturate, x - 250, y)
    connect(steep, "", upward, "")

    cover = node(material, unreal.MaterialExpressionCollectionParameter, x - 550, y + 200)
    cover.set_editor_property("collection", collection)
    cover.set_editor_property("parameter_name", "SnowCover")
    amount = node(material, unreal.MaterialExpressionMultiply, x - 100, y + 100)
    connect(upward, "", amount, "A")
    connect(cover, "", amount, "B")
    alpha = node(material, unreal.MaterialExpressionMultiply, x + 50, y + 100)
    connect(amount, "", alpha, "A")
    connect(scalar(material, MAX_COVER, x - 100, y + 200), "", alpha, "B")

    white = node(material, unreal.MaterialExpressionConstant3Vector, x + 50, y - 150)
    white.set_editor_property("constant", unreal.LinearColor(*SNOW_COLOR, 1.0))
    color = node(material, unreal.MaterialExpressionLinearInterpolate, x + 250, y)
    connect(base_source, base_output, color, "A")
    connect(white, "", color, "B")
    connect(alpha, "", color, "Alpha")
    if not mel.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR):
        raise RuntimeError("cannot connect Base Color")

    if rough_source is not None:
        rough = node(material, unreal.MaterialExpressionLinearInterpolate, x + 250, y + 250)
        connect(rough_source, rough_output, rough, "A")
        connect(scalar(material, SNOW_ROUGHNESS, x + 50, y + 300), "", rough, "B")
        connect(alpha, "", rough, "Alpha")
        if not mel.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS):
            raise RuntimeError("cannot connect Roughness")

    mel.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    log(f"OK: snow added to {MATERIAL}")


main()
