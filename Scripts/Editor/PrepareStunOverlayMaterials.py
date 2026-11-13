"""Prepare the current stun overlay parents before PIE; never set usage during a stun.

Run with UnrealEditor-Cmd <uproject> -run=pythonscript -script=<this file>
-EnablePlugins=PythonScriptPlugin -unattended -nullrhi -nosourcecontrol.
"""

import unreal

MATERIAL_PATHS = (
    "/Game/Enemy/FX/DroneStun/M_DroneStun_Arcs",
    "/Game/Enemy/FX/DroneStun/M_DroneStun_Panels",
)

for path in MATERIAL_PATHS:
    material = unreal.load_asset(path)
    if not isinstance(material, unreal.Material):
        raise RuntimeError("Stun overlay parent material not found: " + path)
    if not material.get_editor_property("used_with_skeletal_mesh"):
        material.set_editor_property("used_with_skeletal_mesh", True)
        unreal.MaterialEditingLibrary.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError("Could not save stun overlay parent: " + path)
    unreal.log("Prepared stun overlay for skeletal meshes: " + path)
