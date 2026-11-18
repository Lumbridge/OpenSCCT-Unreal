"""Build the self-lit display used by the original trigger_hack wall panel."""
import os
import sys

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.project_dir(), "Tools", "Import"))
from scct_materials import FLOAT2, FLOAT3, Graph

g = Graph("/Game/OpenSCCT/Materials/M_ScctHackPanel")
g.material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
g.material.set_editor_property("used_with_nanite", True)
clock = g.node(unreal.MaterialExpressionTime)
uv = g.node(unreal.MaterialExpressionTextureCoordinate)
layers = []
for index in (1, 2):
    pan = g.colour("Pan%d" % index, (0, 0, 0, 0))
    coords = g.custom("return UV + frac(T * Pan.xy);", FLOAT2, UV=uv, T=clock, Pan=pan)
    layers.append(g.texture("Layer%d" % index, "/Engine/EngineResources/WhiteSquareTexture", uvs=coords))
# UE2 subtracts in display colour space. Unreal samples sRGB textures in linear space.
light = g.custom("return pow(saturate(pow(First, 1.0/2.2) - pow(Second, 1.0/2.2)), 2.2);", FLOAT3,
                 First=(layers[0], "RGB"), Second=(layers[1], "RGB"))
g.output(g.in_the_picture(light), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
g.finish()
