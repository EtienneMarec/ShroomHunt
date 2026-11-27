"""Realign the forest PCG volumes on the landscape and delete orphan PCG partition actors.

What it fixes (2026-11-27): a PCG volume moved by accident left strips of ground without trees,
and PCG partition actors generated long ago with another field-to-world placement (before the
landscape existed) kept floating trees outside the landscape, where no volume covers them any
more, so no Cleanup reached them.

1. Every PCG volume is centered on the landscape (the forest field is centered on it; the
   volumes' scale, i.e. their size, is kept).
2. Every PCG partition actor whose cell does not touch the landscape is deleted (World Partition
   descriptors: works whether the actors are loaded or not).
3. The level's changed packages are saved.

Then, in the editor: load the whole map and regenerate the vegetation volume (see
Docs/Milestones/V1.md, "Forest PCG alignment"). Idempotent: a second run finds nothing to do.

Run with the editor closed (headless) or in the editor (Tools > Execute Python Script...):
    UnrealEditor-Cmd.exe ShroomHunt.uproject -run=pythonscript
        -script="<project>/Tools/Python/fix_forest_pcg_alignment.py [level path]"
"""

import sys

import unreal

LEVEL = sys.argv[1] if len(sys.argv) > 1 else "/Game/ShroomHunt/Forests/F01/Lvl_F01_LowlandOakBeech"


def log(message):
    unreal.log(f"[pcg alignment] {message}")


def loaded_actors():
    return unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()


def main():
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world is None or not world.get_path_name().startswith(LEVEL):
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(LEVEL)
    descs = list(unreal.WorldPartitionBlueprintLibrary.get_actor_descs())

    # Landscape extent from its streaming proxies (always right, loaded or not).
    proxies = [d.bounds for d in descs if "LandscapeStreamingProxy" in str(d.native_class)]
    if not proxies:
        raise RuntimeError("no landscape streaming proxy: is this the forest level?")
    min_x = min(b.min.x for b in proxies)
    min_y = min(b.min.y for b in proxies)
    max_x = max(b.max.x for b in proxies)
    max_y = max(b.max.y for b in proxies)
    center = unreal.Vector((min_x + max_x) / 2, (min_y + max_y) / 2, 0.0)
    log(f"landscape x {min_x / 100:.0f}..{max_x / 100:.0f} m, y {min_y / 100:.0f}..{max_y / 100:.0f} m, center ({center.x / 100:.1f}, {center.y / 100:.1f}) m")

    # 1. Volumes.
    volume_descs = [d for d in descs if "PCGVolume" in str(d.native_class)]
    unreal.WorldPartitionBlueprintLibrary.load_actors([d.guid for d in volume_descs])
    moved = 0
    packages = []
    for actor in loaded_actors():
        if not isinstance(actor, unreal.PCGVolume):
            continue
        location = actor.get_actor_location()
        if abs(location.x - center.x) > 1.0 or abs(location.y - center.y) > 1.0 or abs(location.z) > 1.0:
            log(f"{actor.get_actor_label()}: moved from ({location.x / 100:.2f}, {location.y / 100:.2f}, {location.z / 100:.2f}) m to the landscape center")
            actor.modify()
            actor.set_actor_location(center, False, False)
            packages.append(actor.get_package())
            moved += 1
        else:
            log(f"{actor.get_actor_label()}: already centered")

    # 2. Orphan partition actors.
    def touches_landscape(box):
        return box.max.x > min_x and box.min.x < max_x and box.max.y > min_y and box.min.y < max_y

    orphans = [d for d in descs if "PCGPartitionActor" in str(d.native_class) and not touches_landscape(d.bounds)]
    log(f"{len(orphans)} PCG partition actors outside the landscape")
    if orphans:
        unreal.WorldPartitionBlueprintLibrary.load_actors([d.guid for d in orphans])
        names = {str(d.name) for d in orphans}
        actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        doomed = [a for a in loaded_actors() if a.get_name() in names]
        for actor in doomed:
            log(f"deleting {actor.get_name()}")
            packages.append(actor.get_package())
        actor_subsystem.destroy_actors(doomed)

    if packages:
        # External actor packages: saving the package of a destroyed actor deletes its file.
        if not unreal.EditorLoadingAndSavingUtils.save_packages(packages, False):
            raise RuntimeError("saving the level's actor packages failed")
        log(f"saved {len(packages)} packages: {moved} volume(s) moved, {len(orphans)} orphan partition actor(s) deleted")
    else:
        log("nothing to do")


main()
