"""Generic UE parents for OpenSCCT's flags.authored v1. Both loaders bind these exact assets."""
from pathlib import Path

import unreal
from scct_authored_geometry import DIR_FLAG_SLOT
from scct_materials import Graph, FLOAT1, FLOAT2, FLOAT3, asset_tools, editor_assets, lib

FOLDER = "/Game/OpenSCCT/Materials/Authored"
SLOTS = ("baseColor", "normal", "height", "roughness", "metallic", "specular", "emission",
         "ao", "opacity", "detailColor", "detailNormal", "environment")
SCALARS = dict(Rotation=0, Looping=1, PingPong=0, Triplanar=0, ParallaxDepth=0, ParallaxSteps=16,
               Opacity=1, DetailStrength=0, NormalDirectX=0, NormalStrength=1, BumpFromHeight=0,
               Roughness=0.85, Metallic=0, Specular=0.5, AoStrength=1, EmissionEnergy=0)


class AuthoredGraph(Graph):
    def __init__(self, name):
        self.path = FOLDER + "/" + name
        self.count = 0
        if editor_assets.does_asset_exist(self.path):
            self.material = unreal.load_asset(self.path)
            root = lib.get_material_property_input_node(self.material, unreal.MaterialProperty.MP_FRONT_MATERIAL)
            if root:
                lib.delete_material_expression(self.material, root)
            for expression in list(lib.get_material_expressions(self.material)):
                lib.delete_material_expression(self.material, expression)
        else:
            self.material = asset_tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())

    def wire(self, node, output, target, pin):
        if not lib.connect_material_expressions(node, output, target, pin):
            raise RuntimeError("Could not connect %s to %s.%s" % (output, target.get_name(), pin))


def master(translucent, unlit):
    g = AuthoredGraph("M_Authored_%s_%s" % ("Translucent" if translucent else "Solid", "Unlit" if unlit else "Lit"))
    mat = g.material
    mat.set_editor_property("tangent_space_normal", False)
    mat.set_editor_property("used_with_nanite", not translucent)
    if translucent:
        mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)
        mat.set_editor_property("refraction_method", unreal.RefractionMode.RM_2D_OFFSET)

    inputs = dict(UV=g.node(unreal.MaterialExpressionTextureCoordinate), Clock=g.node(unreal.MaterialExpressionTime),
                  Position=g.node(unreal.MaterialExpressionWorldPosition), Eye=g.node(unreal.MaterialExpressionCameraVectorWS),
                  Unlit=g.node(unreal.MaterialExpressionConstant, r=float(unlit)))
    inputs.update({name: g.scalar(name, value) for name, value in SCALARS.items()})
    for name, value in dict(Mapping=(1, 1, 0, 0), Atlas=(1, 1, 1, 12), Motion=(0, 0, 0, 0)).items():
        inputs[name] = g.vector4(name, value)
    for name in ("BaseFactor", "SpecularColor", "EmissionColor"):
        inputs[name] = g.colour(name, (1, 1, 1, 1))
    default = unreal.load_asset("/Engine/EngineMaterials/DefaultDiffuse_TC_Masks")
    for slot in SLOTS:
        # Authored textures arrive as linear RGBA with all four components preserved.
        inputs["T_" + slot] = g.node(unreal.MaterialExpressionTextureObjectParameter, parameter_name="T_" + slot,
                                    texture=default, sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
        inputs["B_" + slot] = g.vector4("B_" + slot, (0, 0, 0, 0))

    evaluator = g.custom(Path(__file__).with_name("scct_authored_surface.ush").read_text(encoding="utf-8"), FLOAT3, **inputs)
    outputs = []
    for name, kind in dict(F0=FLOAT3, WorldNormal=FLOAT3, WorldTangent=FLOAT3, Light=FLOAT3, Rough=FLOAT1, AO=FLOAT1, Alpha=FLOAT1).items():
        out = unreal.CustomOutput()
        out.set_editor_property("output_name", name)
        out.set_editor_property("output_type", kind)
        outputs.append(out)
    evaluator.set_editor_property("additional_outputs", outputs)

    if unlit:
        surface = g.node(unreal.MaterialExpressionSubstrateUnlitBSDF)
        light = g.custom("return Base + Emission;", FLOAT3, Base=(evaluator, "return"), Emission=(evaluator, "Light"))
        g.wire(light, "", surface, "")  # Substrate Unlit's first input is EmissiveColor.
        g.wire(evaluator, "WorldNormal", surface, "Normal")
    else:
        surface = g.node(unreal.MaterialExpressionSubstrateSlabBSDF)
        for output, pin in (("return", "Diffuse Albedo"), ("F0", "F0"), ("WorldNormal", "Normal"), ("Rough", "Roughness")):
            g.wire(evaluator, output, surface, pin)
        # The editor has already baked static emitters into UV1 pages and static-mesh vertex colors.
        # Their stored values are half the light term in display space. Direction is encoded in Y-up space.
        bake_maps = {name: g.node(unreal.MaterialExpressionTextureObjectParameter, parameter_name=name, texture=default,
                                  sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
                     for name in ("Lightmap", "BakedDirection")}
        anisotropy = g.scalar("Anisotropy", 0)
        clearcoat = g.scalar("Clearcoat", 0)
        gloss = g.scalar("ClearcoatGloss", 0.5)
        fill = g.custom("""
float3 stored = HasPage ? Lightmap.SampleGrad(LightmapSampler, UV, ddx(UV), ddy(UV)).rgb : saturate(UseVertex)*Vertex;
float3 term = stored*2;
float3 irradiance = lerp(term/12.92, pow((term+0.055)/1.055,2.4),step(0.04045,term));
bool vertexDirection = !HasPage && UseVertex && HasVertexDirection;
if (HasPage && HasDirection) {
    float4 page = BakedDirection.SampleGrad(LightmapSampler,UV,ddx(UV),ddy(UV));
    float3 axis = normalize(page.rgb*2-1).xzy;
    float ratio = saturate(dot(N,axis))/max(dot(Parameters.TangentToWorld[2],axis),0.1);
    irradiance *= lerp(1,clamp(ratio,0,4),page.a);
}
float3 highlight = 0;
float coherence = saturate(DirZA.y);
float3 encoded = float3(DirXY,DirZA.x)*2-1;
if (vertexDirection && coherence > 0 && dot(encoded,encoded) > 1e-8) {
    float3 axis = normalize(encoded).xzy;
    float nl = saturate(dot(N,axis));
    float ratio = nl/max(dot(normalize(Parameters.TangentToWorld[2]),axis),0.1);
    irradiance *= lerp(1,clamp(ratio,0,4),coherence);
    // Godot am_highlight: only the GGX and coat terms of this baked vertex light.
    if (nl > 0 && dot(Eye+axis,Eye+axis) > 1e-8) {
        float3 h = normalize(Eye + axis);
        float nv = max(dot(N,Eye),0.001), nh = saturate(dot(N,h)), vh = saturate(dot(Eye,h));
        float a = max(0.002,Rough*Rough*lerp(1,0.5+abs(dot(T,h)),Aniso));
        float a2 = a*a, denom = nh*nh*(a2-1)+1;
        float distribution = a2/max(3.14159265*denom*denom,0.00001);
        float k = (Rough+1)*(Rough+1)/8;
        float masking = nv/(nv*(1-k)+k) * nl/(nl*(1-k)+k);
        float3 fresnel = SpecF0 + (1-SpecF0)*pow(1-vh,5);
        float coat = Coat*pow(nh,lerp(8,256,Gloss))*0.25;
        highlight = (distribution*masking*fresnel/max(4*nv*nl,0.001)+coat)*nl*irradiance*coherence;
    }
}
return Light + (Base * AO * irradiance + highlight) * Fill;
""", FLOAT3, UV=g.node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=1),
                        DirXY=g.node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=2),
                        DirZA=g.node(unreal.MaterialExpressionTextureCoordinate, coordinate_index=3),
                        HasVertexDirection=g.node(unreal.MaterialExpressionScalarParameter, parameter_name="HasVertexDirection",
                                                  default_value=0, use_custom_primitive_data=True, primitive_data_index=DIR_FLAG_SLOT),
                        SpecF0=(evaluator, "F0"), Rough=(evaluator, "Rough"), T=(evaluator, "WorldTangent"),
                        Eye=inputs["Eye"], Aniso=anisotropy, Coat=clearcoat, Gloss=gloss,
                        Vertex=g.node(unreal.MaterialExpressionVertexColor), HasPage=g.scalar("HasLightmap", 0),
                        UseVertex=g.node(unreal.MaterialExpressionScalarParameter, parameter_name="BakedVertexLight",
                                         default_value=0, use_custom_primitive_data=True, primitive_data_index=1),
                        HasDirection=g.scalar("HasBakedDirection", 0), Fill=g.scalar("BakedFill", 0.5),
                        N=(evaluator, "WorldNormal"), Light=(evaluator, "Light"), Base=(evaluator, "return"),
                        AO=(evaluator, "AO"), **bake_maps)
        g.wire(fill, "", surface, "Emissive Color")
        g.wire(anisotropy, "", surface, "Anisotropy")
        g.wire(clearcoat, "", surface, "Second Roughness Weight")
        coat = g.custom("return 1 - Gloss;", FLOAT1, Gloss=gloss)
        g.wire(coat, "", surface, "Second Roughness")
        g.output(evaluator, unreal.MaterialProperty.MP_AMBIENT_OCCLUSION, "AO")
    if translucent:
        weight = g.node(unreal.MaterialExpressionSubstrateWeight)
        g.wire(surface, "", weight, "A")
        g.wire(evaluator, "Alpha", weight, "Weight")
        surface = weight
        normal_view = g.node(unreal.MaterialExpressionTransform,
                             transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD,
                             transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_VIEW)
        g.wire(evaluator, "WorldNormal", normal_view, "")
        distortion = g.custom("return N.xy * Strength;", FLOAT2, N=normal_view, Strength=g.scalar("Refraction", 0))
        g.output(distortion, unreal.MaterialProperty.MP_REFRACTION)
    else:
        coverage = g.custom("return step(Cutoff, A);", FLOAT1, A=(evaluator, "Alpha"), Cutoff=g.scalar("Cutoff", 0.5))
        g.output(coverage, unreal.MaterialProperty.MP_OPACITY_MASK)
    g.output(surface, unreal.MaterialProperty.MP_FRONT_MATERIAL)
    return g.finish()


def build():
    for unlit in (False, True):
        for translucent in (False, True):
            parent = master(translucent, unlit)
            for mode in (("translucent",) if translucent else ("opaque", "masked")):
                for two_sided in (False, True):
                    name = "MI_Authored_%s_%s%s" % (mode, "Unlit" if unlit else "Lit", "_TwoSided" if two_sided else "")
                    path = FOLDER + "/" + name
                    instance = unreal.load_asset(path) if editor_assets.does_asset_exist(path) else asset_tools.create_asset(
                        name, FOLDER, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
                    lib.set_material_instance_parent(instance, parent)
                    overrides = unreal.MaterialInstanceBasePropertyOverrides()
                    overrides.set_editor_property("override_two_sided", two_sided)
                    overrides.set_editor_property("two_sided", two_sided)
                    if mode == "masked":
                        overrides.set_editor_property("override_blend_mode", True)
                        overrides.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
                    instance.set_editor_property("base_property_overrides", overrides)
                    lib.update_material_instance(instance)
                    if not editor_assets.save_loaded_asset(instance, only_if_is_dirty=False):
                        raise RuntimeError("Could not save " + path)
