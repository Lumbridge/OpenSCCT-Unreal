using System;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.Game.Svm;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Controls;

internal static class AimTimingChecks
{
    public static unsafe void Run(Driver driver, Action<bool, string> check)
    {
        var session = driver.Bridge.Session;
        var pc = session.LocalController!;
        var pawn = pc.SBPawn!;
        var controls = driver.Controls;
        ControlsStore.Settings = new ControlSettings();
        ControlsStore.Bindings = new ControlBindings();

        void Equip(SWeaponDefense model)
        {
            if (model is MercSidearm pistol)
            {
                SidearmEquipment.Give(pawn, (SidearmKind)pistol.Kind);
            }
            else
            {
                var actor = (SWeaponDefense)pawn.XLevel.SpawnActor(model, pawn.Location, pawn.Rotation, pawn, pawn)!;
                actor.GiveTo(pawn);
                pawn.PrimaryWeapon = actor;
            }
            if (!SidearmEquipment.Select(pawn, model is MercSidearm ? 1 : 0, force: true))
                throw new InvalidOperationException($"Could not equip {model.GetType().Name}");
            pawn.WeaponSwitchUntil = pawn.Level.TimeSeconds;
        }

        float Advance(int fps, int frames, bool held)
        {
            var raw = new BridgeRawInput { Flags = BridgeRawInputFlags.Play | BridgeRawInputFlags.AllowMercFreeLook, DeltaSeconds = 1f / fps };
            raw.Keys[(int)BridgeKey.RightMouse] = held ? (byte)1 : (byte)0;
            for (int n = 0; n < frames; ++n)
            {
                controls.Frame(in raw);
                controls.Keys.FillTick(session.Input, pc);
            }
            return controls.GetView(16f / 9f, 0f).RifleAimBlend;
        }

        var guns = new (Func<SWeaponDefense> Make, float Rate, bool Linear, int Optic)[]
        {
            (() => new M4A1S(), 2.3f * 1.35f, false, 0),
            (() => new DesignatedMarksman(), 2.3f, false, 0),
            (() => new BoltRifle(), 4f, true, 0),
            (() => new MercSidearm { Kind = (int)SidearmKind.Unsuppressed }, 3f, false, 0),
            (() => new M4A1S(), 2.3f * 1.35f, false, WeaponAttachments.Acog),
            (() => new MercSidearm { Kind = (int)SidearmKind.Unsuppressed }, 3f, false, WeaponAttachments.Acog),
        };
        foreach (var (make, rate, linear, optic) in guns)
        {
            float? lowFpsMiddle = null;
            foreach (int fps in new[] { 30, 120 })
            {
                var model = make();
                Equip(model);
                var equipped = (SWeaponDefense)pawn.MyWeapon!;
                WeaponAttachments.Apply(equipped, optic);
                Advance(fps, fps, false);
                var hip = controls.GetView(16f / 9f, 0f);
                float expectedPower = WeaponAttachments.OpticPower(equipped);
                float expectedLens = expectedPower > 1f ? expectedPower : 0f;
                float early = Advance(fps, fps / 15, true);
                float middle = Advance(fps, fps / 15, true);
                var partial = controls.GetView(16f / 9f, 0f);
                float returning = Advance(fps, fps / 15, false);
                float down = Advance(fps, fps / 15, false);
                string sight = optic == WeaponAttachments.Acog ? "ACOG" : "standard sight";
                string context = $"{model.GetType().Name} {sight} @ {fps} FPS (67 ms {early:F4}, 133 ms {middle:F4}, reversed {returning:F4})";
                check(Math.Abs(partial.HorizontalFov - hip.HorizontalFov) < .001f &&
                    Math.Abs(partial.ScopeLensPower - expectedLens) < .001f &&
                    Math.Abs(hip.ScopeLensPower - expectedLens) < .001f,
                    $"{context}: lens power and peripheral FOV hold during the raise");
                check(0 < early && early < middle && middle < 1 && Math.Abs(returning - early) < .001f && down < .0001f,
                    $"{context}: releasing mid-raise retraces the sight movement");
                float phase = rate * (2f / 15f);
                float expected = linear ? phase : .5f * (1f - MathF.Cos(MathF.PI * phase));
                check(Math.Abs(middle - expected) < .001f, $"{context}: reference mid-raise position {expected:F4}");
                if (lowFpsMiddle is float baseline)
                    check(Math.Abs(middle - baseline) < .001f, $"{context}: same position as 30 FPS");
                lowFpsMiddle = middle;
                int durationFrames = (int)MathF.Ceiling(fps / rate);
                float beforeRaised = Advance(fps, durationFrames - 1, true);
                float raised = Advance(fps, 1, true);
                var aimedView = controls.GetView(16f / 9f, 0f);
                check(beforeRaised < 1f && raised > .9999f && Math.Abs(aimedView.ScopeLensPower - expectedLens) < .001f &&
                    Math.Abs(aimedView.HorizontalFov - hip.HorizontalFov) < .001f,
                    $"{context}: fully raised by frame {durationFrames}, sight power {expectedPower:F2}x");
                check(Advance(fps, durationFrames, false) < .0001f, $"{context}: fully lowered within the same duration");
            }
        }
        Equip(new M4A1S());
        Advance(120, 1, false);
        float firstGun = Advance(120, 90, true);
        Equip(new DesignatedMarksman());
        float switched = Advance(120, 1, true);
        check(firstGun > .99f && switched < .01f, "switching while RMB is held starts the new gun at hip");
        Advance(120, 1, false);
        check(Advance(120, 90, true) > .99f, "a fresh press raises the replacement gun");
        Advance(120, 120, false);
    }
}
