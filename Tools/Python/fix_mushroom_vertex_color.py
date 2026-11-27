"""Decode the mushroom vertex colors from sRGB in M_Mushroom (washed-out caps, 2026-11-28).

The mesh builder writes the species colors as sRGB values (the hex colors of Data/Species). A
static mesh stores vertex colors as sRGB-encoded 8-bit values and the shader reads them back as
they are, so the material received sRGB numbers and used them as linear colors: every cap came
out much paler (a red fly agaric looked salmon, a brown cep almost white, a green brittlegill
pale mint). This puts Power(VertexColor.rgb, 2.2) (sRGB to linear, close enough) between the
Vertex Color node and the Base Color input of MF_Wetness.

Idempotent: its nodes are tagged "ShroomVertexGamma" and rebuilt on every run. Run in the editor
(Tools > Execute Python Script...) or headless (editor closed):
    UnrealEditor-Cmd.exe ShroomHunt.uproject -run=pythonscript
        -script="<project>/Tools/Python/fix_mushroom_vertex_color.py [material path]"
"""

import sys

import unreal

MATERIAL = sys.argv[1] if len(sys.argv) > 1 else "/Game/ShroomHunt/Mushrooms/M_Mushroom"
TAG = "ShroomVertexGamma"
GAMMA = 2.2

mel = unreal.MaterialEditingLibrary


def log(message):
    unreal.log(f"[mushroom color] {message}")


def connect(source, output, target, input_name):
    if not mel.connect_material_expressions(source, output, target, input_name):
        raise RuntimeError(f"cannot connect {source.get_name()}.{output or 'out'} -> {target.get_name()}.{input_name or 'in'}")


def main():
    material = unreal.EditorAssetLibrary.load_asset(MATERIAL)
    if material is None:
        raise RuntimeError(f"missing {MATERIAL}")
    expressions = list(mel.get_material_expressions(material))
    old = [e for e in expressions if str(e.get_editor_property("desc")) == TAG]
    for expression in old:
        mel.delete_material_expression(material, expression)
    expressions = list(mel.get_material_expressions(material))

    wetness = [e for e in expressions if isinstance(e, unreal.MaterialExpressionMaterialFunctionCall)
               and "MF_Wetness" in str(e.get_editor_property("material_function"))]
    colors = [e for e in expressions if isinstance(e, unreal.MaterialExpressionVertexColor)]
    if len(wetness) != 1 or len(colors) != 1:
        raise RuntimeError(f"expected one MF_Wetness call and one Vertex Color node, found {len(wetness)} and {len(colors)}")
    wetness, color = wetness[0], colors[0]

    x = color.get_editor_property("material_expression_editor_x")
    y = color.get_editor_property("material_expression_editor_y")
    mask = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, x + 200, y)
    mask.set_editor_property("desc", TAG)
    mask.set_editor_property("r", True)
    mask.set_editor_property("g", True)
    mask.set_editor_property("b", True)
    mask.set_editor_property("a", False)
    connect(color, "", mask, "")
    power = mel.create_material_expression(material, unreal.MaterialExpressionPower, x + 400, y)
    power.set_editor_property("desc", TAG)
    power.set_editor_property("const_exponent", GAMMA)
    connect(mask, "", power, "Base")
    connect(power, "", wetness, "BaseColor")

    mel.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    log(f"OK: removed {len(old)} old nodes; Vertex Color decoded with gamma {GAMMA} in {MATERIAL}")


main()
