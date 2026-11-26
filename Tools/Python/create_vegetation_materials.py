"""Create the two instances of M_Vegetation used by the vegetation meshes (D-044).

- MI_VegetationWood: Blend Mode overridden to Opaque. Bark, branches, berries and ground litter never
  need the opacity mask; opaque keeps Nanite and the virtual shadow maps on their fast path.
- MI_VegetationFoliage: Two Sided overridden on (the master stays Masked). Living leaves and fronds
  are single-sided geometry, so a two-sided material halves their triangles.

The master keeps all the logic (vertex colors, seasons, leaf fall, wetness, snow). Idempotent: existing
instances are updated in place. Run in the editor (Tools > Execute Python Script...) or headless:
    UnrealEditor-Cmd.exe ShroomHunt.uproject -run=pythonscript -script="<project>/Tools/Python/create_vegetation_materials.py"
The editor rebakes the vegetation meshes at its next start to use them.
"""

import unreal

FOLDER = "/Game/ShroomHunt/Environment/Vegetation"
MASTER = f"{FOLDER}/M_Vegetation"


def log(message):
    unreal.log(f"[vegmat] {message}")


def get_or_create(name):
    path = f"{FOLDER}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        log(f"updating {path}")
        return unreal.EditorAssetLibrary.load_asset(path)
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    log(f"creating {path}")
    return tools.create_asset(name, FOLDER, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())


def configure(name, blend_mode=None, two_sided=None):
    master = unreal.EditorAssetLibrary.load_asset(MASTER)
    if master is None:
        raise RuntimeError(f"missing {MASTER}")
    instance = get_or_create(name)
    unreal.MaterialEditingLibrary.set_material_instance_parent(instance, master)
    overrides = instance.get_editor_property("base_property_overrides")
    overrides.set_editor_property("override_blend_mode", blend_mode is not None)
    if blend_mode is not None:
        overrides.set_editor_property("blend_mode", blend_mode)
    overrides.set_editor_property("override_two_sided", two_sided is not None)
    if two_sided is not None:
        overrides.set_editor_property("two_sided", two_sided)
    instance.set_editor_property("base_property_overrides", overrides)
    unreal.MaterialEditingLibrary.update_material_instance(instance)
    unreal.EditorAssetLibrary.save_loaded_asset(instance)
    log(f"{name}: blend {blend_mode if blend_mode is not None else 'inherited'}, two sided {two_sided if two_sided is not None else 'inherited'}")


configure("MI_VegetationWood", blend_mode=unreal.BlendMode.BLEND_OPAQUE, two_sided=False)
configure("MI_VegetationFoliage", two_sided=True)
log("OK")
