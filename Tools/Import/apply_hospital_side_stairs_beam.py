"""Apply the staged Side Stairs skylight shaft to an independently imported MounE level."""

import json
from pathlib import Path
import sys

import unreal

from scct_materials import FLOAT2, FLOAT3, Graph


MAP = "/Game/Imported/Maps/MounE"
DEST = MAP + "/Effects/SideStairsShaft"
MATERIAL = DEST + "/M_MounE_SideStairsShaft"
ACTOR_TAG = "ScctVisibleMapShaft"
ASSETS = unreal.EditorAssetLibrary
EDIT = unreal.MaterialEditingLibrary


class ShaftGraph(Graph):
    def __init__(self):
        existing = unreal.load_asset(MATERIAL) if ASSETS.does_asset_exist(MATERIAL) else None
        if existing is None:
            super().__init__(MATERIAL)
        else:
            self.path = MATERIAL
            self.material = existing
            self.count = 0
            EDIT.delete_all_material_expressions(existing)


def import_mask(source, depth=False):
    asset_path = f"{DEST}/{source.stem}"
    if not depth and ASSETS.does_asset_exist(asset_path):
        existing = unreal.load_asset(asset_path)
        if isinstance(existing, unreal.Texture2D) and not existing.get_editor_property("srgb"):
            return existing.get_path_name()
    task = unreal.AssetImportTask()
    task.filename = str(source)
    task.destination_path = DEST
    task.automated = True
    task.save = False
    task.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    transmission = unreal.load_asset(asset_path)
    if not isinstance(transmission, unreal.Texture2D):
        raise RuntimeError(f"The shaft mask did not import: {source.name}")
    transmission.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
    transmission.set_editor_property("srgb", False)
    transmission.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
    transmission.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
    transmission.set_editor_property("never_stream", True)
    if depth:
        # Do not blend a near obstruction with an adjacent open ray at a depth edge.
        transmission.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
        transmission.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    if not ASSETS.save_loaded_asset(transmission, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save shaft mask {source.name}")
    return transmission.get_path_name()


def build_material(data, transmission, cast_depth):
    graph = ShaftGraph()
    graph.material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    graph.material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    graph.material.set_editor_property("two_sided", True)
    position = graph.node(unreal.MaterialExpressionWorldPosition)
    apex = graph.colour("Apex", (*data["apex"], 1.0))
    centre = graph.colour("GrateCentre", (*data["grate_centre"], 1.0))
    uv = graph.custom("""
        float t = (Centre.z - Apex.z) / min(W.z - Apex.z, -1.0);
        float2 plane = Apex.xy + (W.xy - Apex.xy) * t - Centre.xy;
        return float2(dot(float3(plane, 1.0), U), dot(float3(plane, 1.0), V));
        """, FLOAT2, W=position, Apex=apex, Centre=centre,
        U=graph.colour("GrateU", (*data["grate_u"], 1.0)), V=graph.colour("GrateV", (*data["grate_v"], 1.0)))
    mask = graph.texture("GrateTransmission", transmission, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, uvs=uv)
    cast = graph.texture("CastDepth", cast_depth, unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE, uvs=uv)
    soft = graph.node(unreal.MaterialExpressionDepthFade, fade_distance_default=12.0, opacity_default=1.0)
    emission = graph.custom("""
        float inside = step(0.0, min(UV.x, UV.y)) * step(max(UV.x, UV.y), 1.0);
        float along = saturate(dot(W - Origin, Forward) / Length);
        float depth = Grate.z - W.z;
        float floorward = smoothstep(0.0, 10.0, depth);
        float visible = smoothstep(0.0, 10.0, Cast * CastRange - depth);
        float hall = smoothstep(4016.0, 4028.0, W.x) * (1.0 - smoothstep(4628.0, 4640.0, W.x));
        float side = lerp(0.18, 1.0, smoothstep(0.04, 0.40, abs(dot(Normal, View))));
        float remaining = 1.0 - along;
        float tail = smoothstep(0.08, 0.58, remaining);
        float transmission = inside * Mask * floorward * visible * hall * Soft * side;
        return max(Colour * Strength * remaining, 0.0) * transmission * Gain * tail;
        """, FLOAT3, W=position, Origin=graph.colour("Origin", (*data["origin"], 1.0)),
        Forward=graph.colour("Forward", (*data["forward"], 1.0)), Length=graph.scalar("Length", data["length"]),
        UV=uv, Mask=(mask, "R"), Cast=(cast, "R"), CastRange=graph.scalar("CastRange", data["cast_depth_range"]),
        Grate=centre, Soft=soft, Strength=graph.scalar("Strength", data["strength"]),
        Gain=graph.scalar("DisplayGain", 0.20),
        Colour=graph.colour("BeamColour", (*data["colour"], 1.0)),
        Normal=graph.node(unreal.MaterialExpressionPixelNormalWS), View=graph.node(unreal.MaterialExpressionCameraVectorWS))
    # The original adds each slice to the image. Use the same bounded display conversion as other map beam cards.
    graph.output(graph.in_the_picture(emission), unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.recompile_material(graph.material)
    if not ASSETS.save_loaded_asset(graph.material, only_if_is_dirty=False):
        raise RuntimeError("Could not save the Side Stairs shaft material")
    return graph.material


def import_mesh(path, data):
    task = unreal.AssetImportTask()
    task.filename = str(path)
    task.destination_path = DEST
    task.automated = True
    task.save = True
    task.replace_existing = True
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    meshes = [unreal.load_asset(asset) for asset in task.imported_object_paths]
    meshes = [asset for asset in meshes if isinstance(asset, unreal.StaticMesh)]
    if len(meshes) != 1:
        raise RuntimeError(f"Expected one imported shaft mesh, got {len(meshes)}")
    mesh = meshes[0]
    bounds = mesh.get_bounds()
    for axis, low, high in zip("xyz", *data["bounds"], strict=True):
        centre, extent = getattr(bounds.origin, axis), getattr(bounds.box_extent, axis)
        if abs(centre - extent - low) > 2 or abs(centre + extent - high) > 2:
            raise RuntimeError(f"The shaft's glTF import changed its expected {axis} bounds")
    return mesh


def apply(manifest, material_only=False):
    data = json.loads(manifest.read_text(encoding="utf-8"))
    if data["map"] != "MounE" or data["light"] != "Light3517" or Path(data["mesh"]).name != data["mesh"]:
        raise ValueError("Expected the validated MounE Light3517 beam manifest")
    transmission_name = Path(data["transmission"])
    cast_name = Path(data["cast_depth"])
    if (transmission_name != Path("T_MounE_SideStairsTransmission.png")
            or cast_name != Path("T_MounE_SideStairsCastDepth.png")):
        raise ValueError("Unexpected Side Stairs mask paths")
    if material_only:
        if not ASSETS.does_asset_exist(MATERIAL):
            raise RuntimeError("Apply the beam to a candidate map before doing a material-only update")
        transmission = import_mask(manifest.parent / transmission_name)
        depth = import_mask(manifest.parent / cast_name, depth=True)
        build_material(data, transmission, depth)
        unreal.log_warning("SCCT-IMPORT Updated the Side Stairs shaft with the authored geometry's cast depth")
        return

    world = unreal.EditorLoadingAndSavingUtils.load_map(MAP + "/MounE")
    if not isinstance(world, unreal.World):
        raise RuntimeError("Import Enhanced Mount Hospital into the candidate project first")
    world_actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    lights = [actor for actor in world_actors if "Scct:" + data["light"] in map(str, actor.tags)]
    if len(lights) != 1 or not isinstance(lights[0], unreal.SpotLight):
        raise RuntimeError(f"Expected the imported Light3517 spotlight; got {len(lights)} matches in {len(world_actors)} actors")
    tagged = [actor for actor in world_actors if ACTOR_TAG in map(str, actor.tags)]
    if len(tagged) > 1 or (tagged and not isinstance(tagged[0], unreal.StaticMeshActor)):
        raise RuntimeError("Unexpected shaft actors in the candidate map")

    transmission = import_mask(manifest.parent / transmission_name)
    depth = import_mask(manifest.parent / cast_name, depth=True)
    material = build_material(data, transmission, depth)
    mesh = import_mesh(manifest.parent / data["mesh"], data)
    nanite = mesh.get_editor_property("nanite_settings")
    if nanite.enabled:
        nanite.enabled = False
        mesh.set_editor_property("nanite_settings", nanite)
        if not ASSETS.save_loaded_asset(mesh, only_if_is_dirty=False):
            raise RuntimeError("Could not save the ordinary translucent shaft mesh")
    origin, rotation = unreal.Vector(*data["origin"]), unreal.Rotator()
    if tagged:
        actor = tagged[0]
    else:
        transform = unreal.Transform(origin, rotation, unreal.Vector(1, 1, 1))
        actor = unreal.ScctImportLibrary.spawn_actor_in_world(world, unreal.StaticMeshActor, transform)
    actor.set_actor_location(origin, False, False)
    actor.set_actor_label("Side Stairs skylight shaft")
    actor.set_folder_path("Effects/Skylight")
    actor.tags = [unreal.Name(ACTOR_TAG)]
    component = actor.static_mesh_component
    component.set_static_mesh(mesh)
    component.set_material(0, material)
    component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    component.set_cast_shadow(False)
    # The slices are the visible shaft. Keep the authored projector's floor/wall light independently.
    lights[0].light_component.set_editor_property("volumetric_scattering_intensity", 0.0)
    if not ASSETS.save_loaded_asset(world, only_if_is_dirty=False):
        raise RuntimeError("Could not save the candidate Mount Hospital map")
    unreal.log_warning("SCCT-IMPORT Applied the authored Side Stairs beam slices and grille aperture")


if __name__ == "__main__":
    manifest = Path(sys.argv[1])
    material_only = "--material-only" in sys.argv[2:]
    apply(manifest, material_only)
    if "--include-floor" in sys.argv[2:]:
        import apply_hospital_side_stairs_floor
        apply_hospital_side_stairs_floor.apply(manifest, material_only)
