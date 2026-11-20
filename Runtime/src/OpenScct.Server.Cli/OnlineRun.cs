using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using OpenScct.Maps;
using OpenScct.Net;
using OpenScct.Server.Online;

namespace OpenScct.Server.Cli;

/// <summary>
/// <c>svm-server --config svm-server.json</c>: a dedicated online server (docs/ONLINE.md): the lobby, the rotation,
/// the master listing and the admin console on standard input (kick, ban, map, say, status, close and the rest of
/// <see cref="OnlineServer.CommandHelp"/>). Stops on Ctrl+C, SIGTERM or <c>close</c>, telling players and the master.
/// </summary>
internal static class OnlineRun
{
    public static int Run(ServerConfig config, string? configPath, double seconds)
    {
        string baseDir = configPath is null ? Directory.GetCurrentDirectory() : Path.GetDirectoryName(Path.GetFullPath(configPath))!;
        MapCatalog maps;
        List<LobbySetup> rotation;
        try
        {
            maps = DedicatedMaps(baseDir, config.Maps);
            rotation = config.Setups(maps);
        }
        catch (InvalidOperationException e)
        {
            Log(e.Message);
            return 3;
        }

        LiteNetLibTransport udp;
        try
        {
            // Lobby settings can grow after startup; keep the socket ready for the full supported capacity.
            udp = LiteNetLibTransport.Listen(config.Port, Math.Max(config.MaxPlayers, LobbySetup.MaxMaxPlayers) + config.MaxSpectators + 4, config.LoopbackOnly);
        }
        catch (InvalidOperationException e)
        {
            Log(e.Message);
            return 3;
        }

        string? bansPath = config.BansFile is { Length: > 0 } b ? Path.GetFullPath(Path.Combine(baseDir, b)) : null;
        string? matchLogPath = config.MatchLog is { Length: > 0 } ml ? Path.GetFullPath(Path.Combine(baseDir, ml)) : null;
        string? demoFolder = config.Demos is { Record: true, Folder.Length: > 0 } d ? Path.GetFullPath(Path.Combine(baseDir, d.Folder)) : null;
        if (demoFolder is not null && config.Demos!.KeepLocalDays > 0)
        {
            DemoFolders.Prune(demoFolder, TimeSpan.FromDays(config.Demos.KeepLocalDays), Log);
        }
        var botNavigation = new OpenScct.Bots.AiNavCache();
        var settings = new OnlineServerSettings
        {
            Name = config.Name,
            Dedicated = true,
            MaxPlayers = config.MaxPlayers,
            MaxSpectators = config.MaxSpectators,
            Password = config.Password,
            AdminPassword = config.AdminPassword,
            OpenMapCommands = config.OpenMapCommands,
            Motd = config.Motd,
            Setup = rotation[0],
            Rotation = rotation.Count > 1 ? rotation : [],
            NetRate = config.NetRate,
            Profile = config.Profile,
            MatchSeed = config.Seed,
            Maps = maps,
            Net = new NetDriverSettings { LanPlay = config.LanPlay, IsLan = config.LanPlay, UpdateGate = NetUpdateGate.DecodedGuarded },
            MinPlayersToLaunch = config.MinPlayersToLaunch,
            LaunchCountdownSeconds = config.LaunchCountdownSeconds,
            ResultsSeconds = config.ResultsSeconds,
            LoadTimeoutSeconds = config.LoadTimeoutSeconds,
            Bots = OpenScct.Bots.OnlineServerBots.Factory(botNavigation),
            PrepareBots = OpenScct.Bots.OnlineServerBots.Preparation(botNavigation),
            Bans = new BanList(bansPath, () => DateTimeOffset.UtcNow),
            LanPort = config.LanPort,
            LanLoopbackOnly = config.LoopbackOnly,
            Master = config.Master is { Url.Length: > 0 } m
                ? new MasterLinkSettings
                {
                    Url = m.Url, InviteKey = m.InviteKey, Listed = m.Listed, NatPunch = m.NatPunch, Relay = m.Relay, ServerKey = m.ServerKey,
                    Region = m.Region ?? string.Empty, UploadDemos = config.Demos?.Upload ?? true,
                }
                : null,
            RequireAccounts = config.RequiresAccounts,
            DemoFolder = demoFolder,
            MatchLog = matchLogPath is not null ? new MatchLog(matchLogPath) : null,
            LagCompensation = config.LagComp,
            LagCompensationCeilingMs = config.LagCompCeilingMs,
        };
        if (config.RequireAccounts == true && settings.Master is null)
        {
            Log("RequireAccounts is set but no master is: players get their tickets from the master, so the server runs open.");
            settings = settings with { RequireAccounts = false };
        }

        var clock = new StopwatchClock();
        OnlineServer server;
        try
        {
            server = new OnlineServer(udp, settings, clock.NowMicroseconds, udp);
        }
        catch (InvalidOperationException e)
        {
            udp.Dispose();
            Log(e.Message);
            return 3;
        }

        using (server)
        using (TimerResolution.Request())
        using (var stop = new CancellationTokenSource())
        {
            server.Logged += Log;
            if (demoFolder is not null && config.Demos!.KeepLocalDays > 0)
            {
                int keepDays = config.Demos.KeepLocalDays;
                server.DemoRecorded += (_, _) => DemoFolders.Prune(demoFolder, TimeSpan.FromDays(keepDays), Log);
            }
            using PosixSignalRegistration term = PosixSignalRegistration.Create(PosixSignal.SIGTERM, ctx =>
            {
                ctx.Cancel = true;
                stop.Cancel();
            });
            using PosixSignalRegistration interrupt = PosixSignalRegistration.Create(PosixSignal.SIGINT, ctx =>
            {
                ctx.Cancel = true;
                stop.Cancel();
            });
            if (seconds > 0)
            {
                stop.CancelAfter(TimeSpan.FromSeconds(seconds));
            }

            var console = new ConsoleCommands();
            console.Start();
            Log(string.Create(CultureInfo.InvariantCulture,
                $"{config.Name} on UDP port {udp.LocalPort}{(server.Lan is { } lan ? $", LAN discovery on {lan.Port}" : ", no LAN discovery")}, up to {config.MaxPlayers} players"));
            Log("Next: " + server.Setup.Describe());
            Log("Console: " + OnlineServer.CommandHelp);
            server.RunRealtime(clock, stop.Token, s =>
            {
                while (console.TryTake(out string? line))
                {
                    foreach (string reply in s.Execute(line))
                    {
                        Console.WriteLine(reply);
                    }
                }
            });
            if (!server.CloseRequested)
            {
                server.Shutdown(clock);
            }

            Log("stopped");
        }

        return 0;
    }

    private static MapCatalog DedicatedMaps(string configDirectory, string? mapDirectory)
    {
        string packaged = AppContext.BaseDirectory;
        string? recipe = UnrealSteamInstall.ReadRecipe(Path.Combine(packaged, "contenttools"));
        string generations = Environment.GetEnvironmentVariable(UnrealSteamInstall.ContentDirectoryVariable)
            ?? UnrealSteamInstall.DefaultDirectory;
        string? content = UnrealSteamInstall.ActiveRoot(generations, recipe);
        var roots = new List<string>();
        if (!string.IsNullOrWhiteSpace(mapDirectory))
        {
            var maps = new DirectoryInfo(Path.GetFullPath(mapDirectory, configDirectory));
            if (!maps.Exists) throw new InvalidOperationException($"The configured maps folder does not exist: {maps.FullName}");
            roots.Add(maps.FullName);
            if (maps.Name.Equals("svmap", StringComparison.OrdinalIgnoreCase) && maps.Parent is { } parent)
            {
                if (parent.Parent?.Name == SteamContentInstall.BuildsDirectory &&
                    (recipe is null || UnrealSteamInstall.ReadRecipe(parent.FullName) != recipe || !SteamContentInstall.IsComplete(parent.FullName)))
                    throw new InvalidOperationException("The configured maps belong to a different or incomplete content build. Select this Unreal release's content build.");
                if (Directory.Exists(Path.Combine(parent.FullName, "shared"))) content = parent.FullName;
            }
        }
        string community = Path.Combine(configDirectory, "community");
        ContentPaths.ConfigureUnreal(content, packaged, community);
        roots.AddRange(ContentPaths.MapRoots);
        return new MapCatalog(roots.ToArray());
    }

    internal static void Log(string line) =>
        Console.WriteLine(string.Create(CultureInfo.InvariantCulture, $"[{DateTime.Now:HH:mm:ss}] {line}"));

    /// <summary>Lines typed on standard input, read on their own thread and taken on the server's.</summary>
    private sealed class ConsoleCommands
    {
        private readonly ConcurrentQueue<string> _lines = new();

        public void Start()
        {
            var thread = new Thread(Read) { IsBackground = true, Name = "svm-server console" };
            thread.Start();
        }

        public bool TryTake([System.Diagnostics.CodeAnalysis.NotNullWhen(true)] out string? line) => _lines.TryDequeue(out line);

        private void Read()
        {
            try
            {
                while (Console.In.ReadLine() is { } line)
                {
                    if (line.Trim().Length > 0)
                    {
                        _lines.Enqueue(line.Trim());
                    }
                }
            }
            catch (IOException)
            {
                // No console (a service): commands come from in-game admins only.
            }
        }
    }
}
