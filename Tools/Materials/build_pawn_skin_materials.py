"""Builds the state skins drawn over a pawn's body: the sticky shocker's Stickess skin, the merc's shock-protection
edge and the wet skin of a pawn in the rain.

Runs inside the editor's Python (Tools/Materials/Build-PawnSkinMaterials.ps1 does this headless):

    UnrealEditor-Cmd <project> -run=pythonscript -script="build_pawn_skin_materials.py"

Source/OpenSCCT/Play/ScctPawnSkins lays them over the body as overlay materials (a second draw of the same skinned
mesh, so the body keeps its own materials, the cloak's swap included): per material slot for the Stickess and wet
skins, whose textures follow the slot's own UVs, and over the whole body for the protection edge. Everything is
rebuilt from nothing each time.

  M_ScctStickySkin       SPersoTextures.Stickess_*: the body's own diffuse with SPersoTextures.sfx_perso.pan_sticky
                         (shadwagent_sticky_1 in a TexPanner, rate 0.5 along V) as self-illumination masked by its own
                         alpha. As an overlay: the panned texture laid over the lit body by its alpha, at its painted
                         brightness whatever the exposure, which is what the original's SelfIllumination does.
  M_ScctShockImmunityEdge OpenSCCT's merc shock-protection cue (the Godot client's svm_merc_shock_immunity): a
                         depth-tested additive amber fresnel edge with a faint fill, pulsing, lit from the feet up to
                         the share of protection left (Coverage, of the body from FeetZ to HeadZ, world cm).
  M_ScctWetSkin          SPersoTextures wet_lit_ATT / wet_lit_ATT_2 / lit_def_fx, the LitSpecularMaterials of
                         BeginWetSkin: the wet env map (SPersoTextures.envmap_wet, looked up by the camera-space
                         reflection) times the env colour, added through the slot's SpecularityMask alpha. Its
                         instances MI_ScctWetSkin_ATT (spy body), _ATT2 (spy head) and _DEF (merc body) carry each
                         slot's own mask.

The textures are the original game's, so they are never imported here: every texture parameter (Sticky, EnvMap,
SpecMask) holds a neutral engine texture, and Source/OpenSCCT/Play/ScctPawnSkins sets the real ones at run time, made
from the player's converted content (shared/chars/SPerso/textures under the -ScctContent folder). A Textures folder
left by an older build of this script is deleted.
"""
import os
import sys

import unreal

# Graph and in_the_picture are the master materials' helpers (Tools/Import/scct_materials.py).
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Import"))
from scct_materials import FLOAT1, FLOAT2, FLOAT3, Graph  # noqa: E402

PACKAGE = "/Game/OpenSCCT/Materials/PawnSkins"
TEXTURES = PACKAGE + "/Textures"

# The neutral stand-in of every texture parameter; the game replaces it.
NEUTRAL = "/Engine/EngineResources/Black"

# The wet skins' instances (the game sets each one's SpecMask).
WET_INSTANCES = ["MI_ScctWetSkin_ATT", "MI_ScctWetSkin_ATT2", "MI_ScctWetSkin_DEF"]

editor_assets = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
materials_lib = unreal.MaterialEditingLibrary


def log(text):
    unreal.log_warning("SCCT-SKINS " + text)


def remove_old_textures():
    """An older build imported the original's textures here; they must not stay in the project."""
    if editor_assets.does_directory_exist(TEXTURES):
        editor_assets.delete_directory(TEXTURES)
        log("deleted " + TEXTURES)

def in_the_picture(g, light):
    """`light` as a brightness in the finished picture: times PictureScale, which the game sets each frame to the
    inverse of the exposure it reads (FScctExposure, the scale the effect lights use). Graph.in_the_picture bounds
    its correction at 0.08, made for glows that must stay readable; in AquaE's dark rooms the eye opens up past that,
    and a faint fill of 0.055 painted the merc's whole body solid amber. The game bounds this one far lower."""
    return g.custom("return L * S;", FLOAT3, L=light, S=g.scalar("PictureScale", 1.0))


def overlay_material(path, blend):
    """A new unlit translucent material for a skinned body's overlay draw."""
    g = Graph(path)
    m = g.material
    m.set_editor_property("blend_mode", blend)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("used_with_skeletal_mesh", True)
    return g


def build_sticky():
    g = overlay_material(PACKAGE + "/M_ScctStickySkin", unreal.BlendMode.BLEND_TRANSLUCENT)
    # TexPanner PanDirection yaw 16384 (along V), PanRate 0.5: half the texture per second.
    panner = g.node(unreal.MaterialExpressionPanner, speed_x=0.0, speed_y=0.5)
    sticky = g.texture("Sticky", NEUTRAL, uvs=panner)
    glow = g.custom("return T.rgb * Glow;", FLOAT3, T=(sticky, "RGBA"), Glow=g.scalar("Glow", 1.0))
    g.output(g.in_the_picture(glow), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    alpha = g.custom("return saturate(A * Amount);", FLOAT1, A=(sticky, "A"), Amount=g.scalar("Amount", 1.0))
    g.output(alpha, unreal.MaterialProperty.MP_OPACITY)
    g.finish()
    log("built M_ScctStickySkin")


# The Godot client's svm_merc_shock_immunity fragment: the upper boundary of the lit part moves down as the protection
# runs out (half left lights the bottom half of the body as posed now); a faint fill and a strong fresnel rim. The
# facing comes from the triangle itself (the screen-space slopes of its world position), taken either way round:
# the vertex normal reads as nothing in this unlit overlay draw of a skinned body, which lit every pixel as rim.
# Fill, Rim and Strength are the levels (the game's -ScctSkinEdge holds others to try): Godot's 0.055 fill and 0.42 rim,
# drawn at full strength here, painted a merc in a dark corridor solid amber (the local exposure lifts the shadows the
# overlay sits in), so the fill is lower and the whole is a quarter: an amber outline in the dark, a warm rim in the light.
EDGE_CODE = """
float H = (W.z - FeetZ) / max(HeadZ - FeetZ, 5.0);
float Edge = Coverage >= 0.999 ? 1.0 : 1.0 - smoothstep(Coverage - 0.012, Coverage + 0.012, H);
float3 Nf = cross(ddy(W), ddx(W));
Nf = dot(Nf, Nf) > 1e-12 ? normalize(Nf) : normalize(V);
float Fresnel = pow(1.0 - saturate(abs(dot(Nf, normalize(V)))), 2.2);
return Cue.rgb * (Edge * Pulse * Opacity * (Fill + Fresnel * Rim) * Strength);
"""


def build_edge():
    g = overlay_material(PACKAGE + "/M_ScctShockImmunityEdge", unreal.BlendMode.BLEND_ADDITIVE)
    # Amber (1, 0.72, 0.27) as painted, in linear terms.
    cue = g.colour("CueColor", (1.0, 0.48, 0.06, 1.0))
    edge = g.custom(EDGE_CODE, FLOAT3,
                    W=g.node(unreal.MaterialExpressionWorldPosition),
                    V=g.node(unreal.MaterialExpressionCameraVectorWS),
                    Cue=cue,
                    Coverage=g.scalar("Coverage", 1.0),
                    FeetZ=g.scalar("FeetZ", 0.0),
                    HeadZ=g.scalar("HeadZ", 180.0),
                    Pulse=g.scalar("Pulse", 1.0),
                    Opacity=g.scalar("Opacity", 1.0),
                    Fill=g.scalar("Fill", 0.03),
                    Rim=g.scalar("Rim", 0.42),
                    Strength=g.scalar("Strength", 0.25))
    g.output(in_the_picture(g, edge), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    g.finish()
    log("built M_ScctShockImmunityEdge")


def build_wet():
    path = PACKAGE + "/M_ScctWetSkin"
    g = overlay_material(path, unreal.BlendMode.BLEND_ADDITIVE)
    # The camera-space reflection texture coordinates of a UE2 TexEnvMap: the reflected view ray in view space, its
    # x and y mapped from -1..1 onto the texture.
    reflection = g.node(unreal.MaterialExpressionReflectionVectorWS)
    view_space = g.node(unreal.MaterialExpressionTransform,
                        transform_source_type=unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD,
                        transform_type=unreal.MaterialVectorCoordTransform.TRANSFORM_VIEW)
    g.link(reflection, view_space, "Input")
    env_uv = g.custom("float3 D = normalize(R); return float2(D.x, -D.y) * 0.5 + 0.5;", FLOAT2, R=view_space)
    env = g.texture("EnvMap", NEUTRAL, uvs=env_uv)
    mask = g.texture("SpecMask", NEUTRAL)
    # Strength: half of the original's sum, for the same reason as the edge's quarter (-ScctSkinWet tries others).
    sheen = g.custom("return E.rgb * EnvColor.rgb * M * Strength;", FLOAT3,
                     E=(env, "RGBA"), EnvColor=g.colour("EnvColor", (0.3135, 0.3087, 0.3135, 1.0)),
                     M=(mask, "A"), Strength=g.scalar("Strength", 0.5))
    g.output(in_the_picture(g, sheen), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    material = g.finish()
    log("built M_ScctWetSkin")

    for name in WET_INSTANCES:
        instance_path = "%s/%s" % (PACKAGE, name)
        if editor_assets.does_asset_exist(instance_path):
            editor_assets.delete_asset(instance_path)
        instance = asset_tools.create_asset(name, PACKAGE, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        materials_lib.set_material_instance_parent(instance, material)
        materials_lib.update_material_instance(instance)
        editor_assets.save_loaded_asset(instance, only_if_is_dirty=False)
        log("built " + name)


def main():
    remove_old_textures()
    build_sticky()
    build_edge()
    build_wet()
    log("done: 3 materials, %d instances, no textures" % len(WET_INSTANCES))


main()
