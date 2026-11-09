"""Apply OutlinePolicy.ush to the connected outline Custom node and save only that material.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>.
Set OUTLIER_OUTLINE_VERIFY_ONLY=1 to verify without changing the asset.
"""

import hashlib
import json
import os
from datetime import datetime
from pathlib import Path
import shutil

import unreal


root = Path(unreal.Paths.project_dir()).resolve()
asset_path = "/Game/Material/PostProcess/M_PartnerOutline"
policy_file = root / "Scripts/Editor/OutlinePolicy.ush"
asset_file = root / "Content/Material/PostProcess/M_PartnerOutline.uasset"
report_dir = root / "Saved/DebugReports/2026-11-09-PartnerOutline"
report_dir.mkdir(parents=True, exist_ok=True)
verify_only = os.environ.get("OUTLIER_OUTLINE_VERIFY_ONLY") == "1"

material = unreal.load_asset(asset_path)
if material is None:
    raise RuntimeError("Could not load " + asset_path)

library = unreal.MaterialEditingLibrary
root_node = library.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
pending = [root_node]
seen = set()
candidates = []
while pending:
    node = pending.pop()
    if node is None or node.get_path_name() in seen:
        continue
    seen.add(node.get_path_name())
    if isinstance(node, unreal.MaterialExpressionCustom):
        code = node.get_editor_property("code")
        if "struct FOutlinePolicy" in code and "TeammateColor" in code:
            candidates.append(node)
    pending.extend(library.get_inputs_for_material_expression(material, node))

if len(candidates) != 1:
    raise RuntimeError("Expected one connected outline Custom node, found " + str(len(candidates)))

node = candidates[0]
desired = policy_file.read_text(encoding="utf-8-sig").replace("\r\n", "\n")
previous = node.get_editor_property("code").replace("\r\n", "\n")
changed = previous != desired
backup_dir = None

if changed and verify_only:
    raise RuntimeError("Saved outline Custom code does not match OutlinePolicy.ush")

if changed:
    backup_dir = report_dir / ("asset-backup-" + datetime.now().strftime("%Y%m%d-%H%M%S"))
    backup_dir.mkdir()
    shutil.copy2(asset_file, backup_dir / asset_file.name)
    (backup_dir / "previous-custom-code.hlsl").write_text(previous, encoding="utf-8")
    node.modify()
    material.modify()
    node.set_editor_property("code", desired)
    library.recompile_material(material)
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError("Failed to save outline material")

actual = node.get_editor_property("code").replace("\r\n", "\n")
if actual != desired:
    raise RuntimeError("Outline Custom node verification failed")

report = {
    "material": asset_path,
    "node": node.get_path_name(),
    "verify_only": verify_only,
    "changed": changed,
    "backup": str(backup_dir) if backup_dir else None,
    "policy_sha256": hashlib.sha256(actual.encode("utf-8")).hexdigest(),
    "checks": {
        "stencil_dimensions": "CustomStencilTexture.GetDimensions" in actual,
        "depth_dimensions": "CustomDepthTexture.GetDimensions" in actual,
        "identity_fallback": "return Scene.rgb;" in actual,
    },
}
(report_dir / ("policy-verification.json" if verify_only else "policy-application.json")).write_text(
    json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
)
unreal.log("OUTLIER_OUTLINE_POLICY_OK " + json.dumps(report))
