"""Makes the runtime level, where a converted map the editor never imported is drawn from its render files at run
time (Source/OpenSCCT/Play/ScctRuntimeMapActor.cpp). Runs inside the editor's Python:

    UnrealEditor-Cmd <project> -run=pythonscript -script="run_script.py make_runtime_map.py"

It makes /Game/OpenSCCT/RuntimeMap/L_RuntimeMap, an empty level with what import_map.py gives every imported level
around its geometry (the default sky of map_skies.json on a dome, its sky light, the height fog and the
post-process volume) and the OpenSCCT game mode, and one instance of the surface material per blend mode a map's
materials use (opaque, masked, translucent, additive; lit or unlit; one- or two-sided), with the same overrides
import_map.py sets on each map's own instances. At run time each surface gets a dynamic instance of one of these
with its texture. Nothing here is original game content, so the folder is committed.
"""
import json
import os

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
FOLDER = "/Game/OpenSCCT/RuntimeMap"
LEVEL = "L_RuntimeMap"
SURFACE = "/Game/OpenSCCT/Materials/M_ScctSurface"
SKY = "/Game/OpenSCCT/Materials/M_ScctSky"
THIRD_PARTY = "/Game/OpenSCCT/ThirdParty"

asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
editor_assets = unreal.EditorAssetLibrary
materials_lib = unreal.MaterialEditingLibrary

BLENDS = {
    "Opaque": unreal.BlendMode.BLEND_OPAQUE,
    "Masked": unreal.BlendMode.BLEND_MASKED,
    "Translucent": unreal.BlendMode.BLEND_TRANSLUCENT,
    "Additive": unreal.BlendMode.BLEND_ADDITIVE,
}


def log(text):
    unreal.log_warning("SCCT-IMPORT " + text)


def try_set(target, name, value):
    try:
        target.set_editor_property(name, value)
    except Exception as error:  # pylint: disable=broad-except
        log("property not set: %s (%s)" % (name, str(error).split(":")[0]))


def instance_at(path):
    folder, name = path.rsplit("/", 1)
    if editor_assets.does_asset_exist(path):
        return unreal.load_asset(path)
    return asset_tools.create_asset(name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())


def build_materials():
    surface = unreal.load_asset(SURFACE)
    if not surface:
        raise RuntimeError("the surface material is missing: run Tools/Import/Import-Assets.ps1 first")
    made = 0
    for blend_name, blend in BLENDS.items():
        for unlit in (False, True):
            for two_sided in (False, True):
                name = "MI_ScctRuntime_%s_%s%s" % (blend_name, "Unlit" if unlit else "Lit", "_TwoSided" if two_sided else "")
                instance = instance_at(FOLDER + "/" + name)
                materials_lib.set_material_instance_parent(instance, surface)
                overrides = instance.get_editor_property("base_property_overrides")
                overrides.set_editor_property("override_blend_mode", blend != unreal.BlendMode.BLEND_OPAQUE)
                overrides.set_editor_property("blend_mode", blend)
                overrides.set_editor_property("override_two_sided", two_sided)
                overrides.set_editor_property("two_sided", two_sided)
                overrides.set_editor_property("override_shading_model", unlit)
                overrides.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT if unlit
                                              else unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
                instance.set_editor_property("base_property_overrides", overrides)
                materials_lib.update_material_instance(instance)
                editor_assets.save_loaded_asset(instance)
                made += 1
    log("surface instances: %d" % made)


def build_level():
    level_path = FOLDER + "/" + LEVEL
    if editor_assets.does_asset_exist(level_path):
        editor_assets.delete_asset(level_path)
    world = asset_tools.create_asset(LEVEL, FOLDER, unreal.World, unreal.WorldFactory())

    def spawn(cls, location, rotation, class_turn=False):
        transform = unreal.Transform(location, rotation, unreal.Vector(1, 1, 1))
        actor = unreal.ScctImportLibrary.spawn_actor_in_world(world, cls, transform)
        if not class_turn:
            actor.set_actor_rotation(rotation, False)
            actor.set_actor_scale3d(unreal.Vector(1, 1, 1))
        return actor

    # The atmosphere of import_map.py with the default sky: the runtime level serves every map.
    skies = json.load(open(os.path.join(HERE, "map_skies.json")))
    sky_kind = skies["default"]
    sky_look = skies["kinds"][sky_kind]
    panorama_path = THIRD_PARTY + "/PolyHaven/" + sky_look["panorama"]
    tint = unreal.LinearColor(sky_look["tint"][0], sky_look["tint"][1], sky_look["tint"][2], 1.0)

    sky_material = instance_at(FOLDER + "/MI_ScctRuntimeSky")
    materials_lib.set_material_instance_parent(sky_material, unreal.load_asset(SKY))
    materials_lib.set_material_instance_texture_parameter_value(sky_material, "Panorama", unreal.load_asset(panorama_path))
    materials_lib.set_material_instance_vector_parameter_value(sky_material, "Tint", tint)
    for name, value in (("Brightness", sky_look["brightness"]), ("StarBrightness", sky_look["stars"]), ("Rotation", sky_look["rotation"])):
        materials_lib.set_material_instance_scalar_parameter_value(sky_material, name, float(value))
    materials_lib.update_material_instance(sky_material)
    editor_assets.save_loaded_asset(sky_material)

    dome = spawn(unreal.StaticMeshActor, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    dome.set_actor_label("SkyDome")
    dome.set_actor_scale3d(unreal.Vector(20000, 20000, 20000))
    dome_component = dome.static_mesh_component
    dome_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Sphere"))
    dome_component.set_material(0, sky_material)
    dome_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    dome_component.set_cast_shadow(False)
    for name in ("visible_in_ray_tracing", "affect_dynamic_indirect_lighting", "affect_distance_field_lighting"):
        try_set(dome_component, name, False)

    sky = spawn(unreal.SkyLight, unreal.Vector(0, 0, 3000), unreal.Rotator(0, 0, 0))
    sky_component = sky.light_component
    sky_component.set_mobility(unreal.ComponentMobility.MOVABLE)
    try_set(sky_component, "real_time_capture", False)
    try_set(sky_component, "source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
    try_set(sky_component, "cubemap", unreal.load_asset(panorama_path + "_light"))
    try_set(sky_component, "source_cubemap_angle", float(sky_look["rotation"]))
    sky_component.set_intensity(sky_look["sky_light"])
    sky_component.set_light_color(tint)

    fog = spawn(unreal.ExponentialHeightFog, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    fog_component = fog.component
    fog_look = sky_look.get("fog", {"density": 0.006, "colour": [0.02, 0.03, 0.05]})
    try_set(fog_component, "fog_density", fog_look["density"])
    try_set(fog_component, "fog_height_falloff", 0.02)
    try_set(fog_component, "fog_inscattering_luminance", unreal.LinearColor(fog_look["colour"][0], fog_look["colour"][1], fog_look["colour"][2], 1.0))
    try_set(fog_component, "enable_volumetric_fog", True)
    try_set(fog_component, "volumetric_fog_scattering_distribution", 0.6)
    try_set(fog_component, "volumetric_fog_extinction_scale", 1.2)

    post = spawn(unreal.PostProcessVolume, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
    post.set_editor_property("unbound", True)
    settings = post.get_editor_property("settings")
    for name, value in [
        ("auto_exposure_method", unreal.AutoExposureMethod.AEM_HISTOGRAM),
        ("auto_exposure_min_brightness", -4.0),
        ("auto_exposure_max_brightness", 4.0),
        ("auto_exposure_bias", 0.0),
        ("auto_exposure_speed_up", 5.0),
        ("auto_exposure_speed_down", 2.5),
        ("local_exposure_highlight_contrast_scale", 0.6),
        ("local_exposure_shadow_contrast_scale", 0.6),
        ("bloom_intensity", 0.6),
        ("bloom_threshold", -1.0),
        ("vignette_intensity", 0.45),
        ("film_grain_intensity", 0.12),
        ("scene_fringe_intensity", 0.1),
        ("lens_flare_intensity", 0.4),
        ("ambient_occlusion_intensity", 0.6),
        ("motion_blur_amount", 0.35),
        ("lumen_front_layer_translucency_reflections", True),
    ]:
        try_set(settings, "override_" + name, True)
        try_set(settings, name, value)
    post.set_editor_property("settings", settings)

    world.get_world_settings().set_editor_property("default_game_mode", unreal.load_class(None, "/Script/OpenSCCT.ScctGameMode"))
    editor_assets.save_loaded_asset(world)
    log("saved %s (sky: %s)" % (level_path, sky_kind))


build_materials()
build_level()
editor_assets.save_directory(FOLDER, only_if_is_dirty=True, recursive=True)
