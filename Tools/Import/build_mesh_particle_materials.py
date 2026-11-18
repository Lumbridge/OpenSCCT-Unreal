"""Build the engine-only materials for MeshEmitter.UseMeshBlendMode=false. Run with run_script.py.

The mesh's UVs sample Texture. Custom primitive data 0..3 carries the simulated particle's RGBA.
The stock maps use their own mesh section materials and never need these overrides.
"""
import unreal

from scct_materials import FLOAT1, FLOAT3, FLOAT4, Graph


def build(name, blend):
    graph = Graph("/Game/OpenSCCT/Effects/M_ScctMeshParticle" + name)
    graph.material.set_editor_property("blend_mode", blend)
    graph.material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    graph.material.set_editor_property("two_sided", True)
    texture = graph.texture("Texture", "/Engine/EngineResources/WhiteSquareTexture")
    tint = graph.node(unreal.MaterialExpressionVectorParameter, parameter_name="ParticleColor",
                      default_value=unreal.LinearColor(1, 1, 1, 1),
                      use_custom_primitive_data=True, primitive_data_index=0)
    style = graph.scalar("Style", 3)
    # SetParticleMaterial, as in svm_particle.gdshaderinc. AlphaRef=-1 disables the independent test.
    rgba = graph.custom("return Style == 2 ? lerp(V, T, V.a) : T * V;", FLOAT4,
                        V=(tint, "RGBA"), T=(texture, "RGBA"), Style=style)
    passes = graph.custom("return C.a > Ref ? 1 : 0;", FLOAT1, C=rgba, Ref=graph.scalar("AlphaRef", -1))
    # A rejected texel adds zero or multiplies by one. This also works in non-pixel material passes.
    rgb = graph.custom("if (Style == 2) return lerp(1, 2 * C.rgb, Passes);"
                       " if (Style == 5) return 1 - C.rgb * Passes; return C.rgb * Passes;",
                       FLOAT3, C=rgba, Style=style, Passes=passes)
    light = rgb if name == "Modulate" else graph.in_the_picture(rgb)
    graph.output(light, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if name in ("Alpha", "Premul", "Add"):
        alpha = graph.custom("return Passes * (Style == 3 ? 1 : Style == 6 ? max(C.r,max(C.g,C.b)) : C.a);",
                             FLOAT1, C=rgba, Style=style, Passes=passes)
        graph.output(alpha, unreal.MaterialProperty.MP_OPACITY)
    if name == "Regular":
        # Preserve cutout/depth behaviour in shadow and depth passes as well as in the colour pass.
        graph.output(passes, unreal.MaterialProperty.MP_OPACITY_MASK)
    graph.finish()


for suffix, mode in (("Regular", "MASKED"), ("Alpha", "TRANSLUCENT"), ("Modulate", "MODULATE"),
                     ("Add", "ADDITIVE"), ("Premul", "ALPHA_COMPOSITE")):
    build(suffix, getattr(unreal.BlendMode, "BLEND_" + mode))
