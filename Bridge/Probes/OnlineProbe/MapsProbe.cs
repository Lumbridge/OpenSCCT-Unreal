using System;
using System.IO;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Online;

// OnlineProbe maps [content root] [folder of map folders] [map in that folder]
//
// The map folders Unreal names (MapsExports, Online/MapFolders.cs), through the exported table as Unreal calls it:
// a folder given as -ScctMaps is searched before the content, so the map is found there; a map folder named itself
// stands for its parent; the content's own maps folder (TestBox) is searched last and its maps become playable;
// the online table's installed maps and the renderable list carry them; and a local match on the map loads it from
// that folder.
internal static unsafe class MapsProbe
{
    public static int Run(string[] args)
    {
        string content = Path.GetFullPath(args.Length > 0 ? args[0] : OpenScct.UnrealBridge.DevPaths.ContentRoot());
        string folder = args.Length > 1 ? Path.GetFullPath(args[1]) : throw new ArgumentException("a folder of map folders is needed");
        string map = args.Length > 2 ? args[2] : "AquaD";

        MapsApi maps;
        OnlineApi online;
        ((delegate* unmanaged<MapsApi*, int>)&MapsExports.GetApi)(&maps);
        ((delegate* unmanaged<OnlineApi*, int>)&OnlineExports.GetApi)(&online);
        BridgeSession.Configure(content);
        bool ok = true;

        string before = Folder(maps, map);
        Console.WriteLine($"{map} before the folders are set: {before}");
        int existing;
        fixed (char* first = Path.Combine(folder, map), last = "")
        {
            // The map folder itself, as a player may name it: its parent is searched.
            existing = maps.SetFolders(first, last);
        }

        string after = Folder(maps, map);
        Console.WriteLine($"SetFolders -> {existing} existing; {map} is now in: {after}");
        ok &= existing == 1 && string.Equals(Path.GetDirectoryName(after), folder, StringComparison.OrdinalIgnoreCase);

        string roots = Text(maps, MapsText.Roots);
        Console.WriteLine("roots: " + roots.Replace("\n", "; ", StringComparison.Ordinal));
        ok &= roots.StartsWith(folder, StringComparison.OrdinalIgnoreCase) && roots.EndsWith(Path.Combine(content, "maps"), StringComparison.OrdinalIgnoreCase);

        string testBox = Folder(maps, "TestBox");
        string installed = ReadOnline(online, OnlineText.InstalledMaps);
        string renderable = Text(maps, MapsText.RenderableMaps);
        bool installedHas(string m) => Array.Exists(installed.Split('\n'), line => string.Equals(line, m, StringComparison.OrdinalIgnoreCase));
        bool renderableHas(string m) => Array.Exists(renderable.Split('\n'), line => string.Equals(line, m, StringComparison.OrdinalIgnoreCase));
        Console.WriteLine($"TestBox in: {testBox}; installed maps {installed.Split('\n').Length} (has {map}: {installedHas(map)}, TestBox: {installedHas("TestBox")}); renderable {renderable.Split('\n').Length} (has {map}: {renderableHas(map)}, TestBox: {renderableHas("TestBox")})");
        // Installed releases also ship TestBox beside the bridge. A duplicate in a later
        // root must never displace the copy the player or the application selected first.
        string? expectedTestBox = Array.Find(Array.ConvertAll(roots.Split('\n'), root => Path.Combine(root, "TestBox")),
            candidate => File.Exists(Path.Combine(candidate, "manifest.json")));
        ok &= string.Equals(testBox, expectedTestBox, StringComparison.OrdinalIgnoreCase)
            && installedHas(map) && installedHas("TestBox") && renderableHas(map) && renderableHas("TestBox");

        using (BridgeSession session = BridgeSession.StartLocal(new LocalMatch { Map = map, Bots = 0 }))
        {
            for (int i = 0; i < 20; i++)
            {
                session.Update();
                System.Threading.Thread.Sleep(10);
            }

            Console.WriteLine($"local match on {map} loaded from {session.Session.MapFolder}, {session.Ticks} ticks");
            ok &= string.Equals(Path.GetDirectoryName(session.Session.MapFolder.TrimEnd('\\', '/')), folder, StringComparison.OrdinalIgnoreCase) && session.Ticks > 0;
        }

        Console.WriteLine(ok ? "PROBE OK" : "PROBE FAILED");
        return ok ? 0 : 1;
    }

    private static string Folder(MapsApi maps, string map)
    {
        char* buffer = stackalloc char[1024];
        fixed (char* m = map)
        {
            maps.GetMapFolder(m, buffer, 1024);
        }

        return new string(buffer);
    }

    private static string Text(MapsApi maps, MapsText kind)
    {
        int length = maps.GetText((int)kind, null, 0);
        char[] buffer = new char[length + 1];
        fixed (char* b = buffer)
        {
            maps.GetText((int)kind, b, buffer.Length);
        }

        return new string(buffer, 0, length);
    }

    private static string ReadOnline(OnlineApi online, OnlineText kind)
    {
        int length = online.GetText((int)kind, null, 0);
        char[] buffer = new char[length + 1];
        fixed (char* b = buffer)
        {
            online.GetText((int)kind, b, buffer.Length);
        }

        return new string(buffer, 0, length);
    }
}
