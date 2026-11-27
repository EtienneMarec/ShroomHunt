"""Diagnose how the forest level is cut into World Partition cells (read-only).

Writes Saved/ForestStreaming.txt: the landscape transform and extent (in forest field meters), the
bounds of every landscape streaming proxy and PCG partition actor known to World Partition (loaded
or not, from the actor descriptors), their runtime grids, the World Partition grid settings when
Python can read them, and, for the PCG partition actors currently loaded, their instance counts and
whether their instances stick out of the landscape.

Run with the level open (Tools > Execute Python Script...). It changes nothing.
"""

import json
import os

import unreal

FOREST_JSON = "Data/Forests/F01_LowlandOakBeech.json"
lines = []


def out(text=""):
    lines.append(str(text))


def box_text(box):
    return f"min ({box.min.x / 100:.0f}, {box.min.y / 100:.0f}, {box.min.z / 100:.0f}) m  max ({box.max.x / 100:.0f}, {box.max.y / 100:.0f}, {box.max.z / 100:.0f}) m"


def union(boxes):
    if not boxes:
        return None
    lo = [min(b.min.x for b in boxes), min(b.min.y for b in boxes), min(b.min.z for b in boxes)]
    hi = [max(b.max.x for b in boxes), max(b.max.y for b in boxes), max(b.max.z for b in boxes)]
    return unreal.Box(unreal.Vector(*lo), unreal.Vector(*hi))


def safe(getter, default=None):
    try:
        return getter()
    except Exception as error:  # noqa: BLE001 - diagnostics must not stop on one missing property
        return default if default is not None else f"<{type(error).__name__}: {error}>"


def main():
    project = unreal.Paths.project_dir()
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    out(f"World: {world.get_path_name()}")
    with open(os.path.join(project, FOREST_JSON), encoding="utf-8") as handle:
        forest = json.load(handle)
    field_size = forest.get("HeightmapResolution", 1009) - 1
    out(f"Forest field: {field_size} m x {field_size} m (HeightmapResolution {forest.get('HeightmapResolution')})")

    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    out(f"Loaded actors: {len(actors)}")

    # --- Landscape.
    landscapes = [a for a in actors if isinstance(a, unreal.Landscape)]
    for landscape in landscapes:
        out(f"Landscape {landscape.get_actor_label()}: location {landscape.get_actor_location()} rotation {landscape.get_actor_rotation()} scale {landscape.get_actor_scale3d()}")
    if not landscapes:
        out("No ALandscape loaded.")

    # --- Actor descriptors: every actor World Partition knows, loaded or not.
    descs = safe(lambda: unreal.WorldPartitionBlueprintLibrary.get_actor_descs(), [])
    if isinstance(descs, str):
        out(f"get_actor_descs failed: {descs}")
        descs = []
    descs = list(descs)
    out(f"Actor descriptors: {len(descs)}")
    groups = {}
    for desc in descs:
        cls = safe(lambda: desc.native_class.get_name(), "?")
        groups.setdefault(cls, []).append(desc)
    for cls in sorted(groups, key=lambda c: -len(groups[c])):
        items = groups[cls]
        grids = sorted({str(safe(lambda: d.runtime_grid, "?")) for d in items})
        spatial = sorted({str(safe(lambda: d.is_spatially_loaded, "?")) for d in items})
        bounds = [safe(lambda: d.bounds) for d in items]
        bounds = [b for b in bounds if isinstance(b, unreal.Box)]
        total = union(bounds)
        out(f"  {cls}: {len(items)} actors, runtime grids {grids}, spatially loaded {spatial}" + (f", union {box_text(total)}" if total else ""))
        if any(key in cls for key in ("Landscape", "PCGPartition", "PCGVolume", "PCGWorld")):
            for d, b in sorted(zip(items, bounds), key=lambda p: (p[1].min.y, p[1].min.x))[:12]:
                size = (b.max.x - b.min.x) / 100, (b.max.y - b.min.y) / 100
                out(f"      {safe(lambda: d.label, '?')}: {box_text(b)}  ({size[0]:.0f} x {size[1]:.0f} m)  grid {safe(lambda: d.runtime_grid, '?')}")

    # --- World Partition grids (property names differ between hash types: try both).
    settings = safe(lambda: world.get_world_settings())
    partition = safe(lambda: settings.get_editor_property("world_partition"))
    hash_object = safe(lambda: partition.get_editor_property("runtime_hash"))
    out(f"Runtime hash: {safe(lambda: hash_object.get_class().get_name())}")
    for name in ("grids", "runtime_partitions"):
        entries = safe(lambda: hash_object.get_editor_property(name), [])
        if isinstance(entries, (list, unreal.Array)):
            for entry in entries:
                fields = {}
                for field in ("grid_name", "name", "cell_size", "loading_range", "block_on_slow_streaming", "priority", "main_layer"):
                    value = safe(lambda: entry.get_editor_property(field), "-")
                    if not str(value).startswith("<"):
                        fields[field] = value
                out(f"  {name}: {fields}")
                main_layer = fields.get("main_layer")
                if main_layer not in (None, "-"):
                    for field in ("cell_size", "loading_range", "name"):
                        out(f"      main_layer.{field} = {safe(lambda: main_layer.get_editor_property(field), '-')}")

    # --- Loaded PCG partition actors: instances and where they are.
    land_boxes = [safe(lambda: d.bounds) for cls, items in groups.items() if "Landscape" in cls for d in items]
    land_boxes = [b for b in land_boxes if isinstance(b, unreal.Box)]
    land = union(land_boxes)
    pcg = [a for a in actors if "PCGPartitionActor" in a.get_class().get_name()]
    out(f"Loaded PCG partition actors: {len(pcg)}")
    for actor in pcg:
        comps = actor.get_components_by_class(unreal.InstancedStaticMeshComponent)
        count = sum(safe(lambda: c.get_instance_count(), 0) for c in comps)
        origin, extent = actor.get_actor_bounds(False)
        outside = ""
        if land is not None:
            lo_x, hi_x = (origin.x - extent.x) / 100, (origin.x + extent.x) / 100
            lo_y, hi_y = (origin.y - extent.y) / 100, (origin.y + extent.y) / 100
            if lo_x < land.min.x / 100 - 1 or hi_x > land.max.x / 100 + 1 or lo_y < land.min.y / 100 - 1 or hi_y > land.max.y / 100 + 1:
                outside = "  <-- sticks out of the landscape"
        out(f"  {actor.get_actor_label()}: {len(comps)} ISM, {count} instances, bounds center ({origin.x / 100:.0f}, {origin.y / 100:.0f}) m half-size ({extent.x / 100:.0f}, {extent.y / 100:.0f}) m{outside}")

    path = os.path.join(project, "Saved", "ForestStreaming.txt")
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    unreal.log(f"[forest streaming] report written to {path} ({len(lines)} lines)")


main()
