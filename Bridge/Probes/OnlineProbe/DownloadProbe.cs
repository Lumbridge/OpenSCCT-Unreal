using System;
using System.IO;
using System.Threading;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Online;

// OnlineProbe download <map> <scratch packs folder> [content root] [nav]
//
// A map from OpenSCCT-Content through the exported ContentApi as Unreal calls it: the index is read, the map's
// download is started and polled with its progress until it is installed in the scratch packs folder, and the play
// catalog then finds the map there (the folder Unreal's runtime map level is built from). With "nav" the map's
// prebuilt bot navigation is offered and downloaded too.
internal static unsafe class DownloadProbe
{
    public static int Run(string[] args)
    {
        string map = args.Length > 0 ? args[0] : throw new ArgumentException("a map code is needed");
        string packs = Path.GetFullPath(args.Length > 1 ? args[1] : throw new ArgumentException("a scratch packs folder is needed"));
        string content = Path.GetFullPath(args.Length > 2 ? args[2] : OpenScct.UnrealBridge.DevPaths.ContentRoot());
        bool nav = args.Length > 3 && args[3] == "nav";

        // The packs folder BridgeSession.Configure reads, as a test process of Unreal moves it.
        Environment.SetEnvironmentVariable("OPENSCCT_UNREAL_PACKS", packs);
        BridgeSession.Configure(content);

        ContentApi api;
        int version = ((delegate* unmanaged<ContentApi*, int>)&ContentExports.GetApi)(&api);
        Console.WriteLine($"ContentApi version {version}, {sizeof(ContentApi)} bytes (header: 8 + 14 pointers = {8 + 14 * sizeof(nint)}), info {sizeof(ContentDownloadInfo)} bytes");
        bool ok = sizeof(ContentApi) == 8 + 14 * sizeof(nint) && sizeof(ContentDownloadInfo) == 32;

        Console.WriteLine($"{map} before: {MapsExports.FolderOf(map) ?? "not installed"}");
        int index = 0;
        for (int i = 0; i < 300 && (index = api.RefreshIndex(0)) == 0; i++)
        {
            Thread.Sleep(100);
        }

        Console.WriteLine($"index: {(index == 1 ? "read" : "failed: " + Text(api, ContentText.IndexError, string.Empty))}");
        if (index != 1)
        {
            Console.WriteLine("PROBE FAILED");
            return 1;
        }

        int started;
        fixed (char* m = map)
        {
            started = api.StartMapDownload(m, null);
        }

        Console.WriteLine($"StartMapDownload -> {started}");
        ok &= started == 1;
        ContentDownloadInfo info = default;
        int lastTenth = -1;
        string lastStep = string.Empty;
        DateTime giveUp = DateTime.UtcNow.AddMinutes(10);
        while (started == 1 && DateTime.UtcNow < giveUp)
        {
            int state;
            fixed (char* m = map)
            {
                state = api.GetDownload(m, &info);
            }

            string line = Text(api, ContentText.Download, map);
            int tenth = (int)(info.Fraction * 10);
            if (tenth != lastTenth || !line.StartsWith(lastStep, StringComparison.Ordinal))
            {
                Console.WriteLine($"  [{(ContentDownloadState)state}] {line}");
                lastTenth = tenth;
                lastStep = line.Split(':')[0];
            }

            if (state != (int)ContentDownloadState.Running)
            {
                Console.WriteLine($"  [{(ContentDownloadState)state}] {line}");
                ok &= state == (int)ContentDownloadState.Done;
                break;
            }

            Thread.Sleep(100);
        }

        string installed = Text(api, ContentText.InstalledFolder, map);
        Console.WriteLine($"installed folder: {installed} (render/world.glb: {File.Exists(Path.Combine(installed, "render", "world.glb"))}), generation {api.GetGeneration()}");
        ok &= installed.StartsWith(Path.Combine(packs, "svmap"), StringComparison.OrdinalIgnoreCase) && File.Exists(Path.Combine(installed, "manifest.json"));

        // The catalogs read their folders again on the next frame of the online bridge, as Unreal drives it.
        OnlineBridge.Instance.Update();
        string? folder = MapsExports.FolderOf(map);
        Console.WriteLine($"{map} after: {folder ?? "not installed"}");
        ok &= folder is not null && string.Equals(Path.GetFullPath(folder), Path.GetFullPath(installed), StringComparison.OrdinalIgnoreCase);

        if (nav)
        {
            int offer;
            fixed (char* m = map)
            {
                offer = api.CheckBotNavigation(m);
            }

            Console.WriteLine($"bot navigation: {(BotNavigationState)offer}: {Text(api, ContentText.BotNavigation, map)}");
            if (offer == (int)BotNavigationState.Offer)
            {
                fixed (char* m = map)
                {
                    ok &= api.StartBotNavigation(m) == 1;
                }

                string key = ContentPacks.NavigationKey(map);
                int state;
                do
                {
                    Thread.Sleep(200);
                    fixed (char* k = key)
                    {
                        state = api.GetDownload(k, &info);
                    }
                }
                while (state == (int)ContentDownloadState.Running);
                Console.WriteLine($"  [{(ContentDownloadState)state}] {Text(api, ContentText.Download, key)} -> {Text(api, ContentText.InstalledFolder, key)}");
                fixed (char* m = map)
                {
                    offer = api.CheckBotNavigation(m);
                }

                Console.WriteLine($"bot navigation afterwards: {(BotNavigationState)offer}: {Text(api, ContentText.BotNavigation, map)}");
                ok &= state == (int)ContentDownloadState.Done && offer == (int)BotNavigationState.None;
            }
        }

        Console.WriteLine(ok ? "PROBE OK" : "PROBE FAILED");
        return ok ? 0 : 1;
    }

    private static string Text(ContentApi api, ContentText kind, string key)
    {
        fixed (char* k = key)
        {
            int length = api.GetText((int)kind, k, null, 0);
            char* buffer = stackalloc char[length + 1];
            api.GetText((int)kind, k, buffer, length + 1);
            return new string(buffer);
        }
    }
}
