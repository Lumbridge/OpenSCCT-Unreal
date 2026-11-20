"""Read quick_loop's licensed SPAS-12 game mesh and its original PBR atlases."""
import numpy as np

from svmbuild.sidearm_models import read, values, world_matrices


MATERIALS = {'corps': 'Spas12Receiver', 'reste': 'Spas12Hardware',
             'crosse': 'Spas12Stock', 'canon': 'Spas12Barrel'}


def part_labels(mesh, positions):
    """Separate the moving exterior components on this source's combined meshes."""
    labels = np.full(len(positions), 'corps', dtype='<U5')
    if mesh == 6:
        # The fore-end and receiver are separate islands of the same material.
        labels[positions[:, :, 2].min(axis=1) > -.10] = 'Pump'
    if mesh == 3:
        # The small exposed breech plate is disconnected from the adjacent receiver.
        upper, lower = positions.max(axis=1), positions.min(axis=1)
        plate = (lower[:, 2] > -.45) & (upper[:, 2] < -.35) & (upper[:, 0] < -.032)
        labels[plate] = 'Bolt'
    return labels


def read_spas12(path, maps, library):
    doc, binary = read(path)
    for material in doc['materials']:
        name = MATERIALS[material['name']]
        pbr = material['pbrMetallicRoughness']
        orm = pbr['metallicRoughnessTexture']
        if orm != material['occlusionTexture']:
            raise ValueError(f'{name} requires separate occlusion processing')
        files = []
        for ref, suffix, mime in ((pbr['baseColorTexture'], 'Color.jpg', 'image/jpeg'),
                                  (material['normalTexture'], 'Normal.png', 'image/png'),
                                  (orm, 'ORM.png', 'image/png')):
            if ref.keys() != {'index'}:
                raise ValueError(f'{name} uses unexpected texture coordinates: {ref}')
            image = doc['images'][doc['textures'][ref['index']]['source']]
            if image['mimeType'] != mime:
                raise ValueError(f'Unexpected source format for {name}{suffix}')
            view = doc['bufferViews'][image['bufferView']]
            start = view.get('byteOffset', 0)
            filename = name + suffix
            (maps / filename).write_bytes(binary[start:start + view['byteLength']])
            files.append(filename)
        library[name] = files

    worlds, _ = world_matrices(doc)
    positions, normals, texcoords, sections, names = [], [], [], [], []
    for node_index, node in enumerate(doc['nodes']):
        if 'mesh' not in node:
            continue
        mesh = node['mesh']
        transform = worlds[node_index]
        for primitive in doc['meshes'][mesh]['primitives']:
            ids = values(doc, binary, primitive['indices']).reshape(-1, 3)
            attrs = primitive['attributes']
            source_points = values(doc, binary, attrs['POSITION'])[ids]
            points = source_points @ transform[:3, :3].T + transform[:3, 3]
            directions = values(doc, binary, attrs['NORMAL'])[ids] @ np.linalg.inv(transform[:3, :3])
            directions /= np.linalg.norm(directions, axis=2, keepdims=True)
            positions.append(points)
            normals.append(directions)
            texcoords.append(values(doc, binary, attrs['TEXCOORD_0'])[ids])
            material = MATERIALS[doc['materials'][primitive['material']]['name']]
            labels = part_labels(mesh, source_points)
            section = np.zeros(len(ids), dtype=np.int32)
            for body in np.unique(labels):
                section[labels == body] = len(names)
                names.append((body, 0, material))
            sections.append(section)
    return dict(xyz=np.concatenate(positions), normals=np.concatenate(normals),
                uv=np.concatenate(texcoords), meta=np.concatenate(sections), names=names)
