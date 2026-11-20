"""Author the merc's fixed-power prism optic in the editor. X points through the lenses, rear to front."""
import math
import unreal as ue

from build_merc_optic_cap import save_mesh
from scct_materials import Graph, FLOAT3
from scct_sight_glass import coat_sight_glass

MATERIAL = "/Game/OpenSCCT/Weapons/Materials/"
M4_FINISHES = "/Game/Imported/Chars/OpenScctRifles/m4a1s_first/Materials/OpenScctRifles_"


def coated_glass():
    glass = Graph(MATERIAL + "M_ScctAcogGlass", rebuild_in_place=True)
    uv = glass.node(ue.MaterialExpressionTextureCoordinate)
    coords = glass.custom("return float3(UV * 2 - 1, 1);", FLOAT3, UV=uv)
    # The physical coating overlays the rear image, which is drawn before depth of field.
    return coat_sight_glass(glass, coords, translucency_pass=ue.MaterialTranslucencyPass.MTP_AFTER_DOF, scope_handoff=True)


def scope_materials():
    coated = coated_glass()
    image = ue.load_asset(MATERIAL + "M_ScctPrecisionImage")
    if not image:
        from build_merc_precision_scopes import precision_image
        image = precision_image(MATERIAL)

    mark = Graph(MATERIAL + "M_ScctAcogChevron", rebuild_in_place=True)
    mark.material.set_editor_property("shading_model", ue.MaterialShadingModel.MSM_UNLIT)
    mark.material.set_editor_property("two_sided", True)
    mark.output(mark.in_the_picture(mark.colour("Reticle", (2.6, .055, .016, 1))),
                ue.MaterialProperty.MP_EMISSIVE_COLOR)
    finishes = [ue.load_asset(M4_FINISHES + part) for part in ("OpticAnodized", "OpticRubber", "OpticFasteners")]
    if not all(finishes):
        raise RuntimeError("Could not load the existing rifle finishes")
    return finishes + [coated, mark.finish(), image]


def build():
    materials = scope_materials()
    mesh = ue.DynamicMesh()
    vertices, normals, uvs, faces = [], [], [], []
    count = 0

    def flush(material):
        nonlocal count
        mesh.append_buffers_to_mesh(ue.GeometryScriptSimpleMeshBuffers(
            vertices=vertices, normals=normals, uv0=uvs, triangles=faces), material_id=material)
        count += len(faces)
        vertices.clear()
        normals.clear()
        uvs.clear()
        faces.clear()

    def face(points, normal):
        start = len(vertices)
        for i, point in enumerate(points):
            vertices.append(ue.Vector(*point))
            normals.append(ue.Vector(*normal))
            uvs.append(ue.Vector2D(i % 2, i // 2))
        for i in range(1, len(points)-1):
            faces.append(ue.IntVector(start, start+i, start+i+1))

    def turn(profile, center=(0., 0., 0.), axis="x", sections=64):
        # The last profile ring closes against the first, leaving an actual bore where called for.
        def map_axis(x,y,z):
            return (x,y,z) if axis=="x" else ((z,x,y) if axis=="y" else (y,z,x))
        first = len(vertices)
        for j,(x,r) in enumerate(profile):
            xp,rp=profile[j-1]
            xq,rq=profile[(j+1)%len(profile)]
            nx,nr=rp-rq,xq-xp
            length=math.hypot(nx,nr)
            for i in range(sections):
                c,s=math.cos(i*math.tau/sections),math.sin(i*math.tau/sections)
                dx,dy,dz=map_axis(x,r*c,r*s)
                vertices.append(ue.Vector(center[0]+dx,center[1]+dy,center[2]+dz))
                normals.append(ue.Vector(*map_axis(nx/length,nr*c/length,nr*s/length)))
                uvs.append(ue.Vector2D(i/sections,j/len(profile)))
                a=first+j*sections+i
                b=first+j*sections+(i+1)%sections
                v=first+(j+1)%len(profile)*sections+i
                w=first+(j+1)%len(profile)*sections+(i+1)%sections
                faces.extend((ue.IntVector(a,b,v),ue.IntVector(b,w,v)))

    def prism(x0, x1, outline):
        for (y,z),(ey,ez) in zip(outline,outline[1:]+outline[:1]):
            n=math.hypot(ez-z,ey-y)
            face([(x0,y,z),(x0,ey,ez),(x1,ey,ez),(x1,y,z)],(0,(ez-z)/n,(y-ey)/n))
        face([(x0,y,z) for y,z in reversed(outline)],(-1,0,0))
        face([(x1,y,z) for y,z in outline],(1,0,0))

    # The cast body swells into the objective bell. The recesses remain open end to end.
    turn([(0,2.02),(.24,2.12),(.72,2.14),(1.2,1.97),(2.3,1.98),
          (3.0,2.16),(5.7,2.22),(7.9,2.51),(9.60,2.91),(10.15,2.92),(10.29,2.82),
          (10.29,2.53),(9.97,2.47),(9.38,2.46),(6.90,1.95),(2.1,1.75),(.15,1.73),(0,1.77)])
    # The solid prism belly sits below the line of sight. Chamfered pedestal and clamp contact the rail.
    prism(2.0,7.5,[(-1.18,-4.34),(1.18,-4.34),(1.43,-3.7),(1.83,-1.45),
                    (.95,-1.65),(-.95,-1.65),(-1.83,-1.45),(-1.43,-3.7)])
    prism(1.55,8.4,[(-1.56,-5.28),(1.56,-5.28),(1.72,-4.91),(1.66,-4.38),
                     (1.35,-4.17),(-1.35,-4.17),(-1.66,-4.38),(-1.72,-4.91)])
    turn([(-.03,.43),(.08,.64),(.71,.65),(.83,.54),(.85,.02),(-.03,.02)],
         (4.75,0,2.06),"z",32)
    turn([(-.02,.47),(.05,.61),(.84,.61),(.95,.51),(.96,.02),(-.02,.02)],
         (5.1,2.0,-.16),"y",32)
    flush(0)

    # Ribbed eyepiece and a front retaining ring. The bevel catches the map lights in side view.
    turn([(-.77,1.89),(-.68,2.10),(-.49,2.15),(-.25,2.14),(-.18,2.05),(.12,2.05),
          (.21,2.14),(.40,2.14),(.48,2.04),(.55,1.81),(.34,1.75),(-.62,1.75),(-.77,1.82)])
    for x in (.56,.82,1.08,1.34):
        turn([(x,1.95),(x+.03,2.08),(x+.14,2.08),(x+.18,1.95)])
    turn([(9.81,2.53),(10.17,2.59),(10.24,2.53),(10.24,2.44),(9.85,2.43)])
    flush(1)

    for x in (2.65,6.95):
        turn([(0,.36),(.1,.43),(.27,.42),(.32,.34),(.32,.045),(0,.045)],(x,1.64,-4.7),"y",18)
    # The protected light collector sits on top of the forward casting.
    turn([(0,.13),(.12,.19),(2.05,.19),(2.16,.12),(2.16,.03),(0,.03)],(5.95,0,2.38),"x",24)
    flush(2)

    def lens(x, radius, bulge, faces_eye=True):
        start=len(vertices)
        first_face=len(faces)
        direction=1 if faces_eye else -1
        vertices.append(ue.Vector(x+bulge,0,0))
        normals.append(ue.Vector(-direction,0,0))
        uvs.append(ue.Vector2D(.5,.5))
        slices,rings=64,4
        for n in range(1,rings+1):
            t=n/rings
            for i in range(slices):
                c,s=math.cos(math.tau*i/slices),math.sin(math.tau*i/slices)
                vertices.append(ue.Vector(x+bulge*(1-t*t),radius*t*c,radius*t*s))
                slope = -2*bulge*t/radius
                length = math.hypot(1, slope)
                normals.append(ue.Vector(-direction/length,direction*slope*c/length,direction*slope*s/length))
                uvs.append(ue.Vector2D(.5+.5*t*c,.5+.5*t*s))
                a=start+1+(n-1)*slices+i
                b=start+1+(n-1)*slices+(i+1)%slices
                if n==1: faces.append(ue.IntVector(start,b,a))
                else: faces.extend((ue.IntVector(a-slices,b,a),ue.IntVector(a-slices,b-slices,b)))
        if not faces_eye:
            for i in range(first_face,len(faces)):
                triangle = faces[i]
                faces[i] = ue.IntVector(triangle.x,triangle.z,triangle.y)
    # Rear and front surface coatings stay visible while the local scene capture is inactive.
    lens(-.025,1.77,-.04)
    lens(9.86,2.46,.13,faces_eye=False)
    flush(3)

    # An etched angular mark with its apex at the optical centre. Keep its centre open below the tip.
    def etch(outline):
        face([(-.10,y,z) for y,z in outline],(-1,0,0))
    # Two convex strokes keep the inside of the V empty at every viewing distance.
    etch([(-.092,-.072),(0,0),(0,-.024),(-.080,-.082)])
    etch([(0,0),(.092,-.072),(.080,-.082),(0,-.024)])
    # A short illuminated strip is visible from outside the casting.
    turn([(0,.065),(.12,.095),(1.50,.095),(1.59,.065),(1.59,.01),(0,.01)],
         (6.15,0,2.54),"x",16)
    flush(4)
    lens(.04,1.77,-.04)
    flush(5)
    save_mesh(mesh,"SM_MercACOG",count,materials,
              [("Ocular",(0,0,0)),("ApertureEdge",(.04,1.77,0)),("Objective",(9.86,0,0))])


if __name__ == "__main__":
    build()
