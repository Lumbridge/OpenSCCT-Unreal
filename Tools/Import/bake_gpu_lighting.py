"""Bake the loaded editor world and save its lighting registry before leaving the editor."""
import sys
import traceback
import unreal


def finish():
    unreal.SystemLibrary.execute_console_command(world, "QUIT_EDITOR")


def advance(delta):
    global last_progress
    try:
        if baker.is_running():
            progress = baker.get_percentage()
            if progress // 10 != last_progress // 10:
                unreal.log_warning(f"SCCT-LIGHTING GPU {progress}%")
                last_progress = progress
            return
        unreal.unregister_slate_post_tick_callback(callback)
        probes = unreal.ScctImportLibrary.build_missing_light_probes(world)
        if probes < 0:
            raise RuntimeError("The level has no usable lighting volume")
        unreal.log_warning(f"SCCT-LIGHTING added {probes} indirect-light samples")
        if not unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True):
            raise RuntimeError("Could not save the map lighting packages")
        unreal.log_warning("SCCT-LIGHTING GPU finished")
    except Exception:
        unreal.log_error(traceback.format_exc())
    finish()


world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
try:
    if world.get_name() != sys.argv[1]:
        raise RuntimeError(f"Wrong map for bake: {world.get_name()}")
    baker = next(obj for obj in unreal.ObjectIterator(unreal.GPULightmassSubsystem) if obj.get_outer() == world)
    settings = baker.get_settings()
    samples = {"Preview": 64, "Medium": 128, "High": 256, "Production": 512}[sys.argv[2]]
    settings.set_editor_property("volumetric_lightmap_detail_cell_size", 90)
    settings.set_editor_property("gi_samples", samples)
    settings.set_editor_property("stationary_light_shadow_samples", samples)
    collection = unreal.load_asset("/Game/OpenSCCT/Materials/MPC_ScctLight")
    for name, value in (("Plan", 1.0), ("BakedScale", 0.0), ("Floor", 0.0)):
        unreal.MaterialLibrary.set_scalar_parameter_value(world, collection, name, value)
    unreal.SystemLibrary.execute_console_command(world, "Editor.AsyncAssetCompilationFinishAll")
    baker.set_realtime(False)
    baker.launch()
    if not baker.is_running():
        raise RuntimeError("GPU Lightmass could not start")
    last_progress = -10
    callback = unreal.register_slate_post_tick_callback(advance)
except Exception:
    unreal.log_error(traceback.format_exc())
    finish()
