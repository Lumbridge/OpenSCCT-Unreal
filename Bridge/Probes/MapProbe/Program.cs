using System;
using System.Diagnostics;
using System.IO;
using OpenScct.UnrealBridge;

// The console check of the strategic map's table (Bridge/OpenScct.UnrealBridge/Map/MapExports.cs): the sizes the C++
// mirror asserts; a lobby's overview of a map (its floors and pictures, the legend, the places, Up and Down, no
// markers); then a local round, where the page opens on the player's floor with his marker on the picture and his
// team mates' markers from their PRIs, and the blink and the markers follow the half-second timer.
// Arguments: content folder, map, bots.

string content = args.Length > 0 ? args[0] : OpenScct.UnrealBridge.DevPaths.ContentRoot();
string map = args.Length > 1 ? args[1] : "AquaE";
int bots = args.Length > 2 ? int.Parse(args[2], System.Globalization.CultureInfo.InvariantCulture) : 3;

int failures = 0;
void Check(bool ok, string what)
{
    Console.WriteLine($"   {(ok ? "ok    " : "FAILED")} {what}");
    failures += ok ? 0 : 1;
}

unsafe
{
    Console.WriteLine("Sizes:");
    Check(sizeof(MapApi) == 88, $"MapApi {sizeof(MapApi)}");
    Check(sizeof(BridgeMapState) == 1092, $"BridgeMapState {sizeof(BridgeMapState)}");
    Check(sizeof(BridgeMapPlace) == 16, $"BridgeMapPlace {sizeof(BridgeMapPlace)}");
}

BridgeSession.Configure(content);
Probe.Run(content, map, bots, Check);
Console.WriteLine(failures == 0 ? "ALL OK" : $"{failures} FAILED");
return failures == 0 ? 0 : 1;

internal static unsafe class Probe
{
    private static MapApi s_api;

    public static void Run(string content, string map, int bots, Action<bool, string> check)
    {
        delegate* unmanaged<MapApi*, int> getApi = &MapExports.GetApi;
        MapApi api;
        int version = getApi(&api);
        s_api = api;
        check(version == MapExports.ApiVersion && api.Version == version, $"GetApi version {version}");

        // ---- the lobby's overview ----------------------------------------------------------------------------------
        Console.WriteLine($"The lobby's overview of {map}:");
        check(Open(1, "NoSuchMap") == 0 && Error().Contains("NoSuchMap"), $"a map not in the content is refused: \"{Error()}\"");
        check(Open(1, map) == 1, $"Open(lobby, {map})");
        BridgeMapState s;
        api.GetState(&s);
        string picture = new(s.Picture);
        Console.WriteLine($"   floors {s.FloorCount}, shown {s.Floor}, camera {s.HasCamera}, live {s.Live}, picture \"{picture}\", places {s.PlaceCount}");
        check(s.FloorCount >= 1 && s.HasPicture == 1 && File.Exists(picture), "the first floor's picture is there");
        check(s.Live == 0 && s.YouShown == 0 && s.HasYou == 0 && s.MateCount == 0, "no markers in the lobby");
        string legend = Legend();
        Console.WriteLine($"   legend: {legend.Replace("\n", " | ")}");
        check(legend.Length > 0, "the legend is read from the map's mapinfo");
        int places = api.ListPlaces(null, 0);
        var list = new BridgeMapPlace[Math.Max(places, 1)];
        fixed (BridgeMapPlace* p = list)
        {
            api.ListPlaces(p, list.Length);
        }

        for (int i = 0; i < places; i++)
        {
            Console.WriteLine($"      place {list[i].Number} on floor {list[i].Floor} at U {list[i].U:0.000} V {list[i].V:0.000}");
        }

        check(api.Down() == 0, "Down on the lowest floor does nothing");
        if (s.FloorCount > 1)
        {
            check(api.Up() == 1, "Up goes to the floor above");
            api.GetState(&s);
            check(s.Floor == 1 && new string(s.Picture).EndsWith("floor1.png", StringComparison.Ordinal), $"floor 2's picture: {new string(s.Picture)}");
            check(api.Down() == 1, "Down goes back");
        }

        api.Close();

        // ---- a local round -----------------------------------------------------------------------------------------
        delegate* unmanaged<BridgeApi*, int, int> getBridge = &Exports.GetApiSized;
        BridgeApi bridge;
        getBridge(&bridge, sizeof(BridgeApi));
        int started;
        fixed (char* c = content, m = map, mode = "Story")
        {
            started = bridge.Configure(c) != 0 ? bridge.StartLocal(m, mode, 0, bots) : 0;
        }

        check(started != 0, $"a local match on {map} as a spy with {bots} bots");
        if (started == 0)
        {
            return;
        }

        var run = Stopwatch.StartNew();
        while (run.Elapsed.TotalSeconds < 5)
        {
            bridge.Update();
            System.Threading.Thread.Sleep(5);
        }

        Console.WriteLine("In the round:");
        check(api.CanDisplay() == 1, "the DisplayStrategicMap key may open it");
        check(Open(0, "") == 1, "Open(round)");
        // The mates' positions are the answer to the first ask: give the server a frame, then the timer asks again.
        bridge.Update();
        api.Tick();
        api.GetState(&s);
        Console.WriteLine($"   you at ({s.YouX:0}, {s.YouY:0}, {s.YouZ:0}) yaw {s.YouYaw}, floor {s.YouFloor}; shown floor {s.Floor}; " +
            $"marker {s.YouShown} U {s.YouU:0.000} V {s.YouV:0.000} angle {s.YouAngle:0.00}; mates {s.MateCount}, shown {s.MatesShown}");
        if (Exports.Current?.Session.LocalController is { } pc)
        {
            foreach (OpenScctGame.UI.StrategicMapPosition mate in StrategicMapPage.Mates(pc))
            {
                Console.WriteLine($"      a mate at ({mate.Location.X:0}, {mate.Location.Y:0}, {mate.Location.Z:0}), floor " +
                    $"{StrategicMapPage.Model!.Data.FloorAt(mate.Location.Z) + 1}, on the picture at {StrategicMapPage.Model.Data.Project(mate.Location)}");
            }
        }

        for (int i = 0; i < BridgeMapState.MaxMates; i++)
        {
            if (s.MateShown[i] != 0)
            {
                Console.WriteLine($"      mate {i}: U {s.MateU[i]:0.000} V {s.MateV[i]:0.000} angle {s.MateAngle[i]:0.00}");
            }
        }

        check(s.Live == 1 && s.HasYou == 1 && s.Floor == s.YouFloor, "the page opens on the player's floor");
        check(s.YouShown == 1 && s.YouU is >= 0f and <= 1f && s.YouV is >= 0f and <= 1f, "the you-are-here marker is on the picture");
        check(bots < 3 || (s.MateCount >= 1 && s.MatesShown >= 1), $"{s.MateCount} team mate(s) placed, {s.MatesShown} on this floor (bots of a local match too)");
        int bright = s.Bright;
        api.Tick();
        api.GetState(&s);
        check(s.Bright != bright, "the timer blinks the markers");
        if (s.FloorCount > 1)
        {
            bool moved = s.Floor < s.FloorCount - 1 ? api.Up() == 1 : api.Down() == 1;
            api.GetState(&s);
            check(moved && s.Floor != s.YouFloor && s.YouShown == 0, $"on floor {s.Floor + 1} the player's marker is not shown (he is on {s.YouFloor + 1})");
        }

        api.Close();
        bridge.Stop();
    }

    private static int Open(int context, string map)
    {
        fixed (char* m = map)
        {
            return s_api.Open(context, m);
        }
    }

    private static string Legend()
    {
        char* buffer = stackalloc char[2048];
        s_api.GetLegend(buffer, 2048);
        return new string(buffer);
    }

    private static string Error()
    {
        char* buffer = stackalloc char[1024];
        s_api.LastError(buffer, 1024);
        return new string(buffer);
    }
}
