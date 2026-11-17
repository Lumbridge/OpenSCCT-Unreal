using System;
using System.Collections.Generic;
using System.IO;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge.Pose;

namespace OpenScct.UnrealBridge.Equipment;

/// <summary>
/// The weapon and gadget mesh files of the shared content (<c>shared/chars</c>): the packages the simulation's
/// weapons name their meshes in, with what each file is for and the bones effects hang on. Unreal reads it once to
/// import or map the meshes; the list is made on first use per content folder.
/// <para>Conventions of these files (the converter's, checked against the content): every file is a skinned glb
/// in metres, Y up; its <c>extras.svmap</c> holds the mesh's RotOrigin and scale. The merc's first-person models
/// (1DEF_01, 1DEF_Uzi, 1DEF_shotgun) are arms and gun in one "Bip01" skeleton turned by RotOrigin yaw -16384, with
/// the First* clips. The guns others see (M160, Hk81, BM401, p190) have a small skeleton rooted at the body bone
/// ("corps"...) with RotOrigin 0: the root is the grip, +X of the actor runs along the barrel. Every gun has the
/// muzzle bone "FX" and most the casing bone "FX_DOUILLE".</para>
/// <para>Not listed: a file the pose code cannot read (<see cref="EquipmentModel.FailureOf"/> says why); none in
/// today's content. The first-person pistols of OpenScctSidearms (automatic_first, desert_eagle_first,
/// magnum44_first, suppressed_first, unsuppressed_first, arms_base) have clips that move scene nodes outside the
/// skin (Root, pistol, hammer, trigger, shell, stopper): <see cref="GlbCharacter"/> lists those nodes as bones after
/// the skin's joints, so their bone count is larger than their joint count (<see cref="GlbCharacter.JointCount"/>)
/// and a joint can have a parent with a higher index.</para>
/// </summary>
public static class EquipmentCatalog
{
    /// <summary>The packages under shared/chars that hold equipment (SPerso only for the spy's eye-view arms).</summary>
    private static readonly string[] s_packages = ["SWeaponFirst", "SGoggle", "OpenScctSidearms", "OpenScctRifles"];

    private const string SpyArms = "SPerso.ATT_01_fp";

    private static readonly Dictionary<string, BridgeEquipmentMesh[]> s_lists = new(StringComparer.OrdinalIgnoreCase);
    private static Dictionary<string, string>? s_classOf;

    /// <summary>Writes the catalog of <paramref name="sharedRoot"/>. Returns the entry count (which can be more than fit).</summary>
    public static int Fill(string? sharedRoot, Span<BridgeEquipmentMesh> meshes)
    {
        if (sharedRoot is null)
        {
            return 0;
        }

        BridgeEquipmentMesh[] list;
        lock (s_lists)
        {
            if (!s_lists.TryGetValue(sharedRoot, out list!))
            {
                list = Build(sharedRoot);
                s_lists[sharedRoot] = list;
            }
        }

        list.AsSpan(0, Math.Min(list.Length, meshes.Length)).CopyTo(meshes);
        return list.Length;
    }

    private static BridgeEquipmentMesh[] Build(string sharedRoot)
    {
        var assets = new List<string>();
        foreach (string package in s_packages)
        {
            string folder = Path.Combine(sharedRoot, "chars", package);
            if (!Directory.Exists(folder))
            {
                continue;
            }

            string[] files = Directory.GetFiles(folder, "*.glb");
            Array.Sort(files, StringComparer.OrdinalIgnoreCase);
            foreach (string file in files)
            {
                assets.Add(package + "." + Path.GetFileNameWithoutExtension(file));
            }
        }

        assets.Add(SpyArms);
        Dictionary<string, string> classOf = s_classOf ??= ClassesByMesh();
        var list = new List<BridgeEquipmentMesh>();
        foreach (string asset in assets)
        {
            if (EquipmentModel.Load(asset, sharedRoot) is not { } model)
            {
                continue;
            }

            list.Add(new BridgeEquipmentMesh
            {
                Mesh = model.AssetId,
                MeshPath = model.PathId,
                Role = RoleOf(asset, model),
                Class = EquipmentNames.Id(classOf.GetValueOrDefault(asset) ?? (asset.StartsWith("SGoggle.", StringComparison.OrdinalIgnoreCase) ? nameof(SGoggleBeam) : null)),
                BoneCount = model.Character.BoneCount,
                ClipCount = model.Character.ClipCount,
                MuzzleBone = model.MuzzleBone,
                CasingBone = model.CasingBone,
                LaserBone = model.LaserBone,
                RotOriginPitch = EquipmentMath.Degrees(model.Character.RotOrigin.Pitch),
                RotOriginYaw = EquipmentMath.Degrees(model.Character.RotOrigin.Yaw),
                RotOriginRoll = EquipmentMath.Degrees(model.Character.RotOrigin.Roll),
                ScaleX = model.Character.MeshScale.X,
                ScaleY = model.Character.MeshScale.Y,
                ScaleZ = model.Character.MeshScale.Z,
            });
        }

        return [.. list];
    }

    private static int RoleOf(string asset, EquipmentModel model)
    {
        if (asset.Equals(SpyArms, StringComparison.OrdinalIgnoreCase))
        {
            return BridgeEquipmentRole.SpyFirstPersonArms;
        }

        if (asset.StartsWith("SGoggle.", StringComparison.OrdinalIgnoreCase))
        {
            return BridgeEquipmentRole.GoggleGlow;
        }

        if (model.Character.Find(ViewModelClips.SnipeP190.ToString()) is not null)
        {
            // The eye-view copy of the pistol is the one with the laser socket.
            return model.LaserBone >= 0 ? BridgeEquipmentRole.SpyFirstPersonWeapon : BridgeEquipmentRole.SpyWeapon;
        }

        if (model.Character.Find(ViewModelClips.FirstWaitStFd2.ToString()) is not null)
        {
            return BridgeEquipmentRole.MercFirstPerson;
        }

        return model.MuzzleBone >= 0 ? BridgeEquipmentRole.MercWeapon : BridgeEquipmentRole.Other;
    }

    /// <summary>
    /// The simulation class that draws each mesh by default, read from the classes' own defaults (the rifle, the
    /// UZI and the shotgun with their third-person dummies, the spy's Shocker).
    /// </summary>
    private static Dictionary<string, string> ClassesByMesh()
    {
        var map = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        Func<Actor>[] defaults =
        [
            static () => new SWeaponDefense(), static () => new SWeaponDefense_Uzi(), static () => new SWeaponDefense_Shotgun(),
            static () => new SWeaponDefenseDummy(), static () => new SWeaponDefenseDummy_Uzi(), static () => new SWeaponDefenseDummy_Shotgun(),
            static () => new SStickyShocker(),
        ];
        foreach (Func<Actor> make in defaults)
        {
            Actor actor = make();
            if (actor.Mesh.Path is { } mesh)
            {
                map.TryAdd(mesh, actor.GetType().Name);
            }
        }

        return map;
    }
}
