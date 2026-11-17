using System;
using System.IO;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Online;

/// <summary>
/// OnlineProbe rejoin: the rejoin table without a server.
///   1. the table's and the choice's sizes (ScctRejoinApi.h asserts them);
///   2. a record saved and read back: the server's password is sealed (not in the file as written), and comes back;
///   3. the service with that record and an address nobody answers on: the check asks the address, gives up after
///      its timeout and forgets the record (the file is deleted), nothing is offered;
///   4. a record older than three hours is forgotten without asking.
/// </summary>
internal static unsafe class RejoinProbe
{
    public static int Run(string[] args)
    {
        if (args.Length > 0 && args[0] == "stay")
        {
            return Stay(args[1..]);
        }

        bool ok = true;
        int expectedApi = 8 + 6 * sizeof(void*);
        int expectedChoice = 5 * 4 + (64 + 32 + 64) * 2;
        Console.WriteLine($"layout: RejoinApi {sizeof(RejoinApi)} bytes (header {expectedApi}), RejoinChoiceData {sizeof(RejoinChoiceData)} bytes (header {expectedChoice})");
        ok &= sizeof(RejoinApi) == expectedApi && sizeof(RejoinChoiceData) == expectedChoice;

        RejoinApi api = default;
        int version = ((delegate* unmanaged<RejoinApi*, int>)&RejoinExports.GetApi)(&api);
        Console.WriteLine($"table: rejoin version {version} (expects {RejoinExports.ApiVersion})");
        ok &= version == RejoinExports.ApiVersion;

        string folder = Path.Combine(Path.GetTempPath(), "scct-rejoin-probe-" + Environment.ProcessId);
        Directory.CreateDirectory(folder);
        string file = Path.Combine(folder, "rejoin.json");
        try
        {
            var record = new RejoinRecord
            {
                ServerName = "Probe server", Address = "127.0.0.1:28997", Password = "probe-secret", Map = "AquaE", At = DateTimeOffset.UtcNow,
            };
            record.Save(file);
            string written = File.ReadAllText(file);
            RejoinRecord? back = RejoinRecord.Load(file);
            bool sealedOk = !written.Contains("probe-secret", StringComparison.Ordinal);
            bool backOk = back is { ServerName: "Probe server", Address: "127.0.0.1:28997", Password: "probe-secret", Map: "AquaE", Fresh: true };
            Console.WriteLine($"record: password sealed in the file {(sealedOk ? "yes" : "NO")}, read back {(backOk ? "intact" : "WRONG")}");
            ok &= sealedOk && backOk;

            using var bridge = new OnlineBridge();
            bridge.Rejoin.SetFile(file);
            bridge.Rejoin.Check();
            bool asked = bridge.Rejoin.Checking;
            DateTime until = DateTime.UtcNow.AddSeconds(8);
            while (bridge.Rejoin.Checking && DateTime.UtcNow < until)
            {
                bridge.Update();
                System.Threading.Thread.Sleep(50);
            }

            RejoinChoiceData choice = default;
            int offered = RejoinExports.Fill(bridge.Rejoin, &choice);
            bool forgotten = !File.Exists(file);
            Console.WriteLine($"no answer: asked {(asked ? "yes" : "NO")}, offered {offered}, record forgotten {(forgotten ? "yes" : "NO")}");
            ok &= asked && offered == 0 && forgotten && !bridge.Rejoin.Checking;

            (record with { At = DateTimeOffset.UtcNow.AddHours(-4) }).Save(file);
            using var later = new OnlineBridge();
            later.Rejoin.SetFile(file);
            later.Rejoin.Check();
            bool staleOk = !later.Rejoin.Checking && later.Rejoin.Offer is null && !File.Exists(file);
            Console.WriteLine($"stale record: forgotten without asking {(staleOk ? "yes" : "NO")}");
            ok &= staleOk;

            foreach (OnlineBridge b in new[] { bridge, later })
            {
                while (b.Rejoin.TakeLine(out string line))
                {
                    Console.WriteLine("  rejoin: " + line);
                }
            }
        }
        finally
        {
            Directory.Delete(folder, recursive: true);
        }

        Console.WriteLine(ok ? "PROBE PASSED" : "PROBE FAILED");
        return ok ? 0 : 1;
    }

    /// <summary>
    /// OnlineProbe rejoin stay content address seconds [wait for name]: a second human player for a live rejoin check.
    /// It joins the mercenaries with a fresh identity, says ready once the named player is in the lobby, plays the
    /// round headless (a server sends everyone back to the lobby when the last human leaves, which would end the round
    /// a dropped player is to rejoin) and leaves after the given seconds.
    /// </summary>
    private static int Stay(string[] args)
    {
        string content = args[0];
        string address = args[1];
        double seconds = double.Parse(args[2], System.Globalization.CultureInfo.InvariantCulture);
        string waitFor = args.Length > 3 ? args[3] : "UnrealCheck";
        BridgeSession.Configure(content);
        using var online = new OnlineBridge();
        online.SetIdentity("Stayer", null, null);
        online.Join(OpenScct.Server.Online.JoinTarget.FromAddress(address), null, null, preferredTeam: 1, loopbackOnly: true);
        DateTime until = DateTime.UtcNow.AddSeconds(seconds);
        OnlineState last = OnlineState.Idle;
        bool readied = false;
        while (DateTime.UtcNow < until)
        {
            online.Update();
            if (online.State != last)
            {
                last = online.State;
                Console.WriteLine($"stay: {last} {online.Error}");
            }

            if (online.State is OnlineState.Failed or OnlineState.Left)
            {
                return 1;
            }

            if (online.State == OnlineState.Lobby && online.Client is { } client)
            {
                bool there = false;
                foreach (var p in client.Lobby.Players)
                {
                    there |= p.Name == waitFor;
                }

                if (there != readied)
                {
                    readied = there;
                    online.SetReady(there);
                    Console.WriteLine($"stay: ready {there}");
                }
            }

            if (online.NeedsLoadReport)
            {
                online.ReportLoaded();
            }

            System.Threading.Thread.Sleep(5);
        }

        online.Leave();
        Console.WriteLine("stay: left");
        return 0;
    }
}
