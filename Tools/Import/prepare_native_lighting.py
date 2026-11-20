"""Prepare one imported level for the native Lightmass build. Run through Build-MapLighting.ps1."""
import hashlib
import json
import math
from pathlib import Path
import sys
import unreal
import scct_authored_geometry

content, key = sys.argv[1:3]
root = f"/Game/Imported/Maps/{key}"
level = f"{root}/{key}"
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not levels.load_level(level):
    raise RuntimeError(f"Could not load {level}")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
settings = world.get_world_settings()
if "ScctLumenLighting" in map(str, settings.tags):
    unreal.log_warning(f"SCCT-IMPORT Lightmass skipped: {key} uses live Lumen lighting")
    sys.exit(0)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
mesh_editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.get_default_object(unreal.StaticMeshEditorSubsystem)
assets = unreal.EditorAssetLibrary
lights = json.loads((Path(content) / "svmap" / key / "render/lighting.json").read_text())["lights"]
by_name = {row["name"].casefold(): row for row in lights}
def permanent_fixture(row):
    return (row and row.get("apply_static") and row.get("initially_on", True)
            and not any(row.get(flag) for flag in ("special_lit", "hidden", "triggered", "dynamic", "gameplay"))
            and row.get("effect", 0) == 0 and row.get("ltype", 1) == 1)

level_actors = actors.get_all_level_actors()
materials = json.loads((Path(content) / "svmap" / key / "render/materials.json").read_text())
painted = scct_authored_geometry.restore_painted_mesh_roles(level_actors, root, materials.get("authoredColorMeshes", []))
if painted:
    unreal.log_warning(f"SCCT-IMPORT Restored painted vertex colours on {painted} components")
fixtures = []
for actor in level_actors:
    if isinstance(actor, unreal.Light):
        row = by_name.get(actor.get_actor_label().casefold())
        if permanent_fixture(row):
            fixtures.append((actor, row))
if not fixtures:
    # Some source maps contain only live lights or contain no fixture records.
    # Keep the imported lighting intact when there is nothing to reconstruct.
    if painted and not levels.save_current_level():
        raise RuntimeError(f"Could not save painted vertex colours on {key}")
    unreal.log_warning(f"SCCT-IMPORT No permanent fixtures for {key}; retained source lighting")
else:
    settings.set_editor_property("force_no_precomputed_lighting", False)
    lm = settings.get_editor_property("lightmass_settings")
    lm.set_editor_property("volumetric_lightmap_detail_cell_size", 90.0)
    lm.set_editor_property("num_indirect_lighting_bounces", 3)
    settings.set_editor_property("lightmass_settings", lm)
    if not any(isinstance(actor, unreal.LightmassImportanceVolume) for actor in level_actors):
        points = [actor.get_actor_location() for actor, row in fixtures]
        for actor in level_actors:
            if str(actor.get_folder_path()) == "Geometry/World":
                center, extent = actor.get_actor_bounds(False)
                points.extend((center - extent, center + extent))
        lo = unreal.Vector(*[min(getattr(p, axis) for p in points) - 100 for axis in "xyz"])
        hi = unreal.Vector(*[max(getattr(p, axis) for p in points) + 100 for axis in "xyz"])
        volume = actors.spawn_actor_from_class(unreal.LightmassImportanceVolume, (lo + hi) * 0.5)
        _, half = volume.get_actor_bounds(False)
        target = (hi - lo) * 0.5
        if min(half.x, half.y, half.z) <= 0:
            raise RuntimeError("The Lightmass importance volume has no brush")
        volume.set_actor_scale3d(unreal.Vector(target.x / half.x, target.y / half.y, target.z / half.z))
        volume.set_actor_label("Playable lighting volume")
        unreal.log_warning(f"SCCT-IMPORT Lightmass volume {lo} to {hi}")
    copies = {}
    count = 0
    for actor in level_actors:
        folder = str(actor.get_folder_path())
        if isinstance(actor, unreal.PostProcessVolume):
            post = actor.get_editor_property("settings")
            post.set_editor_property("override_dynamic_global_illumination_method", True)
            post.set_editor_property("dynamic_global_illumination_method", unreal.DynamicGlobalIlluminationMethod.NONE)
            post.set_editor_property("override_reflection_method", True)
            post.set_editor_property("reflection_method", unreal.ReflectionMethod.SCREEN_SPACE)
            actor.set_editor_property("settings", post)
        if folder not in ("Geometry/World", "Geometry/Meshes") or not isinstance(actor, unreal.StaticMeshActor):
            continue
        comp = actor.static_mesh_component
        if comp.mobility != unreal.ComponentMobility.STATIC or not comp.static_mesh:
            continue
        source = comp.static_mesh
        path = source.get_path_name()
        if path.startswith(root + "/NativeLighting/"):
            count += 1
            continue
        if path not in copies:
            # A clean reimport can replace the source geometry at the same asset path.
            package = path.split(".")[0].removeprefix("/Game/")
            source_file = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())) / (package + ".uasset")
            digest = hashlib.sha256(path.encode())
            with source_file.open("rb") as data:
                for block in iter(lambda: data.read(1024 * 1024), b""):
                    digest.update(block)
            dest = root + "/NativeLighting/" + source.get_name() + "_" + digest.hexdigest()[:12]
            mesh = unreal.load_asset(dest) if assets.does_asset_exist(dest) else assets.duplicate_asset(path, dest)
            if not mesh:
                raise RuntimeError(f"Cannot prepare lightmap UVs for {path}")
            channels = mesh_editor.get_num_uv_channels(source, 0)
            channel = max(1, channels)
            if channel >= 8:
                raise RuntimeError(f"No free UV channel on {path}")
            build = mesh_editor.get_lod_build_settings(mesh, 0)
            build.set_editor_property("generate_lightmap_u_vs", True)
            build.set_editor_property("src_lightmap_index", 1 if channels > 1 and folder == "Geometry/World" else 0)
            build.set_editor_property("dst_lightmap_index", channel)
            build.set_editor_property("min_lightmap_resolution", 64)
            mesh_editor.set_lod_build_settings(mesh, 0, build)
            mesh.set_editor_property("light_map_coordinate_index", channel)
            bounds = mesh.get_bounding_box()
            extent = bounds.max - bounds.min
            area = max(extent.x*extent.y, extent.x*extent.z, extent.y*extent.z)
            resolution = max(64, min(1024, 2**math.ceil(math.log2(max(1, math.sqrt(area)/6)))))
            mesh.set_editor_property("light_map_resolution", resolution)
            if not assets.save_loaded_asset(mesh, only_if_is_dirty=False):
                raise RuntimeError(f"Could not save {dest}")
            copies[path] = mesh
        comp.set_static_mesh(copies[path])
        count += 1

    if not count:
        raise RuntimeError(f"No bakeable receivers or permanent lights: {count} receivers, {len(fixtures)} lamps")
    # Reserve stationary shadow channels for isolated fixtures. Remaining permanent lamps use static lighting.
    stationary = []
    for actor, row in sorted(fixtures, key=lambda pair: -pair[1]["brightness"]):
        light = actor.light_component
        at = actor.get_actor_location()
        radius = light.get_editor_property("attenuation_radius")
        key_light = all((at-pos).length() > radius+r for pos, r in stationary)
        if key_light:
            stationary.append((at,radius))
        light.set_mobility(unreal.ComponentMobility.STATIONARY if key_light else unreal.ComponentMobility.STATIC)
        light.set_cast_shadows(True)
        light.set_visibility(True)
        if "ScctNativeBakedLamp" not in map(str, actor.tags):
            actor.tags = list(actor.tags) + [unreal.Name("ScctNativeBakedLamp")]
    if "ScctNativeLightmaps" not in map(str, settings.tags):
        settings.tags = list(settings.tags) + [unreal.Name("ScctNativeLightmaps")]
    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {level}")
    unreal.log_warning(f"SCCT-IMPORT Native lighting {key}: {count} receivers, {len(fixtures)} permanent fixtures, {len(stationary)} stationary")
