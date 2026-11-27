"""Lower the landscape's snow layer around the mushrooms nearest to the camera (M_Landscape_Forest).

The snow is a displacement layer of the landscape (V1-2b, D-043): its thickness is
snow mask x MPC_Season.SnowDepthCm. Mushrooms used to be lifted onto it, and rose and sank with
the snow pack (odd during a thaw). Now they stay on the leaf litter and the snow makes a hollow
around each of them, like snow melting around a stem:

    SnowDepthCm  ->  SnowDepthCm x Hollow(world position)

Hollow is 0 near a mushroom (within 0.6 x its radius), rises smoothly to 1 at its radius plus
1.5 x the snow depth (a gentle cone), and is 1 elsewhere. The Mushroom Spawner writes the eight
mushrooms nearest to the camera into MPC_Season.SnowHollow0..7 (xyz = world position in cm,
w = radius in cm, 0 = unused); run create_season_collection.py first so they exist.

The script finds the Collection Parameter node reading SnowDepthCm in the material, puts a
Multiply by a Custom node (the Hollow above) behind it and rewires everything that read the depth.
Idempotent: its nodes are tagged "ShroomSnowHollow" and rebuilt on every run. Run in the editor
(Tools > Execute Python Script...) or headless (editor closed):
    UnrealEditor-Cmd.exe ShroomHunt.uproject -run=pythonscript
        -script="<project>/Tools/Python/add_snow_hollows.py [material path]"
"""

import sys

import unreal

MATERIAL = sys.argv[1] if len(sys.argv) > 1 else "/Game/ShroomHunt/Environment/Landscape/M_Landscape_Forest"
COLLECTION = "/Game/ShroomHunt/Environment/MPC_Season"
TAG = "ShroomSnowHollow"
HOLLOWS = 8  # AMushroomSpawner::SnowHollowCount

CODE = """float Keep = 1.0;
float4 Hollows[8] = { H0, H1, H2, H3, H4, H5, H6, H7 };
for (int I = 0; I < 8; ++I)
{
    float4 H = Hollows[I];
    if (H.w > 0.0)
    {
        float Inner = 0.6 * H.w;
        float Outer = H.w + 1.5 * Depth + 4.0;
        float T = saturate((distance(P.xy, H.xy) - Inner) / max(Outer - Inner, 1.0));
        Keep = min(Keep, T * T * (3.0 - 2.0 * T));
    }
}
return Keep;"""

mel = unreal.MaterialEditingLibrary


def log(message):
    unreal.log(f"[snow hollows] {message}")


def connect(source, output, target, input_name):
    if not mel.connect_material_expressions(source, output, target, input_name):
        raise RuntimeError(f"cannot connect {source.get_name()}.{output or 'out'} -> {target.get_name()}.{input_name or 'in'}")


def tagged(expression):
    return str(expression.get_editor_property("desc")) == TAG


def collection_parameter(material, collection, name, x, y):
    expression = mel.create_material_expression(material, unreal.MaterialExpressionCollectionParameter, x, y)
    expression.set_editor_property("desc", TAG)
    expression.set_editor_property("collection", collection)
    expression.set_editor_property("parameter_name", name)
    return expression


def main():
    material = unreal.EditorAssetLibrary.load_asset(MATERIAL)
    collection = unreal.EditorAssetLibrary.load_asset(COLLECTION)
    if material is None or collection is None:
        raise RuntimeError(f"missing {MATERIAL} or {COLLECTION}")
    vectors = {str(p.get_editor_property("parameter_name")) for p in collection.get_editor_property("vector_parameters")}
    missing = [f"SnowHollow{i}" for i in range(HOLLOWS) if f"SnowHollow{i}" not in vectors]
    if missing:
        raise RuntimeError(f"{COLLECTION} lacks {', '.join(missing)}: run create_season_collection.py first")

    expressions = list(mel.get_material_expressions(material))
    depth = [e for e in expressions if isinstance(e, unreal.MaterialExpressionCollectionParameter) and not tagged(e)
             and str(e.get_editor_property("parameter_name")) == "SnowDepthCm"]
    if len(depth) != 1:
        raise RuntimeError(f"expected one SnowDepthCm node in {MATERIAL}, found {len(depth)}")
    depth = depth[0]
    old = {e.get_name() for e in expressions if tagged(e)}

    # Everything that reads the depth (or our previous Multiply standing for it).
    readers = []
    for expression in expressions:
        if tagged(expression):
            continue
        names = list(mel.get_material_expression_input_names(expression))
        inputs = list(mel.get_inputs_for_material_expression(material, expression))
        for name, source in zip(names, inputs):
            if source is not None and (source.get_name() == depth.get_name() or source.get_name() in old):
                readers.append((expression, name))
    if not readers:
        raise RuntimeError("nothing reads SnowDepthCm: is the snow displacement wired (Docs/Milestones/V1.md, V1-2b)?")
    for expression in [e for e in expressions if tagged(e)]:
        mel.delete_material_expression(material, expression)
    log(f"removed {len(old)} old nodes; readers: {', '.join(f'{e.get_name()}.{n}' for e, n in readers)}")

    x = depth.get_editor_property("material_expression_editor_x")
    y = depth.get_editor_property("material_expression_editor_y") + 250
    custom = mel.create_material_expression(material, unreal.MaterialExpressionCustom, x + 300, y)
    custom.set_editor_property("desc", TAG)
    custom.set_editor_property("description", "SnowHollow")
    custom.set_editor_property("code", CODE)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    inputs = []
    for name in ["P", "Depth"] + [f"H{i}" for i in range(HOLLOWS)]:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    custom.set_editor_property("inputs", inputs)

    position = mel.create_material_expression(material, unreal.MaterialExpressionWorldPosition, x, y)
    position.set_editor_property("desc", TAG)
    connect(position, "", custom, "P")
    connect(depth, "", custom, "Depth")
    for index in range(HOLLOWS):
        connect(collection_parameter(material, collection, f"SnowHollow{index}", x, y + 80 * (index + 1)), "", custom, f"H{index}")

    multiply = mel.create_material_expression(material, unreal.MaterialExpressionMultiply, x + 600, y - 150)
    multiply.set_editor_property("desc", TAG)
    connect(depth, "", multiply, "A")
    connect(custom, "", multiply, "B")
    for expression, name in readers:
        connect(multiply, "", expression, name)

    mel.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    log(f"OK: snow depth x hollow ({HOLLOWS} mushrooms) in {MATERIAL}")


main()
