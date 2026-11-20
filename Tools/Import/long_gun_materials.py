"""Install the long guns' authored PBR maps with the same occluded lamp term as the pawn."""
import filecmp
import json
import os
from pathlib import Path
import struct
import tempfile

import unreal
from scct_materials import Graph, FLOAT1, FLOAT3, pawn_light

MODELS = ('boltrifle', 'designatedmarksman', 'compactsmg', 'pumpshotgun')
PARENT = '/Game/OpenSCCT/Weapons/Materials/M_ScctLongGun'
LIB = unreal.MaterialEditingLibrary
ASSETS = unreal.EditorAssetLibrary


def is_staged(path):
    return (path.is_relative_to(Path(tempfile.gettempdir()).resolve()) or
            any(part.lower() in ('saved', 'intermediate') for part in path.parts))


def reimport_source(directory, relative, package):
    imported = (directory / relative).resolve(strict=True)
    if not is_staged(imported):
        return imported
    roots = []
    if os.environ.get('SVM_CONTENT'):
        roots.append(Path(os.environ['SVM_CONTENT']) / 'shared' / 'chars' / package)
    roots.append(Path(__file__).resolve().parents[2] / 'Art/Weapons/LongGuns/Export')
    for root in roots:
        candidate = (root / relative).resolve()
        if not is_staged(candidate) and candidate.is_file() and filecmp.cmp(imported, candidate, shallow=False):
            return candidate
    raise RuntimeError(f'No matching persistent reimport source for {imported}. Set SVM_CONTENT to the '
                       f'converted content folder containing the same {relative}, or import from that folder directly.')


def parent_material():
    if ASSETS.does_asset_exist(PARENT):
        return unreal.load_asset(PARENT)
    g = Graph(PARENT)
    g.material.set_editor_property('used_with_skeletal_mesh', True)
    base = g.texture('GunBase', '/Engine/EngineResources/WhiteSquareTexture')
    normal = g.texture('GunNormal', '/Engine/EngineMaterials/DefaultNormal',
                       unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    finish = g.texture('GunORM', '/Engine/EngineMaterials/DefaultDiffuse_TC_Masks',
                       unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    p = unreal.MaterialProperty
    g.output(base, p.MP_BASE_COLOR, 'RGB')
    g.output(normal, p.MP_NORMAL, 'RGB')
    g.output(g.custom('return R * K;',FLOAT1,R=(finish,'G'),K=g.scalar('RoughnessFactor',1)),p.MP_ROUGHNESS)
    g.output(g.custom('return M * K;',FLOAT1,M=(finish,'B'),K=g.scalar('MetallicFactor',1)),p.MP_METALLIC)
    g.output(g.custom('return lerp(1.0, A, K);',FLOAT1,A=(finish,'R'),K=g.scalar('OcclusionStrength',0)),p.MP_AMBIENT_OCCLUSION)
    light,_ = pawn_light(g, g.node(unreal.MaterialExpressionPixelNormalWS), g.node(unreal.MaterialExpressionWorldPosition))
    # Static rooms can have lamps baked into their walls. The moving gun receives the
    # same occlusion-filtered diffuse irradiance selected for the pawn in that room.
    g.output(g.custom('return C * L;',FLOAT3,C=(base,'RGB'),L=light),p.MP_EMISSIVE_COLOR)
    return g.finish()


def apply(chars, package, model, meshes):
    if model.removesuffix('_first') not in MODELS: return
    parent = parent_material()
    directory = Path(chars)/package
    path = directory/(model+'.glb')
    with path.open('rb') as f:
        f.seek(12)
        length,_ = struct.unpack('<II',f.read(8))
        gltf=json.loads(f.read(length))
    base = f'/Game/Imported/Chars/{package}/{model}'
    def texture(ref):
        uri = gltf['images'][gltf['textures'][ref['index']]['source']]['uri']
        result = unreal.load_asset(base+'/Textures/'+Path(uri).stem)
        if not result: raise RuntimeError(f'{model} is missing {uri}')
        return result
    for entry in gltf['materials']:
        name = entry['name'].replace('.','_')
        instance = unreal.load_asset(base+'/Materials/'+name)
        if not isinstance(instance,unreal.MaterialInstanceConstant):
            raise RuntimeError('Missing authored instance '+name)
        pbr=entry['pbrMetallicRoughness']
        LIB.set_material_instance_parent(instance,parent)
        for parameter,ref in [('GunBase',pbr['baseColorTexture']),('GunNormal',entry['normalTexture']),
                              ('GunORM',pbr['metallicRoughnessTexture'])]:
            tex = texture(ref)
            if parameter=='GunORM' and (tex.get_editor_property('srgb') or tex.get_editor_property('compression_settings')!=unreal.TextureCompressionSettings.TC_MASKS):
                tex.set_editor_property('srgb',False)
                tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS)
                if not ASSETS.save_loaded_asset(tex):
                    raise RuntimeError('Could not save packed weapon texture '+tex.get_path_name())
            LIB.set_material_instance_texture_parameter_value(instance,parameter,tex)
        for parameter,value in [('RoughnessFactor',pbr.get('roughnessFactor',1)),('MetallicFactor',pbr.get('metallicFactor',1)),
                                ('OcclusionStrength',entry.get('occlusionTexture',{}).get('strength',1) if 'occlusionTexture' in entry else 0)]:
            LIB.set_material_instance_scalar_parameter_value(instance,parameter,float(value))
        LIB.update_material_instance(instance)
        if not ASSETS.save_loaded_asset(instance,only_if_is_dirty=False):
            raise RuntimeError('Could not save weapon material '+instance.get_path_name())
    # Unreal also stores reimport paths for external textures. Keep these usable when
    # a selective import used a temporary pack containing only this weapon.
    sources = {path.name: Path(path.name)}
    for image in gltf.get('images', []):
        if image.get('uri'):
            relative = Path(image['uri'])
            sources[relative.name] = relative
    for asset_path in list(meshes) + list(ASSETS.list_assets(base + '/Textures', recursive=False)):
        asset = unreal.load_asset(asset_path)
        source = asset.get_editor_property('asset_import_data')
        old = source.get_first_filename() if source else ''
        if not old:
            continue
        relative = sources.get(Path(old).name)
        if relative is None:
            if is_staged(Path(old).resolve()):
                raise RuntimeError(f'Cannot identify staged source {old} for {asset_path}.')
            continue
        target = reimport_source(directory, relative, package)
        if Path(old) != target:
            source.scripted_add_filename(str(target), 0, '')
            if not ASSETS.save_loaded_asset(asset, only_if_is_dirty=False):
                raise RuntimeError('Could not save reimport source for ' + asset.get_path_name())
