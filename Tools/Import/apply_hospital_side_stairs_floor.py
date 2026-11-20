"""Restore Light3517's floor projection from the authored Side Stairs aperture."""

import json
from pathlib import Path
import sys

import unreal

from scct_materials import FLOAT1, FLOAT2, FLOAT3, Graph

MAP = "/Game/Imported/Maps/MounE"
DEST = MAP + "/Effects/SideStairsFloor"
TRANSMISSION = MAP + "/Effects/SideStairsShaft/T_MounE_SideStairsTransmission"
LIGHT_TAG = "ScctSideStairsProjector"
DECAL_TAG = "ScctSideStairsFloor"
FLOOR_Z = -320.0
# Bounds of the reference patch in the uncovered hall. World geometry clips its outer two edges.
FLOOR_X = (4016.0, 4640.0)
FLOOR_Y = (-4834.0, -4112.0)
ASSETS = unreal.EditorAssetLibrary
EDIT = unreal.MaterialEditingLibrary


class ProjectionGraph(Graph):
    def __init__(self, name):
        path = f"{DEST}/{name}"
        existing = unreal.load_asset(path) if ASSETS.does_asset_exist(path) else None
        if existing is None:
            super().__init__(path)
        else:
            self.path = path
            self.material = existing
            self.count = 0
            EDIT.delete_all_material_expressions(existing)


def footprint(graph, data, texture):
    world = graph.node(unreal.MaterialExpressionWorldPosition)
    apex = graph.colour("Apex", (*data["apex"], 1.0))
    centre = graph.colour("GrateCentre", (*data["grate_centre"], 1.0))
    uv = graph.custom("""
        float t = (Centre.z - Apex.z) / min(W.z - Apex.z, -1.0);
        float2 plane = Apex.xy + (W.xy - Apex.xy) * t - Centre.xy;
        return float2(dot(float3(plane, 1.0), U), dot(float3(plane, 1.0), V));
        """, FLOAT2, W=world, Apex=apex, Centre=centre,
        U=graph.colour("GrateU", (*data["grate_u"], 1.0)),
        V=graph.colour("GrateV", (*data["grate_v"], 1.0)))
    mask = graph.texture("GrateTransmission", texture, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, uvs=uv)
    shape = graph.custom("""
        float2 edge = min(UV, 1.0 - UV);
        float aperture = smoothstep(0.0, 0.014, min(edge.x, edge.y)) * Mask;
        float floorDistance = abs(W.z - Floor);
        float receiver = 1.0 - smoothstep(5.0, 24.0, floorDistance);
        float2 toEdge = min(W.xy - BoundsMin.xy, BoundsMax.xy - W.xy);
        float boundary = saturate(min(toEdge.x, toEdge.y) / 14.0);
        return aperture * receiver * boundary;
        """, FLOAT1, W=world, UV=uv, Mask=(mask, "R"), Floor=graph.scalar("Floor", FLOOR_Z),
        BoundsMin=graph.colour("FloorBoundsMin", (FLOOR_X[0], FLOOR_Y[0], 0.0, 0.0)),
        BoundsMax=graph.colour("FloorBoundsMax", (FLOOR_X[1], FLOOR_Y[1], 0.0, 0.0)))
    return world, apex, shape


def build_spot(data, texture):
    graph = ProjectionGraph("M_MounE_SideStairsFloorLight")
    graph.material.set_editor_property("material_domain", unreal.MaterialDomain.MD_LIGHT_FUNCTION)
    # The world-position receiver gate requires the deferred light-function pass.
    graph.material.set_editor_property("force_compatible_with_light_function_atlas", False)
    _, _, shape = footprint(graph, data, texture)
    graph.output(shape, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return graph.finish()


def build_decal(data, texture):
    graph = ProjectionGraph("M_MounE_SideStairsFloorDecal")
    graph.material.set_editor_property("material_domain", unreal.MaterialDomain.MD_DEFERRED_DECAL)
    graph.material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    world, apex, shape = footprint(graph, data, texture)
    # Match the reachable pool of the native spot. The emissive-only decal leaves material channels alone.
    coverage = graph.custom("""
        float radius = length(W - Apex);
        return Shape * pow(saturate(1.0 - (radius * radius) / (Radius * Radius)), 0.65);
        """, FLOAT1, Shape=shape, W=world, Apex=apex, Radius=graph.scalar("Radius", data["projector_range"]))
    graph.output(coverage, unreal.MaterialProperty.MP_OPACITY)
    colour = graph.custom("return Tint * Brightness;", FLOAT3,
                         Tint=graph.colour("Tint", (0.72, 0.76, 1.0, 1.0)),
                         Brightness=graph.scalar("Brightness", 0.075))
    graph.output(graph.in_the_picture(colour), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return graph.finish()


def apply(manifest, material_only=False):
    data = json.loads(manifest.read_text(encoding="utf-8"))
    if data["map"] != "MounE" or data["light"] != "Light3517" or data["grate_centre"][2] < 250:
        raise ValueError("Expected authored Side Stairs fixture data")
    # The source beam reaches 80% of the source projector. Restore the full projector reach for the floor.
    origin_z, apex_z = data["origin"][2], data["apex"][2]
    data["projector_range"] = data["length"] / 0.8 + apex_z - origin_z
    if not 823 < data["projector_range"] < 825:
        raise ValueError("Unexpected projector reach in Side Stairs fixture data")
    texture = unreal.load_asset(TRANSMISSION)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"Missing authored grille transmission asset {TRANSMISSION}")
    texture_path = texture.get_path_name()
    spot_material = build_spot(data, texture_path)
    decal_material = build_decal(data, texture_path)
    if material_only:
        unreal.log_warning("SCCT-IMPORT Updated the Side Stairs floor projection materials")
        return
    world = unreal.EditorLoadingAndSavingUtils.load_map(f"{MAP}/MounE")
    if not isinstance(world, unreal.World):
        raise RuntimeError("Import Enhanced Mount Hospital before applying the floor projection")
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    lights = [actor for actor in actors if "Scct:Light3517" in map(str, actor.tags)]
    decals = [actor for actor in actors if DECAL_TAG in map(str, actor.tags)]
    if len(lights) != 1 or not isinstance(lights[0], unreal.SpotLight):
        raise RuntimeError("Expected one imported Light3517 spotlight")
    if len(decals) > 1 or (decals and not isinstance(decals[0], unreal.DecalActor)):
        raise RuntimeError("Expected at most one Side Stairs floor decal")
    light = lights[0]
    light.light_component.set_editor_property("light_function_material", spot_material)
    light.tags = [tag for tag in light.tags if str(tag) != LIGHT_TAG] + [unreal.Name(LIGHT_TAG)]
    location = unreal.Vector(sum(FLOOR_X) * 0.5, sum(FLOOR_Y) * 0.5, FLOOR_Z)
    rotation = unreal.Rotator(pitch=-90.0)
    decal = decals[0] if decals else unreal.ScctImportLibrary.spawn_actor_in_world(
        world, unreal.DecalActor, unreal.Transform(location, rotation, unreal.Vector(1, 1, 1)))
    decal.set_actor_location(location, False, False)
    decal.set_actor_rotation(rotation, False)
    decal.set_actor_label("Side Stairs floor projection (Low)")
    decal.set_folder_path("Effects/Skylight")
    decal.tags = [unreal.Name(DECAL_TAG)]
    component = decal.get_editor_property("decal")
    component.set_editor_property("decal_size", unreal.Vector(26, (FLOOR_X[1] - FLOOR_X[0]) * 0.5, (FLOOR_Y[1] - FLOOR_Y[0]) * 0.5))
    component.set_editor_property("fade_screen_size", 0.0)
    component.set_decal_material(decal_material)
    component.set_visibility(False)
    if not ASSETS.save_loaded_asset(world, only_if_is_dirty=False):
        raise RuntimeError("Could not save the candidate Mount Hospital map")
    unreal.log_warning("SCCT-IMPORT Applied the authored Side Stairs floor projection")


if __name__ == "__main__":
    apply(Path(sys.argv[1]), "--material-only" in sys.argv[2:])
