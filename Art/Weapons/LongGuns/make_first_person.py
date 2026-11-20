"""Fit each long gun to the playable merc arm rig and bake its hands and mechanism together."""
import argparse
import math
from pathlib import Path
import shutil
import sys

import numpy as np

ROOT = Path(__file__).resolve().parent / 'Export'
WEAPONS = ('compactsmg', 'pumpshotgun', 'boltrifle', 'designatedmarksman')
FIRE_SECONDS = dict(zip(WEAPONS, (.077, .85, 1.05, .27)))
RELOAD_SECONDS = dict(zip(WEAPONS, (2.05, 3., 3.1, 2.7)))


def translation(x=0., y=0., z=0.):
    result = np.eye(4)
    result[:3, 3] = (x, y, z)
    return result


def rotate(axis, degrees):
    i, j = (axis + 1) % 3, (axis + 2) % 3
    c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    result = np.eye(4)
    result[i, i] = result[j, j] = c
    result[j, i], result[i, j] = s, -s
    return result


def key(f, *pairs):
    from svmbuild.reload_motion import smooth
    ts, vs = zip(*pairs)
    return float(smooth(ts, np.array(vs), f))


def mechanisms(weapon, action, f):
    """Return mechanical travel in model coordinates, in sync with the game's clip phase."""
    bolt = pump = roll = grip = mag = shell = 0.
    shell_visible = False
    if action == 'fire':
        if weapon == 'boltrifle':
            grip = key(f,(0,0),(.12,0),(.28,1),(.79,1),(.94,0),(1,0))
            roll = key(f,(0,0),(.26,0),(.37,58),(.72,58),(.81,0),(1,0))
            bolt = key(f,(0,0),(.35,0),(.51,-.092),(.57,-.092),(.72,0),(1,0))
        elif weapon == 'pumpshotgun':
            pump = key(f,(0,0),(.24,0),(.53,-.09),(.59,-.09),(.81,0),(1,0))
            bolt = pump * .72
        else:
            bolt = key(f,(0,0),(.15,-.040 if weapon=='compactsmg' else -.074),(.48,0),(1,0))
    elif action == 'reload':
        if weapon == 'pumpshotgun':
            # Source shell-insert notifies (46 frames, 15 Hz) and the early exit at frame 39.
            inserts = (.788/3, 1.11/3, 1.441333/3, 1.919333/3, 2.279333/3)
            start = .055
            for end in inserts:
                if start <= f <= end:
                    t = (f-start)/(end-start)
                    shell = key(t,(0,0),(.32,0),(.73,.75),(1,1))
                    shell_visible = t > .23
                    break
                start = end
            grip = key(f,(0,0),(.07,0),(.20,1),(.78,1),(.89,0),(1,0))
        else:
            grip = key(f,(0,0),(.08,0),(.23,1),(.75,1),(.90,0),(1,0))
            mag = key(f,(0,0),(.24,0),(.43,1),(.53,1),(.72,0),(1,0))
    return bolt, roll, pump, grip, mag, shell, shell_visible


def mechanism_poses(weapon, action, phase, own, neutral):
    bolt, roll, pump, grip, mag, shell, visible = mechanisms(weapon, action, phase)
    parts = {}
    for name, delta in (('Bolt',translation(bolt) @ rotate(0,-roll)),('Pump',translation(pump)),
                        ('Magazine',translation(-.09*mag,-.32*mag,-.035*mag) @ rotate(2,-12*mag))):
        if name in own:
            parts[name] = neutral[own[name]] @ delta
    if 'Shell' in own:
        load = neutral[own.get('ShellLoad', own['Shell'])].copy()
        load[:3,3] += [-(1-shell)*.105, -(1-shell)*.078, -(1-shell)*.03]
        if not visible:
            load[:3,:3] *= .0001
        parts['Shell'] = load
    return parts, grip


def bake_world_actions(world_path):
    """Use the same component motion in the carried gun as in the hand-authored view."""
    from svmbuild.sidearm_models import read, write, world_matrices
    from svmbuild.pistol_arms import curve, decompose
    from svmbuild.spy_viewmodel import compact
    doc, data = read(world_path)
    neutral, parents = world_matrices(doc)
    own = {doc['nodes'][i]['name']:i for i in doc['skins'][0]['joints']}
    weapon = world_path.stem
    doc['animations'] = []
    for action,seconds in [('fire',FIRE_SECONDS[weapon]),('reload',RELOAD_SECONDS[weapon])]:
        times = np.linspace(0,seconds,max(7,math.ceil(seconds*30)+1))
        animation = dict(name=action.title(),channels=[],samplers=[])
        poses = {}
        for time in times:
            parts,_ = mechanism_poses(weapon,action,time/seconds,own,neutral)
            for name,world in parts.items():
                parent = parents[own[name]]
                poses.setdefault(name,[]).append(decompose(np.linalg.inv(neutral[parent])@world))
        for name,frames in poses.items():
            for k,path in enumerate(('translation','rotation','scale')):
                curve(doc,data,animation,own[name],path,times,[v[k] for v in frames])
        doc['animations'].append(animation)
    write(world_path,doc,compact(doc,data))


def fit(world_path, template):
    from svmbuild.sidearm_models import read, write, append, values, world_matrices
    from svmbuild.pistol_arms import append_parts, curve, decompose, tracks, pose
    from svmbuild.reload_motion import solve_arm
    from svmbuild.spy_viewmodel import compact

    doc, data = read(template)
    source, source_data = read(world_path)
    source_world, source_parents = world_matrices(source)
    own = {source['nodes'][i]['name']:i for i in source['skins'][0]['joints']}
    weapon = world_path.stem
    names = {n.get('name'):i for i,n in enumerate(doc['nodes'])}
    carrier = names['Main_j_049']
    skin = doc['skins'][0]
    template_idle = next(a for a in doc['animations'] if a['name']=='FirstWaitStFd2')
    held = pose(doc, tracks(doc,data,template_idle), 0)
    grip_home = np.array([.155,-.215,.139])
    placement = translation(*(grip_home-source_world[own['Grip']][:3,3]))
    grip_wrist = held[names['R_wrist_028']][:3,3]-grip_home
    support_wrist = np.array([-.074,-.026,-.060])
    # Keep the credited arm mesh, its deform bones and the receiver motion in every
    # contextual clip. Geometry and mechanism bones from the template have no role here.
    keep = carrier + 1
    skin_indices = [i for i,j in enumerate(skin['joints']) if j < keep]
    binds = [v for i,v in enumerate(values(doc,data,skin['inverseBindMatrices']).tolist()) if i in skin_indices]
    skin['joints'] = [skin['joints'][i] for i in skin_indices]
    doc['nodes'] = doc['nodes'][:keep]
    for node in doc['nodes']:
        if 'children' in node: node['children'] = [j for j in node['children'] if j<keep]
    for scene in doc['scenes']: scene['nodes'] = [j for j in scene['nodes'] if j<keep]
    doc['meshes'] = doc['meshes'][:1]
    doc['materials'] = doc['materials'][:1]
    arm_pbr = doc['materials'][0]['pbrMetallicRoughness']
    arm_textures = [arm_pbr['baseColorTexture']['index'],arm_pbr['metallicRoughnessTexture']['index'],
                    doc['materials'][0]['normalTexture']['index']]
    assert arm_textures == [0,1,2] and [t['source'] for t in doc['textures'][:3]] == arm_textures
    doc['images'],doc['textures'] = doc['images'][:3],doc['textures'][:3]
    for anim in doc['animations']:
        anim['channels'] = [c for c in anim['channels'] if c['target']['node']<keep]

    # Copy the articulated gun hierarchy under the mocap's animated carrier. The inverse
    # bind matrices use the rest pose, while mounting uses the actual held pose.
    mapped = {}
    def attach(index):
        if index in mapped: return mapped[index]
        source_parent = source_parents.get(index)
        parent = attach(source_parent) if source_parent is not None else carrier
        local = (np.linalg.inv(source_world[source_parent]) @ source_world[index] if source_parent is not None
                 else np.linalg.inv(held[carrier]) @ placement @ source_world[index])
        p,q,s = decompose(local)
        new = len(doc['nodes'])
        mapped[index] = new
        doc['nodes'].append(dict(name=source['nodes'][index]['name'],translation=p.tolist(),rotation=q.tolist(),scale=s.tolist()))
        doc['nodes'][parent].setdefault('children',[]).append(new)
        skin['joints'].append(new)
        return new
    for index in source['skins'][0]['joints']: attach(index)
    # The managed aiming path selects ADS for its reference, then the UE presenter
    # translates to the ocular of whichever optic is actually mounted on the receiver.
    ads = len(doc['nodes'])
    doc['nodes'].append(dict(name='ADS'))
    doc['nodes'][mapped[own['Sight']]].setdefault('children',[]).append(ads)
    skin['joints'].append(ads)
    rest, parents = world_matrices(doc)
    mesh_world = rest[names['Object_83']]
    for joint in skin['joints'][len(binds):]:
        binds.append((np.linalg.inv(rest[joint])@mesh_world).T.reshape(-1).tolist())
    skin['inverseBindMatrices'] = append(doc,data,binds,'MAT4')
    idle = pose(doc,tracks(doc,data,template_idle),0)
    skin_nodes = {name:skin['joints'].index(mapped[i]) for name,i in own.items()}
    append_parts(doc,data,world_path,placement,skin_nodes,rest,idle,mesh_world)

    right = [names[n] for n in ('R_arm_025','R_elbow_026','R_wrist_028')]
    left = [names[n] for n in ('L_arm_00','L_elbow_01','L_wrist_03')]
    for anim in doc['animations']:
        name = anim['name']
        fire = name == ('FirstShooStFd2S' if weapon=='pumpshotgun' else 'FirstShooStFd2')
        reload = name == ('FirstReloStFd2S' if weapon=='pumpshotgun' else 'FirstReloStFd2')
        ordinary = name.startswith(('FirstWait','FirstWalk','FirstDraw','FirstHolster','FirstJump','FirstLand','FirstFall'))
        if not (fire or reload or ordinary): continue
        original = tracks(doc,data,anim)
        duration = max(float(t[-1]) for _,_,t,_ in original)
        seconds = FIRE_SECONDS[weapon] if fire else RELOAD_SECONDS[weapon] if reload else duration
        times = np.linspace(0,seconds,max(7,math.ceil(seconds*30)+1))
        action = 'fire' if fire else 'reload' if reload else 'carry'
        changed = set(left+right+[left[1]+1,right[1]+1])
        if fire or reload: changed.add(carrier)
        changed.update(mapped[own[n]] for n in ('Bolt','Pump','Magazine','Shell') if n in own)
        prepared = {n:[] for n in changed}
        for t in times:
            f = t/seconds
            old = held if fire or reload else pose(doc,original,t)
            gun = np.eye(4) if fire or reload else old[carrier] @ np.linalg.inv(held[carrier])
            if fire:
                impulse = key(f,(0,0),(.12,1),(.43,.23),(.86,0),(1,0))
                kick = {'boltrifle':4.2,'pumpshotgun':4.8,'designatedmarksman':2.7,'compactsmg':1.1}[weapon]
                gun = translation(*grip_home) @ translation(-.010*kick*impulse) @ rotate(2,kick*impulse) @ translation(*(-grip_home))
            elif reload:
                lift = key(f,(0,0),(.20,1),(.73,1),(.96,0),(1,0))
                gun = translation(*grip_home) @ translation(-.05*lift,.045*lift,0) @ rotate(0,18*lift) @ rotate(2,8*lift) @ translation(*(-grip_home))
            desired = {carrier:gun@held[carrier]}
            parts, grasp = mechanism_poses(weapon,action,f,own,source_world)
            for part, transform in parts.items():
                desired[mapped[own[part]]] = gun@placement@transform
            def marker(name):
                idx = own[name]
                parent = source_parents.get(idx)
                if parent is not None and source['nodes'][parent]['name'] in parts:
                    return (parts[source['nodes'][parent]['name']]@np.linalg.inv(source_world[parent])@source_world[idx])[:3,3]
                return source_world[idx][:3,3]
            trigger_hand = marker('Grip') + grip_wrist
            support_hand = marker('PumpGrip' if 'PumpGrip' in own else 'Support') + support_wrist
            if fire and weapon=='boltrifle':
                trigger_hand = (1-grasp)*trigger_hand + grasp*(marker('BoltHandle')+[-.075,-.022,.018])
            if reload:
                if weapon=='pumpshotgun':
                    if 'ShellLoad' in own:
                        insert = marker('ShellLoad').copy()
                    else:
                        insert = marker('Grip')+[.11,.022,0]
                    shell_phase = mechanisms(weapon,action,f)[5]
                    insert += [-.095*(1-shell_phase),-.075*(1-shell_phase),0.]
                    contact = insert + support_wrist
                else:
                    contact = marker('MagazineGrip') + [-.065,-.008,-.032]
                support_hand = (1-grasp)*support_hand+grasp*contact
            for chain, target in ((left,support_hand),(right,trigger_hand)):
                target = (gun@placement@np.r_[target,1])[:3]
                orientation = gun[:3,:3]@held[chain[2]][:3,:3]
                desired.update(solve_arm(old,chain,target,orientation,twist_node=chain[1]+1))
            def current(node):
                if node in desired: return desired[node]
                if node==carrier: return gun@held[carrier]
                parent = parents.get(node)
                if parent is None: return held[node] if fire or reload else old[node]
                if node in mapped.values(): return current(parent)@np.linalg.inv(rest[parent])@rest[node]
                return old[node]
            for n in prepared:
                prepared[n].append(decompose(np.linalg.inv(current(parents[n]))@current(n)))
        if fire or reload:
            # A clip must fully define the arm/finger pose even on a newly drawn view model.
            source_channels = tracks(doc,data,template_idle)
            anim['channels'],anim['samplers'] = [],[]
            for node,path,times0,frames in source_channels:
                if node not in changed:
                    curve(doc,data,anim,node,path,[0,seconds],[frames[0],frames[0]])
        else:
            anim['channels'] = [c for c in anim['channels'] if c['target']['node'] not in changed]
        for n, poses in prepared.items():
            for i,path in enumerate(('translation','rotation','scale')):
                curve(doc,data,anim,n,path,times,[r[i] for r in poses])

    doc['extras'] = dict(svmap={'mesh':'OpenScctRifles.'+weapon+'_first'},
                         license='Arm rig and textures: see OpenScctRifles/CREDITS.txt. Weapon mesh credits accompany source.')
    # Remove unreferenced animation samplers before packing (including old rifle geometry).
    for anim in doc['animations']:
        kept = [anim['samplers'][c['sampler']] for c in anim['channels']]
        for i,c in enumerate(anim['channels']): c['sampler']=i
        anim['samplers'] = kept
    data = compact(doc,data)
    for img in doc['images']:
        uri = img.get('uri')
        if uri and not (world_path.parent/uri).exists():
            (world_path.parent/uri).parent.mkdir(parents=True,exist_ok=True)
            shutil.copy2(template.parent/uri,world_path.parent/uri)
    output=world_path.with_name(weapon+'_first.glb')
    write(output,doc,data)
    print(f'{output.name}: {len(doc["animations"])} clips, {len(skin["joints"])} bones',flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--content',required=True,type=Path,help='OpenSCCT managed content directory containing svmbuild')
    parser.add_argument('--template',required=True,type=Path,help='Installed OpenScctRifles/m4a1s_first.glb')
    parser.add_argument('--world-only',action='store_true',help='Only refresh third-person moving parts')
    parser.add_argument('weapons',nargs='*',metavar='weapon')
    args=parser.parse_args()
    sys.path.insert(0,str(args.content))
    for weapon in args.weapons or WEAPONS:
        if weapon not in WEAPONS: parser.error(f'unknown weapon: {weapon}')
        path = ROOT/(weapon+'.glb')
        bake_world_actions(path)
        if not args.world_only: fit(path,args.template)
