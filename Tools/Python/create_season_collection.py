"""Create or update /Game/ShroomHunt/Environment/MPC_Season (M2 seasons).

The Environment Director writes the seasonal values into this Material Parameter Collection
every frame; materials read them (foliage colors and leaf fall, snow, frost, fresh litter).

Run inside the Unreal Editor (Tools > Execute Python Script...) or headless:
  UnrealEditor-Cmd.exe ShroomHunt.uproject -run=pythonscript -script="<project>/Tools/Python/create_season_collection.py"

Idempotent: existing parameters keep their ids (materials that use them stay valid), missing
ones are added, nothing is removed. Genera come from Data/Vegetation/Vegetation.json.
"""

import json
import os

import unreal

FOLDER = "/Game/ShroomHunt/Environment"
NAME = "MPC_Season"
PATH = f"{FOLDER}/{NAME}"

# Forest averages (placeholder trees) and ground values, with their default (summer) value.
GLOBAL_SCALARS = {
    "LeafAmount": 1.0,
    "Coloring": 0.0,
    "Freshness": 0.0,
    "FreshLitter": 0.0,
    "Month": 7.0,
    "SnowCover": 0.0,
    "SnowDepthCm": 0.0,
    "SnowFalling": 0.0,
    "Frost": 0.0,
    "Dryness": 0.0,
}
GENUS_SCALARS = {"LeafAmount": 1.0, "Coloring": 0.0, "Freshness": 0.0}
SUMMER_GREEN = unreal.LinearColor(0.22, 0.36, 0.10, 1.0)


def read_genera():
    path = os.path.join(unreal.Paths.project_dir(), "Data", "Vegetation", "Vegetation.json")
    with open(path, encoding="utf-8") as f:
        data = json.load(f)
    return [tree["Genus"] for tree in data["Trees"]]


def get_or_create():
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    if assets.does_asset_exist(PATH):
        unreal.log(f"[season] Using existing {PATH}")
        return assets.load_asset(PATH)
    assets.make_directory(FOLDER)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    collection = tools.create_asset(NAME, FOLDER, unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    if collection is None:
        raise RuntimeError(f"Could not create {PATH}")
    unreal.log(f"[season] Created {PATH}")
    return collection


def main():
    genera = read_genera()
    collection = get_or_create()

    wanted_scalars = dict(GLOBAL_SCALARS)
    for genus in genera:
        for suffix, value in GENUS_SCALARS.items():
            wanted_scalars[genus + suffix] = value
    wanted_vectors = ["FoliageColor"] + [genus + "FoliageColor" for genus in genera]

    scalars = list(collection.get_editor_property("scalar_parameters"))
    vectors = list(collection.get_editor_property("vector_parameters"))
    have_scalars = {str(p.get_editor_property("parameter_name")) for p in scalars}
    have_vectors = {str(p.get_editor_property("parameter_name")) for p in vectors}

    added = []
    for name, value in wanted_scalars.items():
        if name not in have_scalars:
            parameter = unreal.CollectionScalarParameter()
            parameter.set_editor_property("parameter_name", name)
            parameter.set_editor_property("default_value", value)
            scalars.append(parameter)
            added.append(name)
    for name in wanted_vectors:
        if name not in have_vectors:
            parameter = unreal.CollectionVectorParameter()
            parameter.set_editor_property("parameter_name", name)
            parameter.set_editor_property("default_value", SUMMER_GREEN)
            vectors.append(parameter)
            added.append(name)

    if added:
        collection.set_editor_property("scalar_parameters", scalars)
        collection.set_editor_property("vector_parameters", vectors)
        unreal.EditorAssetLibrary.save_loaded_asset(collection, only_if_is_dirty=False)
        unreal.log(f"[season] Added {len(added)} parameters: {', '.join(added)}")
    else:
        unreal.log("[season] Up to date, nothing added")

    final_scalars = [str(p.get_editor_property("parameter_name")) for p in collection.get_editor_property("scalar_parameters")]
    final_vectors = [str(p.get_editor_property("parameter_name")) for p in collection.get_editor_property("vector_parameters")]
    unreal.log(f"[season] {PATH}: {len(final_scalars)} scalars, {len(final_vectors)} vectors")
    unreal.log(f"[season] scalars: {', '.join(final_scalars)}")
    unreal.log(f"[season] vectors: {', '.join(final_vectors)}")


main()
