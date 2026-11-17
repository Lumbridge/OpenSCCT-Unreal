using System;
using System.Diagnostics;
using System.Globalization;
using System.Linq;
using System.Threading;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Online;

/// <summary>
/// OnlineProbe lan [content root] [map] [port] [lan port]
///
/// The server browser without Unreal (Online/ServerBrowser.cs, BrowserExports): a listen server with a password and a
/// loopback LAN beacon is hosted on one OnlineBridge, and a second one opens the browser on the loopback LAN with two
/// saved addresses (the host's game port, and a port nothing listens on). It checks, in this order:
///   1. the browser table's size and its row's size (ScctBrowserApi.h asserts them);
///   2. the LAN search and the saved address find the server as one row (LAN and SAVED) with a measured ping, its
///      name, map, phase, players and the PASSWORD flag, and the silent saved address gives no row; with the LAN
///      searched on a port nothing answers on, the saved address alone finds it, reached directly;
///   3. the selected row's players by side;
///   4. a join of the LAN row with a wrong password fails with the server's reason;
///   5. a join of the LAN row with the password on the merc side reaches the lobby on the merc side.
/// </summary>
internal static unsafe class LanProbe
{
    public static int Run(string[] args)
    {
        string content = args.Length > 0 ? args[0] : @"C:\Users\ryans\source\repos\SVM-Rewrite\.content";
        string map = args.Length > 1 ? args[1] : "AquaE";
        int port = args.Length > 2 ? int.Parse(args[2], CultureInfo.InvariantCulture) : 28951;
        int lanPort = args.Length > 3 ? int.Parse(args[3], CultureInfo.InvariantCulture) : 28952;
        const string password = "x";

        bool ok = true;
        int expectedApi = 8 + 7 * sizeof(void*);
        int expectedRow = 8 * 4 + (96 + 64 + 32 + 16 + 64 + 48) * 2;
        BrowserApi api = default;
        int version = ((delegate* unmanaged<BrowserApi*, int>)&BrowserExports.GetApi)(&api);
        Console.WriteLine($"layout: BrowserApi {sizeof(BrowserApi)} bytes (header asserts {expectedApi}), BrowserRowData {sizeof(BrowserRowData)} bytes (header asserts {expectedRow}), version {version}");
        ok &= sizeof(BrowserApi) == expectedApi && sizeof(BrowserRowData) == expectedRow && version == BrowserExports.ApiVersion;

        BridgeSession.Configure(content);
        OnlineBridge host = OnlineBridge.Instance;
        host.SetIdentity("LanHost", null, null);
        using var guest = new OnlineBridge();
        guest.SetIdentity("LanGuest", null, null);
        try
        {
            int started = HostExports.Host(host, "Lan probe", password, port, 8, $"?MapName={map}?Mode=Story", HostFlags.Lan | HostFlags.LoopbackOnly, lanPort, null);
            Wait(() => host.State == OnlineState.Lobby, 15, host, guest);
            Console.WriteLine($"host: port {started}, state {host.State}, {host.Hosting?.MasterLine}");
            ok &= started == port && host.State == OnlineState.Lobby;

            // 2. The browser: the loopback LAN and two saved addresses, one of which nothing answers on.
            string silent = "127.0.0.1:" + (port + 7).ToString(CultureInfo.InvariantCulture);
            bool opened = guest.Browser.Open(lanPort, $"127.0.0.1:{port}; {silent}", loopbackOnly: true);
            var watch = Stopwatch.StartNew();
            Wait(() => guest.Browser.Rows.Count >= 1 && guest.Browser.Rows.All(r => r.PingMs >= 0 && r.OnLan && r.SavedAddress.Length > 0), 6, host, guest);
            // A few more query rounds: the shown ping is the lowest of the last answers.
            Wait(() => false, 3.5, host, guest);
            Console.WriteLine($"browser: opened {opened}, after {watch.Elapsed.TotalSeconds:0.0} s: {guest.Browser.Status}");
            // The server is found on the LAN and saved: one row that says both. The silent address gives none.
            int lanRow = PrintRows(guest.Browser, port, BrowserSource.Lan, out int count);
            ok &= opened && lanRow >= 0 && count == 1;

            // 2b. Saved only: the LAN searched on a port nothing answers on.
            guest.Browser.Open(lanPort + 1, $"127.0.0.1:{port}", loopbackOnly: true);
            Wait(() => guest.Browser.Rows.Count >= 1 && guest.Browser.Rows.All(r => r.PingMs >= 0), 6, host, guest);
            Console.WriteLine($"browser, saved address only: {guest.Browser.Status}");
            int savedRow = PrintRows(guest.Browser, port, BrowserSource.Saved, out count);
            ok &= savedRow >= 0 && count == 1;
            guest.Browser.Open(lanPort, $"127.0.0.1:{port}", loopbackOnly: true);
            Wait(() => guest.Browser.Rows.Count >= 1 && guest.Browser.Rows[0].PingMs >= 0, 6, host, guest);
            lanRow = PrintRows(guest.Browser, port, BrowserSource.Lan, out _);

            // 3. The players of the selected row.
            string players = ServerBrowser.PlayersText(guest.Browser.Rows[Math.Max(lanRow, 0)]);
            Console.WriteLine($"players of the LAN row: {players}");
            ok &= players.Contains("LanHost", StringComparison.Ordinal);

            // 4. A wrong password.
            bool joined = guest.Browser.Join(lanRow, "wrong", 1);
            Wait(() => guest.State is OnlineState.Failed or OnlineState.Lobby, 10, host, guest);
            Console.WriteLine($"join with a wrong password: started {joined}, state {guest.State}: '{guest.Error}'");
            ok &= joined && guest.State == OnlineState.Failed;
            guest.Leave();

            // 5. The password, on the merc side. The browser is opened again, as the page does when it comes back.
            guest.Browser.Open(lanPort, null, loopbackOnly: true);
            Wait(() => guest.Browser.Rows.Count >= 1 && guest.Browser.Rows[0].PingMs >= 0, 5, host, guest);
            joined = guest.Browser.Join(0, password, 1);
            Wait(() => guest.State == OnlineState.Lobby && guest.Client?.Lobby.Local is not null, 15, host, guest);
            int team = guest.Client?.Lobby.Local?.Team ?? -1;
            Console.WriteLine($"join with the password on the merc side: started {joined}, state {guest.State}, side {team}{(guest.Error is { } e ? ": " + e : string.Empty)}");
            ok &= joined && guest.State == OnlineState.Lobby && team == 1;
            guest.Browser.Close();
        }
        finally
        {
            guest.Leave();
            host.Dispose();
        }

        Console.WriteLine(ok ? "PROBE OK" : "PROBE FAILED");
        return ok ? 0 : 1;
    }

    private static string Fixed(char* text) => new string(text);

    /// <summary>Prints the browser's rows; returns the index of the probe's server found as <paramref name="source"/>, or -1.</summary>
    private static int PrintRows(ServerBrowser browser, int port, BrowserSource source, out int count)
    {
        var rows = new BrowserRowData[8];
        count = BrowserExports.Rows(browser, rows);
        int found = -1;
        for (int i = 0; i < count && i < rows.Length; i++)
        {
            BrowserRowData r = rows[i];
            string name = Fixed(r.Name), address = Fixed(r.Address);
            Console.WriteLine($"row {i}: [{(BrowserSource)r.Source}] '{name}' map {Fixed(r.Map)} mode {r.Mode} phase {r.Phase} players {r.Players}/{r.MaxPlayers} ping {r.PingMs} ms reach {(BrowserReach)r.Reach} address {address} flags 0x{r.Flags:x}{((r.Flags & OnlineServerFlags.Password) != 0 ? " PASSWORD" : string.Empty)}{((r.Flags & BrowserRowFlags.Saved) != 0 ? " SAVED" : string.Empty)}{((r.Flags & BrowserRowFlags.OnLan) != 0 ? " LAN" : string.Empty)} problem '{Fixed(r.Problem)}' key {Fixed(r.Key)}");
            bool good = name.StartsWith("Lan probe", StringComparison.Ordinal) && r.PingMs >= 0 && (r.Flags & OnlineServerFlags.Password) != 0 && r.Players >= 1 &&
                (r.Flags & BrowserRowFlags.Answered) != 0 && address == "127.0.0.1:" + port.ToString(CultureInfo.InvariantCulture) && r.Source == (int)source &&
                (source == BrowserSource.Lan ? r.Reach == (int)BrowserReach.Lan : r.Reach == (int)BrowserReach.Direct && (r.Flags & BrowserRowFlags.Saved) != 0);
            if (good && found < 0)
            {
                found = i;
            }
        }

        return found;
    }

    private static void Wait(Func<bool> done, double seconds, OnlineBridge host, OnlineBridge guest)
    {
        var wait = Stopwatch.StartNew();
        while (!done() && wait.Elapsed.TotalSeconds < seconds)
        {
            host.Update();
            guest.Update();
            Thread.Sleep(10);
        }
    }
}
