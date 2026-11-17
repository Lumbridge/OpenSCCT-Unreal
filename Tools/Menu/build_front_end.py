# Builds the front end level, /Game/OpenSCCT/Maps/L_FrontEnd, from nothing (run by Build-FrontEnd.ps1 in a
# headless editor). The level holds only what the menu needs when no imported map can be shown behind it: a dark
# floor, a few pillars, two coloured lights and fog. AScctFrontEndGameMode streams an imported map in as the real
# backdrop and then removes everything here that carries the tag ScctFallback.
import unreal

DEST = "/Game/OpenSCCT/Maps"
NAME = "L_FrontEnd"
TAG = unreal.Name("ScctFallback")


def log(message):
    unreal.log("SCCT-FRONTEND " + message)


def try_set(target, name, value):
    try:
        target.set_editor_property(name, value)
    except Exception as error:  # a property renamed between engine versions must not stop the build
        log("skipped %s: %s" % (name, error))


def main():
    editor_assets = unreal.EditorAssetLibrary
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    level_path = DEST + "/" + NAME
    # The editor's level tools are not available without its window, so the world is made as an asset and
    # actors are spawned straight into it (as Tools/Import/import_map.py does).
    if editor_assets.does_asset_exist(level_path):
        editor_assets.delete_asset(level_path)
    world = asset_tools.create_asset(NAME, DEST, unreal.World, unreal.WorldFactory())

    def spawn(cls, location, rotation=unreal.Rotator(0, 0, 0), label=None):
        transform = unreal.Transform(location, rotation, unreal.Vector(1, 1, 1))
        actor = unreal.ScctImportLibrary.spawn_actor_in_world(world, cls, transform)
        actor.tags = [TAG]
        if label:
            actor.set_actor_label(label)
        return actor

    def mesh(path, label, location, scale):
        actor = spawn(unreal.StaticMeshActor, location, label=label)
        actor.static_mesh_component.set_static_mesh(unreal.load_asset(path))
        actor.set_actor_scale3d(scale)
        actor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        return actor

    # The camera rests at (-620, 0, 190) and looks along +X (AScctFrontEndGameMode's defaults).
    mesh("/Engine/BasicShapes/Plane", "Floor", unreal.Vector(1500, 0, 0), unreal.Vector(80, 80, 1))
    mesh("/Engine/BasicShapes/Cube", "BackWall", unreal.Vector(3200, 0, 600), unreal.Vector(1, 60, 12))
    for index, (x, y) in enumerate([(500, -520), (500, 520), (1300, -700), (1300, 700), (2200, -420), (2200, 420)]):
        mesh("/Engine/BasicShapes/Cube", "Pillar%d" % index, unreal.Vector(x, y, 400), unreal.Vector(0.9, 0.9, 8))

    def light(label, location, rotation, colour, candelas, cone):
        actor = spawn(unreal.SpotLight, location, rotation, label)
        component = actor.light_component
        component.set_mobility(unreal.ComponentMobility.MOVABLE)
        component.set_editor_property("intensity_units", unreal.LightUnits.CANDELAS)
        component.set_intensity(candelas)
        component.set_light_color(unreal.LinearColor(colour[0], colour[1], colour[2], 1.0))
        component.set_attenuation_radius(4200.0)
        component.set_outer_cone_angle(cone)
        component.set_inner_cone_angle(cone * 0.55)
        try_set(component, "volumetric_scattering_intensity", 3.0)
        return actor

    # The two sides' colours: the spies' cool light from the left, the mercenaries' warm light from the right.
    light("SpyLight", unreal.Vector(900, -900, 760), unreal.Rotator(0, -52, 40), (0.36, 0.84, 1.0), 2600.0, 38.0)
    light("MercLight", unreal.Vector(1900, 900, 760), unreal.Rotator(0, -50, -130), (1.0, 0.66, 0.25), 2200.0, 34.0)
    light("BackLight", unreal.Vector(2900, 0, 900), unreal.Rotator(0, -70, 180), (0.7, 0.8, 1.0), 900.0, 50.0)

    fog = spawn(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0), label="Fog")
    try_set(fog.component, "fog_density", 0.035)
    try_set(fog.component, "fog_height_falloff", 0.05)
    try_set(fog.component, "enable_volumetric_fog", True)
    try_set(fog.component, "volumetric_fog_scattering_distribution", 0.6)
    try_set(fog.component, "volumetric_fog_extinction_scale", 2.0)

    post = spawn(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), label="Look")
    post.set_editor_property("unbound", True)
    settings = post.get_editor_property("settings")
    for name, value in [
        ("auto_exposure_min_brightness", -1.0),
        ("auto_exposure_max_brightness", 1.5),
        ("bloom_intensity", 1.0),
        ("vignette_intensity", 0.55),
        ("film_grain_intensity", 0.12),
    ]:
        try_set(settings, "override_" + name, True)
        try_set(settings, name, value)
    post.set_editor_property("settings", settings)

    world.get_world_settings().set_editor_property("default_game_mode", unreal.load_class(None, "/Script/OpenSCCT.ScctFrontEndGameMode"))

    editor_assets.save_loaded_asset(world)
    log("saved " + level_path)


main()
