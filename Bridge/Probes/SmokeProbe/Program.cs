using System;
using System.Diagnostics;
using OpenScct.Core;
using OpenScct.Core.Engine;
using OpenScct.Game.Equipment;
using OpenScct.Game.SBase;
using OpenScct.Server;
using OpenScct.UnrealBridge;

// Usage: SmokeProbe [contentRoot] [map] [seconds] [off]
// Hosts the map as a spy with responsive smoke on (unless 'off'), pops a smoke grenade ahead of the player at 2 s
// (WorldDebugExports.PopSmoke, on the SERVER's world) and at 5 s pushes a bullet through the cloud's middle on the
// server (ResponsiveSmokeWorld.Bullet, what a gun's trace does). What is printed is what reached the client: the
// smoke state, the cloud's box and its density grid at the three quality sizes with the time each takes to sample,
// and the density along the bullet's line before and after it.
string content = args.Length > 0 ? args[0] : OpenScct.UnrealBridge.DevPaths.ContentRoot();
string map = args.Length > 1 ? args[1] : "AquaE";
double seconds = args.Length > 2 ? double.Parse(args[2], System.Globalization.CultureInfo.InvariantCulture) : 9;
bool on = !(args.Length > 3 && args[3] == "off");

BridgeSession.Configure(content);
var match = new LocalMatch { Map = map, Mode = "Story", Merc = false, Bots = 0 };
using BridgeSession session = BridgeSession.StartLocal(on ? match : match with { ResponsiveSmoke = false });
SmokeFeature smoke = SmokeFeature.Of(session);
var fields = new BridgeSmokeField[8];
var grid = new byte[64 * 64 * 64];
bool popped = false, shot = false;
double nextReport = 0;
FVector shotStart = default, shotEnd = default;
float before = -1f;
int fails = 0;
var holes = new BridgeSmokeHole[32];
int lastOpen = -1;

var run = Stopwatch.StartNew();
while (run.Elapsed.TotalSeconds < seconds)
{
    double t = run.Elapsed.TotalSeconds;
    if (!popped && t > 2 && session.Session.Client.World.Level.Info.GetLocalPlayerController()?.Pawn is { } me)
    {
        popped = true;
        FVector ahead = me.Rotation.Vector();
        ahead.Z = 0f;
        Console.WriteLine($"{t:F1} s: PopSmoke queued {WorldDebugExports.PopSmoke(session, me.Location + new FVector(0f, 0f, 40f), ahead.SafeNormal())}");
    }

    var input = default(BridgeInput);
    session.SetInput(in input);
    session.Update();
    if (session.Failure is { } failure)
    {
        Console.WriteLine("FAILED: " + failure);
        return 1;
    }

    BridgeSmokeState state = smoke.State();
    int count = smoke.WriteFields(fields);
    if (!shot && t > 5 && count > 0 && session.Session.Host is { } host)
    {
        shot = true;
        BridgeSmokeField f = fields[0];
        var center = new FVector(f.CenterX, f.CenterY, f.CenterZ);
        shotStart = center + new FVector(-400f, 30f, 0f);
        shotEnd = center + new FVector(400f, 30f, 0f);
        before = LineDensity(session, shotStart, shotEnd);
        FVector a = shotStart, b = shotEnd;
        void Fire(ServerHost running)
        {
            running.BeforeWorldTick -= Fire;
            foreach (SBasePawn pawn in running.World.Level.AllActors<SBasePawn>())
            {
                if (!pawn.bDeleteMe)
                {
                    ResponsiveSmokeWorld.Bullet(pawn, a, b);
                    break;
                }
            }
        }

        host.BeforeWorldTick += Fire;
        Console.WriteLine($"{t:F1} s: bullet through the middle queued, mean density along its line before {before:F3}");
    }

    if (shot && t < 6.2 && count > 0)
    {
        int open = smoke.WriteHoles(fields[0].EmitterId, holes);
        if (open != lastOpen)
        {
            Console.WriteLine($"{t:F2} s: shot holes open {open}" + (open > 0 ? $", newest strength {holes[0].Strength:F2} radius {holes[0].Radius:F0}" : ""));
            lastOpen = open;
        }
    }

    if (shot && before >= 0f && t > 5.3)
    {
        Console.WriteLine($"{t:F1} s: mean density along the bullet's line after {LineDensity(session, shotStart, shotEnd):F3} (before {before:F3})");
        before = -2f;
    }

    if (t >= nextReport)
    {
        nextReport = t + 1.0;
        Console.WriteLine($"{t:F1} s: smoke enabled {state.Enabled}, clouds {count}, time {state.Time:F2}, uploads {state.Uploads}");
        for (int i = 0; i < count; i++)
        {
            BridgeSmokeField f = fields[i];
            string line = $"   cloud #{f.EmitterId} rev {f.Revision} box ({f.OriginX:F0}, {f.OriginY:F0}, {f.OriginZ:F0}) +{f.Size:F0} age {f.Age:F1}/{f.Lifetime:F0} s, holes {f.Carves}:";
            foreach (int n in new[] { 16, 32, 48 })
            {
                if (!smoke.WriteDensity(f.EmitterId, n, grid))
                {
                    fails++;
                    continue;
                }

                int filled = 0, sum = 0, peak = 0;
                for (int c = 0; c < n * n * n; c++)
                {
                    filled += grid[c] > 64 ? 1 : 0;
                    sum += grid[c];
                    peak = Math.Max(peak, grid[c]);
                }

                line += $" {n}^3 {smoke.LastSampleMilliseconds:F1} ms {100.0 * filled / (n * n * n):F1}% over .25 peak {peak / 255f:F2} mean {sum / 255f / (n * n * n):F3};";
            }

            Console.WriteLine(line);
        }
    }
}

Console.WriteLine($"done: uploads {smoke.Uploads}, failed grids {fails}");
if (UcOptions.GetIntOption(session.Session.Loader!.GameOptions, "SvmResponsiveSmoke", -1) != (on ? 1 : 0) ||
    smoke.State().Enabled != (on ? 1 : 0))
{
    Console.WriteLine("FAILED: the host or client used a different responsive smoke rule.");
    return 1;
}

return fails == 0 ? 0 : 1;

static float LineDensity(BridgeSession session, FVector a, FVector b)
{
    ResponsiveSmokeWorld? world = session.Session.Client.World.Level.GetService<ResponsiveSmokeWorld>();
    if (world is null)
    {
        return 0f;
    }

    float sum = 0f;
    const int steps = 64;
    for (int i = 0; i < steps; i++)
    {
        sum += world.SampleDensity(a + (b - a) * ((i + 0.5f) / steps));
    }

    return sum / steps;
}
