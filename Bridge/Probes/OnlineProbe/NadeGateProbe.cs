using System;
using System.Diagnostics;
using System.Globalization;
using System.Threading;
using OpenScct.Game.SBase;
using OpenScct.UnrealBridge;
using OpenScct.UnrealBridge.Equipment;
using OpenScct.UnrealBridge.Online;

/// <summary>
/// OnlineProbe nade [content root] [map] [port]
///
/// Where the grenade preview (the console command nadepreview) is allowed and where it is refused, without Unreal:
/// the answer Unreal asks for (NadePreviewFeature.IsAllowed on the session in the main slot) and what the preview
/// itself gives (NadePreviewFeature.Read, which asks the same question again every frame).
///   1. a match hosted from the game on a loopback UDP port, not practice, the host alone in it: refused, no path;
///   2. the same with a second human in the round (a second process: "OnlineProbe rejoin stay"): refused, no path;
///   3. a hosted practice match (?SvmPr=1): allowed, and a merc with a grenade selected gets a path;
///   4. an offline match that is not practice: allowed.
/// The host's own player joins its server as any player does, so its round is a round on a server (IsRemote), which
/// is what makes 1 and 2 refuse. Exit code 1 when any answer is wrong.
/// </summary>
internal static class NadeGateProbe
{
    public static int Run(string[] args)
    {
        string content = args.Length > 0 ? args[0] : DevPaths.ContentRoot();
        string map = args.Length > 1 ? args[1] : "AquaE";
        int port = args.Length > 2 ? int.Parse(args[2], CultureInfo.InvariantCulture) : 28951;
        BridgeSession.Configure(content);
        bool ok = true;
        try
        {
            ok &= Hosted("hosted, not practice, the host alone", content, map, port, practice: false, guest: false, expectAllowed: false);
            ok &= Hosted("hosted, not practice, with a second human", content, map, port + 1, practice: false, guest: true, expectAllowed: false);
            ok &= Hosted("hosted practice", content, map, port + 2, practice: true, guest: false, expectAllowed: true);
        }
        finally
        {
            OnlineBridge.Instance.Dispose();
        }

        using (BridgeSession offline = BridgeSession.StartLocal(new LocalMatch { Map = map, Mode = "Story", Merc = true, Bots = 0, InlineHost = true }))
        {
            bool allowed = NadePreviewFeature.IsAllowed(offline);
            bool practice = OpenScct.UnrealBridge.Test.GrabAreaReader.IsPracticeRound(offline.Session);
            Console.WriteLine($"offline, not practice: remote {offline.IsRemote}, practice {practice}, allowed {allowed}{(allowed && !practice ? string.Empty : "   WRONG")}");
            ok &= allowed && !practice && !offline.IsRemote;
        }

        Console.WriteLine($"no session: allowed {NadePreviewFeature.IsAllowed(null)}");
        ok &= !NadePreviewFeature.IsAllowed(null);
        Console.WriteLine(ok ? "PROBE OK" : "PROBE FAILED");
        return ok ? 0 : 1;
    }

    private static bool Hosted(string title, string content, string map, int port, bool practice, bool guest, bool expectAllowed)
    {
        Console.WriteLine($"-- {title}");
        OnlineBridge online = OnlineBridge.Instance;
        online.SetIdentity("NadeHost", null, null);
        Process? second = null;
        try
        {
            string options = $"?MapName={map}?Mode=Story?TimeLimit=5?SvmPr={(practice ? 1 : 0)}";
            int started = HostExports.Host(online, "Nade gate", null, port, 8, options, HostFlags.LoopbackOnly, 0, null);
            if (started != port || !Wait(online, 15, () => online.State == OnlineState.Lobby && online.Client?.Lobby.Local is not null))
            {
                Console.WriteLine($"FAILED: could not host (returned {started}, state {online.State}: {online.Error})");
                return false;
            }

            Console.WriteLine($"lobby: practice {online.Client!.Lobby.Setup.Practice}, the local player is the host: {online.Client.Lobby.Local!.IsHost}");
            if (guest)
            {
                second = Process.Start(new ProcessStartInfo(Environment.ProcessPath!)
                {
                    ArgumentList = { "rejoin", "stay", content, "127.0.0.1:" + port.ToString(CultureInfo.InvariantCulture), "45", "NadeHost" },
                    UseShellExecute = false,
                    RedirectStandardOutput = true,
                });
                second!.OutputDataReceived += (_, e) =>
                {
                    if (e.Data is { Length: > 0 } line)
                    {
                        Console.WriteLine("   second player: " + line);
                    }
                };
                second.BeginOutputReadLine();
                if (!Wait(online, 30, () => online.Client.Lobby.Players.Count >= 2))
                {
                    Console.WriteLine("FAILED: the second player did not reach the lobby");
                    return false;
                }
            }

            // The host plays a merc (the gun is out, so a grenade can be previewed), a second human the other merc.
            online.SetTeam(1);
            online.SetReady(true);
            Wait(online, 2.5, null);
            online.Launch();
            bool playing = Wait(online, 60, () =>
            {
                if (online.NeedsLoadReport)
                {
                    online.ReportLoaded();
                }

                return online.State == OnlineState.Match && Exports.Current is { } s && s.Session.LocalPawn is { Health: > 0 };
            });
            if (!playing || Exports.Current is not { } session)
            {
                Console.WriteLine($"FAILED: no round with a living local player (state {online.State}: {online.Error})");
                return false;
            }

            Wait(online, 2.0, null);
            int humans = 0, players = 0;
            foreach (var p in online.Client.Lobby.Players)
            {
                players++;
                humans += p.IsBot ? 0 : 1;
            }

            SBasePawn pawn = session.Session.LocalPawn!;
            SGadGrenLauncher? grenade = null;
            foreach (SGadGrenLauncher g in pawn.XLevel!.AllActors<SGadGrenLauncher>())
            {
                if (ReferenceEquals(g.OwnerPawn, pawn) && !g.bDeleteMe && grenade is null)
                {
                    grenade = g;
                }
            }

            if (grenade is not null)
            {
                pawn.SelectedGadget = grenade;
                grenade.Charge = Math.Max(grenade.Charge, 1);
            }

            bool allowed = NadePreviewFeature.IsAllowed(session);
            var points = new BridgeNadePoint[96];
            int count = NadePreviewFeature.Of(session).Read(out BridgeNadePreview head, points);
            bool practiceRound = OpenScct.UnrealBridge.Test.GrabAreaReader.IsPracticeRound(session.Session);
            Console.WriteLine($"round: {players} players ({humans} human), hosting on port {online.Hosting?.Port ?? 0}, session remote {session.IsRemote}, practice round {practiceRound}; " +
                $"local {pawn.GetType().Name} health {pawn.Health}, weapon loaded {pawn.WeaponLoaded}, grenade gadget {grenade?.GetType().Name ?? "none"}; " +
                $"allowed {allowed}, the preview read gave {count} points (state {head.State})");
            bool ok = allowed == expectAllowed && practiceRound == practice && session.IsRemote && online.Hosting is not null && (!guest || humans >= 2);
            if (!expectAllowed)
            {
                ok &= count == 0 && head.State == NadePreviewState.None;
            }
            else if (grenade is not null && pawn.WeaponLoaded != 0)
            {
                ok &= count >= 2 && head.State == NadePreviewState.Aimed;
            }

            Console.WriteLine(ok ? "ok" : "WRONG");
            return ok;
        }
        finally
        {
            online.Leave();
            if (second is not null)
            {
                if (!second.WaitForExit(8000))
                {
                    second.Kill(entireProcessTree: true);
                }

                second.Dispose();
            }
        }
    }

    private static bool Wait(OnlineBridge online, double seconds, Func<bool>? until)
    {
        var clock = Stopwatch.StartNew();
        while (clock.Elapsed.TotalSeconds < seconds)
        {
            online.Update();
            if (until?.Invoke() == true)
            {
                return true;
            }

            Thread.Sleep(5);
        }

        return until is null;
    }
}
