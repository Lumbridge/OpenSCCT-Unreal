"""Generate the merc M4's first- and third-person carry-handle sight."""
import math
import unreal as ue

from build_merc_optic_cap import save_mesh
from build_merc_precision_scopes import ScopeMesh
from scct_rifle_cosmetics import solid_finish


def protective_ear(mesh, right):
    # Loft the crown into the forward slope so highlights follow the rounded casting.
    center = .95 if right else -.95
    stations = [(-.65,.29,-1.36,.16),(-.57,.30,-1.38,.34),(-.40,.31,-1.40,.53),
                (-.25,.31,-1.40,.59),(.39,.30,-1.42,.58),(.71,.29,-1.43,.52),
                (1.05,.28,-1.45,.35),(1.97,.25,-1.48,-.22),(2.42,.21,-1.49,-.54)]
    rings = []
    for x, radius, base, top in stations:
        cap = top-radius
        outline = [(-radius+.07,base),(radius-.07,base),(radius,base+.07),(radius,cap)]
        for i in range(1,9):
            theta = math.pi*i/8
            outline.append((radius*math.cos(theta),cap+radius*math.sin(theta)))
        outline.append((-radius,base+.07))
        rings.append([(x,center+y,z) for y,z in outline])
    offset = len(mesh.points)
    count = len(rings[0])
    for i,ring in enumerate(rings):
        for j,p in enumerate(ring):
            before = ring[(j-1)%count]
            after = ring[(j+1)%count]
            dy,dz = after[1]-before[1],after[2]-before[2]
            left = rings[max(0,i-1)][j]
            right_point = rings[min(len(rings)-1,i+1)][j]
            vx,vy,vz = [right_point[k]-left[k] for k in range(3)]
            normal = (dy*vz-dz*vy,dz*vx,-dy*vx)
            length = math.sqrt(sum(v*v for v in normal))
            mesh.vertex(p,tuple(v/length for v in normal),(i/(len(rings)-1),j/count))
            if i:
                a = offset+(i-1)*count+j
                b = offset+(i-1)*count+(j+1)%count
                mesh.triangle(a,b,b+count)
                mesh.triangle(a,b+count,a+count)
    for ring,normal in ((list(reversed(rings[0])),(-1,0,0)),(rings[-1],(1,0,0))):
        first = len(mesh.points)
        for p in ring:
            mesh.vertex(p,normal,(p[1],p[2]))
        for i in range(1,count-1):
            mesh.triangle(first,first+i,first+i+1)


def build():
    mesh = ScopeMesh()

    def bar(x0, x1, y0, y1, lower0, upper0, lower1, upper1):
        # A chamfered casting with a different height at each end.
        rings = []
        for x, lower, upper in ((x0, lower0, upper0), (x1, lower1, upper1)):
            bevel = min(.10, (upper-lower)/3, (y1-y0)/3)
            rings.append([(x,y0+bevel,lower), (x,y1-bevel,lower), (x,y1,lower+bevel),
                          (x,y1,upper-bevel), (x,y1-bevel,upper), (x,y0+bevel,upper),
                          (x,y0,upper-bevel), (x,y0,lower+bevel)])
        def face(points):
            a,b,c = points[:3]
            ux,uy,uz = [b[i]-a[i] for i in range(3)]
            vx,vy,vz = [c[i]-a[i] for i in range(3)]
            normal = (uy*vz-uz*vy, uz*vx-ux*vz, ux*vy-uy*vx)
            length = math.sqrt(sum(value*value for value in normal))
            normal = tuple(value/length for value in normal)
            start = len(mesh.points)
            for x,y,z in points:
                mesh.vertex((x,y,z), normal, (x*.16+y*.12, z*.2))
            for i in range(1,len(points)-1):
                mesh.triangle(start, start+i, start+i+1)
        face(list(reversed(rings[0])))
        face(rings[1])
        for i in range(8):
            j = (i+1) % 8
            face([rings[0][i], rings[0][j], rings[1][j], rings[1][i]])

    # Keep the opening below the bridge and the central sight channel open in the mesh.
    bar(-.95,13.6,-1.18,1.18,-3.70,-3.14,-3.70,-3.14)
    bar(-.85,2.2,-.96,.96,-3.22,-.95,-2.92,-1.00)
    bar(1.75,3.65,-.85,.85,-2.96,-.92,-1.70,-.98)
    bar(-.52,11.8,-.86,.86,-1.67,-.96,-1.59,-.98)
    bar(10.95,13.35,-.80,.80,-1.64,-.98,-3.28,-2.45)
    for right in (False,True):
        protective_ear(mesh,right)
    bar(-.22,.36,-.55,.55,-1.14,-.49,-1.14,-.49)
    bar(-.10,.20,-.30,.30,-.56,-.38,-.56,-.38)
    mesh.revolve([(-.16,.425),(-.20,.451),(-.18,.527),(-.10,.574),(.15,.574),
                  (.22,.534),(.20,.451),(.11,.425),(-.16,.425)],slices=64)
    mesh.finish_section(0)

    # The side dial catches a highlight along the edge of the iron-sight picture.
    dial = [(0,.64),(.06,.73),(.15,.78),(.48,.78),(.55,.72),(.58,.57),(.58,.01)]
    mesh.revolve(dial,(-.02,1.24,-.39),"y",40)
    for x in (1.24,10.7):
        mesh.revolve([(0,.34),(.10,.45),(.32,.45),(.40,.38),(.41,.04)],
                     (x,-1.52,-3.26),"y",20)
    mesh.finish_section(1)
    # These instances receive the same occlusion-tested room lights as the rifle body.
    materials = [solid_finish("MI_ScctCarryCasting",(.043,.046,.049),.48,.48),
                 solid_finish("MI_ScctCarryHardware",(.035,.038,.040),.38,.72)]
    save_mesh(mesh.mesh, "SM_MercM4CarryHandle", mesh.triangles, materials,
              [("Aperture",(0,0,0)), ("RailSeat",(6.8,0,-3.7))])


if __name__ == "__main__":
    build()
