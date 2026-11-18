"""Rebuilds the UE-only parents used by imported and runtime-built editor-authored surfaces."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "Import"))
import scct_authored_materials

scct_authored_materials.build()
