"""Apply the staged preview to the physical MounE level in one editor commandlet."""

from pathlib import Path
import runpy
import sys


TOOLS = Path(__file__).resolve().parent


def apply_script(name, *arguments):
    path = TOOLS / name
    original_args = sys.argv
    try:
        sys.argv = [str(path), *(str(value) for value in arguments)]
        runpy.run_path(str(path), run_name="__main__")
    finally:
        sys.argv = original_args


def main():
    stage, stock_textures = (Path(value) for value in sys.argv[1:])
    apply_script("apply_hospital_surface_art.py", stage / "Colours/base-color-plan.json",
                 stage / "Colours/base-color-rollback.json",
                 stage / "Textures/OriginalRestoration/GlowMasks/self-illum-masks.json",
                 stage / "Textures/Specialists/OriginalSewer/Prison_TEX_plaquegout_N.png")
    apply_script("apply_hospital_box_detail.py")
    apply_script("apply_hospital_effect_textures.py", "--textures", stock_textures)
    apply_script("apply_hospital_lightmaps.py", "--manifest", stage / "Lightmaps/lightmap-manifest.json")
    apply_script("apply_hospital_shared_colors.py", TOOLS / "hospital-shared-colours.json",
                 stage / "SharedColours/manifest.json", TOOLS / "hospital-active-skins.json",
                 stage / "ActiveSkins/manifest.json")
    shaft = stage / "SideStairs/SideStairsShaft.json"
    apply_script("apply_hospital_side_stairs_beam.py", shaft)
    apply_script("apply_hospital_side_stairs_floor.py", shaft)


if __name__ == "__main__":
    main()
