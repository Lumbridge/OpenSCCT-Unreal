"""Generate the merc's post-tonemap camera lens with the Unreal editor Python commandlet."""
import unreal


PACKAGE = "/Game/OpenSCCT/Materials"
NAME = "M_ScctMercCameraLens"

# Division-model lens. UV at the optical center and at each viewport corner is invariant.
# Curvature is a material parameter so HUD projection can read the exact value from the asset.
LENS = """
float2 uv = GetViewportUV(Parameters);
float2 centerOffset = uv - 0.5;
float2 viewSize = View.ViewSizeAndInvSize.xy;
float2 pixelOffset = 2.0 * centerOffset * viewSize;
float radiusSquared = saturate(dot(pixelOffset, pixelOffset) / dot(viewSize, viewSize));
float k = clamp(Curvature, 0.0, 0.45);
float2 warped = 0.5 + centerOffset * ((1.0 - k) / (1.0 - k * radiusSquared));
float2 sceneUV = ClampSceneTextureUV(ViewportUVToSceneTextureUV(warped, 14), 14);
float3 color = SceneTextureLookup(sceneUV, 14, true).rgb;
// Keep the sight picture sharp while adding red/blue separation and radial softness to
// high-contrast features towards the frame edge. Low compiles out all four peripheral taps.
if (EdgeDetail > 0.5 && radiusSquared > 0.18)
{
    float edge = smoothstep(0.18, 1.0, radiusSquared);
    float2 spreadUV = centerOffset * (0.008 * edge * edge);
    float2 innerFarUV = ClampSceneTextureUV(ViewportUVToSceneTextureUV(warped - spreadUV, 14), 14);
    float2 innerNearUV = ClampSceneTextureUV(ViewportUVToSceneTextureUV(warped - spreadUV * 0.45, 14), 14);
    float2 outerNearUV = ClampSceneTextureUV(ViewportUVToSceneTextureUV(warped + spreadUV * 0.45, 14), 14);
    float2 outerFarUV = ClampSceneTextureUV(ViewportUVToSceneTextureUV(warped + spreadUV, 14), 14);
    float3 innerFar = SceneTextureLookup(innerFarUV, 14, true).rgb;
    float3 innerNear = SceneTextureLookup(innerNearUV, 14, true).rgb;
    float3 outerNear = SceneTextureLookup(outerNearUV, 14, true).rgb;
    float3 outerFar = SceneTextureLookup(outerFarUV, 14, true).rgb;
    color = color * float3(0.20, 0.44, 0.20)
          + innerFar * float3(0.00, 0.04, 0.36) + outerFar * float3(0.36, 0.04, 0.00)
          + innerNear * float3(0.06, 0.24, 0.38) + outerNear * float3(0.38, 0.24, 0.06);
}
// The circular housing cuts light at the extreme corners. Every operation on a uniform
// patch preserves or attenuates its value, including fully black cover.
float falloff = smoothstep(0.22, 1.0, radiusSquared);
float housing = 1.0 - 0.84 * smoothstep(0.80, 1.0, radiusSquared);
return color * ((1.0 - 0.30 * falloff) * housing);
"""


def build():
    library = unreal.MaterialEditingLibrary
    path = PACKAGE + "/" + NAME
    material = unreal.EditorAssetLibrary.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if material:
        library.delete_all_material_expressions(material)
    else:
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            NAME, PACKAGE, unreal.Material, unreal.MaterialFactoryNew()
        )
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    # Vision samples depth and stencil in scene coordinates. Warp the finished sensor image,
    # including its priority-10 overlay, so the rifle and marked objects stay registered.
    material.set_editor_property("blendable_priority", 20)

    # A SceneTexture input declares PostProcessInput0 for the custom node's UV-adjusted lookup.
    scene = library.create_material_expression(material, unreal.MaterialExpressionSceneTexture, -500, 0)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    curve = library.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -500, 180)
    curve.set_editor_property("parameter_name", "Curvature")
    curve.set_editor_property("default_value", 0.14)
    cheap = library.create_material_expression(material, unreal.MaterialExpressionConstant, -780, 360)
    cheap.set_editor_property("r", 0.0)
    detailed = library.create_material_expression(material, unreal.MaterialExpressionConstant, -780, 500)
    detailed.set_editor_property("r", 1.0)
    quality = library.create_material_expression(material, unreal.MaterialExpressionQualitySwitch, -500, 360)
    lens = library.create_material_expression(material, unreal.MaterialExpressionCustom, -150, 0)
    lens.set_editor_property("description", "Merc camera lens")
    lens.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    pins = []
    for name in ("Scene", "Curvature", "EdgeDetail"):
        pin = unreal.CustomInput()
        pin.set_editor_property("input_name", name)
        pins.append(pin)
    lens.set_editor_property("inputs", pins)
    lens.set_editor_property("code", LENS.strip())
    if not all((
        library.connect_material_expressions(scene, "Color", lens, "Scene"),
        library.connect_material_expressions(curve, "", lens, "Curvature"),
        library.connect_material_expressions(cheap, "", quality, "Low"),
        library.connect_material_expressions(detailed, "", quality, "Default"),
        library.connect_material_expressions(quality, "", lens, "EdgeDetail"),
        library.connect_material_property(lens, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR),
    )):
        raise RuntimeError("Could not connect the merc camera material graph")
    errors = library.recompile_material(material)
    if errors:
        raise RuntimeError("\n".join(errors))
    stats = library.get_statistics(material)
    unreal.log("Merc camera material statistics: " + str(stats))
    if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError("Could not save " + path)
    unreal.log("Built " + path)


build()
