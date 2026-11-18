"""Build the screen-space variant of the map projector's light function (editor Python)."""
import unreal


def build():
    assets = unreal.EditorAssetLibrary
    library = unreal.MaterialEditingLibrary
    base = "/Game/OpenSCCT/Effects/M_ScctLightFunction"
    path = "/Game/OpenSCCT/Effects/M_ScctEchelonReceiverFilter"
    if assets.does_asset_exist(path):
        assets.delete_asset(path)
    material = assets.duplicate_asset(base, path)
    output = unreal.MaterialProperty.MP_EMISSIVE_COLOR
    shape = library.get_material_property_input_node(material, output)
    if not shape:
        raise RuntimeError("Missing map light-function expression")
    material.set_editor_property("force_compatible_with_light_function_atlas", False)
    depth = library.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter)
    depth.set_editor_property("parameter_name", "ExcludedDepth")
    depth.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/Black"))
    position = library.create_material_expression(material, unreal.MaterialExpressionWorldPosition)
    position.set_editor_property("world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_CAMERA_RELATIVE)
    mask = library.create_material_expression(material, unreal.MaterialExpressionCustom)
    mask.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    inputs = []
    for name in ("Shape", "DepthMap", "TranslatedPosition"):
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        inputs.append(entry)
    mask.set_editor_property("inputs", inputs)
    # LightFunctionCommon.ush reconstructs ScreenPosition from the lit surface. The depth-only
    # pass uses the same camera, resolution and temporal jitter. SceneDepth is in centimetres.
    mask.set_editor_property("code", """
// Auxiliary cameras have their own projection. Only the view that scheduled this pass may read it.
if ((View.PostVolumeUserFlags & 0x40000000) == 0) return Shape;
float4 clip = mul(float4(TranslatedPosition, 1.0), PrimaryView.TranslatedWorldToClip);
float2 uv = clip.xy / clip.w * float2(0.5, -0.5) + 0.5;
float receiver = Texture2DSampleLevel(DepthMap, DepthMapSampler, uv, 0).r;
return abs(receiver - clip.w) <= max(0.002, abs(clip.w) * 0.000002) ? 0.0 : Shape;
""")
    if not (library.connect_material_expressions(shape, "", mask, "Shape")
            and library.connect_material_expressions(depth, "", mask, "DepthMap")
            and library.connect_material_expressions(position, "", mask, "TranslatedPosition")
            and library.connect_material_property(mask, "", output)):
        raise RuntimeError("Could not connect the Echelon receiver depth test")
    errors = library.recompile_material(material)
    if errors:
        raise RuntimeError("Echelon receiver shader failed to compile: " + "\n".join(errors))
    if not assets.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError("Could not save the Echelon receiver light function")


if __name__ == "__main__":
    build()
