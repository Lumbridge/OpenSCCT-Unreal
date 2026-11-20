using System;
using System.Numerics;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.Game.Svm;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Equipment;
using OpenScct.UnrealBridge.Pose;

internal static class RifleRecoilProbe
{
    internal static void Run(string shared)
    {
        foreach (int aim in new[] { 0, 1 })
        {
            Check(shared, new M4A1S(), aim);
            Check(shared, new Famas(), aim);
            Check(shared, new Galil(), aim);
        }
    }

    private static void Check(string shared, RecoilRecoveryRifle weapon, float aim)
    {
        var world = new World(1, ENetMode.NM_Standalone);
        var pawn = world.Level.SpawnActor(new SPawnDefense_1Mesh(), default, default)!;
        var pc = world.Level.SpawnActor(new SPlayerController(), default, default)!;
        pc.Possess(pawn);
        var rifle = world.Level.SpawnActor(weapon, pawn.Location, pawn.Rotation)!;
        rifle.Instigator = pawn;
        rifle.SetOwner(pawn);
        rifle.GiveTo(pawn);
        pawn.WeaponLoaded = 2;
        SidearmEquipment.Give(pawn, SidearmKind.Unsuppressed);
        pc.GotoState(new Name("s_PlayerWalkingSt2H"));
        world.Step(6);
        if (rifle.ROFMode == SRateOfFireMode.ROF_Burst) rifle.SwitchROF();
        // Unreal presents the owning client's predicted shot IDs, including in a local loopback match.
        rifle.Role = ENetRole.ROLE_AutonomousProxy;
        var model = new MercFirstPerson(shared);
        double time = world.Level.TimeSeconds;
        void Tick() => model.OnTick(pc, time, true, aim);
        Xform Muzzle(double sample)
        {
            BridgeFirstPerson view = default;
            if (!model.Draw(pc, 0, 1, ref view, aim, true, sample) || model.Sight is not { } sight)
                throw new InvalidOperationException($"The {rifle.ItemName} rig has no visible muzzle.");
            return sight.Barrel;
        }
        void Match(Xform expected, Xform actual, string phase)
        {
            const float tolerance = .0001f;
            if (Vector3.Distance(expected.Origin, actual.Origin) > tolerance ||
                Vector3.Distance(expected.Basis.X, actual.Basis.X) > tolerance ||
                Vector3.Distance(expected.Basis.Y, actual.Basis.Y) > tolerance)
                throw new InvalidOperationException($"{rifle.ItemName} aim {aim} {phase}: expected {expected.Origin}, got {actual.Origin}.");
        }

        Tick();
        pc.bFire = 1;
        pc.eventFire(1f);
        Tick();
        if (rifle.ShotCounter != 1) throw new InvalidOperationException("The owning client did not predict the opening shot.");
        var reference = new MercRifleRecoil();
        reference.Fire(time, rifle.ShotCounter, aim);
        // Hold the simulation pose fixed and vary only sub-frame presentation time. Reading it after the
        // curve has expired provides the same animated muzzle without the procedural displacement.
        var neutral = Muzzle(time + 2);
        if (Vector3.Distance(neutral.Origin, Muzzle(time + .028).Origin) < .0001f)
            throw new InvalidOperationException($"{rifle.ItemName} opening shot produced no visible muzzle displacement.");
        foreach (double age in new[] { .028, .07, .16, .35 })
            Match(reference.TransformAt(time + age) * neutral, Muzzle(time + age), "opening shot");
        int opening = rifle.ShotCounter;
        for (int ticks = 0; ticks < 8 && rifle.ShotCounter == opening; ticks++)
        {
            world.Step();
            time = world.Level.TimeSeconds;
            Tick();
        }
        if (rifle.ShotCounter != opening + 1) throw new InvalidOperationException("The second shot did not fire.");
        reference.Fire(time, rifle.ShotCounter, aim);
        neutral = Muzzle(time + 2);
        Match(reference.TransformAt(time + .052) * neutral, Muzzle(time + .052), "following shot");

        pc.bFire = 0;
        world.Step(7);
        time = world.Level.TimeSeconds;
        if (!rifle.eventReload()) throw new InvalidOperationException("Reload did not start.");
        Tick();
        Match(Muzzle(time + 2), Muzzle(time + .07), "reload");
        world.Step(210);
        time = world.Level.TimeSeconds;
        int beforeSwap = rifle.ShotCounter;
        pc.bFire = 1;
        pc.eventFire(1f);
        Tick();
        if (rifle.ShotCounter != beforeSwap + 1)
            throw new InvalidOperationException($"{rifle.ItemName} did not fire immediately before the swap.");
        pc.bFire = 0;
        if (!SidearmEquipment.Select(pawn, 1, force: true)) throw new InvalidOperationException("Swapping to the sidearm failed.");
        Tick();
        if (!SidearmEquipment.Select(pawn, 0, force: true)) throw new InvalidOperationException("Returning to the primary failed.");
        Tick();
        Match(Muzzle(time + 2), Muzzle(time + .07), "return from sidearm");
        Console.WriteLine($"PASS {rifle.ItemName}, aim={aim}: opening and following model recoil, reload and re-equip.");
    }
}
