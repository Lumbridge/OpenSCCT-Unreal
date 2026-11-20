"""Read evaluated geometry, UV seams, corner normals and connected parts from a game-art source.

Runs in Blender. The final two arguments are the source mesh and output .npz.
"""
import sys
from pathlib import Path

import bpy
import numpy as np

source, destination = map(Path, sys.argv[-2:])
if source.suffix.lower() == ".blend":
    bpy.ops.wm.open_mainfile(filepath=str(source))
else:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    if source.suffix.lower() == ".obj":
        bpy.ops.wm.obj_import(filepath=str(source), use_split_groups=True)
    else:
        bpy.ops.import_scene.fbx(filepath=str(source))

bpy.context.view_layer.update()
deps = bpy.context.evaluated_depsgraph_get()


def islands(mesh, points):
    """Weld UV/normal splits only while identifying rigid parts. The exported vertices keep their seams."""
    parent = list(range(len(mesh.vertices)))

    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    representatives = {}
    for i, point in enumerate(points):
        key = tuple(point.round(5))
        if key in representatives:
            parent[root(i)] = root(representatives[key])
        else:
            representatives[key] = i
    for edge in mesh.edges:
        parent[root(edge.vertices[0])] = root(edge.vertices[1])
    roots = [root(i) for i in range(len(mesh.vertices))]
    lookup = {value: index for index, value in enumerate(sorted(set(roots)))}
    return [lookup[value] for value in roots]


names = {}
positions, normals, texcoords, sections = [], [], [], []
for obj in bpy.context.scene.objects:
    # Skip the presentation backdrop and loose demonstration cartridges in the source scenes.
    if obj.type != "MESH" or obj.name == "Cube.003" or obj.name.lower().startswith("bullet"):
        continue
    evaluated = obj.evaluated_get(deps)
    mesh = evaluated.to_mesh()
    mesh.calc_loop_triangles()
    basis = evaluated.matrix_world
    normal_basis = basis.to_3x3().inverted_safe().transposed()
    points = np.array([list(basis @ vertex.co) for vertex in mesh.vertices])
    parts = islands(mesh, points)
    uvs = mesh.uv_layers.active
    for triangle in mesh.loop_triangles:
        indices = list(triangle.vertices)
        loops = list(triangle.loops)
        material = mesh.materials[triangle.material_index] if triangle.material_index < len(mesh.materials) else None
        key = (obj.name, parts[indices[0]], material.name if material else "material")
        sections.append(names.setdefault(key, len(names)))
        positions.append(points[indices])
        normals.append([list((normal_basis @ mesh.corner_normals[i].vector).normalized()) for i in loops])
        texcoords.append([[uvs.data[i].uv.x, 1 - uvs.data[i].uv.y] for i in loops] if uvs else [[0, 0]]*3)
    evaluated.to_mesh_clear()

np.savez_compressed(destination, xyz=np.asarray(positions), normals=np.asarray(normals),
                    uv=np.asarray(texcoords), meta=np.asarray(sections), names=np.asarray(list(names), str))
