# List the trees and landscape material added from Fab, for setting up build_course.py.
# Run in the Unreal Editor (Tools > Execute Python Script). Writes exports/ue_assets.txt in the
# OpenTeeGolf folder (and the same to the Output Log).

import os
import unreal

# (every top-level folder in the project's Content, except what build_course.py made)
FOLDERS = sorted({"/Game/" + p.split("/")[2] for p in unreal.EditorAssetLibrary.list_assets("/Game", recursive=True, include_folder=False)
                  if len(p.split("/")) > 3 and p.split("/")[2] != "OpenTeeGolf"})
OUT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "exports", "ue_assets.txt"))

lines = []


def out(s):
    lines.append(s)
    unreal.log("[assets] " + s)


for folder in FOLDERS:
    if not unreal.EditorAssetLibrary.does_directory_exist(folder):
        continue
    out("== " + folder)
    for path in sorted(unreal.EditorAssetLibrary.list_assets(folder, recursive=True, include_folder=False)):
        data = unreal.EditorAssetLibrary.find_asset_data(path)
        cls = str(data.asset_class_path.asset_name) if hasattr(data, "asset_class_path") else str(data.asset_class)
        if cls not in ("StaticMesh", "Material", "MaterialInstanceConstant", "LandscapeGrassType", "LandscapeLayerInfoObject",
                       "FoliageType_InstancedStaticMesh", "Blueprint", "World") and "Vegetation" not in cls and "PVE" not in cls:
            continue
        extra = ""
        if cls == "StaticMesh":
            try:
                m = unreal.load_asset(path)
                b = m.get_bounding_box()
                extra = "  height %.0f cm, width %.0f cm, %d LODs, nanite %s" % (
                    b.max.z - b.min.z, max(b.max.x - b.min.x, b.max.y - b.min.y), m.get_num_lods(),
                    m.get_editor_property("nanite_settings").get_editor_property("enabled"))
            except Exception as e:
                extra = "  (%s)" % e
        elif cls in ("Material", "MaterialInstanceConstant"):
            try:
                m = unreal.load_asset(path)
                names = []
                for getter in ("get_scalar_parameter_names", "get_texture_parameter_names", "get_vector_parameter_names"):
                    try:
                        names += [str(n) for n in getattr(unreal.MaterialEditingLibrary, getter)(m)]
                    except Exception:
                        pass
                extra = "  params: " + ", ".join(names[:60])
            except Exception as e:
                extra = "  (%s)" % e
        out("%s  [%s]%s" % (path.split(".")[0], cls, extra))

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))
unreal.log("[assets] Written to " + OUT)
