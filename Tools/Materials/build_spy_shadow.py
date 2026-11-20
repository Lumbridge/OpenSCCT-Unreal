"""Build the bounded, framebuffer-modulating character shadow projection in editor Python."""

import os
import sys

import unreal

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Import"))
from scct_materials import FLOAT1, Graph  # noqa: E402


# Keep in the same order as ShadowParts in ScctSpyShadow.cpp.
PARTS = 13
SHADOW_MASK = """
struct FProjectedPart
{
    float Coverage(float3 receiver, float4 a, float4 b)
    {
        float2 span = b.xy - a.xy;
        float t = saturate(dot(receiver.xy - a.xy, span) / max(dot(span, span), 0.01));
        float4 centre = lerp(a, b, t);
        float edge = length(receiver.xy - centre.xy) - centre.w;
        float below = receiver.z - centre.z;
        // A soft penumbra and a short depth fade keep the mask off the casting limb itself.
        return (1.0 - smoothstep(-2.0, 4.0, edge))
             * smoothstep(centre.w, centre.w + 6.0, below) * step(0.1, centre.w);
    }
};
// PixelDepth and SceneDepth are both linear view depth. The cube's back face supplies the view ray.
float3 ray = Box - Eye;
float3 receiver = Eye - CastOrigin + ray * (Scene / max(Pixel, 0.01));
float3 normal = cross(ddx(receiver), ddy(receiver));
normal *= dot(normal, Eye - CastOrigin - receiver) < 0.0 ? -1.0 : 1.0;
// The first blocking receiver along the cast separates this room from surfaces behind it.
float receiverSide = dot(receiver, ReceiverPlane.xyz) + ReceiverPlane.w;
float3 projected = float3(dot(receiver, CastAcross), dot(receiver, CastUp), dot(receiver, CastAlong));
[branch] if (projected.z < 0.0 || projected.z >= CastRange || dot(normal, CastAlong) >= 0.0 || receiverSide < -1.5)
    return 1.0;
FProjectedPart part;
float covered = 0.0;
"""


def build():
    graph = Graph("/Game/OpenSCCT/Effects/M_ScctSpyShadow", rebuild_in_place=True)
    material = graph.material
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MODULATE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("disable_depth_test", True)
    material.set_editor_property("translucency_pass", unreal.MaterialTranslucencyPass.MTP_BEFORE_DOF)
    values = dict(
        Box=graph.node(unreal.MaterialExpressionWorldPosition),
        Eye=graph.node(unreal.MaterialExpressionCameraPositionWS),
        Scene=graph.node(unreal.MaterialExpressionSceneDepth),
        Pixel=graph.node(unreal.MaterialExpressionPixelDepth),
        CastOrigin=graph.colour("CastOrigin", (0, 0, 0, 0)),
        CastAlong=graph.colour("CastAlong", (1, 0, 0, 0)),
        CastAcross=graph.colour("CastAcross", (0, 1, 0, 0)),
        CastUp=graph.colour("CastUp", (0, 0, 1, 0)),
        ReceiverPlane=graph.vector4("ReceiverPlane", (0, 0, 0, 0)),
        CastRange=graph.scalar("CastRange", 300.0),
        Darkness=graph.scalar("Darkness", 0.0),
    )
    code = SHADOW_MASK
    for index in range(PARTS):
        start = f"PartStart{index}"
        end = f"PartEnd{index}"
        values[start] = graph.vector4(start, (0, 0, 0, 0))
        values[end] = graph.vector4(end, (0, 0, 0, 0))
        code += f"covered = max(covered, part.Coverage(projected, {start}, {end}));\n"
    code += "return 1.0 - Darkness * covered * (1.0 - projected.z / CastRange);"
    graph.output(graph.custom(code, FLOAT1, **values), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    graph.finish()


if __name__ == "__main__":
    build()
