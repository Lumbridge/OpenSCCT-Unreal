using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Controls;
using OpenScct.UnrealBridge.Events;
using OpenScct.UnrealBridge.Loadout;
using OpenScctGame.Play.Session;

// The console check of the merc's frag: how long after the throw it goes off and how far it flies, on the host (the
// fuse that counts), on the client (what the player's machine simulates) and in the effect list Unreal draws from.
// Every throw is made twice, each in a match of its own, from the same place with the same aim:
//   "unreal": a bridge session with the right mouse button held through the input mapper, as Unreal sends the devices;
//   "godot" : the play session alone, the class the Godot client's PlayRoot runs, stepped by its own Update with the
//             alternate fire button and command set on its input as the Godot client's PlayInput sets them.
// The throws: a tap, a short hold and a full charge along the level, then taps and a hold aimed at the floor (some
// of those go off early by the simulation's own bounce rule; the check is that both paths do so alike).
// Arguments: content root, map, frame rate (default 120), bindings file ("-" for the default bindings), merc weapon
// ("-" for the stock rifle; a class path such as Svm.AK47 also turns the match's other rifles and sidearms on),
// bots (default 0).

string content = args.Length > 0 ? args[0] : DevPaths.ContentRoot();
string map = args.Length > 1 ? args[1] : "AquaE";
double fps = args.Length > 2 ? double.Parse(args[2], System.Globalization.CultureInfo.InvariantCulture) : 120;
string? bindingsFile = args.Length > 3 && args[3] != "-" ? args[3] : null;
string? weapon = args.Length > 4 && args[4] != "-" ? args[4] : null;
int bots = args.Length > 5 ? int.Parse(args[5], System.Globalization.CultureInfo.InvariantCulture) : 0;

int failures = 0;
void Check(bool ok, string what)
{
    Console.WriteLine($"   {(ok ? "ok    " : "FAILED")} {what}");
    failures += ok ? 0 : 1;
}

BridgeSession.Configure(content);
LoadoutServices.Hook();
ControlsStore.Settings = new ControlSettings();
ControlsStore.Bindings = bindingsFile is null ? new ControlBindings() : ControlBindings.Load(bindingsFile);
Console.WriteLine($"Merc RightMouse binding: {(ControlsStore.Bindings.Merc.Keys.Bindings.TryGetValue("RightMouse", out string? rm) ? rm : "(none)")}");
LoadoutChoice kit = LoadoutStore.Fresh();
kit.MercGadgets = ["SBase.SGadFragGrenade", "SBase.SGadMultiMinesLauncher", "SBase.SGadGasMask", "SBase.SGadBackPack"];
if (weapon is not null)
{
    kit.MercWeapon = weapon;
}

LoadoutStore.Set(kit);

// Yaw is turned a twelfth of a circle from the spawn's first: on AquaE that way is open for 850 uu.
const int YawTurn = 5460;
(string Name, int Pitch, double Hold)[] throws =
[
    ("level tap 0.05 s", 0, 0.05),
    ("level hold 0.40 s", 0, 0.40),
    ("level full charge 1.60 s", 0, 1.60),
    ("22 deg down tap", -4000, 0.05),
    ("38 deg down tap", -7000, 0.05),
    ("40 deg down tap", -7280, 0.05),
    ("44 deg down tap", -8000, 0.05),
    ("44 deg down hold 0.40 s", -8000, 0.40),
    ("49 deg down tap", -9000, 0.05),
    ("66 deg down tap", -12000, 0.05),
];

var results = new List<Throw>();
foreach ((string name, int pitch, double hold) in throws)
{
    Console.WriteLine();
    Console.WriteLine($"== {name} ==");
    foreach (bool unreal in new[] { true, false })
    {
        var match = new LocalMatch
        {
            Map = map, Mode = "Story", Merc = true, Bots = bots, OfflineRules = true, UnlimitedGear = true,
            Sidearms = weapon is not null, Rifles = weapon is not null, ResponsiveSmoke = weapon is not null,
        };
        using Rig rig = unreal ? Rig.Unreal(match, fps) : Rig.Godot(match, fps);
        if (!rig.WaitForWeapon())
        {
            Console.WriteLine($"FAILED: no armed local pawn on the {rig.Path} path" + (rig.Failure is { } f ? ": " + f : ""));
            return 1;
        }

        rig.Run(1.0);
        SBasePawn pawn = rig.Session.LocalController!.SBPawn!;
        for (int slot = 0; slot < 8; slot++)
        {
            if (PawnInventory.GadgetInSlot(pawn, slot) is SGadFragGrenade)
            {
                rig.Session.Input.Commands.Add(new InputCommand("SelectGadget", slot));
            }
        }

        // The rifle finishes its draw before it throws.
        rig.Run(2.5);
        if (pawn.SelectedGadget is not SGadFragGrenade frag || frag.ProjectileDefaults is not { } defaults)
        {
            Console.WriteLine($"FAILED: no frag in the merc's hand on the {rig.Path} path (selected {pawn.SelectedGadget?.GetType().Name})");
            return 1;
        }

        if (results.Count < 2)
        {
            Console.WriteLine($"   {rig.Path}: {map}, {bots} bots, practice rules; weapon {pawn.MyWeapon?.GetType().Name}; {frag.GetType().Name} charge {frag.Charge} refire {frag.RefireRate} s; {defaults.GetType().Name} TempsExplosion {defaults.TempsExplosion} s, ProjectileSpeed {defaults.ProjectileSpeed}, rebound {defaults.WallReboundFactor}; tap window {K.ThrowTapWindow} s, ramp {K.ThrowRamp} s");
            rig.MeasureClock(3.0);
        }

        rig.Turn(YawTurn, pitch);
        Throw t = rig.ThrowFrag(name, hold);
        t.Unreal = unreal;
        t.Fuse = defaults.TempsExplosion;
        results.Add(t);
        t.Print(rig.Path);
        failures += rig.Failure is null ? 0 : 1;
    }
}

Console.WriteLine();
Console.WriteLine("== Verdict ==");
int early = 0;
for (int i = 0; i + 1 < results.Count; i += 2)
{
    Throw u = results[i], g = results[i + 1];
    bool same = u.HostSeen && g.HostSeen && MathF.Abs(u.HostFuse - g.HostFuse) <= 0.05f && MathF.Abs(u.HostLaunchSpeed - g.HostLaunchSpeed) <= 60f &&
        MathF.Abs(u.HostReach - g.HostReach) <= 40f;
    Check(same, $"{u.Name}: unreal {u.HostFuse:F3} s, {u.HostLaunchSpeed:F0} uu/s, {u.HostReach:F0} uu; godot {g.HostFuse:F3} s, {g.HostLaunchSpeed:F0} uu/s, {g.HostReach:F0} uu");
    if (u.HostSeen && u.HostFuse < u.Fuse - 0.1f)
    {
        // SProjGrenade.HitWall (the simulation, grenade-throw.md rule 10): at a hit on the level's own geometry a
        // line is traced from the grenade along one second of its velocity, and a line that hits nothing sets the
        // grenade off. A frag that has nearly come to rest on a floor meets the floor at under 2 uu/s (its collision
        // half height), so that line ends above the floor. Both clients run that rule.
        early++;
        Console.WriteLine($"          (went off {u.Fuse - u.HostFuse:F2} s before its {u.Fuse} s fuse{(g.HostFuse < g.Fuse - 0.1f ? " on both paths" : " on the unreal path only")}: the bounce rule of the simulation, the host's last ticks are printed above)");
    }

    Check(u.DrawnFirstReal > 0 && Math.Abs((u.DrawnExplosionReal - u.DrawnFirstReal) - (u.ProjGoneReal - u.ProjReal)) < 0.05,
        $"{u.Name}: Unreal's effect list shows the frag for {u.DrawnExplosionReal - u.DrawnFirstReal:F3} s, the client's world held it {u.ProjGoneReal - u.ProjReal:F3} s");
}

Console.WriteLine($"{results.Count / 2} throws on each path, {early} went off before the fuse on the unreal path");
Console.WriteLine(failures == 0 ? "ALL OK" : $"{failures} FAILED");
return failures == 0 ? 0 : 1;

internal sealed class Throw
{
    public string Name = "";
    public bool Unreal;
    public int Fuse;
    public FRotator Aim;
    public FVector From;
    public double PressReal, ReleaseReal, PressSim, ReleaseSim, HeldSim;
    public float Value;

    // Host (its own thread): the projectile from the tick it exists to the tick it is gone.
    public bool HostSeen;
    public float HostFuse, HostLaunchSpeed, HostReach, HostPath, HostTimerRate, HostTimerLast;
    public double HostFirstReal, HostGoneReal;
    public List<string> HostLastTicks = new();

    // Client world.
    public bool ClientSeen;
    public double ProjSim, ProjReal, ProjGoneSim, ProjGoneReal, ExplosionSim = double.NaN, ExplosionReal = double.NaN;
    public float ClientReach;

    // The effect list Unreal draws from, read every frame (the unreal path only).
    public double DrawnFirstReal, DrawnGoneReal, DrawnExplosionReal = double.NaN;
    public float DrawnReach;

    public void Print(string path)
    {
        Console.WriteLine($"   {path}: from ({From.X:F0}, {From.Y:F0}, {From.Z:F0}) yaw {Aim.Yaw & 65535} pitch {(short)Aim.Pitch}; button held {ReleaseReal - PressReal:F3} real s, charged {HeldSim:F3} sim s, gauge at release {Value:F2}");
        Console.WriteLine(HostSeen
            ? $"      host  : frag lived {HostFuse:F3} sim s ({HostGoneReal - HostFirstReal:F3} real s), fuse timer {HostTimerLast:F3} of {HostTimerRate:F0} s when it went; launch {HostLaunchSpeed:F0} uu/s; went off {HostReach:F0} uu from the launch point after a path of {HostPath:F0} uu"
            : "      host  : no frag was seen");
        if (HostSeen && HostFuse < Fuse - 0.1f)
        {
            foreach (string row in HostLastTicks)
            {
                Console.WriteLine("         " + row);
            }
        }

        Console.WriteLine(ClientSeen
            ? $"      client: frag appeared {ProjReal - ReleaseReal:F3} real s after the release, lived {ProjGoneSim - ProjSim:F3} sim s ({ProjGoneReal - ProjReal:F3} real s), reach {ClientReach:F0} uu; explosion {ExplosionSim - ReleaseSim:F3} sim s / {ExplosionReal - ReleaseReal:F3} real s after the release"
            : "      client: no frag was seen");
        if (DrawnFirstReal > 0)
        {
            Console.WriteLine($"      drawn : frag listed {DrawnFirstReal - ReleaseReal:F3} real s after the release for {DrawnGoneReal - DrawnFirstReal:F3} real s, reach {DrawnReach:F0} uu; explosion listed {DrawnExplosionReal - ReleaseReal:F3} real s after the release");
        }
    }
}

/// <summary>One match and the frames that drive it, on the Unreal path (bridge and mapper) or the Godot path (the play session alone).</summary>
internal sealed class Rig : IDisposable
{
    private readonly Stopwatch _clock = Stopwatch.StartNew();
    private readonly double _pace;
    private readonly BridgeSession? _bridge;
    private readonly ControlsFeature? _controls;
    private readonly EventsFeature? _events;
    private readonly BridgeEffectActor[] _effects = new BridgeEffectActor[512];
    private readonly ConcurrentQueue<(int Id, float Time, double Real, FVector At, FVector Velocity, float TimerRate, float TimerCounter)> _host = new();
    private readonly int _fragClass = EventNames.Id("SBase.SProjFrag"), _explosionClass = EventNames.Id("SBase.SGrenadeExplosion");
    private readonly HashSet<int> _oldExplosions = new();
    private double _last;
    private bool _held, _sent, _wasUsing;
    private Throw? _throw;
    private SProjGrenade? _clientFrag;
    private FVector _clientStart, _drawnStart;
    private int _drawnId;
    private long _ticks;
    private float _hostAt, _mouseX, _mouseY;
    private double _hostReal;

    private Rig(PlaySession session, BridgeSession? bridge, double fps)
    {
        Session = session;
        _bridge = bridge;
        _pace = 1.0 / fps;
        if (bridge is not null)
        {
            _controls = ControlsFeature.Of(bridge);
            _events = EventsFeature.Of(bridge);
            _events.Quiet = true;
            bridge.Ticked += OnTick;
        }
        else
        {
            // PlayRoot's BeforeTick and AfterTick: the input of the tick (PlayInput.FillTick: the button while the
            // key is down, the exec command once on the press, the mouse counts), then the tick's result.
            session.BeforeClientTick = (_, _) =>
            {
                LocalInput input = session.Input;
                input.Set("bAltFire", _held);
                if (_held && !_sent)
                {
                    input.Commands.Add(new InputCommand("AltFire", 0));
                }

                _sent = _held;
            };
            session.AfterClientTick = (_, _) => OnTick();
        }

        // On the host's thread, before each of its ticks: its clock and every grenade it holds.
        session.Host.BeforeWorldTick += host =>
        {
            Level level = host.World.Level;
            Volatile.Write(ref _hostAt, level.TimeSeconds);
            Volatile.Write(ref _hostReal, _clock.Elapsed.TotalSeconds);
            foreach (SProjGrenade g in level.AllActors<SProjGrenade>())
            {
                if (!g.bDeleteMe)
                {
                    _host.Enqueue((g.ActorId, level.TimeSeconds, _clock.Elapsed.TotalSeconds, g.Location, g.Velocity, g.TimerRate, g.TimerCounter));
                }
            }
        };
    }

    public PlaySession Session { get; }

    public string Path => _bridge is not null ? "unreal" : "godot ";

    public string? Failure => _bridge is not null ? _bridge.Failure : Session.ServerFailure?.ToString() ?? Session.LoginError;

    private double SimNow => Session.Client.World.Level.TimeSeconds;

    public static Rig Unreal(LocalMatch match, double fps)
    {
        BridgeSession bridge = BridgeSession.StartLocal(match);
        return new Rig(bridge.Session, bridge, fps);
    }

    /// <summary>The play session as game/Scripts/Play/PlayRoot.cs makes it for a practice match (--practice).</summary>
    public static Rig Godot(LocalMatch match, double fps)
    {
        var settings = new PlaySettings
        {
            Map = match.Map,
            Side = match.Merc ? PlaySide.Merc : PlaySide.Spy,
            Mode = match.Mode,
            Rules = PlayRules.MapSettings,
            HostMode = PlayHostMode.Thread,
            Bots = match.Bots,
            Difficulty = match.Difficulty,
            ResponsiveSmoke = match.ResponsiveSmoke,
            Sidearms = match.Sidearms,
            Rifles = match.Rifles,
            UnlimitedGear = match.OfflineRules,
            OfflineRules = match.OfflineRules,
        };
        return new Rig(new PlaySession(settings, new ProbeClock()), null, fps);
    }

    public void Dispose()
    {
        if (_bridge is not null)
        {
            _bridge.Dispose();
        }
        else
        {
            Session.Dispose();
        }
    }

    public bool WaitForWeapon()
    {
        double until = _clock.Elapsed.TotalSeconds + 20;
        while (_clock.Elapsed.TotalSeconds < until && Failure is null)
        {
            Frame();
            Thread.Sleep(2);
            if (Session.LocalController?.SBPawn is { WeaponLoaded: not 0, MyWeapon: not null, MyGadget: not null })
            {
                return true;
            }
        }

        return false;
    }

    public void Run(double seconds)
    {
        double until = _clock.Elapsed.TotalSeconds + seconds;
        do
        {
            Paced();
        }
        while (_clock.Elapsed.TotalSeconds < until);
    }

    private void Paced()
    {
        double next = _clock.Elapsed.TotalSeconds + _pace;
        Frame();
        while (_clock.Elapsed.TotalSeconds < next)
        {
            Thread.SpinWait(50);
        }
    }

    /// <summary>Simulated seconds against real seconds, on the client and on the host.</summary>
    public void MeasureClock(double seconds)
    {
        Run(0.2);
        double sim0 = SimNow, real0 = _clock.Elapsed.TotalSeconds;
        long ticks0 = _ticks;
        float host0 = Volatile.Read(ref _hostAt);
        double hostReal0 = Volatile.Read(ref _hostReal);
        Run(seconds);
        double sim1 = SimNow, real1 = _clock.Elapsed.TotalSeconds;
        float host1 = Volatile.Read(ref _hostAt);
        double hostReal1 = Volatile.Read(ref _hostReal);
        Console.WriteLine($"   {Path}: clock: client {sim1 - sim0:F3} sim s in {real1 - real0:F3} real s ({_ticks - ticks0} ticks, {(sim1 - sim0) / (real1 - real0):F3} x); " +
            $"host {host1 - host0:F3} sim s in {hostReal1 - hostReal0:F3} real s ({(host1 - host0) / (hostReal1 - hostReal0):F3} x); time dilation {Session.Client.World.Level.Info.TimeDilation}");
    }

    /// <summary>Turns the view by rotation units through the mouse, as the player does.</summary>
    public void Turn(int yaw, int pitch)
    {
        const int Frames = 20;
        _mouseX = yaw / MouseLook.UnitsPerCount / Frames;
        _mouseY = pitch / MouseLook.UnitsPerCount / Frames;
        for (int i = 0; i < Frames; i++)
        {
            Paced();
        }

        _mouseX = _mouseY = 0f;
        Run(0.3);
    }

    public Throw ThrowFrag(string name, double hold)
    {
        while (_host.TryDequeue(out _))
        {
        }

        _oldExplosions.Clear();
        foreach (SGrenadeExplosion e in Session.Client.World.Level.AllActors<SGrenadeExplosion>())
        {
            _oldExplosions.Add(e.ActorId);
        }

        SPlayerController pc = Session.LocalController!;
        var t = new Throw { Name = name, Aim = pc.Rotation, From = pc.SBPawn?.Location ?? default };
        _clientFrag = null;
        _drawnId = 0;
        _wasUsing = false;
        _throw = t;
        t.PressReal = _clock.Elapsed.TotalSeconds;
        _held = true;
        Run(hold);
        _held = false;
        t.ReleaseReal = _clock.Elapsed.TotalSeconds;
        Run(3.0);
        _throw = null;

        // The host's record of the first grenade it made.
        var rows = new List<(int Id, float Time, double Real, FVector At, FVector Velocity, float TimerRate, float TimerCounter)>();
        while (_host.TryDequeue(out var row))
        {
            rows.Add(row);
        }

        if (rows.Count > 0)
        {
            int id = rows[0].Id;
            rows = rows.Where(r => r.Id == id).ToList();
            t.HostSeen = true;

            // Seen before a tick: first seen one tick after it was made, last seen one tick before it went.
            t.HostFuse = rows[^1].Time - rows[0].Time + (2f * Sim.DeltaTime);
            t.HostFirstReal = rows[0].Real;
            t.HostGoneReal = rows[^1].Real + (2.0 * Sim.DeltaTime);
            t.HostLaunchSpeed = rows[0].Velocity.Size();
            t.HostReach = (rows[^1].At - rows[0].At).Size();
            t.HostTimerRate = rows[^1].TimerRate;
            t.HostTimerLast = rows[^1].TimerCounter;
            for (int i = 1; i < rows.Count; i++)
            {
                t.HostPath += (rows[i].At - rows[i - 1].At).Size();
            }

            for (int i = Math.Max(0, rows.Count - 5); i < rows.Count; i++)
            {
                var r = rows[i];
                t.HostLastTicks.Add($"host tick -{rows.Count - 1 - i}: at ({r.At.X:F1}, {r.At.Y:F1}, {r.At.Z:F1}) velocity ({r.Velocity.X:F1}, {r.Velocity.Y:F1}, {r.Velocity.Z:F1}) = {r.Velocity.Size():F1} uu/s, fuse timer {r.TimerCounter:F3}");
            }
        }

        return t;
    }

    private void OnTick()
    {
        _ticks++;
        if (_throw is not { } t || Session.LocalController is not { } pc)
        {
            return;
        }

        double now = _clock.Elapsed.TotalSeconds, sim = SimNow;
        Level level = Session.Client.World.Level;
        if (pc.bUsingNow && !_wasUsing)
        {
            t.PressSim = pc.TimerUSe;
        }

        if (!pc.bUsingNow && _wasUsing)
        {
            t.ReleaseSim = sim;
            t.HeldSim = sim - t.PressSim;
            t.Value = WeaponRules.ThrowValue((float)t.HeldSim, true, K.ThrowTapWindow, K.ThrowRamp);
        }

        _wasUsing = pc.bUsingNow;
        if (_clientFrag is null && !t.ClientSeen)
        {
            foreach (SProjGrenade g in level.AllActors<SProjGrenade>())
            {
                if (!g.bDeleteMe)
                {
                    _clientFrag = g;
                    _clientStart = g.Location;
                    t.ClientSeen = true;
                    t.ProjSim = sim;
                    t.ProjReal = now;
                    break;
                }
            }
        }
        else if (_clientFrag is { } frag)
        {
            if (frag.bDeleteMe)
            {
                t.ProjGoneSim = sim;
                t.ProjGoneReal = now;
                _clientFrag = null;
            }
            else
            {
                t.ClientReach = (frag.Location - _clientStart).Size();
            }
        }

        if (double.IsNaN(t.ExplosionSim))
        {
            foreach (SGrenadeExplosion e in level.AllActors<SGrenadeExplosion>())
            {
                if (!e.bDeleteMe && !_oldExplosions.Contains(e.ActorId))
                {
                    t.ExplosionSim = sim;
                    t.ExplosionReal = now;
                    break;
                }
            }
        }
    }

    private unsafe void Frame()
    {
        double now = _clock.Elapsed.TotalSeconds;
        if (_bridge is null)
        {
            // PlayRoot._Process: the frame's mouse counts go to the input, the session runs the ticks that are due,
            // and the drawn frame is reported after it.
            Session.Input.MouseX += _mouseX;
            Session.Input.MouseY += _mouseY;
            Session.Update();
            Session.Client.ReportRenderedFrame(Session.Alpha);
            _last = now;
            return;
        }

        var raw = new BridgeRawInput { DeltaSeconds = (float)(now - _last), Flags = BridgeRawInputFlags.Play, MouseX = _mouseX, MouseY = _mouseY };
        _last = now;
        if (_held)
        {
            raw.Keys[(int)BridgeKey.RightMouse] = 1;
        }

        _controls!.Frame(in raw);
        _bridge.Update();

        // What Unreal reads to draw, once a frame.
        int count = _events!.GetEffectActors(_effects);
        if (_throw is not { } t)
        {
            return;
        }

        now = _clock.Elapsed.TotalSeconds;
        for (int i = 0; i < count; i++)
        {
            ref BridgeEffectActor e = ref _effects[i];
            if (e.ClassId == _fragClass && (e.Flags & BridgeEffectFlags.Deleted) == 0)
            {
                var at = new FVector(e.X, e.Y, e.Z);
                if (_drawnId == 0)
                {
                    _drawnId = e.ActorId;
                    _drawnStart = at;
                    t.DrawnFirstReal = now;
                }

                if (e.ActorId == _drawnId)
                {
                    t.DrawnGoneReal = now;
                    t.DrawnReach = (at - _drawnStart).Size();
                }
            }
            else if (e.ClassId == _explosionClass && double.IsNaN(t.DrawnExplosionReal) && !_oldExplosions.Contains(e.ActorId))
            {
                t.DrawnExplosionReal = now;
            }
        }
    }

    private sealed class ProbeClock : OpenScct.Net.IClock
    {
        private readonly long _start = Stopwatch.GetTimestamp();

        public long NowMicroseconds => (long)Stopwatch.GetElapsedTime(_start).TotalMicroseconds;
    }
}
