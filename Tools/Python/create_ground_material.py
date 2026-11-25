"""Create (or rebuild) the material function MF_GroundLayers from Data/Environment/GroundTextures.json.

The function blends the generated ground textures (T_Ground_<Layer>_D / _N, D-042) of every landscape
paint layer with a height blend (the albedo alpha is the height, so leaves and pebbles stay on top at
layer borders) and outputs BaseColor, Normal and Roughness. M_Landscape_Forest calls it instead of
its flat colors (Docs/Milestones/V1.md, step V1-2).

Idempotent: an existing function is emptied and rebuilt in place, so the materials that call it keep
their reference. Logs what it creates.

Run in the editor (Tools > Execute Python Script...) or headless (editor closed):
    UnrealEditor-Cmd.exe ShroomHunt.uproject -run=pythonscript
        -script="<project>/Tools/Python/create_ground_material.py"
The textures must exist (the editor generates them at startup, or sh.Ground.BakeTextures).
"""

import json
import os

import unreal

FOLDER = "/Game/ShroomHunt/Environment/Landscape"
TEXTURE_FOLDER = FOLDER + "/Generated"
NAME = "MF_GroundLayers"
DATA = os.path.join(unreal.Paths.project_dir(), "Data", "Environment", "GroundTextures.json")

mel = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary


def log(message):
    unreal.log(f"[ground] {message}")


def load_function():
    path = f"{FOLDER}/{NAME}"
    if assets.does_asset_exist(path):
        function = assets.load_asset(path)
        mel.delete_all_material_expressions_in_function(function)
        log(f"rebuilding {path}")
        return function
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    function = tools.create_asset(NAME, FOLDER, unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew())
    log(f"created {path}")
    return function


def connect(source, output, target, input_name):
    """connect_material_expressions only returns False on a wrong pin name: make that loud."""
    if not mel.connect_material_expressions(source, output, target, input_name):
        raise RuntimeError(f"cannot connect {source.get_name()}.{output or 'out'} -> {target.get_name()}.{input_name or 'in'}")


def node(function, cls, x, y):
    return mel.create_material_expression_in_function(function, cls, x, y)


def layer_blend(function, layers, x, y):
    blend = node(function, unreal.MaterialExpressionLandscapeLayerBlend, x, y)
    inputs = []
    for index, style in enumerate(layers):
        entry = unreal.LayerBlendInput()
        entry.set_editor_property("layer_name", style["Layer"])
        entry.set_editor_property("blend_type", unreal.LandscapeLayerBlendType.LB_HEIGHT_BLEND)
        entry.set_editor_property("preview_weight", 1.0 if index == 0 else 0.0)
        inputs.append(entry)
    blend.set_editor_property("layers", inputs)
    return blend


def texture_sample(function, path, sampler_type, coords, x, y):
    texture = assets.load_asset(path)
    if texture is None:
        raise RuntimeError(f"missing texture {path}: start the editor once or run sh.Ground.BakeTextures")
    sample = node(function, unreal.MaterialExpressionTextureSample, x, y)
    sample.set_editor_property("texture", texture)
    sample.set_editor_property("sampler_type", sampler_type)
    # Shared wrap sampler: a landscape material may use only 16 samplers, weightmaps included.
    sample.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
    connect(coords, "", sample, "UVs")
    return sample


def main():
    with open(DATA, encoding="utf-8") as file:
        layers = json.load(file)["Layers"]
    function = load_function()
    function.set_editor_property("description", "Generated ground layers (D-042): height-blended albedo, normal and roughness per landscape paint layer.")

    color = layer_blend(function, layers, 900, 0)
    normal = layer_blend(function, layers, 900, 700)
    rough = layer_blend(function, layers, 900, 1400)
    for index, style in enumerate(layers):
        layer = style["Layer"]
        y = index * 360
        coords = node(function, unreal.MaterialExpressionLandscapeLayerCoords, -900, y)
        # Landscape coordinates are in quads (1 m at scale 100): one tile every TileM meters.
        coords.set_editor_property("mapping_scale", float(style.get("TileM", 3.0)))
        albedo = texture_sample(function, f"{TEXTURE_FOLDER}/T_Ground_{layer}_D", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, coords, -500, y)
        normal_map = texture_sample(function, f"{TEXTURE_FOLDER}/T_Ground_{layer}_N", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL, coords, -500, y + 180)
        roughness = node(function, unreal.MaterialExpressionConstant, 400, 1400 + index * 80)
        roughness.set_editor_property("r", float(style.get("Roughness", 0.85)))

        connect(albedo, "RGB", color, f"Layer {layer}")
        connect(normal_map, "RGB", normal, f"Layer {layer}")
        connect(roughness, "", rough, f"Layer {layer}")
        for blend in (color, normal, rough):
            connect(albedo, "A", blend, f"Height {layer}")
        log(f"layer {layer}: tile {style.get('TileM', 3.0)} m, roughness {style.get('Roughness', 0.85)}")

    for priority, (name, source, y) in enumerate((("BaseColor", color, 0), ("Normal", normal, 700), ("Roughness", rough, 1400))):
        output = node(function, unreal.MaterialExpressionFunctionOutput, 1400, y)
        output.set_editor_property("output_name", name)
        output.set_editor_property("sort_priority", priority)
        connect(source, "", output, "")

    mel.update_material_function(function)
    assets.save_loaded_asset(function)
    log(f"OK: {NAME} with {len(layers)} layers")


main()
