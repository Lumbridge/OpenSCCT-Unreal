"""Generate the neutral cosmetic master and UV mask depth material. All suit images are supplied at runtime."""
import os
import sys
import unreal

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "Import"))
from scct_materials import build_surface, Graph, FLOAT1

build_surface(cosmetic=True)
g = Graph("/Game/OpenSCCT/Materials/M_ScctVisionBand")
g.material.set_editor_property("used_with_skeletal_mesh", True)
g.material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
g.material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
g.material.set_editor_property("two_sided", True)
# Packed R is the converted cool-form heat texel, G the non-inverted EMF texel.
# The fallback for an unpainted slot is the pawn's flat intensity in either mode.
mask = g.texture("VisionMask", "/Engine/EngineResources/WhiteSquareTexture")
band = g.custom("""float m = HasMask > .5 ? (Heat > .5 ? Mask.r*4 : Mask.g) : 1;
    return abs(round(saturate(m*Intensity)*Steps)-Band)<.5 ? 1.0 : 0.0;""",
    FLOAT1, Mask=(mask,"RGB"), HasMask=g.scalar("HasMask",0), Heat=g.scalar("Heat",1),
    Intensity=g.scalar("Intensity",1), Steps=g.scalar("Steps",7), Band=g.scalar("Band",0))
g.output(band, unreal.MaterialProperty.MP_OPACITY_MASK)
g.finish()
