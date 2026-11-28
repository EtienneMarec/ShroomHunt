"""Decode the vegetation colors from sRGB in M_Vegetation (pale bark and foliage, 2026-11-28).

Like the mushrooms (fix_mushroom_vertex_color.py): the mesh builder writes the colors of
Data/Vegetation/Vegetation.json as sRGB values, a static mesh hands them to the shader as stored,
and the material used them as linear colors, so bark and leaves came out much paler (beech trunks
almost white). The seasonal foliage colors of MPC_Season come from the same data, in the same
space, and are mixed with the vertex color by a Lerp: this puts Power(rgb, 2.2) (sRGB to linear)
right after that Lerp and rewires everything that read it.

Idempotent: its nodes are tagged "ShroomVertexGamma" and rebuilt on every run. Run in the editor
(Tools > Execute Python Script...) or headless (editor closed):
    UnrealEditor-Cmd.exe ShroomHunt.uproject -run=pythonscript
        -script="<project>/Tools/Python/fix_vegetation_vertex_color.py [material path]"
"""

import sys

import unreal

MATERIAL = sys.argv[1] if len(sys.argv) > 1 else "/Game/ShroomHunt/Environment/Vegetation/M_Vegetation"
TAG = "ShroomVertexGamma"
GAMMA = 2.2

mel = unreal.MaterialEditingLibrary


def log(message):
    unreal.log(f"[vegetation color] {message}")


def connect(source, output, target, input_name):
    if not mel.connect_material_expressions(source, output, target, input_name):
        raise RuntimeError(f"cannot connect {source.get_name()}.{output or 'out'} -> {target.get_name()}.{input_name or 'in'}")


def tagged(expression):
    return str(expression.get_editor_property("desc")) == TAG


def inputs_of(material, expression):
    names = list(mel.get_material_expression_input_names(expression))
    sources = list(mel.get_inputs_for_material_expression(material, expression))
    return list(zip(names, sources))


def main():
    material = unreal.EditorAssetLibrary.load_asset(MATERIAL)
    if material is None:
        raise RuntimeError(f"missing {MATERIAL}")
    expressions = list(mel.get_material_expressions(material))
    old = {e.get_name() for e in expressions if tagged(e)}

    # The Lerp mixing the vertex color (A) with the seasonal foliage color.
    mixes = [e for e in expressions if isinstance(e, unreal.MaterialExpressionLinearInterpolate) and not tagged(e)
             and any(name == "A" and isinstance(source, unreal.MaterialExpressionVertexColor) for name, source in inputs_of(material, e))]
    if len(mixes) != 1:
        raise RuntimeError(f"expected one Lerp with a Vertex Color in A, found {len(mixes)}")
    mix = mixes[0]

    # Everything that reads the mix (or our previous Power standing for it).
    readers = []
    for expression in expressions:
        if tagged(expression):
            continue
        for name, source in inputs_of(material, expression):
            if source is not None and (source.get_name() == mix.get_name() or source.get_name() in old):
                readers.append((expression, name))
    if not readers:
        raise RuntimeError("nothing reads the vertex color mix")
    for expression in [e for e in expressions if tagged(e)]:
        mel.delete_material_expression(material, expression)
    log(f"removed {len(old)} old nodes; readers: {', '.join(f'{e.get_name()}.{n}' for e, n in readers)}")

    x = mix.get_editor_property("material_expression_editor_x")
    y = mix.get_editor_property("material_expression_editor_y")
    mask = mel.create_material_expression(material, unreal.MaterialExpressionComponentMask, x + 200, y + 150)
    mask.set_editor_property("desc", TAG)
    for channel, keep in (("r", True), ("g", True), ("b", True), ("a", False)):
        mask.set_editor_property(channel, keep)
    connect(mix, "", mask, "")
    power = mel.create_material_expression(material, unreal.MaterialExpressionPower, x + 400, y + 150)
    power.set_editor_property("desc", TAG)
    power.set_editor_property("const_exponent", GAMMA)
    connect(mask, "", power, "Base")
    for expression, name in readers:
        connect(power, "", expression, name)

    mel.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    log(f"OK: vegetation colors decoded with gamma {GAMMA} in {MATERIAL}")


main()
