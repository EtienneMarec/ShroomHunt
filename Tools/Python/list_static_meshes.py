"""List the static meshes under a content folder with their size and render settings.

Used to map third-party vegetation (Fab / Megascans trees) onto the vegetation palette
(Project Settings > ShroomHunt > Vegetation > Mesh Overrides): tree height tells the form
(sapling, pole, mature, old), Nanite and LOD count tell the cost.

Run inside the Unreal Editor (Output Log in "Python" mode):
    exec(open(unreal.Paths.project_dir() + "Tools/Python/list_static_meshes.py").read())
which lists /Game, or headless (editor closed) with a folder argument:
    UnrealEditor-Cmd.exe ShroomHunt.uproject -run=pythonscript
        -script="<project>/Tools/Python/list_static_meshes.py /Game/Megascans"

Read-only: changes nothing. Writes Saved/MeshInventory.csv and logs one line per mesh.
"""

import csv
import os
import sys

import unreal


def list_meshes(folder: str) -> list:
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    filter_ = unreal.ARFilter(
        package_paths=[folder],
        class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "StaticMesh")],
        recursive_paths=True,
    )
    rows = []
    for data in sorted(registry.get_assets(filter_), key=lambda d: str(d.package_name)):
        mesh = data.get_asset()
        if mesh is None:
            continue
        bounds = mesh.get_bounds()
        extent = bounds.box_extent
        origin = bounds.origin
        nanite = mesh.get_editor_property("nanite_settings").enabled
        rows.append({
            "path": str(data.package_name),
            "height_m": round((origin.z + extent.z) / 100.0, 2),
            "bottom_m": round((origin.z - extent.z) / 100.0, 2),
            "width_m": round(2.0 * max(extent.x, extent.y) / 100.0, 2),
            "nanite": nanite,
            # The editor subsystems do not exist in the headless commandlet: ask the mesh.
            "lods": mesh.get_num_lods(),
        })
    return rows


def main() -> None:
    folder = sys.argv[1] if len(sys.argv) > 1 else "/Game"
    rows = list_meshes(folder)
    out_path = os.path.join(unreal.Paths.project_saved_dir(), "MeshInventory.csv")
    with open(out_path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=["path", "height_m", "bottom_m", "width_m", "nanite", "lods"])
        writer.writeheader()
        writer.writerows(rows)
    for row in rows:
        unreal.log(f"MeshInventory: {row['path']}  height {row['height_m']} m  width {row['width_m']} m  "
                   f"bottom {row['bottom_m']} m  nanite {row['nanite']}  lods {row['lods']}")
    unreal.log(f"MeshInventory: {len(rows)} static meshes under {folder} -> {out_path}")


main()
