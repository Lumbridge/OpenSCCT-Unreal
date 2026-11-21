"""Build the single refractive coating on the merc's first-person M4 micro red dot."""
from pathlib import Path
import sys

import unreal

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Import"))
from scct_materials import Graph, FLOAT3
from scct_sight_glass import coat_sight_glass


def build():
    g = Graph("/Game/OpenSCCT/Materials/M_ScctMercSightLens", rebuild_in_place=True)
    uv = g.node(unreal.MaterialExpressionTextureCoordinate)
    coords = g.custom("return float3(UV * 2 - 1, 1);", FLOAT3, UV=uv)
    coat_sight_glass(g, coords, translucency_pass=unreal.MaterialTranslucencyPass.MTP_BEFORE_DOF,
                     magnification=1.07)
    unreal.log_warning("SCCT-IMPORT M4 forward optical coating saved")


build()
