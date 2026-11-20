"""Surface and optional paint on the original four imported rifles, using the pawn's room lighting."""
import json
from pathlib import Path
import struct

import unreal

from scct_materials import FLOAT1, FLOAT3, Graph, pawn_light


RIFLE_FINISH = '/Game/OpenSCCT/Materials/M_ScctRifleFinish'
RIFLES = ('m4a1s', 'ak47', 'galil', 'famas')


def build(refresh=False):
    if unreal.EditorAssetLibrary.does_asset_exist(RIFLE_FINISH) and not refresh:
        return unreal.load_asset(RIFLE_FINISH)
    graph = Graph(RIFLE_FINISH, rebuild_in_place=True)
    graph.material.set_editor_property('used_with_skeletal_mesh', True)
    p = unreal.MaterialProperty
    white = '/Engine/EngineResources/WhiteSquareTexture'
    albedo = graph.texture('BaseColorTexture', white)
    base_factor = graph.colour('BaseColorFactor', (1, 1, 1, 1))
    colour = graph.custom('return Colour * Factor;', FLOAT3, Colour=(albedo, 'RGB'), Factor=base_factor)
    graph.output(colour, p.MP_BASE_COLOR)
    irradiance, _ = pawn_light(graph, graph.node(unreal.MaterialExpressionPixelNormalWS), graph.node(unreal.MaterialExpressionWorldPosition))
    graph.output(graph.custom('return Colour * Light;', FLOAT3, Colour=colour, Light=irradiance), p.MP_EMISSIVE_COLOR)

    normal = graph.texture('NormalTexture', '/Engine/EngineMaterials/DefaultNormal', unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    graph.output(graph.custom('return normalize(float3(N.xy * Strength, N.z));', FLOAT3,
                              N=(normal, 'RGB'), Strength=graph.scalar('NormalScale', 1)), p.MP_NORMAL)
    orm = graph.texture('MetallicRoughnessTexture', '/Engine/EngineMaterials/DefaultDiffuse_TC_Masks', unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    mask = graph.texture('FinishMask', '/Engine/EngineMaterials/DefaultDiffuse_TC_Masks', unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    gold = graph.scalar('GoldMirror', 0)
    with_map = graph.scalar('HasSourceORM', 0)
    coverage = graph.custom('return Mask * Enabled;', FLOAT1, Mask=(mask, 'R'), Enabled=graph.scalar('Coated', 0))
    for output, channel, factor, coat in [(p.MP_ROUGHNESS, 'G', 'RoughnessFactor', 'lerp(.16, .085, Gold)'),
                                          (p.MP_METALLIC, 'B', 'MetallicFactor', 'Gold')]:
        graph.output(graph.custom(f'return lerp(lerp(1.0, Packed, HasMap) * Factor, {coat}, Coverage);', FLOAT1,
                                  Packed=(orm, channel), HasMap=with_map, Factor=graph.scalar(factor, 1),
                                  Gold=gold, Coverage=coverage), output)
    return graph.finish()


def solid_finish(name, colour, roughness, metallic):
    folder = "/Game/OpenSCCT/Weapons/Materials"
    path = folder + "/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        mat = unreal.load_asset(path)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    edit = unreal.MaterialEditingLibrary
    edit.set_material_instance_parent(mat, build())
    edit.set_material_instance_vector_parameter_value(mat, "BaseColorFactor", unreal.LinearColor(*colour,1))
    edit.set_material_instance_scalar_parameter_value(mat, "RoughnessFactor", roughness)
    edit.set_material_instance_scalar_parameter_value(mat, "MetallicFactor", metallic)
    edit.update_material_instance(mat)
    if not unreal.EditorAssetLibrary.save_loaded_asset(mat,only_if_is_dirty=False):
        raise RuntimeError("Could not save " + path)
    return mat


def apply(chars, model, attachments_only=False):
    weapon = model.removesuffix('_first')
    if weapon not in RIFLES:
        return
    with (Path(chars) / 'OpenScctRifles' / (model + '.glb')).open('rb') as stream:
        stream.seek(12)
        length, _ = struct.unpack('<II', stream.read(8))
        doc = json.loads(stream.read(length))
    base = f'/Game/Imported/Chars/OpenScctRifles/{model}'
    lib = unreal.MaterialEditingLibrary
    parent = build()
    material_names = {}
    for material in doc['materials']:
        name = material['name']
        # Interchange gives duplicate glTF material names a numeric suffix, including
        # the second anodized section on the M4's integral sight.
        slot_name = name.replace('.', '_')
        occurrence = material_names.get(slot_name, 0)
        material_names[slot_name] = occurrence + 1
        if occurrence:
            slot_name += str(occurrence)
        if (name == 'OpenScctRifles.Arms' or material.get('alphaMode', 'OPAQUE') != 'OPAQUE'
                or 'KHR_materials_unlit' in material.get('extensions', {})
                or 'emissiveTexture' in material or any(material.get('emissiveFactor', []))):
            continue
        # The folding backup sight is painted as part of the M4 receiver.
        receiver = name.lower().startswith('openscctrifles.' + weapon + '_') or name == 'OpenScctRifles.M4BackupSight'
        if attachments_only and receiver:
            continue
        path = f'{base}/Materials/{slot_name}'
        if occurrence and not unreal.EditorAssetLibrary.find_package_referencers_for_asset(path):
            continue  # Interchange can collapse identical sections onto the first instance.
        instance = unreal.load_asset(path)
        if not instance:
            raise RuntimeError('Missing rifle surface ' + slot_name)
        lib.set_material_instance_parent(instance, parent)
        overrides = instance.get_editor_property('base_property_overrides')
        two_sided = material.get('doubleSided', False)
        overrides.set_editor_property('override_two_sided', two_sided)
        overrides.set_editor_property('two_sided', two_sided)
        instance.set_editor_property('base_property_overrides', overrides)
        pbr = material['pbrMetallicRoughness']
        for key, desc in [('BaseColorTexture', pbr.get('baseColorTexture')), ('NormalTexture', material.get('normalTexture')),
                          ('MetallicRoughnessTexture', pbr.get('metallicRoughnessTexture'))]:
            if not desc: continue
            uri = doc['images'][doc['textures'][desc['index']]['source']]['uri']
            tex = unreal.load_asset(f'{base}/Textures/' + Path(uri).stem)
            if not tex: raise RuntimeError(f'{model}: missing {uri}')
            if key == 'MetallicRoughnessTexture' and (tex.srgb or tex.compression_settings != unreal.TextureCompressionSettings.TC_MASKS):
                tex.set_editor_property('srgb', False)
                tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
                if not unreal.EditorAssetLibrary.save_loaded_asset(tex): raise RuntimeError('Could not save ' + uri)
            lib.set_material_instance_texture_parameter_value(instance, key, tex)
        lib.set_material_instance_vector_parameter_value(instance, 'BaseColorFactor', unreal.LinearColor(*pbr.get('baseColorFactor', [1, 1, 1, 1])))
        for key, value in [('HasSourceORM', 'metallicRoughnessTexture' in pbr), ('MetallicFactor', pbr.get('metallicFactor', 1)),
                           ('RoughnessFactor', pbr.get('roughnessFactor', 1)), ('NormalScale', material.get('normalTexture', {}).get('scale', 1))]:
            lib.set_material_instance_scalar_parameter_value(instance, key, float(value))
        lib.update_material_instance(instance)
        if not unreal.EditorAssetLibrary.save_loaded_asset(instance, only_if_is_dirty=False): raise RuntimeError('Could not save ' + slot_name)
