using System;
using System.Diagnostics;
using OpenScct.Core;
using OpenScct.Game.Camera;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Pose;

// Usage: CameraBoneProbe [contentRoot] [map]
// Hosts the map twice as a mercenary with no bots: once with the camera's bone service (Pose/PoseCameraBones.cs) and
// once with its answers switched off. Each round plays 2 s as it comes (ordinary play: the view must sit on the
// cylinder's eye, the camera not on a bone), then the local controller is knocked down backwards (s_FallDown, which attaches
// the camera to 'B Head' and lies the merc on the ground) and the first-person view is compared with the head bone
// of the pose and with the cylinder's eye for 3 s. With the service the view must be on the head and leave the eye;
// without it the view must stay on the eye. Exit code 1 on a failure.
string content = args.Length > 0 ? args[0] : DevPaths.ContentRoot();
string map = args.Length > 1 ? args[1] : "AquaE";
BridgeSession.Configure(content);

Round with = Play(off: false);
Round without = Play(off: true);
bool ok = true;
ok &= Check("ordinary play, service on: camera not on a bone", !with.OrdinaryAttached);
ok &= Check($"ordinary play, service on: view on the cylinder's eye (farthest {with.OrdinaryFromEye:F3} uu)", with.OrdinarySamples > 0 && with.OrdinaryFromEye < 0.01f);
ok &= Check($"ordinary play, service off: view on the cylinder's eye (farthest {without.OrdinaryFromEye:F3} uu)", without.OrdinarySamples > 0 && without.OrdinaryFromEye < 0.01f);
ok &= Check("knocked down, service on: camera attached to 'B Head'", with.Attached);
ok &= Check($"knocked down, service on: head lookups answered ({with.Answered})", with.Answered > 0);
ok &= Check($"knocked down, service on: view on the head bone (nearest {with.NearestHead:F3} uu, farthest {with.FarthestHead:F3} uu)",
    with.NearestHead < 0.01f && with.FarthestHead < 20f);
ok &= Check($"knocked down, service on: view left the cylinder's eye (by up to {with.FarthestEye:F1} uu)", with.FarthestEye > 30f);
ok &= Check("knocked down, service off: camera attached to 'B Head'", without.Attached);
ok &= Check($"knocked down, service off: no lookup answered ({without.Answered})", without.Answered == 0);
ok &= Check($"knocked down, service off: view stays on the cylinder's eye (farthest {without.FarthestEye:F3} uu)", without.FarthestEye < 0.01f);
Console.WriteLine(ok ? "PASSED" : "FAILED");
return ok ? 0 : 1;

static bool Check(string what, bool passed)
{
    Console.WriteLine($"  [{(passed ? "ok" : "FAIL")}] {what}");
    return passed;
}

Round Play(bool off)
{
    PoseCameraBones.Off = off;
    long answeredAtStart = PoseCameraBones.Answered;
    var round = new Round { NearestHead = float.MaxValue };
    Console.WriteLine($"--- {map} as a mercenary, bone service {(off ? "off" : "on")}");
    using BridgeSession session = BridgeSession.StartLocal(new LocalMatch { Map = map, Mode = "Story", Merc = true, Bots = 0 });
    var head = new Name("B Head");
    var run = Stopwatch.StartNew();
    double spawned = -1, knocked = -1, printed = 0;
    long lastTick = -1;
    while (true)
    {
        // The head as the next tick's calc view will find it: the pose of the last tick (nothing draws here).
        SBasePawn? before = session.Session.LocalPawn;
        bool hadHead = false;
        FVector headBefore = default;
        if (before is not null && session.PoserOf(before.ActorId) is { } poser && ReferenceEquals(poser.Pawn, before))
        {
            hadHead = poser.TryGetBone(head, out headBefore, out _);
        }

        var input = default(BridgeInput);
        session.SetInput(in input);
        session.Update();
        if (session.Failure is { } failure)
        {
            Console.WriteLine("FAILED: " + failure);
            Environment.Exit(1);
        }

        double now = run.Elapsed.TotalSeconds;
        SPlayerController? pc = session.Session.LocalController;
        SBasePawn? pawn = session.Session.LocalPawn;
        var view = session.Session.View;
        if (pc is null || pawn is null || !view.Valid || !ReferenceEquals(pawn, before))
        {
            if (now > 30)
            {
                Console.WriteLine("FAILED: no pawn within 30 s");
                Environment.Exit(1);
            }

            System.Threading.Thread.Sleep(5);
            continue;
        }

        if (spawned < 0)
        {
            spawned = now;
        }

        bool newTick = view.Tick != lastTick;
        bool oneTick = view.Tick == lastTick + 1;
        lastTick = view.Tick;
        SPlayerCam? camera = PlayerCamera.Of(pc);
        FVector eye = pawn.Location + pawn.WalkBob + new FVector(0f, 0f, pawn.EyeHeight) + pc.ShakeOffset;
        float fromEye = FVector.Dist(view.Location, eye);
        float fromHead = hadHead ? FVector.Dist(view.Location, headBefore + pc.ShakeOffset) : float.NaN;
        if (knocked < 0)
        {
            // Ordinary play, from half a second after the spawn (the spawn's own view settles first).
            if (newTick && now - spawned > 0.5)
            {
                round.OrdinarySamples++;
                round.OrdinaryFromEye = MathF.Max(round.OrdinaryFromEye, fromEye);
                round.OrdinaryAttached |= camera?.m_bAttachToBone == true;
            }

            if (now - spawned > 2)
            {
                knocked = now;
                Console.WriteLine($"  ordinary play: {round.OrdinarySamples} ticks, state {pc.GetStateName()}, behind view {pc.bBehindView}, " +
                    $"view {Show(view.Location)} eye {Show(eye)} head {(hadHead ? Show(headBefore) : "none")}");
                CameraBoneExports.Knock(session);
            }
        }
        else if (newTick)
        {
            round.Attached |= camera?.m_bAttachToBone == true;
            if (camera?.m_bAttachToBone == true && now - knocked > 0.2)
            {
                round.FarthestEye = MathF.Max(round.FarthestEye, fromEye);
                if (hadHead && oneTick)
                {
                    round.NearestHead = MathF.Min(round.NearestHead, fromHead);
                    round.FarthestHead = MathF.Max(round.FarthestHead, fromHead);
                }
            }

            if (now - printed > 0.5)
            {
                printed = now;
                Console.WriteLine($"  {now - knocked:F1} s after the knock: state {pc.GetStateName()}, attached {camera?.m_bAttachToBone} to '{camera?.m_parentBone}', " +
                    $"behind view {pc.bBehindView}, view {Show(view.Location)} rot {view.Rotation.Pitch},{view.Rotation.Yaw},{view.Rotation.Roll}, " +
                    $"head {(hadHead ? Show(headBefore) : "none")}, from head {fromHead:F2}, from eye {fromEye:F2}");
            }

            if (now - knocked > 3)
            {
                break;
            }
        }

        System.Threading.Thread.Sleep(5);
    }

    round.Answered = PoseCameraBones.Answered - answeredAtStart;
    PoseCameraBones.Off = false;
    return round;
}

static string Show(FVector v) => $"({v.X:F1}, {v.Y:F1}, {v.Z:F1})";

internal sealed class Round
{
    public int OrdinarySamples;
    public float OrdinaryFromEye;
    public bool OrdinaryAttached;
    public bool Attached;
    public long Answered;
    public float NearestHead;
    public float FarthestHead;
    public float FarthestEye;
}
