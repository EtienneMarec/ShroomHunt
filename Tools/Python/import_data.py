"""Import ShroomHunt gameplay data (Data/*.json) into editor assets.

Run inside the Unreal Editor:
  - Tools > Execute Python Script... and pick this file, or
  - Output Log, switch the command box to "Python" and type:
        exec(open(unreal.Paths.project_dir() + "Tools/Python/import_data.py").read())

What it does (idempotent, safe to re-run):
  1. Creates /Game/ShroomHunt/Data/DT_Species (row struct MushroomSpeciesRow) if missing.
  2. Parses and validates every Data/Species/*.json through the C++ parser
     (unreal.SpeciesDataLibrary), so errors are identical to the automation tests.
  3. If valid, replaces all rows and saves the DataTable. If not, the table is left
     unchanged and every error is printed with its file and field.
"""

import unreal

DATA_FOLDER = "/Game/ShroomHunt/Data"
SPECIES_TABLE_NAME = "DT_Species"
SPECIES_TABLE_PATH = f"{DATA_FOLDER}/{SPECIES_TABLE_NAME}"


def _log_report(report: str) -> None:
    for line in report.splitlines():
        if line.startswith("ERROR") or line.startswith("FAILED"):
            unreal.log_error(line)
        elif line.startswith("WARNING"):
            unreal.log_warning(line)
        else:
            unreal.log(line)


def get_or_create_species_table() -> unreal.DataTable:
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)

    if assets.does_asset_exist(SPECIES_TABLE_PATH):
        table = assets.load_asset(SPECIES_TABLE_PATH)
        unreal.log(f"[import_data] Using existing {SPECIES_TABLE_PATH}")
        return table

    assets.make_directory(DATA_FOLDER)
    factory = unreal.DataTableFactory()
    factory.set_editor_property("struct", unreal.MushroomSpeciesRow.static_struct())
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    table = tools.create_asset(SPECIES_TABLE_NAME, DATA_FOLDER, unreal.DataTable, factory)
    if table is None:
        raise RuntimeError(f"Could not create {SPECIES_TABLE_PATH}")
    unreal.log(f"[import_data] Created {SPECIES_TABLE_PATH}")
    return table


def import_species() -> bool:
    folder = unreal.SpeciesDataLibrary.get_default_species_folder()
    unreal.log(f"[import_data] Importing species from {folder}")

    table = get_or_create_species_table()
    report = unreal.SpeciesDataLibrary.import_species_folder_into_table(table, folder)
    _log_report(report)

    if not report.startswith("OK"):
        unreal.log_error("[import_data] Species import FAILED: DataTable left unchanged.")
        return False

    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    if not assets.save_loaded_asset(table, False):
        unreal.log_error(f"[import_data] Could not save {SPECIES_TABLE_PATH}")
        return False

    unreal.log(f"[import_data] Species import OK, {SPECIES_TABLE_PATH} saved.")
    return True


def main() -> None:
    ok = import_species()
    unreal.log("[import_data] Done." if ok else "[import_data] Finished with errors (see above).")


main()
