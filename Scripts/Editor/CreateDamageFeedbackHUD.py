"""Create M_DamageFeedbackHUD from DamageFeedbackHUD.ush and bind it to WBP_DamageFeedbackLayer.DamageFeedbackHUD.

Run with UnrealEditor-Cmd <uproject> -run=pythonscript -script=<this file> -EnablePlugins=PythonScriptPlugin.
Re-running rebuilds the material graph and reuses the existing DamageFeedbackHUD image.
"""

import json
import shutil
from datetime import datetime
from pathlib import Path

import unreal


root = Path(unreal.Paths.project_dir()).resolve()
report_dir = root / "Saved/DebugReports/2026-11-09-DamageFeedbackHUD"
report_dir.mkdir(parents=True, exist_ok=True)

material_dir = "/Game/Material/Widget"
material_name = "M_DamageFeedbackHUD"
material_path = material_dir + "/" + material_name
custom_code_file = root / "Scripts/Editor/DamageFeedbackHUD.ush"
placeholder_texture_path = "/Game/Material/Texture/T_DamagedEffect"

wbp_path = "/Game/Blueprints/Widget/FirstPerson/WBP_DamageFeedbackLayer"
wbp_object_path = wbp_path + ".WBP_DamageFeedbackLayer"
wbp_file = root / "Content/Blueprints/Widget/FirstPerson/WBP_DamageFeedbackLayer.uasset"
image_name = "DamageFeedbackHUD"

library = unreal.MaterialEditingLibrary
assets = unreal.EditorAssetLibrary


def build_material():
    material = unreal.load_asset(material_path) if assets.does_asset_exist(material_path) else None
    if material is None:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            material_name, material_dir, unreal.Material, unreal.MaterialFactoryNew())
    if material is None:
        raise RuntimeError("Could not create " + material_path)

    material.modify()
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_UI)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    # Reuse the artist-assigned textures when rebuilding the two-level graph.
    textures = {}
    pending = [library.get_material_property_input_node(material, prop) for prop in
               (unreal.MaterialProperty.MP_EMISSIVE_COLOR, unreal.MaterialProperty.MP_OPACITY)]
    seen = set()
    while pending:
        node = pending.pop()
        if node is None or node.get_path_name() in seen:
            continue
        seen.add(node.get_path_name())
        if isinstance(node, unreal.MaterialExpressionTextureSampleParameter2D):
            textures[str(node.get_editor_property("parameter_name"))] = node.get_editor_property("texture")
        pending.extend(library.get_inputs_for_material_expression(material, node))
    library.delete_all_material_expressions(material)

    placeholder = unreal.load_asset(placeholder_texture_path)
    if placeholder is None:
        placeholder = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")

    custom = library.create_material_expression(material, unreal.MaterialExpressionCustom, -400, 0)
    custom.set_editor_property("code", custom_code_file.read_text(encoding="utf-8-sig").replace("\r\n", "\n"))
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property("description", "DamageFeedbackHUD")
    input_names = ["Tex1", "Tex2", "CurrentLevel", "NextLevel", "Blend", "Opacity"]
    custom_inputs = []
    for name in input_names:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        custom_inputs.append(custom_input)
    custom.set_editor_property("inputs", custom_inputs)

    for index in range(2):
        tex = library.create_material_expression(
            material, unreal.MaterialExpressionTextureSampleParameter2D, -900, -450 + index * 260)
        tex.set_editor_property("parameter_name", "Level%dTex" % (index + 1))
        tex.set_editor_property("texture", textures.get("Level%dTex" % (index + 1)) or placeholder)
        tex.set_editor_property("group", "Level")
        library.connect_material_expressions(tex, "RGBA", custom, "Tex%d" % (index + 1))

    # 기본값 0: 레이어가 값을 넣기 전(디자이너/초기화)에는 아무것도 그리지 않는다.
    for index, name in enumerate(["CurrentLevel", "NextLevel", "Blend", "Opacity"]):
        scalar = library.create_material_expression(
            material, unreal.MaterialExpressionScalarParameter, -900, 350 + index * 120)
        scalar.set_editor_property("parameter_name", name)
        scalar.set_editor_property("default_value", 0.0)
        scalar.set_editor_property("group", "State")
        library.connect_material_expressions(scalar, "", custom, name)

    rgb = library.create_material_expression(material, unreal.MaterialExpressionComponentMask, -150, -60)
    rgb.set_editor_property("r", True)
    rgb.set_editor_property("g", True)
    rgb.set_editor_property("b", True)
    rgb.set_editor_property("a", False)
    alpha = library.create_material_expression(material, unreal.MaterialExpressionComponentMask, -150, 80)
    alpha.set_editor_property("r", False)
    alpha.set_editor_property("g", False)
    alpha.set_editor_property("b", False)
    alpha.set_editor_property("a", True)
    library.connect_material_expressions(custom, "", rgb, "")
    library.connect_material_expressions(custom, "", alpha, "")
    library.connect_material_property(rgb, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    library.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)

    library.recompile_material(material)
    if not assets.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError("Failed to save " + material_path)
    return material


def bind_widget(material):
    wbp = unreal.load_asset(wbp_path)
    if wbp is None:
        raise RuntimeError("Could not load " + wbp_path)

    widget_tree = unreal.find_object(None, wbp_object_path + ":WidgetTree")
    canvas = unreal.find_object(None, wbp_object_path + ":WidgetTree.FeedbackCanvas")
    if widget_tree is None or canvas is None:
        raise RuntimeError("WidgetTree or FeedbackCanvas not found in " + wbp_path)

    before = [canvas.get_child_at(i).get_name() for i in range(canvas.get_children_count())]

    backup_dir = report_dir / ("asset-backup-" + datetime.now().strftime("%Y%m%d-%H%M%S"))
    backup_dir.mkdir()
    shutil.copy2(wbp_file, backup_dir / wbp_file.name)

    wbp.modify()
    widget_tree.modify()
    canvas.modify()

    image = unreal.find_object(None, wbp_object_path + ":WidgetTree." + image_name)
    created = image is None
    if created:
        image = unreal.new_object(unreal.Image, outer=widget_tree, name=image_name)
    image.modify()
    image.set_brush_from_material(material)
    image.set_visibility(unreal.SlateVisibility.HIT_TEST_INVISIBLE)

    slot = image.get_editor_property("slot")
    if slot is None or image.get_parent() != canvas:
        slot = canvas.add_child_to_canvas(image)
    # 화면 전체를 덮는다. 디자인 타임 자식이라 런타임 풀 인디케이터보다 아래에 그려진다.
    slot.set_anchors(unreal.Anchors(minimum=unreal.Vector2D(0.0, 0.0), maximum=unreal.Vector2D(1.0, 1.0)))
    slot.set_offsets(unreal.Margin(0.0, 0.0, 0.0, 0.0))
    slot.set_alignment(unreal.Vector2D(0.0, 0.0))
    slot.set_auto_size(False)
    slot.set_z_order(0)

    # Update serialized Blueprint overrides as well as the native defaults.
    defaults = unreal.get_default_object(unreal.load_class(None, wbp_object_path + "_C"))
    defaults.modify()
    defaults.set_editor_property("health_damage_level1_threshold", 1.0)
    defaults.set_editor_property("health_damage_level2_threshold", 20.0)
    unreal.BlueprintEditorLibrary.compile_blueprint(wbp)
    if not assets.save_loaded_asset(wbp, only_if_is_dirty=False):
        raise RuntimeError("Failed to save " + wbp_path)

    after = [canvas.get_child_at(i).get_name() for i in range(canvas.get_children_count())]
    brush_resource = image.get_editor_property("brush").get_editor_property("resource_object")
    return {
        "created_image": created,
        "canvas_children_before": before,
        "canvas_children_after": after,
        "image_brush": brush_resource.get_path_name() if brush_resource else None,
        "level1_threshold": defaults.get_editor_property("health_damage_level1_threshold"),
        "level2_threshold": defaults.get_editor_property("health_damage_level2_threshold"),
        "backup": str(backup_dir),
    }


material = build_material()
widget_report = bind_widget(material)
report = {"material": material_path, "widget": wbp_path}
report.update(widget_report)
(report_dir / "apply.json").write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.log("OUTLIER_DAMAGE_HUD_OK " + json.dumps(report))
