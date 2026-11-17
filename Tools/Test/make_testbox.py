"""Makes the TestBox fixture level, /Game/OpenSCCT/Test/TestBox, from OpenSCCT's own fixture (fixtures/testbox in the
OpenSCCT checkout): its render world (one mesh of the box's walls, floor and rooms), the sliding door placed where the
level's Mover0 is (tagged and movable like an imported map's movers), a sun, a sky light and the game mode. TestBox is
synthetic (svmbuild.testbox): nothing of the original game is in it, so the level is kept in Git with the project's
own content. Its simulation files are what the bridge ships in Binaries/Bridge/maps/TestBox.

Run inside the editor's Python through Tools/Import/run_script.py (Tools/Test/Make-TestBox.ps1 does that):

    run_script.py <full path>/make_testbox.py <fixture folder>
"""
import json
import os
import re
import struct
import sys

import unreal

FIXTURE = sys.argv[1]
RENDER = os.path.join(FIXTURE, "render")
DEST = "/Game/OpenSCCT/Test"
GEOMETRY = DEST + "/TestBoxGeometry"
LEVEL = "TestBox"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary


def log(text):
    unreal.log_warning("SCCT-IMPORT " + text)


def key(name):
    """A name reduced to letters and digits, as the importer's asset names are matched to the glTF's (import_map.py)."""
    return re.sub(r"[^a-z0-9]", "", name.lower())


def read_glb_json(path):
    with open(path, "rb") as f:
        f.read(12)
        length, _ = struct.unpack("<I4s", f.read(8))
        return json.loads(f.read(length))


def main():
    glb_path = os.path.join(RENDER, "world.glb")
    gltf = read_glb_json(glb_path)
    gameplay = json.load(open(os.path.join(FIXTURE, "sim", "gameplay.json")))

    if editor_assets.does_directory_exist(GEOMETRY):
        editor_assets.delete_directory(GEOMETRY)
    task = unreal.AssetImportTask()
    task.filename = glb_path
    task.destination_path = GEOMETRY
    task.automated = True
    task.save = True
    task.replace_existing = True
    asset_tools.import_asset_tasks([task])

    meshes = {}
    for path in editor_assets.list_assets(GEOMETRY, recursive=True):
        mesh = unreal.load_asset(path)
        if isinstance(mesh, unreal.StaticMesh):
            meshes[key(str(mesh.get_name()))] = mesh
    log("meshes: %s" % sorted(meshes))

    level_path = DEST + "/" + LEVEL
    if editor_assets.does_asset_exist(level_path):
        editor_assets.delete_asset(level_path)
    world = asset_tools.create_asset(LEVEL, DEST, unreal.World, unreal.WorldFactory())

    def spawn(cls, location, rotation):
        actor = unreal.ScctImportLibrary.spawn_actor_in_world(world, cls, unreal.Transform(location, rotation, unreal.Vector(1, 1, 1)))
        actor.set_actor_rotation(rotation, False)
        actor.set_actor_scale3d(unreal.Vector(1, 1, 1))
        return actor

    def place(mesh, label, location, rotation, movable):
        actor = spawn(unreal.StaticMeshActor, location, rotation)
        actor.static_mesh_component.set_static_mesh(mesh)
        actor.set_actor_label(label)
        actor.tags = [unreal.Name("Scct:" + label)]
        component = actor.static_mesh_component
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)  # the simulation owns collision
        if movable:
            component.set_mobility(unreal.ComponentMobility.MOVABLE)
        return actor

    # The world mesh: the glTF importer puts it in Unreal's space, where the simulation's collision is.
    placed = 0
    for node in gltf["nodes"]:
        if node.get("extras", {}).get("svmap", {}).get("role") == "world":
            mesh = meshes.get(key(node["name"])) or meshes.get(key(gltf["meshes"][node["mesh"]]["name"]))
            if mesh is None:
                raise RuntimeError("the import made no mesh for the world node %s (meshes: %s)" % (node["name"], sorted(meshes)))
            place(mesh, node["name"], unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0), False)
            placed += 1
    if placed == 0:
        raise RuntimeError("the fixture's render world has no world node")

    # Every gameplay actor with a library mesh (the sliding door) where the actor is, as the Godot client's TestBox
    # check expects of the drawn map.
    for actor in gameplay["actors"]:
        props = actor.get("props", {})
        asset = (props.get("StaticMesh") or {}).get("$asset")
        if not asset:
            continue
        mesh = meshes.get(key(asset))
        if mesh is None:
            raise RuntimeError("no mesh for %s of %s" % (asset, actor["id"]))
        loc = props.get("Location", {})
        rot = props.get("Rotation", {})
        rotation = unreal.Rotator(roll=rot.get("Roll", 0) * 360.0 / 65536.0, pitch=rot.get("Pitch", 0) * 360.0 / 65536.0, yaw=rot.get("Yaw", 0) * 360.0 / 65536.0)
        place(mesh, actor["id"], unreal.Vector(loc.get("X", 0.0), loc.get("Y", 0.0), loc.get("Z", 0.0)), rotation,
              movable=actor["class"] == "Engine.Mover")
        placed += 1
    log("placed %d meshes" % placed)

    sun = spawn(unreal.DirectionalLight, unreal.Vector(0, 0, 2000), unreal.Rotator(roll=0, pitch=-50, yaw=35))
    sun.set_actor_label("Sun")
    sun.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sun.light_component.set_intensity(6.0)
    sun.light_component.set_cast_shadows(True)

    sky = spawn(unreal.SkyLight, unreal.Vector(0, 0, 2000), unreal.Rotator(0, 0, 0))
    sky.light_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)
    sky.light_component.set_editor_property("lower_hemisphere_is_black", False)
    sky.light_component.set_intensity(1.5)

    spawn(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))

    world.get_world_settings().set_editor_property("default_game_mode", unreal.load_class(None, "/Script/OpenSCCT.ScctGameMode"))
    editor_assets.save_loaded_asset(world)
    editor_assets.save_directory(DEST, only_if_is_dirty=True, recursive=True)
    log("saved " + level_path)


main()
